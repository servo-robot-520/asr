#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace asr {

    namespace detail {

        /// 去掉首尾 ASCII 空白。
        [[nodiscard]] inline std::string_view trim_ascii(const std::string_view value) {
            const auto first = value.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\n\r\f\v");
            return value.substr(first, last - first + 1);
        }

        /// 去掉尾部多余的 '/'，保留根路径。
        [[nodiscard]] inline std::string_view trim_trailing_slashes(std::string_view value) {
            while (value.size() > 1 && value.back() == '/') {
                value.remove_suffix(1);
            }
            return value;
        }

        /// 校验 ROS 名称：非空、绝对、非根、无空白、无空路径段。
        inline void require_absolute_ros_name(const std::string_view name, const char* what) {
            if (name.empty() || name.front() != '/' || name == "/" ||
                name.find("//") != std::string_view::npos ||
                name.find_first_of(" \t\n\r\v\f") != std::string_view::npos ||
                name.find('~') != std::string_view::npos) {
                throw std::invalid_argument(
                    std::string{what} + " must be an absolute, non-root ROS name without whitespace or '~'");
            }
        }

    } // namespace detail

    /// 把接口包提供的默认节点全名限定到当前节点的 namespace。
    ///
    /// `audio_io_interface::kDefaultNodeFqn` 是根 namespace 下的 `/audio_io`；
    /// 当 asr 运行在 `/robot1/asr` 时，本函数得到 `/robot1/audio_io`。
    /// 之后可直接把该 FQN 传给 `topic_capture()` / `topic_event()`。
    [[nodiscard]] inline std::string qualify_node_fqn(const std::string_view node_namespace,
                                                      const std::string_view default_node_fqn) {
        const auto base = detail::trim_ascii(default_node_fqn);
        if (base.empty() || base == "/" || base.front() != '/') {
            throw std::invalid_argument("default node FQN must be absolute and non-root");
        }

        auto ns = detail::trim_trailing_slashes(node_namespace);
        if (ns.empty()) {
            ns = "/";
        }
        if (ns != "/") {
            detail::require_absolute_ros_name(ns, "node namespace");
        }

        std::string resolved = ns == "/" ? std::string{base} : std::string{ns} + std::string{base};
        resolved = std::string{detail::trim_trailing_slashes(resolved)};
        detail::require_absolute_ros_name(resolved, "resolved node FQN");
        return resolved;
    }

} // namespace asr
