#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace asr {

    /// 带状态的线性重采样器。
    ///
    /// 采集流按块到达，重采样必须在块边界保留插值状态，否则每块都会产生
    /// 咔哒声。48 kHz → 16 kHz 时每 3 个输入样本产生 1 个输出样本。
    class LinearResampler {
    public:
        /// input_rate == output_rate 时直接拷贝；否则按线性插值降/升采样。
        LinearResampler(uint32_t input_rate, uint32_t output_rate);

        /// 推入一块 mono float；返回本块产生的输出。
        std::vector<float> push(const float* samples, std::size_t count);

        /// 构造时的输入采样率，单位 Hz。
        [[nodiscard]] uint32_t input_rate() const {
            return input_rate_;
        }

        /// 构造时的输出采样率，单位 Hz。
        [[nodiscard]] uint32_t output_rate() const {
            return output_rate_;
        }

        /// 清空跨块状态，用于音频流重启或采集序号跳变。
        void reset();

    private:
        uint32_t input_rate_{0}; ///< 输入采样率
        uint32_t output_rate_{0}; ///< 输出采样率
        double ratio_{1.0}; ///< input_rate / output_rate
        std::vector<float> buffer_; ///< 跨块保留的尾部样本
        double cursor_{0.0}; ///< 下一个输出在 buffer_ 中的位置
    };

} // namespace asr
