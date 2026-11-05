#include "asr/asr_node.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "asr_interface/ros_msg.hpp"
#include "audio_io_interface/ros_msg.hpp"
#include "common_cpp/logging/log_interface/log_macro.hpp"

namespace asr {

    namespace {

        constexpr std::string_view kBackendName = "sherpa_onnx_sense_voice";
        constexpr uint32_t kTargetSampleRate = 16000;

        /// 返回单调时钟当前时间；用于播放门控的 holdoff/max 计时。
        std::chrono::steady_clock::time_point steady_now() {
            return std::chrono::steady_clock::now();
        }

    } // namespace

    AsrNode::AsrNode(rclcpp::Node::SharedPtr node) :
        node_(std::move(node)) {
        if (!node_) {
            throw std::invalid_argument("AsrNode requires a non-null rclcpp::Node");
        }
        declare_parameters();
        initialize_backends();
        create_interfaces();
    }

    AsrNode::~AsrNode() {
        stop();
    }

    void AsrNode::declare_parameters() {
        // audio_io 的默认节点名来自 audio_io_interface，不在本包重复配置；
        // 只把它限定到 asr 当前 namespace，再用官方 helper 生成话题名。
        audio_io_node_ = audio_io_interface::qualify_node_fqn(node_->get_namespace());
        capture_topic_ = audio_io_interface::topic_capture(audio_io_node_);
        stream_id_ = declare_or_get<std::string>("capture.stream_id", "mic");
        frame_id_ = declare_or_get<std::string>("frame_id", "mic");

        gate_enabled_ = declare_or_get<bool>("gate.enabled", true);
        gate_holdoff_ms_ = std::max(0, declare_or_get<int>("gate.holdoff_ms", 300));
        gate_max_ms_ = std::max(gate_holdoff_ms_, declare_or_get<int>("gate.max_ms", 15000));
        state_period_ms_ = std::max(50, declare_or_get<int>("state.period_ms", 500));
        queue_max_ms_ = std::max(100, declare_or_get<int>("queue.max_ms", 2000));
    }

    void AsrNode::initialize_backends() {
        SenseVoiceConfig recognizer_config;
        recognizer_config.model = declare_or_get<std::string>("recognizer.model", "");
        recognizer_config.tokens = declare_or_get<std::string>("recognizer.tokens", "");
        recognizer_config.language = declare_or_get<std::string>("recognizer.language", "auto");
        recognizer_config.use_itn = declare_or_get<bool>("recognizer.use_itn", true);
        recognizer_config.num_threads = std::max(1, declare_or_get<int>("recognizer.num_threads", 1));
        recognizer_config.provider = declare_or_get<std::string>("recognizer.provider", "cpu");
        recognizer_config.debug = declare_or_get<bool>("recognizer.debug", false);

        VadConfig vad_config;
        vad_config.model = declare_or_get<std::string>("vad.model", "");
        vad_config.threshold = declare_or_get<float>("vad.threshold", 0.25F);
        vad_config.min_silence_duration = declare_or_get<float>("vad.min_silence_duration", 0.5F);
        vad_config.min_speech_duration = declare_or_get<float>("vad.min_speech_duration", 0.25F);
        vad_config.max_speech_duration = declare_or_get<float>("vad.max_speech_duration", 10.0F);
        vad_config.window_size = std::max(1, declare_or_get<int>("vad.window_size", 512));
        vad_config.buffer_seconds = declare_or_get<float>("vad.buffer_seconds", 30.0F);
        vad_config.num_threads = std::max(1, declare_or_get<int>("vad.num_threads", 1));
        vad_config.provider = declare_or_get<std::string>("vad.provider", "cpu");
        vad_config.debug = declare_or_get<bool>("vad.debug", false);

        std::string error;
        if (!recognizer_.initialize(recognizer_config, &error)) {
            set_last_error("recognizer: " + error);
            LOG_ERROR("ASR recognizer unavailable: {}", error);
        }
        if (!vad_.initialize(vad_config, &error)) {
            if (!last_error_.empty()) {
                set_last_error(last_error_ + "; vad: " + error);
            }
            else {
                set_last_error("vad: " + error);
            }
            LOG_ERROR("ASR VAD unavailable: {}", error);
        }

        model_loaded_ = recognizer_.ok() && vad_.ok();
        LOG_INFO("ASR backends: recognizer={} vad={} model_loaded={}",
                 recognizer_.ok(), vad_.ok(), model_loaded_.load());
    }

    void AsrNode::create_interfaces() {
        event_publisher_ = node_->create_publisher<asr_interface::msg::AsrEvent>(
            std::string(asr_interface::kPrivateTopicEvent), rclcpp::QoS(50));
        state_publisher_ = node_->create_publisher<asr_interface::msg::AsrState>(
            std::string(asr_interface::kPrivateTopicState), rclcpp::QoS(1).transient_local());

        capture_subscription_ = node_->create_subscription<audio_io_interface::msg::AudioStream>(
            capture_topic_, rclcpp::QoS(2).best_effort(),
            [this](const audio_io_interface::msg::AudioStream::SharedPtr message) {
                on_capture(message);
            });

        const auto audio_event_topic = audio_io_interface::topic_event(audio_io_node_);
        audio_event_subscription_ = node_->create_subscription<audio_io_interface::msg::AudioEvent>(
            audio_event_topic, rclcpp::QoS(50),
            [this](const audio_io_interface::msg::AudioEvent::SharedPtr message) {
                on_audio_event(message);
            });

        state_timer_ = node_->create_wall_timer(
            std::chrono::milliseconds(state_period_ms_),
            [this]() {
                update_gate();
                publish_state();
            });
        publish_state();
    }

    void AsrNode::start() {
        if (running_.exchange(true)) {
            return;
        }
        worker_ = std::thread(&AsrNode::worker_loop, this);
        LOG_INFO("ASR node started: capture={} target_rate={}", capture_topic_, kTargetSampleRate);
    }

    void AsrNode::stop() {
        if (!running_.exchange(false)) {
            return;
        }
        queue_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        LOG_INFO("ASR node stopped");
    }

    std::size_t AsrNode::bytes_per_sample(const std::string& encoding) {
        if (encoding == "s16le") {
            return 2;
        }
        if (encoding == "f32le") {
            return 4;
        }
        return 0;
    }

    void AsrNode::on_capture(const audio_io_interface::msg::AudioStream::SharedPtr& message) {
        if (!model_loaded_.load()) {
            return;
        }
        if (!stream_id_.empty() && message->stream_id != stream_id_) {
            return;
        }
        if (gated_.load()) {
            update_gate();
            if (gated_.load()) {
                return;
            }
        }

        const auto sample_bytes = bytes_per_sample(message->encoding);
        if (sample_bytes == 0 || message->channels == 0 || message->channels > 2 ||
            message->sample_rate == 0) {
            if (!warned_format_.exchange(true)) {
                LOG_WARN("ASR dropping unsupported AudioStream: encoding={} channels={} rate={}",
                         message->encoding, message->channels, message->sample_rate);
            }
            return;
        }
        const auto frame_bytes = sample_bytes * message->channels;
        if (!message->data.empty() && message->data.size() % frame_bytes != 0) {
            if (!warned_format_.exchange(true)) {
                LOG_WARN("ASR dropping AudioStream with unaligned data: bytes={} frame_bytes={}",
                         message->data.size(), frame_bytes);
            }
            return;
        }

        if (!message->eos) {
            if (sequence_valid_ && message->sequence != expected_sequence_) {
                audio_gap_count_.fetch_add(1);
                LOG_WARN("ASR audio gap: expected_sequence={} actual={}",
                         expected_sequence_, message->sequence);
                request_reset();
            }
            expected_sequence_ = message->sequence + 1;
            sequence_valid_ = true;
        }
        else {
            sequence_valid_ = false;
        }

        CaptureChunk chunk;
        chunk.data = message->data;
        chunk.encoding = message->encoding;
        chunk.sample_rate = message->sample_rate;
        chunk.channels = message->channels;
        chunk.sequence = message->sequence;
        chunk.frames = chunk.data.size() / frame_bytes;
        chunk.eos = message->eos;
        chunk.header = message->header;

        {
            std::lock_guard lock(queue_mutex_);
            const double chunk_ms = chunk.sample_rate > 0
                ? 1000.0 * static_cast<double>(chunk.frames) /
                    static_cast<double>(chunk.sample_rate)
                : 0.0;
            queued_ms_ += chunk_ms;
            while (queued_ms_ > static_cast<double>(queue_max_ms_) && queue_.size() > 1) {
                const auto& front = queue_.front();
                queued_ms_ -= front.sample_rate > 0
                    ? 1000.0 * static_cast<double>(front.frames) /
                        static_cast<double>(front.sample_rate)
                    : 0.0;
                queue_.pop_front();
            }
            queued_ms_ = std::max(0.0, queued_ms_);
            queue_.push_back(std::move(chunk));
        }
        queue_cv_.notify_one();
    }

    void AsrNode::on_audio_event(const audio_io_interface::msg::AudioEvent::SharedPtr& message) {
        if (!gate_enabled_) {
            return;
        }

        bool cancel_session = false;
        {
            std::lock_guard lock(gate_mutex_);
            if (message->event == audio_io_interface::msg::AudioEvent::EVENT_STARTED) {
                if (active_playbacks_ == 0) {
                    gate_started_ = steady_now();
                }
                ++active_playbacks_;
                gate_release_valid_ = false;
                gated_ = true;
                cancel_session = true;
            }
            else if (message->event == audio_io_interface::msg::AudioEvent::EVENT_FINISHED ||
                     message->event == audio_io_interface::msg::AudioEvent::EVENT_FAILED ||
                     message->event == audio_io_interface::msg::AudioEvent::EVENT_PREEMPTED) {
                if (active_playbacks_ > 0) {
                    --active_playbacks_;
                }
                if (active_playbacks_ == 0) {
                    gate_release_at_ = steady_now() + std::chrono::milliseconds(gate_holdoff_ms_);
                    gate_release_valid_ = true;
                }
            }
        }

        if (cancel_session) {
            request_reset();
        }
    }

    void AsrNode::update_gate() {
        if (!gate_enabled_) {
            return;
        }
        std::lock_guard lock(gate_mutex_);
        if (!gated_.load()) {
            return;
        }
        const auto now = steady_now();
        if (active_playbacks_ == 0 && gate_release_valid_ && now >= gate_release_at_) {
            gated_ = false;
            gate_release_valid_ = false;
            LOG_DEBUG("ASR gate released after playback holdoff");
        }
        if (now - gate_started_ >= std::chrono::milliseconds(gate_max_ms_)) {
            active_playbacks_ = 0;
            gate_release_valid_ = false;
            gated_ = false;
            LOG_WARN("ASR gate forced open after {} ms", gate_max_ms_);
        }
    }

    void AsrNode::request_reset() {
        reset_requested_ = true;
        {
            std::lock_guard lock(queue_mutex_);
            queue_.clear();
            queued_ms_ = 0.0;
        }
        if (session_active_.exchange(false)) {
            publish_event(asr_interface::msg::AsrEvent::EVENT_CANCELED, "", "", "");
        }
    }

    void AsrNode::worker_loop() {
        while (true) {
            CaptureChunk chunk;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this]() {
                    return !running_.load() || !queue_.empty();
                });
                if (!running_.load() && queue_.empty()) {
                    break;
                }
                chunk = std::move(queue_.front());
                queue_.pop_front();
                queued_ms_ -= chunk.sample_rate > 0
                    ? 1000.0 * static_cast<double>(chunk.frames) /
                        static_cast<double>(chunk.sample_rate)
                    : 0.0;
                queued_ms_ = std::max(0.0, queued_ms_);
            }

            if (reset_requested_.exchange(false)) {
                vad_.reset();
                speech_active_ = false;
            }

            try {
                process_chunk(chunk);
            }
            catch (const std::exception& exception) {
                set_last_error(exception.what());
                LOG_ERROR("ASR worker error: {}", exception.what());
                publish_event(asr_interface::msg::AsrEvent::EVENT_FAILED, "", "", exception.what());
            }
        }
    }

    std::vector<float> AsrNode::to_16k_mono(const CaptureChunk& chunk) {
        const auto sample_bytes = bytes_per_sample(chunk.encoding);
        if (sample_bytes == 0 || chunk.channels == 0) {
            return {};
        }

        std::vector<float> mono(chunk.frames, 0.0F);
        const auto channels = static_cast<std::size_t>(chunk.channels);
        const auto payload = chunk.data.data();

        for (std::size_t frame = 0; frame < chunk.frames; ++frame) {
            float sum = 0.0F;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto offset = (frame * channels + channel) * sample_bytes;
                if (chunk.encoding == "s16le") {
                    int16_t value = 0;
                    std::memcpy(&value, payload + offset, sizeof(value));
                    sum += static_cast<float>(value) / 32768.0F;
                }
                else {
                    float value = 0.0F;
                    std::memcpy(&value, payload + offset, sizeof(value));
                    sum += value;
                }
            }
            mono[frame] = sum / static_cast<float>(channels);
        }

        if (!resampler_ || resampler_input_rate_ != chunk.sample_rate) {
            resampler_ = std::make_unique<LinearResampler>(chunk.sample_rate, kTargetSampleRate);
            resampler_input_rate_ = chunk.sample_rate;
        }
        return resampler_->push(mono.data(), mono.size());
    }

    void AsrNode::process_chunk(const CaptureChunk& chunk) {
        if (chunk.eos) {
            vad_.flush();
            if (!speech_active_ && !vad_.empty()) {
                begin_session();
            }
            drain_segments();
            return;
        }
        if (gated_.load() || !model_loaded_.load()) {
            return;
        }

        const auto samples = to_16k_mono(chunk);
        if (samples.empty()) {
            return;
        }

        vad_.accept(samples.data(), samples.size());
        if (!speech_active_ && (vad_.detected() || !vad_.empty())) {
            begin_session();
        }
        drain_segments();
    }

    void AsrNode::drain_segments() {
        while (!vad_.empty()) {
            auto segment = vad_.pop();
            if (!segment.has_value()) {
                break;
            }
            if (gated_.load() || reset_requested_.load()) {
                end_session();
                continue;
            }
            if (!session_active_.load()) {
                begin_session();
            }

            publish_event(asr_interface::msg::AsrEvent::EVENT_ENDPOINT, "", "", "");
            const auto result = recognizer_.recognize(segment->samples.data(), segment->samples.size());

            if (gated_.load() || reset_requested_.load() || !session_active_.load()) {
                end_session();
                continue;
            }
            if (!result.has_value()) {
                set_last_error(result.error());
                LOG_ERROR("ASR recognition failed: {}", result.error());
                publish_event(asr_interface::msg::AsrEvent::EVENT_FAILED, "", "", result.error());
                end_session();
                continue;
            }

            if (result->text.empty()) {
                LOG_DEBUG("ASR segment produced empty text");
                end_session();
                continue;
            }

            set_last_error("");
            std::string session_id;
            {
                std::lock_guard lock(state_mutex_);
                session_id = current_session_id_;
            }
            LOG_INFO("ASR final: session={} text={}", session_id, result->text);
            publish_event(asr_interface::msg::AsrEvent::EVENT_FINAL,
                          result->text, result->language, "");
            end_session();
        }
    }

    void AsrNode::begin_session() {
        speech_active_ = true;
        session_active_ = true;
        {
            std::lock_guard lock(state_mutex_);
            current_session_id_ = std::to_string(++session_counter_);
        }
        publish_event(asr_interface::msg::AsrEvent::EVENT_SPEECH_START, "", "", "");
    }

    void AsrNode::end_session() {
        speech_active_ = false;
        session_active_ = false;
        std::lock_guard lock(state_mutex_);
        current_session_id_.clear();
    }

    void AsrNode::publish_event(const uint8_t event, const std::string& text,
                                const std::string& language, const std::string& error) const {
        asr_interface::msg::AsrEvent message;
        message.header.stamp = node_->now();
        message.header.frame_id = frame_id_;
        message.event = event;
        message.text = text;
        message.language = language;
        message.backend = kBackendName;
        message.error = error;
        message.confidence = 0.0F;
        {
            std::lock_guard lock(state_mutex_);
            message.session_id = current_session_id_;
        }
        event_publisher_->publish(message);
    }

    void AsrNode::publish_state() const {
        asr_interface::msg::AsrState message;
        message.header.stamp = node_->now();
        message.header.frame_id = frame_id_;
        message.backend = kBackendName;
        message.model_loaded = model_loaded_.load();
        message.mode = "free_asr";
        message.enabled = true;
        message.gated = gated_.load();
        message.session_active = session_active_.load();
        message.audio_gap_count = audio_gap_count_.load();
        {
            std::lock_guard lock(state_mutex_);
            message.last_error = last_error_;
        }
        state_publisher_->publish(message);
    }

    void AsrNode::set_last_error(std::string error) {
        std::lock_guard lock(state_mutex_);
        last_error_ = std::move(error);
    }

} // namespace asr
