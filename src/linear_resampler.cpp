#include "asr/linear_resampler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace asr {

    LinearResampler::LinearResampler(const uint32_t input_rate, const uint32_t output_rate) :
        input_rate_(input_rate), output_rate_(output_rate) {
        if (input_rate_ == 0 || output_rate_ == 0) {
            throw std::invalid_argument("resampler rates must be positive");
        }
        ratio_ = static_cast<double>(input_rate_) / static_cast<double>(output_rate_);
        reset();
    }

    std::vector<float> LinearResampler::push(const float* samples, const std::size_t count) {
        std::vector<float> output;
        if (count == 0) {
            return output;
        }
        if (input_rate_ == output_rate_) {
            output.assign(samples, samples + count);
            return output;
        }

        buffer_.insert(buffer_.end(), samples, samples + count);
        if (buffer_.size() < 2) {
            return output;
        }

        // cursor_ 指向下一个输出样本在 buffer_ 中的浮点位置；保留最后一个
        // 输入样本，保证下一块可以继续插值。
        while (cursor_ + 1.0 < static_cast<double>(buffer_.size())) {
            const auto index = static_cast<std::size_t>(cursor_);
            const auto fraction = static_cast<float>(cursor_ - static_cast<double>(index));
            output.push_back(buffer_[index] * (1.0F - fraction) + buffer_[index + 1] * fraction);
            cursor_ += ratio_;
        }

        if (const auto consumed = std::min<std::size_t>(static_cast<std::size_t>(cursor_), buffer_.size() - 1);
            consumed > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
            cursor_ -= static_cast<double>(consumed);
        }
        return output;
    }

    void LinearResampler::reset() {
        buffer_.clear();
        cursor_ = 0.0;
    }

} // namespace asr
