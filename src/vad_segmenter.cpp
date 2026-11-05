#include "asr/vad_segmenter.hpp"

#include <algorithm>
#include <utility>

#include <sherpa-onnx/c-api/c-api.h>

#include "asr/path_utils.hpp"

namespace asr {

    VadSegmenter::~VadSegmenter() {
        if (vad_ != nullptr) {
            SherpaOnnxDestroyVoiceActivityDetector(vad_);
            vad_ = nullptr;
        }
    }

    bool VadSegmenter::initialize(const VadConfig& config, std::string* error) {
        if (config.model.empty()) {
            if (error != nullptr) {
                *error = "vad.model must be set";
            }
            return false;
        }
        if (config.window_size <= 0) {
            if (error != nullptr) {
                *error = "vad.window_size must be positive";
            }
            return false;
        }

        const auto model = expand_user_path(config.model);
        window_size_ = config.window_size;

        SherpaOnnxVadModelConfig vad_config{};
        vad_config.silero_vad.model = model.c_str();
        vad_config.silero_vad.threshold = config.threshold;
        vad_config.silero_vad.min_silence_duration = config.min_silence_duration;
        vad_config.silero_vad.min_speech_duration = config.min_speech_duration;
        vad_config.silero_vad.max_speech_duration = config.max_speech_duration;
        vad_config.silero_vad.window_size = config.window_size;
        vad_config.sample_rate = 16000;
        vad_config.num_threads = config.num_threads;
        vad_config.provider = config.provider.c_str();
        vad_config.debug = config.debug ? 1 : 0;

        vad_ = SherpaOnnxCreateVoiceActivityDetector(&vad_config, config.buffer_seconds);
        if (vad_ == nullptr) {
            if (error != nullptr) {
                *error = "SherpaOnnxCreateVoiceActivityDetector failed; check vad.model";
            }
            return false;
        }
        pending_.clear();
        pending_.reserve(static_cast<std::size_t>(window_size_));
        return true;
    }

    void VadSegmenter::accept(const float* samples, const std::size_t count) {
        if (vad_ == nullptr || samples == nullptr || count == 0) {
            return;
        }

        const auto window = static_cast<std::size_t>(window_size_);
        std::size_t offset = 0;
        if (!pending_.empty()) {
            const auto needed = std::min(window - pending_.size(), count);
            pending_.insert(pending_.end(), samples, samples + needed);
            offset += needed;
            if (pending_.size() == window) {
                SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, pending_.data(), window_size_);
                pending_.clear();
            }
        }

        while (count - offset >= window) {
            SherpaOnnxVoiceActivityDetectorAcceptWaveform(
                vad_, samples + offset, static_cast<int32_t>(window));
            offset += window;
        }

        if (offset < count) {
            pending_.insert(pending_.end(), samples + offset, samples + count);
        }
    }

    bool VadSegmenter::detected() const {
        return vad_ != nullptr && SherpaOnnxVoiceActivityDetectorDetected(vad_) != 0;
    }

    bool VadSegmenter::empty() const {
        return vad_ == nullptr || SherpaOnnxVoiceActivityDetectorEmpty(vad_) != 0;
    }

    std::optional<VadSegment> VadSegmenter::pop() {
        if (vad_ == nullptr || empty()) {
            return std::nullopt;
        }

        const SherpaOnnxSpeechSegment* segment = SherpaOnnxVoiceActivityDetectorFront(vad_);
        if (segment == nullptr) {
            return std::nullopt;
        }

        VadSegment output;
        output.start = segment->start;
        if (segment->samples != nullptr && segment->n > 0) {
            output.samples.assign(segment->samples, segment->samples + segment->n);
        }
        SherpaOnnxDestroySpeechSegment(segment);
        SherpaOnnxVoiceActivityDetectorPop(vad_);
        return output;
    }

    void VadSegmenter::flush() {
        if (vad_ == nullptr) {
            return;
        }
        if (!pending_.empty()) {
            // 尾部不足一个窗口时补零，避免最后一段语音被丢弃。
            pending_.resize(static_cast<std::size_t>(window_size_), 0.0F);
            SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, pending_.data(), window_size_);
            pending_.clear();
        }
        SherpaOnnxVoiceActivityDetectorFlush(vad_);
    }

    void VadSegmenter::reset() {
        pending_.clear();
        if (vad_ != nullptr) {
            SherpaOnnxVoiceActivityDetectorClear(vad_);
            SherpaOnnxVoiceActivityDetectorReset(vad_);
        }
    }

} // namespace asr
