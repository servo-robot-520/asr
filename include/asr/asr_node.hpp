#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

#include "asr/asr_backend.hpp"
#include "asr/linear_resampler.hpp"
#include "asr_interface/msg/asr_event.hpp"
#include "asr_interface/msg/asr_state.hpp"
#include "audio_io_interface/msg/audio_event.hpp"
#include "audio_io_interface/msg/audio_stream.hpp"

namespace asr {

    /// 组合式 ASR 节点。
    ///
    /// 本类不继承 rclcpp::Node，而是持有一个外部传入的节点。其他功能包可以
    /// 把一个组合节点传进来，共享参数、执行器和生命周期：
    ///
    /// @code
    /// auto node = std::make_shared<rclcpp::Node>("duck_asr");
    /// auto asr = std::make_shared<asr::AsrNode>(node);
    /// asr->start();
    /// @endcode
    ///
    /// 数据流：
    /// /audio_io/capture → 下混/重采样 16 kHz → AsrBackend → AsrEvent。
    /// 具体后端由 `backend` 参数选择；模型缺失时节点保持运行，只在 AsrState 中报告错误。
    class AsrNode {
    public:
        explicit AsrNode(rclcpp::Node::SharedPtr node);
        ~AsrNode();

        AsrNode(const AsrNode&) = delete;
        AsrNode& operator=(const AsrNode&) = delete;

        [[nodiscard]] const rclcpp::Node::SharedPtr& node() const noexcept {
            return node_;
        }

        /// 启动识别工作线程；重复调用安全。
        void start();

        /// 停止工作线程并等待退出；重复调用安全。
        void stop();

    private:
        /// 采集回调与工作线程之间传递的一块原始 PCM。
        struct CaptureChunk {
            std::vector<uint8_t> data;
            std::string encoding;
            uint32_t sample_rate{0};
            uint16_t channels{0};
            uint64_t sequence{0};
            std::size_t frames{0};
            bool eos{false};
            std_msgs::msg::Header header;
        };

        void declare_parameters();
        void create_interfaces();
        void initialize_backends();

        void on_capture(const audio_io_interface::msg::AudioStream::SharedPtr& message);
        void on_audio_event(const audio_io_interface::msg::AudioEvent::SharedPtr& message);

        void worker_loop();
        void process_chunk(const CaptureChunk& chunk);
        void handle_backend_events(const std::vector<BackendEvent>& events);
        void begin_session();
        void end_session();
        void request_reset();

        void update_gate();
        void publish_state() const;
        void publish_event(uint8_t event, const std::string& text,
                           const std::string& language, const std::string& error) const;
        void set_last_error(std::string error);

        [[nodiscard]] std::vector<float> to_16k_mono(const CaptureChunk& chunk);
        [[nodiscard]] static std::size_t bytes_per_sample(const std::string& encoding);

        template <typename T>
        T declare_or_get(const std::string& name, const T& default_value) {
            if (node_->has_parameter(name)) {
                return node_->get_parameter(name).get_value<T>();
            }
            return node_->declare_parameter<T>(name, default_value);
        }

        rclcpp::Node::SharedPtr node_;
        rclcpp::Subscription<audio_io_interface::msg::AudioStream>::SharedPtr capture_subscription_;
        rclcpp::Subscription<audio_io_interface::msg::AudioEvent>::SharedPtr audio_event_subscription_;
        rclcpp::Publisher<asr_interface::msg::AsrEvent>::SharedPtr event_publisher_;
        rclcpp::Publisher<asr_interface::msg::AsrState>::SharedPtr state_publisher_;
        rclcpp::TimerBase::SharedPtr state_timer_;

        std::unique_ptr<LinearResampler> resampler_;
        uint32_t resampler_input_rate_{0};

        std::string backend_type_;
        std::unique_ptr<AsrBackend> backend_;
        std::string capture_topic_;
        std::string stream_id_;
        std::string frame_id_;

        bool gate_enabled_{true};
        int gate_holdoff_ms_{300};
        int gate_max_ms_{15000};
        int state_period_ms_{500};
        int queue_max_ms_{2000};

        std::deque<CaptureChunk> queue_;
        mutable std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        double queued_ms_{0.0};

        std::thread worker_;
        std::atomic<bool> running_{false};
        std::atomic<bool> reset_requested_{false};

        std::atomic<bool> gated_{false};
        std::atomic<bool> model_loaded_{false};
        std::atomic<bool> session_active_{false};
        std::atomic<uint64_t> audio_gap_count_{0};
        std::atomic<bool> warned_format_{false};

        mutable std::mutex state_mutex_;
        std::string current_session_id_;
        std::string last_error_;
        uint64_t session_counter_{0};

        bool sequence_valid_{false};
        uint64_t expected_sequence_{0};

        std::mutex gate_mutex_;
        int active_playbacks_{0};
        bool gate_release_valid_{false};
        std::chrono::steady_clock::time_point gate_started_{};
        std::chrono::steady_clock::time_point gate_release_at_{};
    };

} // namespace asr
