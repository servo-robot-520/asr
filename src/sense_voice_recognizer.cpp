#include "asr/sense_voice_recognizer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>

#include <sherpa-onnx/c-api/c-api.h>

#include "asr/path_utils.hpp"

namespace asr {

    namespace {

        /// 去掉字符串首尾的 ASCII 空白。
        std::string trim(const std::string_view value) {
            const auto first = value.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\n\r\f\v");
            return std::string{value.substr(first, last - first + 1)};
        }

        /// SenseVoice 返回的 language 形如 "<|zh|>"，转换为 "zh"。
        std::string normalize_language(const char* language) {
            if (language == nullptr) {
                return {};
            }
            std::string_view value{language};
            if (value.size() >= 4 && value.starts_with("<|") && value.ends_with("|>")) {
                value.remove_prefix(2);
                value.remove_suffix(2);
            }
            return std::string{value};
        }

    } // namespace

    SenseVoiceRecognizer::~SenseVoiceRecognizer() {
        if (recognizer_ != nullptr) {
            SherpaOnnxDestroyOfflineRecognizer(recognizer_);
            recognizer_ = nullptr;
        }
    }

    bool SenseVoiceRecognizer::initialize(const SenseVoiceConfig& config, std::string* error) {
        if (config.model.empty() || config.tokens.empty()) {
            if (error != nullptr) {
                *error = "recognizer.model and recognizer.tokens must be set";
            }
            return false;
        }

        const auto model = expand_user_path(config.model);
        const auto tokens = expand_user_path(config.tokens);

        SherpaOnnxOfflineRecognizerConfig recognizer_config{};
        recognizer_config.feat_config.sample_rate = 16000;
        recognizer_config.feat_config.feature_dim = 80;
        recognizer_config.model_config.sense_voice.model = model.c_str();
        recognizer_config.model_config.sense_voice.language = config.language.c_str();
        recognizer_config.model_config.sense_voice.use_itn = config.use_itn ? 1 : 0;
        recognizer_config.model_config.tokens = tokens.c_str();
        recognizer_config.model_config.num_threads = config.num_threads;
        recognizer_config.model_config.provider = config.provider.c_str();
        recognizer_config.model_config.debug = config.debug ? 1 : 0;
        recognizer_config.decoding_method = "greedy_search";

        recognizer_ = SherpaOnnxCreateOfflineRecognizer(&recognizer_config);
        if (recognizer_ == nullptr) {
            if (error != nullptr) {
                *error = "SherpaOnnxCreateOfflineRecognizer failed; check model and tokens";
            }
            return false;
        }
        return true;
    }

    std::expected<RecognitionResult, std::string>
    SenseVoiceRecognizer::recognize(const float* samples, const std::size_t count) const {
        if (recognizer_ == nullptr) {
            return std::unexpected("recognizer is not initialized");
        }
        if (samples == nullptr || count == 0) {
            return RecognitionResult{};
        }
        if (count > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
            return std::unexpected("utterance is too long for sherpa-onnx");
        }

        const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer_);
        if (stream == nullptr) {
            return std::unexpected("SherpaOnnxCreateOfflineStream failed");
        }

        SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, static_cast<int32_t>(count));
        SherpaOnnxDecodeOfflineStream(recognizer_, stream);
        const SherpaOnnxOfflineRecognizerResult* result = SherpaOnnxGetOfflineStreamResult(stream);
        if (result == nullptr) {
            SherpaOnnxDestroyOfflineStream(stream);
            return std::unexpected("SherpaOnnxGetOfflineStreamResult failed");
        }

        RecognitionResult output;
        if (result->text != nullptr) {
            output.text = trim(result->text);
        }
        output.language = normalize_language(result->lang);

        SherpaOnnxDestroyOfflineRecognizerResult(result);
        SherpaOnnxDestroyOfflineStream(stream);
        return output;
    }

} // namespace asr
