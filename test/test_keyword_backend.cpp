#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sherpa-onnx/c-api/c-api.h>

#include "asr/keyword_backend.hpp"

namespace {

    const char* env_or_null(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? value : nullptr;
    }

    TEST(KeywordBackend, DetectsKeywordInTestWave) {
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

        asr::KeywordBackend backend(std::move(config));
        std::string error;
        ASSERT_TRUE(backend.initialize(&error)) << error;
        ASSERT_TRUE(backend.ok());

        std::vector<asr::BackendEvent> events;
        constexpr std::size_t kBlock = 1600;
        for (std::size_t offset = 0; offset < static_cast<std::size_t>(wave->num_samples); offset += kBlock) {
            const auto count = std::min<std::size_t>(
                kBlock, static_cast<std::size_t>(wave->num_samples) - offset);
            auto block = backend.push(wave->samples + offset, count);
            events.insert(events.end(), std::make_move_iterator(block.begin()),
                          std::make_move_iterator(block.end()));
        }
        auto tail = backend.flush();
        events.insert(events.end(), std::make_move_iterator(tail.begin()),
                      std::make_move_iterator(tail.end()));

        bool found = false;
        bool expected_found = false;
        const char* expected = env_or_null("ASR_KWS_EXPECT");
        for (const auto& event : events) {
            if (event.type == asr::BackendEventType::Final && !event.text.empty()) {
                found = true;
                if (expected != nullptr && event.text.find(expected) != std::string::npos) {
                    expected_found = true;
                }
            }
        }
        EXPECT_TRUE(found);
        if (expected != nullptr) {
            EXPECT_TRUE(expected_found) << "expected keyword not detected: " << expected;
        }
        SherpaOnnxFreeWave(wave);
    }

} // namespace
