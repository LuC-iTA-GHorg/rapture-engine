// rapture.ini reader/writer (WP-9). moses.dll 0x1000ff40 / 0x1000ff60 / 0x1000ffa0. See Ini.h.
#include "moses/Ini.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace moses {

namespace {

std::string g_file = kRaptureIniName;

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool EqNoCase(const std::string& a, const char* b) {
    size_t n = std::char_traits<char>::length(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

bool ReadLines(std::vector<std::string>& lines) {
    std::ifstream in(g_file, std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return true;
}

// Section header? "[name]" (name trimmed).
bool IsSection(const std::string& line, std::string& name) {
    std::string t = Trim(line);
    if (t.size() < 2 || t[0] != '[') return false;
    size_t e = t.find(']');
    if (e == std::string::npos) return false;
    name = Trim(t.substr(1, e - 1));
    return true;
}

// key=value line (not a comment)? Fills key (trimmed) and value (trimmed).
bool IsKeyValue(const std::string& line, std::string& key, std::string& val) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = Trim(t.substr(0, eq));
    val = Trim(t.substr(eq + 1));
    return true;
}

// Index of the line holding section/key (first match), or -1. secEnd = index after the last line of the section (-1 when
// the section does not exist).
int Find(const std::vector<std::string>& lines, const char* section, const char* key, int& secEnd) {
    secEnd = -1;
    bool in = false;
    int found = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        std::string name, k, v;
        if (IsSection(lines[i], name)) {
            if (in) break;
            in = EqNoCase(name, section);
            if (in) secEnd = i + 1;
            continue;
        }
        if (!in) continue;
        if (!Trim(lines[i]).empty()) secEnd = i + 1; // insert after the last non-blank line
        if (found < 0 && IsKeyValue(lines[i], k, v) && EqNoCase(k, key)) found = i;
    }
    return found;
}

} // namespace

void IniSetFile(const char* path) { g_file = (path && path[0]) ? path : kRaptureIniName; }
const char* IniGetFile() { return g_file.c_str(); }

bool IniGetString(const char* section, const char* key, std::string& out) {
    std::vector<std::string> lines;
    if (!section || !key || !ReadLines(lines)) return false;
    int secEnd;
    int i = Find(lines, section, key, secEnd);
    if (i < 0) return false;
    std::string k;
    IsKeyValue(lines[i], k, out);
    return true;
}

// moses.dll 0x1000ff40
u32 IniGetInt(const char* section, const char* key, i16 def) {
    const u32 d = (u32)(int)def; // (int)param_3 in the decompilation: sign-extended
    std::string v;
    if (!IniGetString(section, key, v) || v.empty()) return d;
    size_t i = 0;
    bool neg = false;
    if (v[i] == '-') { neg = true; ++i; }
    u32 n = 0;
    for (; i < v.size() && v[i] >= '0' && v[i] <= '9'; ++i) n = n * 10 + (u32)(v[i] - '0');
    return neg ? (u32)(0u - n) : n;
}

// moses.dll 0x1000ffa0
bool IniWriteString(const char* section, const char* key, const char* value) {
    if (!section || !key) return false;
    std::vector<std::string> lines;
    ReadLines(lines);
    int secEnd;
    int i = Find(lines, section, key, secEnd);
    if (!value) { // delete
        if (i < 0) return true;
        lines.erase(lines.begin() + i);
    } else {
        std::string line = std::string(key) + "=" + value;
        if (i >= 0) lines[i] = line;
        else if (secEnd >= 0) lines.insert(lines.begin() + secEnd, line);
        else {
            if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
            lines.push_back(std::string("[") + section + "]");
            lines.push_back(line);
        }
    }
    std::ofstream out(g_file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const std::string& l : lines) out << l << "\r\n";
    return (bool)out;
}

// moses.dll 0x1000ff60: wsprintfA(buf, "%d", (int)value)
void IniWriteInt(const char* section, const char* key, i16 value) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", (int)value);
    IniWriteString(section, key, buf);
}

} // namespace moses
