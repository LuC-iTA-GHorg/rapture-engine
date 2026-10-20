// CombinePath / ExecutablePath (WP-4). See Path.h.
#include "aaron/Path.h"

#include <cstdlib>
#include <filesystem>

#if defined(__linux__)
#include <sys/types.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

namespace aaron {

namespace {

// Separator of the host file system: the original works with "\\" paths and drive letters; on other platforms (Linux/macOS) the same
// logic runs with "/" and no drive letters, so a path built here can be opened as it is.
#ifdef _WIN32
constexpr char kSep = '\\';
constexpr bool kDrives = true;
#else
constexpr char kSep = '/';
constexpr bool kDrives = false;
#endif

struct Split {
    std::string drive, dir, file; // file = name + extension
};

// _splitpath: "C:\a\b\f.ext" -> drive "C:", dir "\a\b\", file "f.ext" (drive "" when there is none)
Split split_path(const std::string& p) {
    Split s;
    size_t i = 0;
    if (kDrives && p.size() >= 2 && p[1] == ':') {
        s.drive = p.substr(0, 2);
        i = 2;
    }
    size_t cut = p.find_last_of("\\/");
    if (cut == std::string::npos || cut < i) {
        s.file = p.substr(i);
    } else {
        s.dir = p.substr(i, cut - i + 1);
        s.file = p.substr(cut + 1);
    }
    for (char& c : s.dir)
        if (c == '/' || c == '\\') c = kSep;
    return s;
}

std::string full_path(const std::string& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path r = fs::absolute(fs::path(p), ec);
    if (ec) return p;
    r = r.lexically_normal();
    std::string out = r.make_preferred().string();
    return out;
}

} // namespace

std::string CombinePath(const std::string& base, const std::string& name) { // ord 438 0x10014270
    Split n = split_path(name);
    if (!n.drive.empty()) return full_path(name); // the name carries a drive: _fullpath(name)
    Split b = split_path(base);
    std::string dir;
    if (!n.dir.empty() && n.dir[0] == kSep) dir = n.dir; // rooted: replaces the base directory
    else dir = b.dir + n.dir;                            // relative: appended
    return full_path(b.drive + dir + n.file);
}

std::string ExecutablePath() {
#if defined(_WIN32)
    // CRT variable (not a Win32 API call): also valid in the RAPTURE_FORCE_PORTABLE build on MinGW
    return _pgmptr ? std::string(_pgmptr) : std::string();
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return std::string();
    buf[n] = 0;
    return buf;
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return std::string();
    return full_path(buf);
#else
    return std::string(); // unknown: callers treat "" as "no module path"
#endif
}

} // namespace aaron
