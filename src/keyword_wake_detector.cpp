#include "asr/keyword_wake_detector.hpp"

#include <fstream>
#include <string>
#include <utility>

#include "asr/path_utils.hpp"

namespace asr {

    namespace {

        std::string trim(const std::string& value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        /// 从 text2token 生成的 keywords.txt 中读取 @ 后的显示词。
        std::vector<std::string> load_labels(const std::string& path, std::string* error) {
            std::vector<std::string> labels;
            std::ifstream file(expand_user_path(path));
            if (!file) {
                if (error != nullptr) {
                    *error = "cannot open wake.keywords_file: " + path;
                }
                return labels;
            }

            std::string line;
            while (std::getline(file, line)) {
                const auto at = line.find_last_of('@');
                if (at == std::string::npos) {
                    continue;
                }
                auto label = trim(line.substr(at + 1));
                if (!label.empty()) {
                    labels.push_back(std::move(label));
                }
            }
            if (labels.empty() && error != nullptr) {
                *error = "wake.keywords_file has no '@label' entries: " + path;
            }
            return labels;
        }

    } // namespace

    KeywordWakeDetector::KeywordWakeDetector(KeywordBackendConfig config) :
        keywords_file_(config.keywords_file), backend_(std::move(config)) {
    }

    std::string_view KeywordWakeDetector::name() const noexcept {
        return "sherpa_onnx_keyword_wake";
    }

    bool KeywordWakeDetector::initialize(std::string* error) {
        if (!backend_.initialize(error)) {
            return false;
        }
        labels_ = load_labels(keywords_file_, error);
        return !labels_.empty();
    }

    bool KeywordWakeDetector::ok() const {
        return backend_.ok() && !labels_.empty();
    }

    std::vector<WakeEvent> KeywordWakeDetector::push(const float* samples, std::size_t count) {
        std::vector<WakeEvent> wakes;
        for (auto& event : backend_.push(samples, count)) {
            if (event.type == BackendEventType::Final && !event.text.empty()) {
                wakes.push_back(WakeEvent{.keyword = std::move(event.text)});
            }
        }
        return wakes;
    }

    void KeywordWakeDetector::reset() {
        backend_.reset();
    }

    const std::vector<std::string>& KeywordWakeDetector::labels() const {
        return labels_;
    }

} // namespace asr
