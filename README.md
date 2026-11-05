# asr

## 简介

`asr` 是通用的 ROS 2 离线语音识别功能包：

```text
/audio_io/capture → 下混/重采样 16 kHz → silero VAD → SenseVoice → /asr/event
```

本包不包含唤醒词、鸭子口令语义、电量查询或 TTS；这些属于上层业务包（例如 `duck_asr`）。
当前实现使用 **sherpa-onnx + SenseVoice（离线）**，没有唤醒词：VAD 检测到语音后直接识别，
因此适合“按键触发/上层已经决定开始收音”的场景。固定口令建议由上层用 VAD 句段加文本匹配实现，
后续可在本包增加 KWS 唤醒词阶段。

本包提供两个 target：

| target | 类型 | 作用 |
| --- | --- | --- |
| `asr` | 共享库 `libasr.so` | 组合式 `AsrNode`、SenseVoice、silero VAD、重采样、采集队列 |
| `asr_node` | 可执行文件 | 只做 `main()`、执行器和 common_cpp 日志桥接 |

### 核心设计

- **组合式节点**：`asr::AsrNode` 不继承 `rclcpp::Node`，构造函数接收外部 `Node::SharedPtr`，其他功能包可以把它嵌入自己的进程。
- **麦克风唯一所有者仍是 `audio_io`**：本包只订阅 `AudioStream`，不打开 ALSA。
- **回调不做推理**：采集回调只校验、拷贝并入有界队列；VAD 和 SenseVoice 在独立工作线程串行执行。
- **播放门控**：订阅 `audio_io` 的播放事件，机器人自己说话时暂停识别，避免 TTS/叫声自触发。
- **失败不退出**：模型缺失、格式错误或识别失败只上报 `AsrState.last_error` / `AsrEvent.error`，节点继续运行。

## 接口

节点默认全名 `/asr`，话题相对于节点私有命名空间。

| 端点 | 类型 | 说明 |
| --- | --- | --- |
| `~/event` | `asr_interface/msg/AsrEvent` | `EVENT_SPEECH_START`、`EVENT_ENDPOINT`、`EVENT_FINAL`、`EVENT_FAILED`、`EVENT_CANCELED` |
| `~/state` | `asr_interface/msg/AsrState` | 模型、门控、会话、采集缺口与最近错误 |

SenseVoice 是离线模型，当前不发布 `EVENT_PARTIAL`；一句话只会得到一个 `EVENT_FINAL`。

组合使用示例：

```cpp
#include "asr/asr_node.hpp"

auto node = std::make_shared<rclcpp::Node>("duck_asr");
auto asr = std::make_shared<asr::AsrNode>(node);
asr->start();

rclcpp::executors::MultiThreadedExecutor executor;
executor.add_node(node);
executor.spin();

asr->stop();
```

## 安装 sherpa-onnx

本包使用 sherpa-onnx 的 C API。预编译包没有 CMake config，构建时通过
`SHERPA_ONNX_ROOT`、`-DSHERPA_ONNX_ROOT=...` 或默认的 `/opt/sherpa-onnx`、`/usr/local` 查找。

### 1. 安装 C++ 运行库与头文件

本机验证使用 `v2.0.1` 的 Linux x64 shared 包：

```bash
sudo mkdir -p /opt/sherpa-onnx
cd /tmp
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-linux-x64-shared.tar.bz2
sudo tar -xjf sherpa-onnx-v2.0.1-linux-x64-shared.tar.bz2 \
  -C /opt/sherpa-onnx --strip-components=1

# 让 asr_node 在运行时找到 libsherpa-onnx-c-api.so / libonnxruntime.so
sudo cp -a /opt/sherpa-onnx/lib/* /usr/local/lib/
sudo ldconfig
```

如果不想安装到 `/usr/local`，运行时把库目录加入 `LD_LIBRARY_PATH`：

```bash
export LD_LIBRARY_PATH=/opt/sherpa-onnx/lib:$LD_LIBRARY_PATH
```

### 2. 下载 SenseVoice 模型

```bash
sudo mkdir -p /opt/sherpa-onnx/models
cd /tmp
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2
sudo tar -xjf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2 \
  -C /opt/sherpa-onnx/models
sudo mv /opt/sherpa-onnx/models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17 \
  /opt/sherpa-onnx/models/sense-voice
```

安装后应存在：

```text
/opt/sherpa-onnx/models/sense-voice/model.int8.onnx
/opt/sherpa-onnx/models/sense-voice/tokens.txt
```

### 3. 下载 silero VAD 模型

```bash
sudo wget -O /opt/sherpa-onnx/models/silero_vad.onnx \
  https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
```

## 构建

```bash
source /opt/ros/humble/setup.bash

export SHERPA_ONNX_ROOT=/opt/sherpa-onnx
colcon build --base-paths src --packages-up-to asr \
  --cmake-args -DSHERPA_ONNX_ROOT=/opt/sherpa-onnx \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13
```

> 需要 C++23（GCC 13+）。如果只构建本包，先确保 `common_cpp`、`audio_io_interface`、
> `asr_interface` 已构建或安装。

### CLion / 根工程聚合构建

CLion 使用仓库根 `CMakeLists.txt` 时会直接配置 `src/asr`，因此必须让 CMake 能找到
sherpa-onnx：

- 推荐按上面的步骤安装到 `/opt/sherpa-onnx`；或
- 在 `Settings → Build, Execution, Deployment → CMake → CMake options` 增加
  `-DSHERPA_ONNX_ROOT=/path/to/sherpa-onnx`；或
- 在启动 CLion 前导出 `SHERPA_ONNX_ROOT`。

修改后执行 `Tools → CMake → Reset Cache and Reload Project`。之前失败过一次的
`cmake-build-*` 缓存通常可以直接恢复；如果仍报找不到，删除该构建目录后重新加载。

构建产物：

```text
install/asr/lib/libasr.so
install/asr/lib/asr/asr_node
install/asr/share/asr/config/asr.yaml
install/asr/share/asr/launch/asr.launch.py
install/asr/share/asr/tools/replay_capture.py
```

## 运行

默认配置假设模型安装在 `/opt/sherpa-onnx/models`：

```bash
source install/setup.bash
ros2 launch asr asr.launch.py
```

另开终端查看识别结果：

```bash
source install/setup.bash
ros2 topic echo /asr/event
ros2 topic echo /asr/state
```

没有 `audio_io` 或麦克风时，可以把 WAV 直接回放到采集话题。WAV 必须是 16-bit PCM：

```bash
python3 install/asr/share/asr/tools/replay_capture.py /path/to/test.wav
```

使用 `audio_io` 时，`/audio_io`（或对应 namespace 下的 `/robot1/audio_io`）必须先运行并且
`capture.enabled=true`，否则 `asr` 收不到数据。

## 参数

默认值见 [`config/asr.yaml`](config/asr.yaml)。

`audio_io` 的节点全名不通过参数配置：

```text
audio_io_fqn  = qualify_node_fqn(asr 的 namespace, audio_io_interface::kDefaultNodeFqn)
capture.topic = audio_io_interface::topic_capture(audio_io_fqn)
audio_io 事件 = audio_io_interface::topic_event(audio_io_fqn)
```

因此 `audio_io` 与 `asr` 使用同一 namespace 时无需修改任何配置文件。

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `capture.stream_id` | `mic` | 只接受该 `stream_id`；空 = 任意 |
| `gate.enabled` | `true` | 播放时暂停识别 |
| `gate.holdoff_ms` | `300` | 播放结束后的静音保持 |
| `gate.max_ms` | `15000` | 门控兜底，避免事件丢失导致永久静音 |
| `queue.max_ms` | `2000` | 采集队列上限；溢出丢最旧数据 |
| `vad.model` | `/opt/sherpa-onnx/models/silero_vad.onnx` | silero VAD 模型 |
| `vad.threshold` | `0.25` | 语音概率阈值 |
| `vad.min_silence_duration` | `0.5` | 判句尾的最短静音 |
| `vad.min_speech_duration` | `0.25` | 最短有效语音 |
| `vad.max_speech_duration` | `10.0` | 单段最长语音 |
| `recognizer.model` | `/opt/sherpa-onnx/models/sense-voice/model.int8.onnx` | SenseVoice ONNX 模型 |
| `recognizer.tokens` | `/opt/sherpa-onnx/models/sense-voice/tokens.txt` | tokens 文件 |
| `recognizer.language` | `auto` | `auto` / `zh` / `en` / `ja` / `ko` / `yue` |
| `recognizer.use_itn` | `true` | 逆文本归一化 |
| `recognizer.num_threads` | `1` | ONNX Runtime 线程数 |
| `state.period_ms` | `500` | `AsrState` 发布周期 |
| `frame_id` | `mic` | 事件 header |

使用子命名空间时：

```bash
ros2 launch asr asr.launch.py namespace:=robot1
```

节点全名为 `/robot1/asr`，自动使用 `/robot1/audio_io`：

```text
capture.topic  → /robot1/audio_io/capture
audio_io event → /robot1/audio_io/event
```

当前实现假设 `audio_io` 与 `asr` 在同一 namespace，并使用接口包给出的默认节点名
`audio_io`；没有单独的 `audio_io.node` / `capture.topic` 覆盖参数。

## 测试

没有模型时，模型相关测试会 `GTEST_SKIP`：

```bash
colcon test --packages-select asr
colcon test-result --test-result-base build/asr/test_results --verbose
```

带上真实模型执行：

```bash
export LD_LIBRARY_PATH=/opt/sherpa-onnx/lib:$LD_LIBRARY_PATH
export ASR_SENSE_VOICE_MODEL=/opt/sherpa-onnx/models/sense-voice/model.int8.onnx
export ASR_SENSE_VOICE_TOKENS=/opt/sherpa-onnx/models/sense-voice/tokens.txt
export ASR_VAD_MODEL=/opt/sherpa-onnx/models/silero_vad.onnx
export ASR_TEST_WAV=/path/to/zh.wav
export ASR_EXPECT_TEXT=时间
colcon test --packages-select asr
```

`tools/replay_capture.py` 用于端到端验证：

```text
/test.wav → /audio_io/capture (AudioStream, s16le) → asr_node → /asr/event
```

## 失败模式

| 失败 | 行为 |
| --- | --- |
| 模型缺失/加载失败 | 节点继续运行，`AsrState.model_loaded=false`、`last_error` 说明原因；不发布识别事件 |
| 采集格式不支持 | 丢帧并只告警一次；不影响其他节点 |
| 采集序号跳变 | 重置 VAD/解码状态，`AsrState.audio_gap_count` 增加 |
| 识别失败 | 发布 `EVENT_FAILED`，本次会话结束，节点继续运行 |
| 播放门控事件丢失 | `gate.max_ms` 到期自动恢复 |
| `audio_io` 未运行 | 无输入、无事件；节点保持运行 |

## 已知限制

- 当前没有唤醒词/KWS，VAD 检测到语音就会识别；需要“召唤词”时应在后续增加 KWS 阶段或由上层按键触发。
- SenseVoice 是离线模型，不支持流式 partial；`EVENT_PARTIAL` 当前不会发布。
- 一次只处理一个句段，识别期间新音频进入有界队列；长句/慢模型可能导致丢帧。
- 模型文件不随包分发，需按上面的步骤下载；使用前请确认模型和 sherpa-onnx 的上游许可证。

设计背景：[`docs/duck-ros2-asr-architecture.zh-CN.md`](../../docs/duck-ros2-asr-architecture.zh-CN.md)。
接口定义：[`src/asr_interface`](../asr_interface)。
