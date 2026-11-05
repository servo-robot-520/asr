#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>
#include <sherpa-onnx/c-api/c-api.h>

#include "asr/vad_segmenter.hpp"

namespace {

    const char* env_or_null(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? value : nullptr;
    }

    TEST(VadSegmenter, SplitsTestWaveIntoSpeechSegments) {
        const char* model = env_or_null("ASR_VAD_MODEL");
        const char* wave_path = env_or_null("ASR_TEST_WAV");
        if (model == nullptr || wave_path == nullptr) {
            GTEST_SKIP() << "set ASR_VAD_MODEL and ASR_TEST_WAV";
        }

        const SherpaOnnxWave* wave = SherpaOnnxReadWave(wave_path);
        ASSERT_NE(wave, nullptr) << "failed to read " << wave_path;

        asr::VadConfig config;
        config.model = model;
        config.min_silence_duration = 0.3F;
        config.min_speech_duration = 0.2F;

        asr::VadSegmenter vad;
        std::string error;
        ASSERT_TRUE(vad.initialize(config, &error)) << error;

        constexpr std::size_t kBlock = 512;
        for (std::size_t offset = 0; offset < static_cast<std::size_t>(wave->num_samples); offset += kBlock) {
            const auto count = std::min<std::size_t>(kBlock, static_cast<std::size_t>(wave->num_samples) - offset);
            vad.accept(wave->samples + offset, count);
        }
        vad.flush();

        std::size_t segment_count = 0;
        std::size_t sample_count = 0;
        while (!vad.empty()) {
            const auto segment = vad.pop();
            ASSERT_TRUE(segment.has_value());
            EXPECT_FALSE(segment->samples.empty());
            ++segment_count;
            sample_count += segment->samples.size();
        }

        EXPECT_GE(segment_count, 1U);
        EXPECT_GT(sample_count, 0U);
        SherpaOnnxFreeWave(wave);
    }

} // namespace
