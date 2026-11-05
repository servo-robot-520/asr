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

    /// 把配置中的节点名解析成绝对 FQN。
    ///
    /// - 以 '/' 开头：按绝对名称使用，便于跨 namespace 显式指定；
    /// - 其他：拼到当前节点 namespace 后面。
    ///
    /// 例如 namespace=/robot1、configured=audio_io 得到 /robot1/audio_io；
    /// namespace=/、configured=audio_io 得到 /audio_io。
    [[nodiscard]] inline std::string resolve_node_fqn(const std::string_view node_namespace,
                                                      const std::string_view configured) {
        const auto name = detail::trim_ascii(configured);
        if (name.empty()) {
            throw std::invalid_argument("audio_io.node must not be empty");
        }

        std::string resolved;
        if (name.front() == '/') {
            resolved = std::string{name};
        }
        else {
            auto ns = detail::trim_trailing_slashes(node_namespace);
            if (ns.empty()) {
                ns = "/";
            }
            resolved = ns == "/" ? "/" + std::string{name} : std::string{ns} + "/" + std::string{name};
        }

        resolved = std::string{detail::trim_trailing_slashes(resolved)};
        detail::require_absolute_ros_name(resolved, "audio_io.node");
        return resolved;
    }

    /// 把配置中的话题名解析成绝对名称。
    ///
    /// 默认话题名是相对名（如 audio_io/capture），随节点 namespace 自动解析；
    /// 以 '/' 开头时按绝对名称使用。私有名 `~` 不支持，避免把 audio_io 的话题
    /// 错解析到 asr 节点自身。
    [[nodiscard]] inline std::string resolve_topic_name(const std::string_view node_namespace,
                                                        const std::string_view configured) {
        const auto name = detail::trim_ascii(configured);
        if (name.empty()) {
            throw std::invalid_argument("capture.topic must not be empty");
        }

        std::string resolved;
        if (name.front() == '/') {
            resolved = std::string{name};
        }
        else {
            auto ns = detail::trim_trailing_slashes(node_namespace);
            if (ns.empty()) {
                ns = "/";
            }
            resolved = ns == "/" ? "/" + std::string{name} : std::string{ns} + "/" + std::string{name};
        }

        resolved = std::string{detail::trim_trailing_slashes(resolved)};
        detail::require_absolute_ros_name(resolved, "capture.topic");
        return resolved;
    }

} // namespace asr
