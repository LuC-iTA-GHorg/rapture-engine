// CFileIO (mmio wrapper) + MakeModulePath / FormatDebugMessage. moses.dll 0x10002bb0..0x10003000, 0x10014f80, 0x10015310.
// See FileIO.h for the mmio emulation over stdio.
#include "moses/FileIO.h"

#include <SDL.h>

#include <cstdarg>
#include <string>
#include <cstdlib>
#include <cstring>

namespace moses {

namespace {
CMem& DefaultMem() { // &DAT_1001c6b8: the global CMem used when the caller passes none
    static CMem mem(0, 0);
    return mem;
}

bool RdU32(std::FILE* f, u32& v) {
    u8 b[4];
    if (std::fread(b, 1, 4, f) != 4) return false;
    v = u32(b[0]) | u32(b[1]) << 8 | u32(b[2]) << 16 | u32(b[3]) << 24;
    return true;
}
bool WrU32(std::FILE* f, u32 v) {
    u8 b[4] = {u8(v), u8(v >> 8), u8(v >> 16), u8(v >> 24)};
    return std::fwrite(b, 1, 4, f) == 4;
}
} // namespace

// moses.dll 0x10002bb0
CFileIO::CFileIO(const char* path) {
    if (path) {
        std::strncpy(path_, path, sizeof path_ - 1); // lstrcpyA into a 260 byte field
        path_[sizeof path_ - 1] = 0;
    }
}

// moses.dll 0x10002be0
CFileIO::~CFileIO() {
    if (f_) Close();
}

// moses.dll 0x10002f20: refused while open
int CFileIO::SetFile(const char* path) {
    if (f_) return 0;
    std::strncpy(path_, path ? path : "", sizeof path_ - 1);
    path_[sizeof path_ - 1] = 0;
    return 1;
}

// moses.dll 0x10002c50: mmioOpen(path, MMIO_ALLOCBUF | MMIO_CREATE | MMIO_READWRITE)
int CFileIO::Create() {
    if (f_) return 0;
    f_ = std::fopen(path_, "w+b");
    return f_ != nullptr;
}

// moses.dll 0x10002dc0
int CFileIO::Open(u32 mode) {
    if (f_) return 1;
    if (mode & MMIO_CREATE) return Create();
    // mmioOpen(path, mode | MMIO_ALLOCBUF): READ -> read only; WRITE/READWRITE open an existing file for update
    f_ = std::fopen(path_, (mode & 3) == MMIO_READ ? "rb" : "r+b");
    return f_ != nullptr;
}

// moses.dll 0x10002c30
void CFileIO::Close() {
    if (f_) {
        std::fclose(f_);
        f_ = nullptr;
    }
}

// moses.dll 0x10002c80: Close(); mmioOpen(path, MMIO_DELETE)
int CFileIO::Delete() {
    Close();
    return std::remove(path_) == 0;
}

// moses.dll 0x10002cd0: mmioOpen(path, MMIO_EXIST)
int CFileIO::Exists() {
    if (f_) return 1;
    std::FILE* t = std::fopen(path_, "rb");
    if (!t) return 0;
    std::fclose(t);
    return 1;
}

// moses.dll 0x10002cf0
void CFileIO::Flush() {
    if (f_) std::fflush(f_);
}

// moses.dll 0x10002f00 (mmioSeek: -1 on error, also on a closed file)
u32 CFileIO::Seek(long off, u32 origin) {
    if (!f_) return 0xFFFFFFFFu;
    int w = origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET;
    if (std::fseek(f_, off, w) != 0) return 0xFFFFFFFFu;
    long p = std::ftell(f_);
    return p < 0 ? 0xFFFFFFFFu : u32(p);
}

// moses.dll 0x10002f50
u32 CFileIO::Tell() { return Seek(0, 1); }

// moses.dll 0x10002d10
u32 CFileIO::GetSize() {
    u32 here = Tell();
    u32 size = Seek(0, 2);
    Seek(long(here), 0);
    return size;
}

// moses.dll 0x10002e10
int CFileIO::Read(void* buf, u32 n) {
    if (!f_) return 0;
    return std::fread(buf, 1, n, f_) == n;
}

// moses.dll 0x10002e40
u32 CFileIO::ReadSize(void* buf, u32 n) {
    if (!f_) return 0;
    return u32(std::fread(buf, 1, n, f_));
}

// moses.dll 0x10002e60
u32 CFileIO::ReadString(char* buf, u32 n) {
    if (buf) *buf = 0;
    u16 len = 0;
    if (!Read(&len, 2) || len == 0) return 0; // (little endian host assumed, as the file format is)
    char* tmp = static_cast<char*>(DefaultMem().Alloc(u32(len) + 1, 0));
    if (!tmp) return 0;
    if (!Read(tmp, len)) {
        len = 0;
    } else {
        tmp[len] = 0;
        if (buf && n) { // lstrcpynA
            std::strncpy(buf, tmp, n - 1);
            buf[n - 1] = 0;
        }
    }
    DefaultMem().Free(tmp);
    return len;
}

// moses.dll 0x10002f60
int CFileIO::Write(const void* buf, u32 n) {
    if (!f_) return 0;
    return std::fwrite(buf, 1, n, f_) == n;
}

// moses.dll 0x10002fe0
u32 CFileIO::WriteSize(const void* buf, u32 n) {
    if (!f_) return 0;
    return u32(std::fwrite(buf, 1, n, f_));
}

// moses.dll 0x10002f90: wvsprintfA into 512 bytes, then CR LF
int CFileIO::WriteLine(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > int(sizeof buf) - 3) n = int(sizeof buf) - 3;
    buf[n] = '\r';
    buf[n + 1] = '\n';
    return Write(buf, u32(n) + 2);
}

// moses.dll 0x10003000 (a null string is stored as length 0)
u32 CFileIO::WriteString(const char* s) {
    u16 len = s ? u16(std::strlen(s)) : u16(0);
    if (!Write(&len, 2)) return 0;
    int ok = 1;
    if (s) ok = Write(s, len);
    return ok ? u32(len) + 2 : 0;
}

// moses.dll 0x10002d40
u8* CFileIO::Load(CMem* mem, u32* size) {
    bool wasOpen = f_ != nullptr;
    if (!wasOpen && !Open(0)) return nullptr;
    u32 n = GetSize();
    if (!mem) mem = &DefaultMem();
    u8* p = static_cast<u8*>(mem->Alloc(n, 0));
    if (p) Read(p, n);
    if (!wasOpen) Close();
    if (size) *size = n;
    return p;
}

// moses.dll 0x10002ca0 = mmioDescend
int CFileIO::Descend(MMCKINFO* ck, MMCKINFO* parent, u32 flags) {
    if (!f_ || !ck) return 0;
    u32 limit = 0xFFFFFFFFu; // end of the parent's data
    if (parent) limit = parent->dwDataOffset + parent->cksize;
    for (;;) {
        u32 hdr = Tell();
        u32 id = 0, size = 0;
        if (hdr == 0xFFFFFFFFu || hdr + 8 > limit) return 0;
        if (!RdU32(f_, id) || !RdU32(f_, size)) return 0;
        MMCKINFO found;
        found.ckid = id;
        found.cksize = size;
        found.dwDataOffset = hdr + 8;
        found.dwFlags = 0;
        bool isForm = id == FourCC('R', 'I', 'F', 'F') || id == FourCC('L', 'I', 'S', 'T');
        if (isForm && !RdU32(f_, found.fccType)) return 0;
        bool match;
        if (flags & MMIO_FINDCHUNK) match = id == ck->ckid;
        else if (flags & MMIO_FINDRIFF) match = id == FourCC('R', 'I', 'F', 'F') && found.fccType == ck->fccType;
        else if (flags & MMIO_FINDLIST) match = id == FourCC('L', 'I', 'S', 'T') && found.fccType == ck->fccType;
        else match = true;
        if (match) {
            *ck = found;
            return 1;
        }
        // skip this chunk (chunks are word aligned)
        u32 next = found.dwDataOffset + size + (size & 1);
        if (Seek(long(next), 0) == 0xFFFFFFFFu) return 0;
    }
}

// moses.dll 0x10002c00 = mmioAscend
int CFileIO::Ascend(MMCKINFO* ck) {
    if (!f_ || !ck) return 0;
    if (ck->dwFlags & MMIO_DIRTY) { // chunk made with CreateChunk: patch the size
        u32 pos = Tell();
        u32 size = pos - ck->dwDataOffset;
        if (size & 1) { u8 pad = 0; Write(&pad, 1); }
        ck->cksize = size;
        if (Seek(long(ck->dwDataOffset) - 4, 0) == 0xFFFFFFFFu || !WrU32(f_, size)) return 0;
        ck->dwFlags &= ~MMIO_DIRTY;
    }
    return Seek(long(ck->dwDataOffset + ck->cksize + (ck->cksize & 1)), 0) != 0xFFFFFFFFu;
}

// port: mmioCreateChunk
int CFileIO::CreateChunk(MMCKINFO* ck, u32 flags) {
    if (!f_ || !ck) return 0;
    u32 id = ck->ckid;
    bool form = false;
    if (flags & MMIO_CREATERIFF) { id = FourCC('R', 'I', 'F', 'F'); form = true; }
    else if (flags & MMIO_CREATELIST) { id = FourCC('L', 'I', 'S', 'T'); form = true; }
    u32 hdr = Tell();
    if (hdr == 0xFFFFFFFFu || !WrU32(f_, id) || !WrU32(f_, 0)) return 0;
    if (form && !WrU32(f_, ck->fccType)) return 0;
    ck->ckid = id;
    ck->cksize = 0;
    ck->dwDataOffset = hdr + 8;
    ck->dwFlags = MMIO_DIRTY;
    return 1;
}

// ---------------------------------------------------------------------------------------------
// moses.dll 0x10014f80: GetModuleFileNameA(module) + MakeFullPath(out, thatPath, file)
void MakeModulePath(void*, char* out, const char* file) {
    char* base = SDL_GetBasePath(); // folder of the executable, with a trailing separator
    std::string exe = std::string(base ? base : "") + "module.exe";
    if (base) SDL_free(base);
    MakeFullPath(out, exe.c_str(), file);
}

// moses.dll 0x10015310
void FormatDebugMessage(char* out) {
    if (out) *out = 0;
}

} // namespace moses
