#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace asr {

    /// 后端向上层发出的事件类型。
    enum class BackendEventType {
        SpeechStart, ///< 检测到语音起点
        Endpoint, ///< 句尾/分段结束
        Partial, ///< 中间识别结果（可选）
        Final, ///< 最终结果
        Failed, ///< 后端识别失败
    };

    /// 后端事件；AsrNode 负责把它转换成 AsrEvent 并关联 session_id。
    struct BackendEvent {
        BackendEventType type{BackendEventType::Final};
        std::string text;
        std::string language;
        std::string error;
        float confidence{0.0F};
    };

    /// ASR 后端接口。
    ///
    /// 后端自行决定是否需要 VAD、是否流式、使用哪个模型；AsrNode 只负责
    /// 采集、重采样、门控、队列和 ROS 事件，不直接依赖 SenseVoice/KWS。
    /// 输入固定为 16 kHz mono float。
    class AsrBackend {
    public:
        virtual ~AsrBackend() = default;

        /// 后端名称，写入 AsrState/AsrEvent.backend。
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /// 加载模型；失败时 error 写入英文原因。
        virtual bool initialize(std::string* error) = 0;

        /// 模型是否已成功加载。
        [[nodiscard]] virtual bool ok() const = 0;

        /// 送入一块 16 kHz mono PCM，返回本次产生的事件。
        virtual std::vector<BackendEvent> push(const float* samples, std::size_t count) = 0;

        /// 冲刷尾部音频；用于采集 eos 或节点停止。
        virtual std::vector<BackendEvent> flush() = 0;

        /// 清空后端内部状态；用于播放门控或采集序号跳变。
        virtual void reset() = 0;
    };

} // namespace asr
