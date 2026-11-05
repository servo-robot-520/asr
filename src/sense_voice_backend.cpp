#include "asr/sense_voice_backend.hpp"

#include <iterator>
#include <utility>

namespace asr {

    SenseVoiceBackend::SenseVoiceBackend(SenseVoiceBackendConfig config) :
        config_(std::move(config)) {
    }

    std::string_view SenseVoiceBackend::name() const noexcept {
        return "sherpa_onnx_sense_voice";
    }

    bool SenseVoiceBackend::initialize(std::string* error) {
        std::string recognizer_error;
        if (!recognizer_.initialize(config_.recognizer, &recognizer_error)) {
            if (error != nullptr) {
                *error = "recognizer: " + recognizer_error;
            }
            return false;
        }

        std::string vad_error;
        if (!vad_.initialize(config_.vad, &vad_error)) {
            if (error != nullptr) {
                *error = "vad: " + vad_error;
            }
            return false;
        }
        return true;
    }

    bool SenseVoiceBackend::ok() const {
        return recognizer_.ok() && vad_.ok();
    }

    std::vector<BackendEvent> SenseVoiceBackend::push(const float* samples, const std::size_t count) {
        std::vector<BackendEvent> events;
        if (!ok() || samples == nullptr || count == 0) {
            return events;
        }

        vad_.accept(samples, count);
        if (!speech_active_ && (vad_.detected() || !vad_.empty())) {
            speech_active_ = true;
            BackendEvent event;
            event.type = BackendEventType::SpeechStart;
            events.push_back(std::move(event));
        }

        auto segments = drain_segments();
        events.insert(events.end(), std::make_move_iterator(segments.begin()),
                      std::make_move_iterator(segments.end()));
        return events;
    }

    std::vector<BackendEvent> SenseVoiceBackend::flush() {
        std::vector<BackendEvent> events;
        if (!ok()) {
            return events;
        }
        vad_.flush();
        if (!speech_active_ && !vad_.empty()) {
            speech_active_ = true;
            BackendEvent event;
            event.type = BackendEventType::SpeechStart;
            events.push_back(std::move(event));
        }
        auto segments = drain_segments();
        events.insert(events.end(), std::make_move_iterator(segments.begin()),
                      std::make_move_iterator(segments.end()));
        return events;
    }

    void SenseVoiceBackend::reset() {
        vad_.reset();
        speech_active_ = false;
    }

    std::vector<BackendEvent> SenseVoiceBackend::drain_segments() {
        std::vector<BackendEvent> events;
        while (!vad_.empty()) {
            auto segment = vad_.pop();
            if (!segment.has_value()) {
                break;
            }

            BackendEvent endpoint;
            endpoint.type = BackendEventType::Endpoint;
            events.push_back(std::move(endpoint));
            const auto result = recognizer_.recognize(segment->samples.data(), segment->samples.size());
            if (!result.has_value()) {
                BackendEvent failed;
                failed.type = BackendEventType::Failed;
                failed.error = result.error();
                events.push_back(std::move(failed));
                speech_active_ = false;
                continue;
            }

            speech_active_ = false;
            if (result->text.empty()) {
                continue;
            }

            BackendEvent final;
            final.type = BackendEventType::Final;
            final.text = result->text;
            final.language = result->language;
            events.push_back(std::move(final));
        }
        return events;
    }

} // namespace asr
