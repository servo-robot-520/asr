#include "asr/keyword_backend.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include <sherpa-onnx/c-api/c-api.h>

#include "asr/path_utils.hpp"

namespace asr {

    KeywordBackend::KeywordBackend(KeywordBackendConfig config) :
        config_(std::move(config)) {
    }

    KeywordBackend::~KeywordBackend() {
        if (stream_ != nullptr) {
            SherpaOnnxDestroyOnlineStream(stream_);
            stream_ = nullptr;
        }
        if (spotter_ != nullptr) {
            SherpaOnnxDestroyKeywordSpotter(spotter_);
            spotter_ = nullptr;
        }
    }

    std::string_view KeywordBackend::name() const noexcept {
        return "sherpa_onnx_keyword";
    }

    bool KeywordBackend::initialize(std::string* error) {
        if (config_.encoder.empty() || config_.decoder.empty() || config_.joiner.empty() ||
            config_.tokens.empty() || config_.keywords_file.empty()) {
            if (error != nullptr) {
                *error = "keyword.encoder/decoder/joiner/tokens/keywords_file must be set";
            }
            return false;
        }

        const auto encoder = expand_user_path(config_.encoder);
        const auto decoder = expand_user_path(config_.decoder);
        const auto joiner = expand_user_path(config_.joiner);
        const auto tokens = expand_user_path(config_.tokens);
        const auto keywords = expand_user_path(config_.keywords_file);

        SherpaOnnxKeywordSpotterConfig config{};
        config.feat_config.sample_rate = 16000;
        config.feat_config.feature_dim = 80;
        config.model_config.transducer.encoder = encoder.c_str();
        config.model_config.transducer.decoder = decoder.c_str();
        config.model_config.transducer.joiner = joiner.c_str();
        config.model_config.tokens = tokens.c_str();
        config.model_config.num_threads = config_.num_threads;
        config.model_config.provider = config_.provider.c_str();
        config.model_config.debug = config_.debug ? 1 : 0;
        config.max_active_paths = config_.max_active_paths;
        config.num_trailing_blanks = config_.num_trailing_blanks;
        config.keywords_score = config_.keywords_score;
        config.keywords_threshold = config_.keywords_threshold;
        config.keywords_file = keywords.c_str();

        spotter_ = SherpaOnnxCreateKeywordSpotter(&config);
        if (spotter_ == nullptr) {
            if (error != nullptr) {
                *error = "SherpaOnnxCreateKeywordSpotter failed; check keyword model files";
            }
            return false;
        }

        stream_ = SherpaOnnxCreateKeywordStream(spotter_);
        if (stream_ == nullptr) {
            if (error != nullptr) {
                *error = "SherpaOnnxCreateKeywordStream failed";
            }
            SherpaOnnxDestroyKeywordSpotter(spotter_);
            spotter_ = nullptr;
            return false;
        }
        return true;
    }

    bool KeywordBackend::ok() const {
        return spotter_ != nullptr && stream_ != nullptr;
    }

    std::vector<BackendEvent> KeywordBackend::push(const float* samples, std::size_t count) {
        std::vector<BackendEvent> events;
        if (!ok() || samples == nullptr || count == 0) {
            return events;
        }
        if (count > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
            return events;
        }

        SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, samples, static_cast<int32_t>(count));
        while (SherpaOnnxIsKeywordStreamReady(spotter_, stream_) != 0) {
            SherpaOnnxDecodeKeywordStream(spotter_, stream_);
            const SherpaOnnxKeywordResult* result = SherpaOnnxGetKeywordResult(spotter_, stream_);
            if (result == nullptr) {
                break;
            }

            if (result->keyword != nullptr && std::strlen(result->keyword) > 0) {
                BackendEvent event;
                event.type = BackendEventType::Final;
                event.text = result->keyword;
                events.push_back(std::move(event));
                SherpaOnnxResetKeywordStream(spotter_, stream_);
            }
            SherpaOnnxDestroyKeywordResult(result);
        }
        return events;
    }

    std::vector<BackendEvent> KeywordBackend::flush() {
        return {};
    }

    void KeywordBackend::reset() {
        if (ok()) {
            SherpaOnnxResetKeywordStream(spotter_, stream_);
        }
    }

} // namespace asr
