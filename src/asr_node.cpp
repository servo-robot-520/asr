#include "asr/asr_node.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "asr/backend_factory.hpp"
#include "asr_interface/ros_msg.hpp"
#include "audio_io_interface/ros_msg.hpp"
#include "common_cpp/logging/log_interface/log_macro.hpp"

namespace asr {

    namespace {

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
        // audio_io 的默认节点名由 audio_io_interface 提供；topic_capture()
        // 会先用当前 asr 的 namespace 限定节点名，再生成采集话题。
        capture_topic_ = audio_io_interface::topic_capture(
            audio_io_interface::kDefaultNodeFqn, node_->get_namespace());
        stream_id_ = declare_or_get<std::string>("capture.stream_id", "mic");
        frame_id_ = declare_or_get<std::string>("frame_id", "mic");

        gate_enabled_ = declare_or_get<bool>("gate.enabled", true);
        gate_holdoff_ms_ = std::max(0, declare_or_get<int>("gate.holdoff_ms", 300));
        gate_max_ms_ = std::max(gate_holdoff_ms_, declare_or_get<int>("gate.max_ms", 15000));
        state_period_ms_ = std::max(50, declare_or_get<int>("state.period_ms", 500));
        queue_max_ms_ = std::max(100, declare_or_get<int>("queue.max_ms", 2000));

        wake_enabled_ = declare_or_get<bool>("wake.enabled", false);
        const auto pre_roll_ms = std::max(0, declare_or_get<int>("wake.pre_roll_ms", 1000));
        pre_roll_capacity_ = static_cast<std::size_t>(kTargetSampleRate) *
            static_cast<std::size_t>(pre_roll_ms) / 1000U;
        followup_timeout_ms_ = std::max(1000, declare_or_get<int>("wake.followup_timeout_ms", 6000));
    }

    void AsrNode::initialize_backends() {
        backend_type_ = declare_or_get<std::string>("backend", "sense_voice");

        std::string error;
        backend_ = create_backend(backend_type_, *node_, &error);
        if (!backend_) {
            set_last_error(error);
            LOG_ERROR("ASR backend creation failed: {}", error);
            model_loaded_ = false;
            return;
        }

        if (!backend_->initialize(&error)) {
            set_last_error(backend_type_ + ": " + error);
            LOG_ERROR("ASR backend '{}' unavailable: {}", backend_type_, error);
        }

        model_loaded_ = backend_->ok();
        LOG_INFO("ASR backend: type={} name={} model_loaded={}",
                 backend_type_, backend_->name(), model_loaded_.load());

        if (!wake_enabled_) {
            return;
        }

        std::string wake_error;
        wake_detector_ = create_wake_word_detector(*node_, &wake_error);
        if (!wake_detector_ || !wake_detector_->initialize(&wake_error)) {
            wake_ready_ = false;
            set_last_error("wake: " + wake_error);
            LOG_ERROR("ASR wake detector unavailable: {}", wake_error);
            return;
        }

        wake_labels_ = wake_detector_->labels();
        wake_ready_ = true;
        LOG_INFO("ASR wake detector: name={} labels={} followup_ms={} pre_roll_ms={}",
                 wake_detector_->name(), wake_labels_.size(), followup_timeout_ms_,
                 pre_roll_capacity_ * 1000U / kTargetSampleRate);
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

        const auto audio_event_topic = audio_io_interface::topic_event(
            audio_io_interface::kDefaultNodeFqn, node_->get_namespace());
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
        if (wake_enabled_ && !wake_ready_) {
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
        bool pause_wake_session = false;
        {
            std::lock_guard lock(gate_mutex_);
            if (message->event == audio_io_interface::msg::AudioEvent::EVENT_STARTED) {
                if (active_playbacks_ == 0) {
                    gate_started_ = steady_now();
                }
                ++active_playbacks_;
                gate_release_valid_ = false;
                gated_ = true;
                // 唤醒窗口内播放“我在”不应取消会话；暂停后等播放结束继续等命令。
                pause_wake_session = wake_enabled_ && session_active_.load();
                if (pause_wake_session) {
                    gate_pause_wake_ = true;
                }
                cancel_session = !pause_wake_session;
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

        if (cancel_session || pause_wake_session) {
            request_reset(cancel_session);
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
            if (gate_pause_wake_.exchange(false)) {
                resume_wake_ = true;
            }
            LOG_DEBUG("ASR gate released after playback holdoff");
        }
        if (now - gate_started_ >= std::chrono::milliseconds(gate_max_ms_)) {
            active_playbacks_ = 0;
            gate_release_valid_ = false;
            gated_ = false;
            if (gate_pause_wake_.exchange(false)) {
                resume_wake_ = true;
            }
            LOG_WARN("ASR gate forced open after {} ms", gate_max_ms_);
        }
    }

    void AsrNode::request_reset(const bool cancel_session) {
        reset_requested_ = true;
        {
            std::lock_guard lock(queue_mutex_);
            queue_.clear();
            queued_ms_ = 0.0;
        }
        if (cancel_session && session_active_.exchange(false)) {
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
                if (backend_) {
                    backend_->reset();
                }
            }
            if (resume_wake_.exchange(false)) {
                if (backend_) {
                    backend_->reset();
                }
                if (session_active_.load() && listening_) {
                    listen_deadline_ = steady_now() + std::chrono::milliseconds(followup_timeout_ms_);
                }
                pre_roll_.clear();
            }
            if (!session_active_.load()) {
                listening_ = false;
                pre_roll_.clear();
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
        if (!backend_) {
            return;
        }
        if (chunk.eos) {
            if (listening_ || !wake_enabled_) {
                handle_backend_events(backend_->flush());
            }
            return;
        }
        if (gated_.load() || !model_loaded_.load()) {
            return;
        }
        if (wake_enabled_ && !wake_ready_) {
            return;
        }

        const auto samples = to_16k_mono(chunk);
        if (samples.empty()) {
            return;
        }

        if (!wake_enabled_) {
            handle_backend_events(backend_->push(samples.data(), samples.size()));
            return;
        }

        if (listening_) {
            check_listen_timeout();
            if (!listening_) {
                return;
            }
            handle_backend_events(backend_->push(samples.data(), samples.size()));
            return;
        }

        // 未唤醒：只跑 KWS，把音频留给唤醒后的 ASR 做 pre-roll。
        pre_roll_.insert(pre_roll_.end(), samples.begin(), samples.end());
        if (pre_roll_.size() > pre_roll_capacity_) {
            pre_roll_.erase(pre_roll_.begin(),
                            pre_roll_.begin() +
                                static_cast<std::ptrdiff_t>(pre_roll_.size() - pre_roll_capacity_));
        }

        if (wake_detector_) {
            const auto wakes = wake_detector_->push(samples.data(), samples.size());
            if (!wakes.empty()) {
                start_wake_session(wakes.front().keyword, samples);
            }
        }
    }

    void AsrNode::start_wake_session(const std::string& keyword,
                                     const std::vector<float>& samples) {
        if (listening_) {
            return;
        }
        listening_ = true;
        begin_session(false);
        listen_deadline_ = steady_now() + std::chrono::milliseconds(followup_timeout_ms_);
        publish_event(asr_interface::msg::AsrEvent::EVENT_WAKE, keyword, "", "",
                      wake_detector_ ? std::string{wake_detector_->name()} : std::string{});
        publish_event(asr_interface::msg::AsrEvent::EVENT_SPEECH_START, "", "", "");

        // pre_roll_ 已包含当前块；一次性送入主 ASR，覆盖“唤醒词后立刻说命令”。
        if (!pre_roll_.empty()) {
            handle_backend_events(backend_->push(pre_roll_.data(), pre_roll_.size()));
            pre_roll_.clear();
        }
        else {
            handle_backend_events(backend_->push(samples.data(), samples.size()));
        }
    }

    void AsrNode::check_listen_timeout() {
        if (!listening_ || steady_now() < listen_deadline_) {
            return;
        }
        LOG_DEBUG("ASR wake session timed out after {} ms", followup_timeout_ms_);
        publish_event(asr_interface::msg::AsrEvent::EVENT_CANCELED, "", "", "");
        end_session();
    }

    void AsrNode::handle_backend_events(const std::vector<BackendEvent>& events) {
        for (const auto& event : events) {
            if (gated_.load() || reset_requested_.load()) {
                end_session();
                continue;
            }

            switch (event.type) {
            case BackendEventType::SpeechStart:
                if (!session_active_.load()) {
                    begin_session();
                }
                break;
            case BackendEventType::Endpoint:
                if (session_active_.load()) {
                    publish_event(asr_interface::msg::AsrEvent::EVENT_ENDPOINT, "", "", "");
                }
                break;
            case BackendEventType::Partial:
                if (session_active_.load()) {
                    publish_event(asr_interface::msg::AsrEvent::EVENT_PARTIAL,
                                  event.text, event.language, "");
                }
                break;
            case BackendEventType::Final:
                if (!session_active_.load()) {
                    begin_session();
                }
                if (wake_enabled_ && listening_ && is_wake_only(event.text)) {
                    LOG_DEBUG("ASR final contains only the wake word; keep listening");
                    listen_deadline_ = steady_now() + std::chrono::milliseconds(followup_timeout_ms_);
                    continue;
                }
                set_last_error("");
                {
                    std::string session_id;
                    {
                        std::lock_guard lock(state_mutex_);
                        session_id = current_session_id_;
                    }
                    LOG_INFO("ASR final: session={} backend={} text={}",
                             session_id, backend_type_, event.text);
                }
                publish_event(asr_interface::msg::AsrEvent::EVENT_FINAL,
                              event.text, event.language, "");
                end_session();
                break;
            case BackendEventType::Failed:
                set_last_error(event.error);
                LOG_ERROR("ASR backend failed: {}", event.error);
                publish_event(asr_interface::msg::AsrEvent::EVENT_FAILED,
                              "", "", event.error);
                end_session();
                break;
            }
        }
    }

    void AsrNode::begin_session(const bool publish_start) {
        session_active_ = true;
        {
            std::lock_guard lock(state_mutex_);
            current_session_id_ = std::to_string(++session_counter_);
        }
        if (publish_start) {
            publish_event(asr_interface::msg::AsrEvent::EVENT_SPEECH_START, "", "", "");
        }
    }

    void AsrNode::end_session() {
        session_active_ = false;
        listening_ = false;
        pre_roll_.clear();
        gate_pause_wake_ = false;
        resume_wake_ = false;
        if (wake_detector_) {
            wake_detector_->reset();
        }
        std::lock_guard lock(state_mutex_);
        current_session_id_.clear();
    }

    void AsrNode::publish_event(const uint8_t event, const std::string& text,
                                const std::string& language, const std::string& error,
                                const std::string& backend_override) const {
        asr_interface::msg::AsrEvent message;
        message.header.stamp = node_->now();
        message.header.frame_id = frame_id_;
        message.event = event;
        message.text = text;
        message.language = language;
        if (!backend_override.empty()) {
            message.backend = backend_override;
        }
        else {
            message.backend = backend_ ? std::string{backend_->name()} : backend_type_;
        }
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
        message.backend = backend_ ? std::string{backend_->name()} : backend_type_;
        message.model_loaded = model_loaded_.load();
        if (!wake_enabled_) {
            message.mode = "free_asr";
        }
        else {
            message.mode = wake_ready_ ? "wake_command" : "wake_error";
        }
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

    std::string AsrNode::normalize_wake_text(const std::string_view text) {
        std::string normalized{text};
        for (const std::string_view token : {
                 " ",
                 "\t",
                 "\n",
                 "\r",
                 ",",
                 ".",
                 "!",
                 "?",
                 ";",
                 ":",
                 "，",
                 "。",
                 "！",
                 "？",
                 "；",
                 "：",
                 "、",
             }) {
            std::size_t position = 0;
            while ((position = normalized.find(token, position)) != std::string::npos) {
                normalized.erase(position, token.size());
            }
        }
        return normalized;
    }

    bool AsrNode::is_wake_only(const std::string_view text) const {
        const auto normalized = normalize_wake_text(text);
        if (normalized.empty()) {
            return false;
        }
        for (const auto& label : wake_labels_) {
            if (normalize_wake_text(label) == normalized) {
                return true;
            }
        }
        return false;
    }

} // namespace asr
