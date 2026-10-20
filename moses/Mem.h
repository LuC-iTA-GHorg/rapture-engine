// CMem + path/debug helpers of moses.dll (moses.md sec. 8).
//
// CFileIO is in FileIO.h and CObjCache / CGdiCache (software pen/brush/font caches) in ObjCache.h (WP-5).
#pragma once

#include "moses/Types.h"

namespace moses {

struct MEMINFO { // CMem::GetMemInfo: 4 global counters
    u32 allocs = 0, frees = 0, bytes = 0, peak = 0;
};

// Heap wrapper. Port: calloc/realloc/free + the counters. Shoot.exe/Shoot.dll instantiate a global
// `CMem g_Mem(0, 0)` and use Alloc/Realloc/Free for their dynamic arrays.
class CMem {
public:
    CMem(u32 size = 0, int flags = 0);        // moses.dll CMem::CMem(uint,int)   (export, Shoot imports)
    virtual ~CMem();                          // moses.dll CMem::~CMem            (export, Shoot imports)
    void* Alloc(u32 size, int zero);          // moses.dll CMem::Alloc(uint,int)  zero != 0 -> zero filled
    void* Realloc(void* p, u32 size, int zero); // moses.dll CMem::Realloc
    void* Free(void* p);                      // moses.dll CMem::Free            returns nullptr
    void* Copy(void* p);                      // moses.dll CMem::Copy            duplicate a block
    int Lock() { return 1; }                  // moses.dll CMem::Lock   (no-op)
    int Unlock() { return 1; }                // moses.dll CMem::Unlock (no-op)
    u32 Compact() { return 0; }               // moses.dll CMem::Compact (no-op)
    static void GetMemInfo(MEMINFO* info);    // moses.dll CMem::GetMemInfo
};

// ---- global helpers (moses.dll exports _FileExists@4, MakeFilePath, MakeFullPath, GetPathAndFile, DebugMsg...)
// Paths are relative to the current directory (= the game folder, the one with *.rrs and Shoot.exe).
int FileExists(const char* path);                                    // moses.dll _FileExists@4
void GetPathAndFile(const char* full, char* path, char* file);       // moses.dll GetPathAndFile
char* MakeFilePath(char* dir, const char* file);                     // moses.dll _MakeFilePath@8 (joins with '\\')
void MakeFullPath(char* out, const char* dir, const char* file);     // moses.dll MakeFullPath
void DebugMsg(const char* fmt, ...);                                 // moses.dll DebugMsg (stderr / debug log)
void DebugStr(const char* s);                                        // moses.dll DebugStr

// MosesInitLibrary / MosesFreeLibrary (moses.dll exports). Init returns 1; the port has nothing to
// initialise besides making sure SDL is up (done by the host, see Application::Run).
int MosesInitLibrary();
void MosesFreeLibrary();

} // namespace moses
