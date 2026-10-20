// RegKey: registry replacement backed by an .ini file (format: see RegKey.h). Package 3.
// Aaron RegKey ord 11/12 (ctors), 247/248/251 (Get*), 620/621/624 (Set*), 172 (DeleteKey), 175 (DeleteValue).
#include "aaron/RegKey.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace aaron {

namespace {

using Section = std::map<std::string, std::string>;

struct IniStore {
    std::string path;
    bool pathSet = false;
    bool loaded = false;
    bool dirty = false;
    unsigned lastFlush = 0;
    std::map<std::string, Section> sections;
};

IniStore& store() {
    static IniStore s;
    return s;
}

// Settings location chosen by the game (HostConfig::settingsFolder / settingsFile); SetIniPath() overrides it.
std::string g_folder = "Aaron", g_file = "aaron.ini";

std::string default_path() {
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) return std::string(appdata) + "\\" + g_folder + "\\" + g_file;
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/" + g_folder + "/" + g_file;
    return g_file; // fallback: working directory
}

void load(IniStore& s) {
    if (s.loaded) return;
    s.loaded = true;
    if (!s.pathSet) {
        s.path = default_path();
        s.pathSet = true;
    }
    std::ifstream f(s.path);
    if (!f) return;
    std::string line, cur;
    bool inSec = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') {
            cur = line.substr(1, line.size() - 2);
            s.sections[cur]; // an empty section is a key too
            inSec = true;
            continue;
        }
        if (!inSec) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        s.sections[cur][line.substr(0, eq)] = line.substr(eq + 1);
    }
}

bool write_file(IniStore& s) {
    std::error_code ec;
    std::filesystem::path p(s.path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(s.path, std::ios::trunc);
    if (!f) return false;
    for (auto& kv : s.sections) {
        f << '[' << kv.first << "]\n";
        for (auto& v : kv.second) f << v.first << '=' << v.second << '\n';
        f << '\n';
    }
    return bool(f);
}

const char* hive_name(RegKey::Hive h) { return h == RegKey::HKLM ? "HKLM" : "HKCU"; }

Section* find_section(const std::string& path) {
    IniStore& s = store();
    load(s);
    auto it = s.sections.find(path);
    return it == s.sections.end() ? nullptr : &it->second;
}

Section& make_section(const std::string& path) {
    IniStore& s = store();
    load(s);
    return s.sections[path];
}

const char* hex_digits = "0123456789abcdef";

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------------------------
RegKey::RegKey(Hive hive, const char* path) {                  // ord 12
    path_ = hive_name(hive);
    if (path && *path) {
        path_ += '\\';
        path_ += path;
    }
}

RegKey::RegKey(const char* name, RegKey* parent) {             // ord 11 0x10008d18
    path_ = parent ? parent->path_ : std::string("HKCU");
    if (name && *name) {
        path_ += '\\';
        path_ += name;
    }
}

RegKey::~RegKey() {} // ord 67 0x10008de5

u32 RegKey::GetDword(const char* name, u32 def) {              // ord 248 0x10008f90
    Section* sec = find_section(path_);
    if (!sec) return def;
    auto it = sec->find(name);
    if (it == sec->end()) return def;
    const std::string& v = it->second;
    if (v.empty() || !std::isdigit((unsigned char)v[0])) return def;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) return u32(std::strtoul(v.c_str() + 2, nullptr, 16)); // hand-edited files
    return u32(std::strtoul(v.c_str(), nullptr, 10));
}

bool RegKey::GetBinary(const char* name, void* buf, u32 size, const void* def) { // ord 247 0x10008f20
    Section* sec = find_section(path_);
    if (sec) {
        auto it = sec->find(name);
        if (it != sec->end() && it->second.compare(0, 4, "hex:") == 0) {
            const std::string& v = it->second;
            u32 n = u32((v.size() - 4) / 2);
            if (n > size) n = size;
            unsigned char* out = static_cast<unsigned char*>(buf);
            for (u32 i = 0; i < n; ++i) {
                int hi = hexval(v[4 + 2 * i]), lo = hexval(v[5 + 2 * i]);
                out[i] = (unsigned char)(((hi < 0 ? 0 : hi) << 4) | (lo < 0 ? 0 : lo));
            }
            // a stored value shorter than the request: the tail is taken from the default, if any
            if (n < size && def) std::memcpy(out + n, static_cast<const unsigned char*>(def) + n, size - n);
            return true;
        }
    }
    if (def && buf) std::memcpy(buf, def, size);
    return false;
}

bool RegKey::GetString(const char* name, char* buf, u32 size, const char* def) { // ord 251
    if (!buf || size == 0) return false;
    Section* sec = find_section(path_);
    if (sec) {
        auto it = sec->find(name);
        if (it != sec->end() && it->second.compare(0, 4, "str:") == 0) {
            std::snprintf(buf, size, "%s", it->second.c_str() + 4);
            return true;
        }
    }
    if (def) std::snprintf(buf, size, "%s", def); else buf[0] = 0;
    return false;
}

static void touch() { store().dirty = true; }

void RegKey::SetDword(const char* name, u32 value) {           // ord 621 0x1000908b
    make_section(path_)[name] = std::to_string(value);
    touch();
}

void RegKey::SetBinary(const char* name, const void* data, u32 size) { // ord 620 0x10009052
    std::string v = "hex:";
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (u32 i = 0; i < size; ++i) {
        v += hex_digits[p[i] >> 4];
        v += hex_digits[p[i] & 15];
    }
    make_section(path_)[name] = v;
    touch();
}

void RegKey::SetString(const char* name, const char* value) { // ord 624
    make_section(path_)[name] = std::string("str:") + (value ? value : "");
    touch();
}

void RegKey::DeleteKey() {                                     // ord 172 0x10008e95: recursive
    IniStore& s = store();
    load(s);
    std::string prefix = path_ + "\\";
    for (auto it = s.sections.begin(); it != s.sections.end();) {
        if (it->first == path_ || it->first.compare(0, prefix.size(), prefix) == 0) {
            it = s.sections.erase(it);
            s.dirty = true;
        } else {
            ++it;
        }
    }
}

void RegKey::DeleteValue(const char* name) {                   // ord 175 0x10008edd
    Section* sec = find_section(path_);
    if (sec && sec->erase(name)) touch();
    // RegDeleteKey(hkey, name): the sub key of that name goes too, unless it has sub keys of its own
    IniStore& s = store();
    std::string sub = path_ + "\\" + (name ? name : "");
    std::string prefix = sub + "\\";
    for (auto& kv : s.sections)
        if (kv.first.compare(0, prefix.size(), prefix) == 0) return;
    if (s.sections.erase(sub)) s.dirty = true;
}

u32 RegKey::QueryString(const char* name, char* buf, u32 size, const char* def) { // ord 251 0x10008fdf
    Section* sec = find_section(path_);
    if (sec) {
        auto it = sec->find(name);
        if (it != sec->end() && it->second.compare(0, 4, "str:") == 0) {
            u32 need = u32(it->second.size() - 4) + 1;
            if (!buf) return need;
            if (need <= size) {
                std::memcpy(buf, it->second.c_str() + 4, need);
                return need;
            }
        }
    }
    if (buf && def && std::strlen(def) + 1 <= size) { // (the original lstrcpy's without checking the size)
        std::strcpy(buf, def);
        return u32(std::strlen(def)) + 1;
    }
    return 0;
}

// ---- ini storage control ------------------------------------------------------------------------
void RegKey::SetIniPath(const std::string& path) {
    IniStore& s = store();
    s.path = path;
    s.pathSet = true;
    s.loaded = false; // reload from the new file on next use
    s.dirty = false;
    s.sections.clear();
}

void RegKey::FlushAll() {
    IniStore& s = store();
    if (s.loaded && s.dirty) {
        if (write_file(s)) s.dirty = false;
    }
}

void RegKey::FlushIfDue(unsigned nowMs) {
    IniStore& s = store();
    if (!s.dirty) return;
    if (nowMs - s.lastFlush >= 1000u) {
        s.lastFlush = nowMs;
        FlushAll();
    }
}

} // namespace aaron

namespace aaron {
void RegKey::SetDefaultLocation(const char* folder, const char* file) {
    if (folder && *folder) g_folder = folder;
    if (file && *file) g_file = file;
}
} // namespace aaron
