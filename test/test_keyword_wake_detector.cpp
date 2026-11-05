#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sherpa-onnx/c-api/c-api.h>

#include "asr/keyword_wake_detector.hpp"

namespace {

    const char* env_or_null(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? value : nullptr;
    }

    TEST(KeywordWakeDetector, DetectsWakeWordAndExposesLabels) {
        const char* encoder = env_or_null("ASR_KWS_ENCODER");
        const char* decoder = env_or_null("ASR_KWS_DECODER");
        const char* joiner = env_or_null("ASR_KWS_JOINER");
        const char* tokens = env_or_null("ASR_KWS_TOKENS");
        const char* keywords = env_or_null("ASR_KWS_KEYWORDS");
        const char* wave_path = env_or_null("ASR_KWS_TEST_WAV");
        if (encoder == nullptr || decoder == nullptr || joiner == nullptr || tokens == nullptr ||
            keywords == nullptr || wave_path == nullptr) {
            GTEST_SKIP() << "set ASR_KWS_ENCODER/DECODER/JOINER/TOKENS/KEYWORDS/TEST_WAV";
        }

        const SherpaOnnxWave* wave = SherpaOnnxReadWave(wave_path);
        ASSERT_NE(wave, nullptr) << "failed to read " << wave_path;

        asr::KeywordBackendConfig config;
        config.encoder = encoder;
        config.decoder = decoder;
        config.joiner = joiner;
        config.tokens = tokens;
        config.keywords_file = keywords;

        asr::KeywordWakeDetector detector(config);
        std::string error;
        ASSERT_TRUE(detector.initialize(&error)) << error;
        ASSERT_TRUE(detector.ok());
        ASSERT_FALSE(detector.labels().empty());

        std::vector<asr::WakeEvent> wakes;
        constexpr std::size_t kBlock = 1600;
        for (std::size_t offset = 0; offset < static_cast<std::size_t>(wave->num_samples); offset += kBlock) {
            const auto count = std::min<std::size_t>(
                kBlock, static_cast<std::size_t>(wave->num_samples) - offset);
            auto block = detector.push(wave->samples + offset, count);
            wakes.insert(wakes.end(), std::make_move_iterator(block.begin()),
                         std::make_move_iterator(block.end()));
        }

        ASSERT_FALSE(wakes.empty());
        EXPECT_FALSE(wakes.front().keyword.empty());
        if (const char* expected = env_or_null("ASR_KWS_EXPECT"); expected != nullptr) {
            bool matched = false;
            for (const auto& wake : wakes) {
                matched = matched || wake.keyword.find(expected) != std::string::npos;
            }
            EXPECT_TRUE(matched);
        }
        SherpaOnnxFreeWave(wave);
    }

} // namespace
