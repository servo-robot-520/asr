# asr 功能包设计与架构

> 对象：`microduck_rev` 的 `asr` 功能包
> 状态：已实现可切换后端（SenseVoice / KWS）和常开唤醒词阶段
> 相关包：`audio_io`、`audio_io_interface`、`asr_interface`、`common_cpp`

---

## 1. 目标与边界

`asr` 负责把 `audio_io` 的麦克风 PCM 变成识别事件，并把“是否开始识别”的会话门控留在本包：

```text
audio_io/capture → AsrNode → AsrBackend → AsrEvent
```

负责：

- 订阅 `/audio_io/capture`，不打开 ALSA；
- 下混、重采样到 16 kHz mono；
- 采集回调与推理线程解耦的有界队列；
- 播放门控：机器人自己说话时暂停识别；
- 可切换 ASR 后端；
- 常开唤醒词、pre-roll、唤醒后识别窗口、超时；
- 发布 `AsrEvent` / `AsrState`。

不负责：

- 唤醒词的具体内容（由使用方比如 `duck_asr` 提供的 `wake.keywords_file` 决定）；
- 鸭子口令语义（“电量”“站起来”）；
- TTS 回复“我在”（使用方 的策略）；
- 麦克风设备、播放、音库、舵机。

---

## 2. Target

| target | 类型 | 说明 |
|---|---|---|
| `asr` | `libasr.so` | `AsrNode`、后端接口/工厂、所有后端、重采样 |
| `asr_node` | executable | 只做 `main()`、执行器、`common_cpp` 日志桥接 |

---

## 3. 节点结构

`AsrNode` 是组合式节点，不继承 `rclcpp::Node`：

```cpp
auto node = std::make_shared<rclcpp::Node>("duck_asr");
auto asr = std::make_shared<asr::AsrNode>(node);
asr->start();
```

内部结构：

```text
AsrNode (rclcpp::Node + worker thread)
├── capture subscription  ~/capture (audio_io)
├── audio_io event       (播放门控)
├── event publisher      ~/event
├── state publisher      ~/state
├── state timer
├── capture queue        bounded by queue.max_ms
├── LinearResampler      any rate → 16 kHz
├── WakeWordDetector     optional, always-on KWS
└── AsrBackend           selected by backend
```

关键约束：

- 采集回调只校验、拷贝并入队；不在回调里跑 VAD/ASR；
- 所有后端推理都在单工作线程串行执行；
- 麦克风唯一所有者仍是 `audio_io`；
- 后端初始化失败只影响识别，不退出节点。

---

## 4. 后端接口

`include/asr/asr_backend.hpp`：

```cpp
struct BackendEvent {
    BackendEventType type;  // SpeechStart / Endpoint / Partial / Final / Failed
    std::string text;
    std::string language;
    std::string error;
    float confidence;
};

class AsrBackend {
    virtual std::string_view name() const noexcept = 0;
    virtual bool initialize(std::string* error) = 0;
    virtual bool ok() const = 0;
    virtual std::vector<BackendEvent> push(const float* samples, std::size_t count) = 0;
    virtual std::vector<BackendEvent> flush() = 0;
    virtual void reset() = 0;
};
```

`AsrNode` 只调用这四个操作，不包含任何具体模型字段。

### 4.1 `sense_voice`

```text
16k PCM → silero VAD → 完整语音段 → SenseVoice → 文本
```

- 事件：`SpeechStart` → `Endpoint` → `Final`；
- 适合完整转写；
- 模型：`recognizer.*` + `vad.*`。

### 4.2 `keyword`

```text
16k PCM → sherpa-onnx KeywordSpotter → 命中的关键词
```

- 事件：`Final`（text=关键词）；
- 无 VAD、无 SenseVoice；
- 适合固定词/轻量场景；
- 模型：`keyword.*`，关键词：`keyword.keywords_file`。

### 4.3 工厂

`backend_factory.cpp` 根据 `backend` 参数创建后端并读取该后端的参数：

```text
backend: sense_voice | keyword
```

新增后端：

1. 实现 `AsrBackend`；
2. 在 `backend_factory.cpp` 增加分支；
3. 增加参数和测试。

AsrNode 不需要修改。

---

## 5. 唤醒词设计

### 5.1 职责

唤醒词机制放在 `asr`，因为它是音频会话门控，不是鸭子语义：

| 能力 | `asr` | 使用方 |
|---|---|---|
| 常开 KWS | ✅ | ❌ |
| pre-roll / 识别窗口 / 超时 | ✅ | ❌ |
| 主 ASR | ✅ | ❌ |
| “小鸭小鸭”具体词表 | 接收文件 | ✅ 提供 |
| “我在”反馈 | ❌ | ✅ |
| 电量/姿势意图 | ❌ | ✅ |

### 5.2 组件

```text
WakeWordDetector
  └── KeywordWakeDetector
        └── 复用 KeywordBackend 的 KWS 解码
```

`wake.keywords_file` 是独立文件；KWS 模型复用 `keyword.encoder/decoder/joiner/tokens`。
`KeywordWakeDetector` 同时读取 `keywords.txt` 中 `@` 后的显示词，供“只识别到唤醒词”过滤。

### 5.3 状态机

```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Listening: KWS 命中唤醒词
  Listening --> Listening: 主 ASR 运行
  Listening --> Idle: 非唤醒词 FINAL
  Listening --> Listening: FINAL 只含唤醒词（重新计时）
  Listening --> Idle: followup_timeout_ms 超时
  Listening --> Paused: 播放 ack
  Paused --> Listening: 播放结束 + holdoff（重新计时）
```

### 5.4 两种交互

**一次说完**

```text
用户：小鸭小鸭，当前电量是多少
KWS：EVENT_WAKE
asr：pre-roll + 后续音频送主 ASR
duck_asr：ack 延迟内收到 SPEECH_START → 取消“我在”
asr：EVENT_FINAL
```

- 唤醒后立即打开窗口；
- `pre_roll_ms` 保留唤醒词之前的音频，避免命令开头被 KWS 延迟切掉；
- 使用方 需要去除 ASR 文本里的唤醒前缀。

**分两次说**

```text
用户：小鸭小鸭
KWS：EVENT_WAKE
duck_asr：延迟后播放“我在”
audio_io：播放门控
asr：暂停会话（不取消）
播放结束：重新计时 followup_timeout_ms
用户：当前电量是多少
asr：EVENT_FINAL
```

- 播放门控期间不取消唤醒会话；
- 播放结束后重置 backend、清空 pre-roll、重新开始超时；
- 只识别到唤醒词的 final 不结束会话；
- `followup_timeout_ms` 到期发 `EVENT_CANCELED`。

**ack 的前提**：如果 使用方 每次唤醒都立即播放“我在”，一次说完的命令会被播放门控挡住。
ack 必须“延迟 + 可取消”，或者需要 AEC/全双工。ack 的内容和延迟属于 使用方。

### 5.5 唤醒参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `wake.enabled` | `false` | 启用常开唤醒 |
| `wake.keywords_file` | 空 | 唤醒词表；`@` 后为显示词 |
| `wake.pre_roll_ms` | `1000` | 唤醒前保留音频 |
| `wake.followup_timeout_ms` | `6000` | 唤醒后等待命令 |

唤醒 KWS 的模型参数在 `keyword.*`。

### 5.6 固定口令路线

| 路线 | 做法 | 当前状态 |
|---|---|---|
| `keyword_command` | 唤醒后只跑 KWS 命令词 | 可用：`backend: keyword` + `wake.enabled: true`，命令词表 `keyword.keywords_file` |
| `wake_command` | 唤醒后跑词表约束 ASR | 部分可用：`backend: sense_voice` + `wake.enabled: true`；词表约束本身未实现 |
| 自由识别 | 唤醒后跑完整 ASR | 可用：`backend: sense_voice` + `wake.enabled: true` |

固定口令如果只需要关键词命中，KWS 路线更轻；需要同义说法/槽位/自由语句时用 SenseVoice。

---

## 6. 事件与状态

### 6.1 `AsrEvent`

`asr_interface/msg/AsrEvent`：

```text
uint8 EVENT_WAKE=0
uint8 EVENT_SPEECH_START=1
uint8 EVENT_PARTIAL=2
uint8 EVENT_ENDPOINT=3
uint8 EVENT_FINAL=4
uint8 EVENT_FAILED=5
uint8 EVENT_CANCELED=6

std_msgs/Header header
string session_id
uint8 event
string text
float32 confidence
string language
string backend
string error
```

| 事件 | 来源 | 语义 |
|---|---|---|
| `EVENT_WAKE` | 唤醒阶段 | 命中 `wake.keywords_file`，text=显示词 |
| `EVENT_SPEECH_START` | 唤醒/主 ASR | 会话开始 |
| `EVENT_PARTIAL` | 后端可选 | 中间结果，当前内置后端不发 |
| `EVENT_ENDPOINT` | SenseVoice | 句尾 |
| `EVENT_FINAL` | 主 ASR | 最终文本 |
| `EVENT_FAILED` | 后端 | 失败，error 英文原因 |
| `EVENT_CANCELED` | 门控/超时 | 会话取消 |

QoS：reliable，depth 50。事件是边沿，不是电平。一次唤醒只发一次 `EVENT_WAKE`；一次命令只发一次 `EVENT_FINAL`。

### 6.2 `AsrState`

`asr_interface/msg/AsrState`：

```text
std_msgs/Header header
string backend
bool model_loaded
string mode
bool enabled
bool gated
bool session_active
uint64 audio_gap_count
string last_error
```

- `backend`：主后端名称；
- `model_loaded`：主后端模型是否加载；
- `mode`：`free_asr` / `wake_command` / `wake_error`；
- `gated`：当前是否因播放静音；
- `session_active`：是否有唤醒会话；
- `audio_gap_count`：采集序号缺口；
- `last_error`：最近错误。

QoS：reliable，transient_local，depth 1；新订阅者可以立即拿到最近状态。

---

## 7. 线程、队列与门控

### 7.1 输入契约

```text
topic        audio_io_interface::topic_capture(kDefaultNodeFqn, asr namespace)
stream_id    mic
encoding     s16le / f32le
sample_rate  48000（可配）
channels     1（1–2，2 声道下混）
QoS          best_effort，depth 2
```

- `AudioStream.sequence` 跳变时重置 VAD/backend 和队列，`audio_gap_count++`；
- `eos=true` 结束当前处理并等待恢复，不退出节点；
- 不合法 encoding/声道/字节长度丢帧，只告警一次；
- 采集是 best_effort，板端 CPU 紧张时丢包会直接降低识别率。

### 7.2 处理线程

```text
audio_io capture (10 ms)
  → on_capture：校验 + 入有界队列
  → worker：重采样 → KWS / backend
  → publish event
```

- 队列按毫秒预算（`queue.max_ms`），溢出丢最旧块；
- 播放事件：
  - 普通状态：取消当前会话；
  - 唤醒窗口内：暂停会话，不取消；播放结束后恢复并重新计时；
- 门控兜底 `gate.max_ms`，避免事件丢失导致永久静音。

---

## 8. 参数

完整默认值见 [`../config/asr.yaml`](../config/asr.yaml)。

| 参数 | 默认 | 说明 |
|---|---|---|
| `backend` | `sense_voice` | `sense_voice` / `keyword` |
| `capture.stream_id` | `mic` | 空 = 任意 |
| `gate.enabled` | `true` | 播放时暂停识别 |
| `gate.holdoff_ms` | `300` | 播放结束后的静音保持 |
| `gate.max_ms` | `15000` | 门控兜底 |
| `queue.max_ms` | `2000` | 采集队列预算 |
| `state.period_ms` | `500` | `AsrState` 周期 |
| `wake.enabled` | `false` | 启用常开唤醒 |
| `wake.keywords_file` | 空 | 唤醒词表 |
| `wake.pre_roll_ms` | `1000` | 唤醒前保留音频 |
| `wake.followup_timeout_ms` | `6000` | 唤醒后等待命令 |
| `keyword.encoder` | 空 | KWS encoder |
| `keyword.decoder` | 空 | KWS decoder |
| `keyword.joiner` | 空 | KWS joiner |
| `keyword.tokens` | 空 | KWS tokens |
| `keyword.keywords_file` | 空 | keyword 后端命令词表 |
| `keyword.max_active_paths` | `4` | KWS 搜索路径 |
| `keyword.num_trailing_blanks` | `1` | 触发前 blank 数 |
| `keyword.keywords_score` | `1.0` | 关键词加分 |
| `keyword.keywords_threshold` | `0.25` | 检测阈值 |
| `keyword.num_threads` | `1` | ONNX Runtime 线程 |
| `keyword.provider` | `cpu` | 执行提供者 |
| `keyword.debug` | `false` | KWS 调试 |
| `vad.model` | 空 | silero VAD 模型 |
| `vad.threshold` | `0.25` | 语音概率阈值 |
| `vad.min_silence_duration` | `0.5` | 判句尾最短静音 |
| `vad.min_speech_duration` | `0.25` | 最短有效语音 |
| `vad.max_speech_duration` | `10.0` | 单段最长语音 |
| `vad.window_size` | `512` | VAD 窗口 |
| `vad.buffer_seconds` | `30.0` | VAD 缓冲 |
| `vad.num_threads` | `1` | VAD 线程 |
| `vad.provider` | `cpu` | VAD 执行提供者 |
| `vad.debug` | `false` | VAD 调试 |
| `recognizer.model` | 空 | SenseVoice 模型 |
| `recognizer.tokens` | 空 | SenseVoice tokens |
| `recognizer.language` | `auto` | `auto` / `zh` / `en` / `ja` / `ko` / `yue` |
| `recognizer.use_itn` | `true` | 逆文本归一化 |
| `recognizer.num_threads` | `1` | SenseVoice 线程 |
| `recognizer.provider` | `cpu` | SenseVoice 执行提供者 |
| `recognizer.debug` | `false` | SenseVoice 调试 |
| `frame_id` | `mic` | 事件 header |

`audio_io` 的节点名和话题由 `audio_io_interface` 的 helper 结合当前 namespace 生成，不通过参数配置。

---

## 9. 失败模式

| 失败 | 行为 |
|---|---|
| 主后端模型缺失/初始化失败 | 节点继续运行，`model_loaded=false`，`last_error` 说明原因 |
| 唤醒模型/词表缺失 | `wake.enabled=true` 时 `mode=wake_error`，不识别；不退出节点 |
| 唤醒 KWS 未命中 | 主 ASR 不运行，只维护 pre-roll |
| 只识别到唤醒词 | 不结束会话，继续等待 follow-up |
| 唤醒后超时 | `EVENT_CANCELED`，回到 Idle |
| 播放门控 | 暂停识别；唤醒窗口内只暂停不取消 |
| 采集格式错误 | 丢帧，只告警一次 |
| 下游无订阅者 | 事件丢弃，不阻塞工作线程 |

---

## 10. 构建与测试

```bash
source /opt/ros/humble/setup.bash
export SHERPA_ONNX_ROOT=/opt/sherpa-onnx
colcon build --base-paths src --packages-select asr \
  --cmake-args -DSHERPA_ONNX_ROOT=/opt/sherpa-onnx \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13
```

测试：

```bash
colcon test --packages-select asr
colcon test-result --test-result-base build/asr/test_results --verbose
```

模型测试通过 `ASR_SENSE_VOICE_*`、`ASR_VAD_MODEL`、`ASR_TEST_WAV`、`ASR_KWS_*` 环境变量启用；
没有模型时相关测试 `GTEST_SKIP`。

回放工具：

```bash
python3 install/asr/share/asr/tools/replay_capture.py /path/to/test.wav
```

### 10.1 实现状态

| 项 | 状态 |
|---|---|
| 可切换 `sense_voice` / `keyword` 后端 | 已实现 |
| 唤醒阶段：KWS、`EVENT_WAKE`、pre-roll、follow-up | 已实现 |
| 只含唤醒词的 final 过滤 | 已实现 |
| 播放 ack 时暂停/恢复会话 | 已实现（asr 侧） |
| ack 延迟/取消/内容 | 待 使用方 实现 |
| 词表约束 ASR、`EVENT_PARTIAL` | 未实现 |
| `~/enable`、`~/listen` action | 未实现 |

---

## 11. 扩展点

### 新增主后端

实现 `AsrBackend`，在 `backend_factory.cpp` 增加分支；AsrNode 不需要改。

### 新增唤醒检测器

实现 `WakeWordDetector`，在 `create_wake_word_detector()` 中替换/增加工厂分支；主 ASR 和会话逻辑不变。

### 后续

- 唤醒后“我在”的 ack 延迟/取消在 使用方 实现；
- 词表约束、`EVENT_PARTIAL`、`~/enable`、`~/listen` 尚未实现；
- 若需要非 sherpa 后端且完全不链接 sherpa，需要把后端拆成可选库或插件。
