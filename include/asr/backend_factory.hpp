#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <rclcpp/rclcpp.hpp>

#include "asr/asr_backend.hpp"
#include "asr/wake_word_detector.hpp"

namespace asr {

    /// 根据 backend 参数创建后端；不在此处调用 initialize()。
    ///
    /// 新增后端 = 实现 AsrBackend + 在 backend_factory.cpp 增加一个分支；
    /// AsrNode 不直接依赖任何具体后端实现。
    [[nodiscard]] std::unique_ptr<AsrBackend>
    create_backend(const std::string_view backend_type, rclcpp::Node& node, std::string* error);

    /// 创建唤醒词检测器；使用 keyword.* 的 KWS 模型和 wake.keywords_file。
    [[nodiscard]] std::unique_ptr<WakeWordDetector>
    create_wake_word_detector(rclcpp::Node& node, std::string* error);

} // namespace asr
