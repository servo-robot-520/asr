#include "asr/backend_factory.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "asr/keyword_backend.hpp"
#include "asr/keyword_wake_detector.hpp"
#include "asr/sense_voice_backend.hpp"

namespace asr {

    namespace {

        template <typename T>
        T declare_or_get(rclcpp::Node& node, const std::string& name, const T& default_value) {
            if (node.has_parameter(name)) {
                return node.get_parameter(name).get_value<T>();
            }
            return node.declare_parameter<T>(name, default_value);
        }

        std::unique_ptr<AsrBackend> create_sense_voice(rclcpp::Node& node) {
            SenseVoiceBackendConfig config;
            config.recognizer.model = declare_or_get<std::string>(node, "recognizer.model", "");
            config.recognizer.tokens = declare_or_get<std::string>(node, "recognizer.tokens", "");
            config.recognizer.language = declare_or_get<std::string>(node, "recognizer.language", "auto");
            config.recognizer.use_itn = declare_or_get<bool>(node, "recognizer.use_itn", true);
            config.recognizer.num_threads =
                std::max(1, declare_or_get<int>(node, "recognizer.num_threads", 1));
            config.recognizer.provider =
                declare_or_get<std::string>(node, "recognizer.provider", "cpu");
            config.recognizer.debug = declare_or_get<bool>(node, "recognizer.debug", false);

            config.vad.model = declare_or_get<std::string>(node, "vad.model", "");
            config.vad.threshold = declare_or_get<float>(node, "vad.threshold", 0.25F);
            config.vad.min_silence_duration =
                declare_or_get<float>(node, "vad.min_silence_duration", 0.5F);
            config.vad.min_speech_duration =
                declare_or_get<float>(node, "vad.min_speech_duration", 0.25F);
            config.vad.max_speech_duration =
                declare_or_get<float>(node, "vad.max_speech_duration", 10.0F);
            config.vad.window_size = std::max(1, declare_or_get<int>(node, "vad.window_size", 512));
            config.vad.buffer_seconds = declare_or_get<float>(node, "vad.buffer_seconds", 30.0F);
            config.vad.num_threads = std::max(1, declare_or_get<int>(node, "vad.num_threads", 1));
            config.vad.provider = declare_or_get<std::string>(node, "vad.provider", "cpu");
            config.vad.debug = declare_or_get<bool>(node, "vad.debug", false);

            return std::make_unique<SenseVoiceBackend>(std::move(config));
        }

        KeywordBackendConfig read_keyword_config(rclcpp::Node& node) {
            KeywordBackendConfig config;
            config.encoder = declare_or_get<std::string>(node, "keyword.encoder", "");
            config.decoder = declare_or_get<std::string>(node, "keyword.decoder", "");
            config.joiner = declare_or_get<std::string>(node, "keyword.joiner", "");
            config.tokens = declare_or_get<std::string>(node, "keyword.tokens", "");
            config.max_active_paths =
                std::max(1, declare_or_get<int>(node, "keyword.max_active_paths", 4));
            config.num_trailing_blanks =
                std::max(0, declare_or_get<int>(node, "keyword.num_trailing_blanks", 1));
            config.keywords_score = declare_or_get<float>(node, "keyword.keywords_score", 1.0F);
            config.keywords_threshold =
                declare_or_get<float>(node, "keyword.keywords_threshold", 0.25F);
            config.num_threads = std::max(1, declare_or_get<int>(node, "keyword.num_threads", 1));
            config.provider = declare_or_get<std::string>(node, "keyword.provider", "cpu");
            config.debug = declare_or_get<bool>(node, "keyword.debug", false);
            return config;
        }

        std::unique_ptr<AsrBackend> create_keyword(rclcpp::Node& node) {
            auto config = read_keyword_config(node);
            config.keywords_file = declare_or_get<std::string>(node, "keyword.keywords_file", "");
            return std::make_unique<KeywordBackend>(std::move(config));
        }

    } // namespace

    std::unique_ptr<AsrBackend> create_backend(const std::string_view backend_type,
                                               rclcpp::Node& node,
                                               std::string* error) {
        if (backend_type == "sense_voice") {
            return create_sense_voice(node);
        }
        if (backend_type == "keyword") {
            return create_keyword(node);
        }
        if (error != nullptr) {
            *error = "unknown backend '" + std::string{backend_type} +
                "'; supported: sense_voice, keyword";
        }
        return nullptr;
    }

    std::unique_ptr<WakeWordDetector> create_wake_word_detector(rclcpp::Node& node,
                                                                std::string* error) {
        auto config = read_keyword_config(node);
        config.keywords_file = declare_or_get<std::string>(node, "wake.keywords_file", "");
        if (config.keywords_file.empty()) {
            if (error != nullptr) {
                *error = "wake.keywords_file must be set when wake.enabled=true";
            }
            return nullptr;
        }
        return std::make_unique<KeywordWakeDetector>(std::move(config));
    }

} // namespace asr
