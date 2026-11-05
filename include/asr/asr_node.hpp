#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

#include "asr/asr_backend.hpp"
#include "asr/linear_resampler.hpp"
#include "asr/wake_word_detector.hpp"
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
        /// 保存外部节点并初始化参数、后端和 ROS 接口。
        explicit AsrNode(rclcpp::Node::SharedPtr node);
        /// 停止工作线程。
        ~AsrNode();

        AsrNode(const AsrNode&) = delete;
        AsrNode& operator=(const AsrNode&) = delete;

        /// 返回外部传入的组合节点。
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
            std::vector<uint8_t> data; ///< 原始 PCM 字节
            std::string encoding; ///< s16le / f32le
            uint32_t sample_rate{0}; ///< 输入采样率
            uint16_t channels{0}; ///< 输入声道数
            uint64_t sequence{0}; ///< AudioStream 包序号
            std::size_t frames{0}; ///< 本块帧数
            bool eos{false}; ///< 是否为流结束块
            std_msgs::msg::Header header; ///< 原始消息头
        };

        /// 声明本节点参数并计算 topic/队列等派生配置。
        void declare_parameters();
        /// 创建订阅、发布器和状态定时器。
        void create_interfaces();
        /// 创建主后端，以及在启用时创建唤醒检测器。
        void initialize_backends();

        /// 采集回调：校验 AudioStream、检测序号缺口并入队。
        void on_capture(const audio_io_interface::msg::AudioStream::SharedPtr& message);
        /// audio_io 播放事件回调：维护门控和唤醒会话暂停/恢复。
        void on_audio_event(const audio_io_interface::msg::AudioEvent::SharedPtr& message);

        /// 工作线程：从队列取块并执行重采样/唤醒/识别。
        void worker_loop();
        /// 处理单个采集块（eos、门控、唤醒窗口、主后端）。
        void process_chunk(const CaptureChunk& chunk);
        /// 把后端事件转换成 AsrEvent，并维护 session 生命周期。
        void handle_backend_events(const std::vector<BackendEvent>& events);
        /// 唤醒命中后打开识别窗口，并把 pre-roll 送入主后端。
        void start_wake_session(const std::string& keyword, const std::vector<float>& samples);
        /// 检查唤醒后的 follow-up 是否超时。
        void check_listen_timeout();
        /// 开始一个会话；publish_start=false 时由调用方负责发 SPEECH_START。
        void begin_session(bool publish_start = true);
        /// 结束当前会话并清理唤醒/后端状态。
        void end_session();
        /// 请求工作线程重置后端；cancel_session=false 用于 ack 暂停。
        void request_reset(bool cancel_session = true);

        /// 根据活跃播放数和 holdoff 更新门控状态。
        void update_gate();
        /// 发布 AsrState 快照。
        void publish_state() const;
        /// 发布一条 AsrEvent；backend_override 用于 WAKE 事件。
        void publish_event(uint8_t event, const std::string& text,
                           const std::string& language, const std::string& error,
                           const std::string& backend_override = {}) const;
        /// 线程安全地记录最近错误。
        void set_last_error(std::string error);

        /// 把原始 PCM 下混/重采样为 16 kHz mono float。
        [[nodiscard]] std::vector<float> to_16k_mono(const CaptureChunk& chunk);
        /// 返回 encoding 对应的单样本字节数；不支持时返回 0。
        [[nodiscard]] static std::size_t bytes_per_sample(const std::string& encoding);
        /// 去掉唤醒词比较时不关心的空白/标点。
        [[nodiscard]] static std::string normalize_wake_text(std::string_view text);
        /// 判断 ASR final 是否只包含唤醒词。
        [[nodiscard]] bool is_wake_only(std::string_view text) const;

        /// 读取参数；未声明时使用 default_value 声明。
        template <typename T>
        T declare_or_get(const std::string& name, const T& default_value) {
            if (node_->has_parameter(name)) {
                return node_->get_parameter(name).get_value<T>();
            }
            return node_->declare_parameter<T>(name, default_value);
        }

        rclcpp::Node::SharedPtr node_; ///< 外部传入的组合节点
        rclcpp::Subscription<audio_io_interface::msg::AudioStream>::SharedPtr capture_subscription_; ///< 采集订阅
        rclcpp::Subscription<audio_io_interface::msg::AudioEvent>::SharedPtr audio_event_subscription_; ///< 播放门控订阅
        rclcpp::Publisher<asr_interface::msg::AsrEvent>::SharedPtr event_publisher_; ///< 识别事件发布器
        rclcpp::Publisher<asr_interface::msg::AsrState>::SharedPtr state_publisher_; ///< 识别状态发布器
        rclcpp::TimerBase::SharedPtr state_timer_; ///< 状态/门控定时器

        std::unique_ptr<LinearResampler> resampler_; ///< 当前输入采样率对应的重采样器
        uint32_t resampler_input_rate_{0}; ///< resampler_ 的输入采样率

        std::string backend_type_; ///< backend 参数的字符串值
        std::unique_ptr<AsrBackend> backend_; ///< 当前主 ASR 后端
        std::unique_ptr<WakeWordDetector> wake_detector_; ///< 可选唤醒词检测器
        std::vector<std::string> wake_labels_; ///< 唤醒词显示词，用于过滤唤醒词-only final
        bool wake_enabled_{false}; ///< 是否启用唤醒阶段
        bool wake_ready_{false}; ///< 唤醒检测器是否初始化成功
        int followup_timeout_ms_{6000}; ///< 唤醒后等待命令的时长
        std::vector<float> pre_roll_; ///< 唤醒前保留的 16 kHz 音频
        std::size_t pre_roll_capacity_{0}; ///< pre_roll_ 的最大样本数
        bool listening_{false}; ///< 当前是否处于唤醒后的识别窗口（工作线程私有）
        std::chrono::steady_clock::time_point listen_deadline_{}; ///< 识别窗口截止时间

        std::string capture_topic_; ///< 实际订阅的采集话题
        std::string stream_id_; ///< 只接受的 AudioStream.stream_id
        std::string frame_id_; ///< 事件 header.frame_id

        bool gate_enabled_{true}; ///< 是否启用播放门控
        int gate_holdoff_ms_{300}; ///< 播放结束后的静音保持
        int gate_max_ms_{15000}; ///< 门控强制恢复上限
        int state_period_ms_{500}; ///< AsrState 发布周期
        int queue_max_ms_{2000}; ///< 采集队列时长上限

        std::deque<CaptureChunk> queue_; ///< 采集回调到工作线程的 PCM 队列
        mutable std::mutex queue_mutex_; ///< queue_/queued_ms_ 保护
        std::condition_variable queue_cv_; ///< 工作线程等待/唤醒
        double queued_ms_{0.0}; ///< 当前队列累计音频时长

        std::thread worker_; ///< 推理/重采样工作线程
        std::atomic<bool> running_{false}; ///< 工作线程运行标志
        std::atomic<bool> reset_requested_{false}; ///< 请求工作线程重置后端

        std::atomic<bool> gated_{false}; ///< 当前是否因播放静音
        std::atomic<bool> model_loaded_{false}; ///< 主后端模型是否加载
        std::atomic<bool> session_active_{false}; ///< 是否有唤醒/识别会话
        std::atomic<uint64_t> audio_gap_count_{0}; ///< AudioStream 序号缺口次数
        std::atomic<bool> warned_format_{false}; ///< 非法格式是否已告警

        mutable std::mutex state_mutex_; ///< 当前 session/错误保护
        std::string current_session_id_; ///< 当前会话 id
        std::string last_error_; ///< 最近错误
        uint64_t session_counter_{0}; ///< 会话 id 计数器（工作线程）

        bool sequence_valid_{false}; ///< expected_sequence_ 是否有效（采集回调）
        uint64_t expected_sequence_{0}; ///< 期望的下一个 AudioStream 序号

        std::mutex gate_mutex_; ///< 门控计数器/时间保护
        int active_playbacks_{0}; ///< 活跃播放请求数
        bool gate_release_valid_{false}; ///< holdoff 截止时间是否有效
        std::atomic<bool> gate_pause_wake_{false}; ///< 播放期间是否暂停唤醒会话
        std::atomic<bool> resume_wake_{false}; ///< 播放结束需要恢复唤醒窗口
        std::chrono::steady_clock::time_point gate_started_{}; ///< 本轮门控开始时间
        std::chrono::steady_clock::time_point gate_release_at_{}; ///< 允许恢复的时间
    };

} // namespace asr
