#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace asr {

    /// 唤醒词命中事件。
    struct WakeEvent {
        std::string keyword;
    };

    /// 常开唤醒词检测接口。
    ///
    /// 与 AsrBackend 解耦：唤醒阶段只负责判断“是否出现召唤词”，主 ASR 只在
    /// 唤醒后的会话窗口内运行。输入固定为 16 kHz mono float。
    class WakeWordDetector {
    public:
        virtual ~WakeWordDetector() = default;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        virtual bool initialize(std::string* error) = 0;
        [[nodiscard]] virtual bool ok() const = 0;

        /// 送入一块 PCM，返回本次命中的唤醒词。
        virtual std::vector<WakeEvent> push(const float* samples, std::size_t count) = 0;
        virtual void reset() = 0;

        /// 配置中的显示词列表；用于过滤只含唤醒词的 ASR final。
        [[nodiscard]] virtual const std::vector<std::string>& labels() const = 0;
    };

} // namespace asr
