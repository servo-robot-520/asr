#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "asr/linear_resampler.hpp"

namespace {

    std::vector<float> make_ramp(const std::size_t count) {
        std::vector<float> samples(count);
        for (std::size_t index = 0; index < count; ++index) {
            samples[index] = static_cast<float>(index) * 0.001F;
        }
        return samples;
    }

    TEST(LinearResampler, PassThroughWhenRatesMatch) {
        asr::LinearResampler resampler(16000, 16000);
        const auto input = make_ramp(320);
        const auto output = resampler.push(input.data(), input.size());

        ASSERT_EQ(output.size(), input.size());
        EXPECT_EQ(output, input);
    }

    TEST(LinearResampler, Downsamples48kTo16kAcrossBlocks) {
        asr::LinearResampler resampler(48000, 16000);
        const auto input = make_ramp(4800);
        std::vector<float> output;

        for (std::size_t offset = 0; offset < input.size(); offset += 480) {
            const auto block = resampler.push(input.data() + offset, 480);
            output.insert(output.end(), block.begin(), block.end());
        }

        // 线性插值有一个样本的块内延迟，允许少量误差。
        EXPECT_NEAR(static_cast<double>(output.size()), 1600.0, 2.0);
        for (const float sample : output) {
            EXPECT_TRUE(std::isfinite(sample));
        }
    }

    TEST(LinearResampler, Upsamples16kTo48k) {
        asr::LinearResampler resampler(16000, 48000);
        const auto input = make_ramp(1600);
        const auto output = resampler.push(input.data(), input.size());

        EXPECT_NEAR(static_cast<double>(output.size()), 4800.0, 2.0);
        EXPECT_TRUE(std::isfinite(output.front()));
        EXPECT_TRUE(std::isfinite(output.back()));
    }

    TEST(LinearResampler, ResetClearsInterpolationState) {
        asr::LinearResampler resampler(48000, 16000);
        const auto input = make_ramp(480);
        static_cast<void>(resampler.push(input.data(), input.size()));
        resampler.reset();

        const auto output = resampler.push(input.data(), input.size());
        EXPECT_NEAR(static_cast<double>(output.size()), 160.0, 2.0);
    }

} // namespace
