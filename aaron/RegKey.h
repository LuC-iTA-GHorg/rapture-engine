// RegKey: registry replacement (aaron.md sec. 2.6). Backed by an .ini file in %APPDATA%\<folder>
// (folder and file name chosen by the game: HostConfig::settingsFolder / settingsFile; on non-Windows hosts
// $XDG_CONFIG_HOME/<folder> or the working directory).
//
// MAPPING
//   * a key  (HKCU\Software\Disney Interactive\<Game> Key\US\Save\0)  ->  an ini section whose name is the
//     full key path with the hive prefix:  [HKCU\Software\Disney Interactive\<Game> Key\US\Save\0]
//   * DWORD  -> `Name=1234`            (decimal, unsigned; `0x1f` is also accepted when read, for hand-edited files such as FKeys=0x1)
//   * BINARY -> `Name=hex:0a0b0c...`  (lower case hex, no separators)
//   * STRING -> `Name=str:text`
//   * keys are created lazily on the first Set*; reading a missing value returns the default.
//   * HKCU (RegKey ord 12 root 0x80000001) = per-user settings: `Scene::GetUserKey()`;
//     HKLM (0x80000002) = per-machine settings: `Scene::GetMachineKey()` / `Application::GetMachineKey()`.
//     The port keeps both in the same file under different hive prefixes.
//   * The whole file is loaded into memory on first use; writes mark it dirty and it is flushed at
//     Application shutdown and at most once every ~1 s from the main loop (RegKey::FlushAll()).
//   * Subkey enumeration/deletion (`DeleteKey` removes the key and its subkeys) works on the section list.
#pragma once

#include "aaron/RObject.h"

#include <string>

namespace aaron {

class RegKey : public RObject {
public:
    enum Hive : u32 { HKCU = 0x80000001, HKLM = 0x80000002 };

    // Root key. Aaron ord 12 0x10008d8d: RegKey(path, machine): root = (machine != 0) + 0x80000001 (HKCU / HKLM), path copied (the key is opened
    // lazily: RegOpenKeyEx for reads, RegCreateKeyEx for writes).
    RegKey(Hive hive, const char* path);
    // The original's argument order / types for ord 12: `machine` 0 = HKCU, nonzero = HKLM.
    static RegKey* Create(const char* path, int machine) { return new RegKey(machine ? HKLM : HKCU, path); }
    // Sub key of `parent`. Aaron ord 11 0x10008d18 (RegKey::RegKey_Sub) - Shoot passes (name, parent).
    RegKey(const char* name, RegKey* parent);
    ~RegKey() override;                              // Aaron ord 67 0x10008de5 (RegCloseKey of the opened handle)

    // Aaron ord 171 0x10008e1e: a new RegKey(name, this) = the sub key `name` of this key (RegKey::RegKey_Sub). The caller owns it.
    RegKey* OpenSubKey(const char* name) { return new RegKey(name, this); }

    // Aaron ord 248 0x10008f90. Missing -> returns `def`.
    u32 GetDword(const char* name, u32 def = 0);
    // Aaron ord 247 0x10008f20. Copies up to `size` bytes; if the value is missing and `def` != null copies `def`
    // (size bytes) instead. Returns true if the value existed.  TODO: confirm the 4th parameter is a default buffer.
    bool GetBinary(const char* name, void* buf, u32 size, const void* def = nullptr);
    // Aaron ord 251 0x10008fdf is QueryString below (it returns the length, not a bool).
    bool GetString(const char* name, char* buf, u32 size, const char* def = nullptr);
    // Aaron ord 251 0x10008fdf (RegKey::GetString of the original): RegQueryValueEx(name, buf, &size); returns the size in bytes INCLUDING the NUL of
    // the stored value; when the value is missing (or does not fit): `def` is copied to `buf` and strlen(def) + 1 returned, or 0 when there is no
    // buffer / default. A null `buf` with an existing value returns the size needed.
    u32 QueryString(const char* name, char* buf, u32 size, const char* def = nullptr);
    // Aaron ord 621 0x1000908b
    void SetDword(const char* name, u32 value);
    // Aaron ord 620 0x10009052
    void SetBinary(const char* name, const void* data, u32 size);
    // Aaron ord 624 0x100090c2: RegSetValueEx(REG_SZ, strlen + 1 bytes), creating the key if needed.
    void SetString(const char* name, const char* value);
    // Aaron ord 172 0x10008e95: deletes this key and all its subkeys (Shoot: HighScore is deleted before being rewritten).
    void DeleteKey();
    // Aaron ord 175 0x10008edd: RegDeleteValue(name) AND RegDeleteKey(name). QUIRK kept: a SUB KEY with the same name is removed too (when it has
    // no sub keys of its own, as RegDeleteKey on NT).
    void DeleteValue(const char* name);

    const std::string& Path() const { return path_; }

    // ---- ini storage control (port additions) -------------------------------------------------
    static void SetIniPath(const std::string& path); // default: %APPDATA%\<folder>\<file> (see SetDefaultLocation)
    // The game's settings location (HostConfig::settingsFolder / settingsFile): %APPDATA%\<folder>\<file>, on non-Windows
    // $XDG_CONFIG_HOME/<folder>/<file> (else <file> in the working directory).  Call before the first key is used.
    static void SetDefaultLocation(const char* folder, const char* file);
    static void FlushAll();                          // write the file if dirty
    static void FlushIfDue(unsigned nowMs);          // called by the main loop

private:
    std::string path_; // section name (hive + path)
};

} // namespace aaron
