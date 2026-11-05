#!/usr/bin/env python3
"""把 WAV 文件按 AudioStream 发布到采集话题，用于没有 audio_io 时测试 asr。

用法：
  python3 replay_capture.py /path/to/test.wav
  python3 replay_capture.py /path/to/test.wav --topic /robot1/audio_io/capture
"""

from __future__ import annotations

import argparse
import time
import wave

import rclpy
from audio_io_interface.msg import AudioStream
from rclpy.node import Node
from std_msgs.msg import Header


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Replay a WAV file as AudioStream")
    parser.add_argument("wav", help="mono 16-bit PCM WAV file")
    parser.add_argument("--topic", default="/audio_io/capture", help="capture topic")
    parser.add_argument("--stream-id", default="mic", help="AudioStream stream_id")
    parser.add_argument("--chunk-ms", type=int, default=10, help="publish period per chunk")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rclpy.init()
    node = Node("asr_replay_capture")
    publisher = node.create_publisher(AudioStream, args.topic, 2)

    try:
        with wave.open(args.wav, "rb") as wav:
            if wav.getsampwidth() != 2:
                node.get_logger().error("only 16-bit PCM WAV is supported")
                return 1
            sample_rate = wav.getframerate()
            channels = wav.getnchannels()
            # 先让 DDS 完成发现，再按真实时间发布，避免第一块被丢弃。
            for _ in range(10):
                rclpy.spin_once(node, timeout_sec=0.05)
            node.get_logger().info(
                f"replaying {args.wav}: {sample_rate} Hz, {channels} ch, topic={args.topic}"
            )

            frames_per_chunk = max(1, sample_rate * args.chunk_ms // 1000)
            sequence = 0
            started = time.perf_counter()
            while True:
                data = wav.readframes(frames_per_chunk)
                if not data:
                    break
                message = AudioStream()
                message.header = Header()
                message.header.stamp = node.get_clock().now().to_msg()
                message.header.frame_id = "mic"
                message.stream_id = args.stream_id
                message.encoding = "s16le"
                message.sample_rate = sample_rate
                message.channels = channels
                message.sequence = sequence
                message.eos = False
                message.data = list(data)
                publisher.publish(message)
                sequence += 1

                target = started + sequence * args.chunk_ms / 1000.0
                delay = target - time.perf_counter()
                if delay > 0:
                    time.sleep(delay)

            eos = AudioStream()
            eos.header = Header()
            eos.header.stamp = node.get_clock().now().to_msg()
            eos.header.frame_id = "mic"
            eos.stream_id = args.stream_id
            eos.encoding = "s16le"
            eos.sample_rate = sample_rate
            eos.channels = channels
            eos.sequence = sequence
            eos.eos = True
            publisher.publish(eos)
            node.get_logger().info(f"replay finished: {sequence} chunks")
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
