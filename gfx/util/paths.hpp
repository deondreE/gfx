#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <windows.h>

namespace gfx::util::paths {

namespace fs = std::filesystem;

// Directory containing the running executable.
inline fs::path executable_dir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (len == 0 || len == std::size(buf)) {
        throw std::runtime_error("GetModuleFileNameW failed.");
    }
    return fs::path(buf).parent_path();
}

// Walks up from the exe dir until a directory containing `marker` is found.
// Falls back to the exe dir if nothing matches.
inline fs::path find_root(const fs::path& start, const fs::path& marker) {
    for (fs::path p = start;; p = p.parent_path()) {
        std::error_code ec;
        if (fs::exists(p / marker, ec)) return p;
        if (p == p.parent_path()) break; // reached drive root
    }
    return start;
}

// Cached project root. Marker is a folder or file that only exists at your
// project root (e.g. "assets", "shaders", or ".root").
inline const fs::path& root() {
    static const fs::path r = find_root(executable_dir(), "assets");
    return r;
}

// Resolve a root-relative path to an absolute, normalized path.
inline fs::path resolve(const fs::path& relative) {
    if (relative.is_absolute()) return relative.lexically_normal();
    return (root() / relative).lexically_normal();
}

inline std::wstring resolve_w(const fs::path& relative) {
    return resolve(relative).wstring();
}

inline std::string resolve_s(const fs::path& relative) {
    return resolve(relative).string();
}

} // namespace util::paths
