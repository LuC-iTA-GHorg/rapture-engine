// CFileIO: file wrapper over the Windows multimedia I/O (mmio*) API of moses.dll (0x10002bb0..0x10003000), plus the two
// small helpers MakeModulePath / FormatDebugMessage.
//
// The original keeps an HMMIO at +4 and the path (260 chars) at +8.  The port implements the mmio semantics it needs
// over C stdio (a RIFF chunk reader/writer included: Descend/Ascend/CreateChunk), so a WAV/AVI/RIFF file can be parsed
// exactly the way the original does with mmioDescend / mmioAscend.  <windows.h> is not used.
//
// Open mode flags are the mmio ones: MMIO_READ 0, MMIO_WRITE 1, MMIO_READWRITE 2, MMIO_CREATE 0x1000, MMIO_ALLOCBUF
// 0x10000 (ignored: stdio is buffered anyway), MMIO_DENY* ignored.
//
// Seek origins: 0 = begin, 1 = current, 2 = end (SEEK_SET/CUR/END); Seek returns the new position or 0xFFFFFFFF.
#pragma once

#include "moses/Mem.h"
#include "moses/Types.h"

#include <cstdio>

namespace moses {

// mmio constants used by the callers
enum : u32 {
    MMIO_READ = 0x0, MMIO_WRITE = 0x1, MMIO_READWRITE = 0x2,
    MMIO_FINDCHUNK = 0x10, MMIO_FINDRIFF = 0x20, MMIO_FINDLIST = 0x40,
    MMIO_CREATERIFF = 0x20, MMIO_CREATELIST = 0x40,
    MMIO_DELETE = 0x200, MMIO_CREATE = 0x1000, MMIO_EXIST = 0x4000, MMIO_ALLOCBUF = 0x10000,
};
constexpr u32 MMIO_DIRTY = 0x1; // MMCKINFO.dwFlags: chunk created by CreateChunk, size to be patched by Ascend

// Win32 MMCKINFO (20 bytes)
struct MMCKINFO {
    u32 ckid = 0;          // chunk id (FOURCC, little endian: 'RIFF' = 0x46464952)
    u32 cksize = 0;        // data size
    u32 fccType = 0;       // form/list type (RIFF and LIST chunks)
    u32 dwDataOffset = 0;  // file offset of the data (for RIFF/LIST: of the form type)
    u32 dwFlags = 0;
};
constexpr u32 FourCC(char a, char b, char c, char d) { return u32(u8(a)) | u32(u8(b)) << 8 | u32(u8(c)) << 16 | u32(u8(d)) << 24; }

class CFileIO {
public:
    explicit CFileIO(const char* path = nullptr);   // moses.dll 0x10002bb0
    virtual ~CFileIO();                             // moses.dll 0x10002be0 (closes when open)
    CFileIO(const CFileIO&) = delete;
    CFileIO& operator=(const CFileIO&) = delete;

    int SetFile(const char* path);                  // moses.dll 0x10002f20  0 if already open
    const char* GetFile() const { return path_; }   // port: accessor
    int Open(u32 mode = 0);                         // moses.dll 0x10002dc0  1 = ok (or already open); MMIO_CREATE -> Create()
    int Create();                                   // moses.dll 0x10002c50  create/truncate for read+write; 0 if already open
    void Close();                                   // moses.dll 0x10002c30
    int Delete();                                   // moses.dll 0x10002c80  closes, removes the file (nonzero = deleted)
    int Exists();                                   // moses.dll 0x10002cd0  1 when open, else file exists
    void Flush();                                   // moses.dll 0x10002cf0
    int IsOpen() const { return f_ != nullptr; }

    u32 Seek(long off, u32 origin);                 // moses.dll 0x10002f00
    u32 Tell();                                     // moses.dll 0x10002f50
    u32 GetSize();                                  // moses.dll 0x10002d10  (position preserved)

    int Read(void* buf, u32 n);                     // moses.dll 0x10002e10  1 only if all n bytes were read
    u32 ReadSize(void* buf, u32 n);                 // moses.dll 0x10002e40  number of bytes read
    // moses.dll 0x10002e60: u16 length + chars (no NUL); copies at most n-1 chars + NUL (lstrcpyn); returns the stored length
    u32 ReadString(char* buf, u32 n);
    int Write(const void* buf, u32 n);              // moses.dll 0x10002f60  1 only if all n bytes were written
    u32 WriteSize(const void* buf, u32 n);          // moses.dll 0x10002fe0
    int WriteLine(const char* fmt, ...);            // moses.dll 0x10002f90  printf (512 byte buffer) + "\r\n"
    u32 WriteString(const char* s);                 // moses.dll 0x10003000  u16 length + chars; returns length + 2
    // moses.dll 0x10002d40: reads the whole file into a block allocated with `mem` (nullptr: the global CMem); *size = bytes
    u8* Load(CMem* mem, u32* size);

    // RIFF chunks (mmioDescend / mmioAscend).  `flags`: MMIO_FINDCHUNK / MMIO_FINDRIFF / MMIO_FINDLIST or 0 (= next chunk).
    int Descend(MMCKINFO* ck, MMCKINFO* parent, u32 flags);   // moses.dll 0x10002ca0  1 = ok
    int Ascend(MMCKINFO* ck);                                 // moses.dll 0x10002c00  1 = ok
    // port: mmioCreateChunk (the original class has no wrapper for it; callers used the raw HMMIO)
    int CreateChunk(MMCKINFO* ck, u32 flags);

private:
    std::FILE* f_ = nullptr;   // +4  HMMIO
    char path_[260] = {0};     // +8
};

// moses.dll 0x10014f80: MakeFullPath(out, <directory of the module's file>, file).  Port: the "module" is the running
// executable (the game folder); `module` is accepted for signature fidelity and ignored.
void MakeModulePath(void* module, char* out, const char* file);
// moses.dll 0x10015310: the release build stores an empty string.
void FormatDebugMessage(char* out);

} // namespace moses
