#include <cstdlib>
#include <string>

#include <gtest/gtest.h>
#include <sherpa-onnx/c-api/c-api.h>

#include "asr/sense_voice_recognizer.hpp"

namespace {

    const char* env_or_null(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? value : nullptr;
    }

    TEST(SenseVoiceRecognizer, RecognizesTestWave) {
        const char* model = env_or_null("ASR_SENSE_VOICE_MODEL");
        const char* tokens = env_or_null("ASR_SENSE_VOICE_TOKENS");
        const char* wave_path = env_or_null("ASR_TEST_WAV");
        if (model == nullptr || tokens == nullptr || wave_path == nullptr) {
            GTEST_SKIP() << "set ASR_SENSE_VOICE_MODEL, ASR_SENSE_VOICE_TOKENS and ASR_TEST_WAV";
        }

        const SherpaOnnxWave* wave = SherpaOnnxReadWave(wave_path);
        ASSERT_NE(wave, nullptr) << "failed to read " << wave_path;

        asr::SenseVoiceConfig config;
        config.model = model;
        config.tokens = tokens;
        config.language = env_or_null("ASR_LANGUAGE") != nullptr ? env_or_null("ASR_LANGUAGE") : "zh";

        asr::SenseVoiceRecognizer recognizer;
        std::string error;
        ASSERT_TRUE(recognizer.initialize(config, &error)) << error;
        ASSERT_TRUE(recognizer.ok());

        const auto result = recognizer.recognize(wave->samples, static_cast<std::size_t>(wave->num_samples));
        ASSERT_TRUE(result.has_value()) << result.error();
        EXPECT_FALSE(result->text.empty());
        EXPECT_EQ(result->language, "zh");

        if (const char* expected = env_or_null("ASR_EXPECT_TEXT"); expected != nullptr) {
            EXPECT_NE(result->text.find(expected), std::string::npos) << result->text;
        }

        SherpaOnnxFreeWave(wave);
    }

} // namespace
