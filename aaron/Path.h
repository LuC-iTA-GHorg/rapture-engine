// Path helpers of Aaron.dll (WP-4): CombinePath (ord 438) and the executable path used by the exe Module (ord 43).
#pragma once

#include <string>

namespace aaron {

// Aaron ord 438 0x10014270 CombinePath(out, base, name) -> out:
//   * `name` has a drive ("C:..."): the result is _fullpath(name);
//   * otherwise `name` is split (_splitpath) into dir / file / ext and joined with the drive of `base`: a relative dir is appended to base's dir
//     (`base` is expected to be a DIRECTORY, e.g. "C:\games\x\" - its file name and extension are dropped, as the original does), a dir
//     starting with '\' replaces it; then _fullpath normalises it ("." and ".." resolved against the current directory if still relative).
// Separators: both '\' and '/' are accepted on input; the result uses the native separator of the host (backslashes on Windows).
std::string CombinePath(const std::string& base, const std::string& name);

// GetModuleFileName(NULL): full path of the running executable ("" if unknown).
std::string ExecutablePath();

} // namespace aaron
