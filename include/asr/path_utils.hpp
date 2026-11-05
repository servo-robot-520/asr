#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace asr {

    /// 展开以 "~/" 开头的用户路径；其他路径原样返回。
    inline std::string expand_user_path(const std::string_view path) {
        if (path.size() < 2 || path[0] != '~' || path[1] != '/') {
            return std::string{path};
        }
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            return std::string{path};
        }
        return std::string{home} + std::string{path.substr(1)};
    }

} // namespace asr
