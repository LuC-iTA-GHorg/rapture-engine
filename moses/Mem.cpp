// CMem + path/debug helpers of moses.dll (moses.md sec. 8). Package 2.
// Port: calloc/realloc/free with a 16 byte header holding the block size, plus the 4 global counters
// of CMem::GetMemInfo. The original used HeapAlloc/VirtualAlloc with a 4 byte prefix.
#include "moses/Mem.h"

#include <cstdarg>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace moses {

namespace {
constexpr size_t kHdr = 16;
MEMINFO g_info; // moses.dll globals behind CMem::GetMemInfo

u32 BlockSize(void* p) { return *reinterpret_cast<u32*>(static_cast<u8*>(p) - kHdr); }

void Account(i32 delta) {
    g_info.bytes = u32(i32(g_info.bytes) + delta);
    if (g_info.bytes > g_info.peak) g_info.peak = g_info.bytes;
}
} // namespace

// moses.dll CMem::CMem(uint,int): the original creates a private heap (size/flags); nothing to do.
CMem::CMem(u32, int) {}
CMem::~CMem() {}

// moses.dll CMem::Alloc: zero != 0 -> zero filled.
void* CMem::Alloc(u32 size, int zero) {
    u8* raw = static_cast<u8*>(std::malloc(size_t(size) + kHdr));
    if (!raw) return nullptr;
    *reinterpret_cast<u32*>(raw) = size;
    void* p = raw + kHdr;
    if (zero) std::memset(p, 0, size);
    ++g_info.allocs;
    Account(i32(size));
    return p;
}

// moses.dll CMem::Realloc
void* CMem::Realloc(void* p, u32 size, int zero) {
    if (!p) return Alloc(size, zero);
    u32 old = BlockSize(p);
    u8* raw = static_cast<u8*>(std::realloc(static_cast<u8*>(p) - kHdr, size_t(size) + kHdr));
    if (!raw) return nullptr;
    *reinterpret_cast<u32*>(raw) = size;
    void* q = raw + kHdr;
    if (zero && size > old) std::memset(static_cast<u8*>(q) + old, 0, size - old);
    Account(i32(size) - i32(old));
    return q;
}

// moses.dll CMem::Free: returns nullptr.
void* CMem::Free(void* p) {
    if (!p) return nullptr;
    u32 sz = BlockSize(p);
    std::free(static_cast<u8*>(p) - kHdr);
    ++g_info.frees;
    Account(-i32(sz));
    return nullptr;
}

// moses.dll CMem::Copy: duplicates a block.
void* CMem::Copy(void* p) {
    if (!p) return nullptr;
    u32 sz = BlockSize(p);
    void* q = Alloc(sz, 0);
    if (q) std::memcpy(q, p, sz);
    return q;
}

// moses.dll CMem::GetMemInfo
void CMem::GetMemInfo(MEMINFO* info) {
    if (info) *info = g_info;
}

// ---------------------------------------------------------------------------------------------
// Path helpers (moses.dll 0x10014d00..0x10014eb0). The original splits with _splitpath
// (FUN_10014a5e) and recombines with _makepath (FUN_10014fc0).
namespace {
struct Split { std::string drive, dir, fname, ext; };

bool IsSep(char c) { return c == '\\' || c == '/'; }

Split SplitPath(const char* p) { // _splitpath
    Split s;
    std::string str = p ? p : "";
    size_t pos = 0;
    if (str.size() >= 2 && str[1] == ':') { s.drive = str.substr(0, 2); pos = 2; }
    size_t last = std::string::npos;
    for (size_t i = pos; i < str.size(); ++i) if (IsSep(str[i])) last = i;
    size_t nameStart = pos;
    if (last != std::string::npos) { s.dir = str.substr(pos, last + 1 - pos); nameStart = last + 1; }
    std::string name = str.substr(nameStart);
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) { s.fname = name.substr(0, dot); s.ext = name.substr(dot); }
    else s.fname = name;
    return s;
}

std::string MakePath(const std::string& drive, const std::string& dir, const std::string& fname, const std::string& ext) {
    std::string r = drive; // _makepath: inserts a backslash between dir and name when missing
    r += dir;
    if (!dir.empty() && !IsSep(dir.back()) && (!fname.empty() || !ext.empty())) r += '\\';
    r += fname;
    r += ext;
    return r;
}
} // namespace

// moses.dll _FileExists@4 0x10014d00 (CFileIO::Exists)
int FileExists(const char* path) {
    if (!path) return 0;
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    std::fclose(f);
    return 1;
}

// moses.dll GetPathAndFile 0x10014d40: path = drive+dir, file = fname+ext.
void GetPathAndFile(const char* full, char* path, char* file) {
    Split s = SplitPath(full);
    if (path) std::strcpy(path, (s.drive + s.dir).c_str());
    if (file) std::strcpy(file, (s.fname + s.ext).c_str());
}

// moses.dll _MakeFilePath@8 0x10014dc0. ORIGINAL SEMANTICS (the header's parameter names are misleading):
// `dir` is the path to complete IN PLACE, `file` is a base path whose drive+dir is prepended unless the
// first one already has a drive; an absolute dir (backslash first) in the first path is kept.
char* MakeFilePath(char* dir, const char* file) {
    if (!file) return dir;
    Split a = SplitPath(dir);   // local_604.. (first)
    Split b = SplitPath(file);  // local_608.. (second)
    if (a.drive.empty()) {
        std::string d;
        if (!a.dir.empty() && a.dir[0] == '\\') d = a.dir;
        else d = b.dir + a.dir;
        std::string r = MakePath(b.drive, d, a.fname, a.ext);
        std::strcpy(dir, r.c_str());
    }
    return dir;
}

// moses.dll MakeFullPath 0x10014eb0: drive+dir of `dir` + fname+ext of `file`.
void MakeFullPath(char* out, const char* dir, const char* file) {
    Split a = SplitPath(dir), b = SplitPath(file);
    std::string r = a.drive + a.dir;
    std::string tail = b.fname + b.ext;
    // original uses lstrcat (no separator is added)
    std::strcpy(out, (r + tail).c_str());
}

// moses.dll DebugMsg 0x100152c0 is an empty function in the release build; the port prints to stderr.
void DebugMsg(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stderr);
    std::fputc('\n', stderr);
}

// moses.dll DebugStr 0x100152d0 (OutputDebugString)
void DebugStr(const char* s) {
    if (s) std::fputs(s, stderr);
}

// moses.dll 0x10002626 / 0x10002630
int MosesInitLibrary() { return 1; }
void MosesFreeLibrary() {}

} // namespace moses
