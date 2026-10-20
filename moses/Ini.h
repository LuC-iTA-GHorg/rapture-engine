// rapture.ini reader/writer (WP-9). moses.dll IniGetInt 0x1000ff40, IniWriteInt 0x1000ff60, IniWriteString 0x1000ffa0.
//
// The original wraps GetPrivateProfileIntA / wsprintfA + WritePrivateProfileStringA with the fixed file name "rapture.ini"
// (relative: current directory; Windows would also try %WINDIR%, the port does not). This is a portable re-implementation of
// those API semantics on a plain text file. See docs/RAPTURE_INI.md.
#pragma once

#include "moses/Types.h"

#include <string>

namespace moses {

// File name used by the original (string literal in the three functions).
constexpr const char* kRaptureIniName = "rapture.ini";

// Port/test hook: use another file instead of "rapture.ini" (nullptr = back to the default).
void IniSetFile(const char* path);
const char* IniGetFile();

// moses.dll 0x1000ff40: GetPrivateProfileInt(section, key, (int)def, "rapture.ini").
// Missing file/section/key (or empty value) -> def sign-extended to 32 bit; otherwise the leading integer of the value
// ("-" allowed, decimal only: "0x1" reads as 0, trailing text ignored; the result wraps as an unsigned like the Windows API).
// Section/key names are case-insensitive. Callers that store the result in a short/byte truncate it themselves (as the original).
u32 IniGetInt(const char* section, const char* key, i16 def);

// moses.dll 0x1000ff60: formats (int)value with wsprintf "%d" and calls IniWriteString.
void IniWriteInt(const char* section, const char* key, i16 value);

// moses.dll 0x1000ffa0: WritePrivateProfileString. value == nullptr deletes the key. Creates the file/section when missing.
// Returns true when the file was written (the original ignores the BOOL).
bool IniWriteString(const char* section, const char* key, const char* value);

// Port addition: raw string value (false = key not present). Used for `[port]` options.
bool IniGetString(const char* section, const char* key, std::string& out);

} // namespace moses
