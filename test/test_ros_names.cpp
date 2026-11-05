#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "asr/ros_names.hpp"

namespace {

    TEST(RosNames, ResolvesRelativeNodeInRootNamespace) {
        EXPECT_EQ(asr::resolve_node_fqn("/", "audio_io"), "/audio_io");
    }

    TEST(RosNames, ResolvesRelativeNodeInNamespace) {
        EXPECT_EQ(asr::resolve_node_fqn("/robot1", "audio_io"), "/robot1/audio_io");
        EXPECT_EQ(asr::resolve_node_fqn("/robot1/", "audio_io"), "/robot1/audio_io");
        EXPECT_EQ(asr::resolve_node_fqn("/robot1", "audio_io///"), "/robot1/audio_io");
    }

    TEST(RosNames, KeepsAbsoluteNodeOverride) {
        EXPECT_EQ(asr::resolve_node_fqn("/robot1", "/shared/audio_io"), "/shared/audio_io");
    }

    TEST(RosNames, ResolvesRelativeTopicInNamespace) {
        EXPECT_EQ(asr::resolve_topic_name("/robot1", "audio_io/capture"),
                  "/robot1/audio_io/capture");
        EXPECT_EQ(asr::resolve_topic_name("/", "audio_io/capture"), "/audio_io/capture");
        EXPECT_EQ(asr::resolve_topic_name("/robot1", "/shared/audio_io/capture"),
                  "/shared/audio_io/capture");
    }

    TEST(RosNames, RejectsInvalidNames) {
        EXPECT_THROW(static_cast<void>(asr::resolve_node_fqn("/robot1", "")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(asr::resolve_node_fqn("/robot1", "~/audio_io")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(asr::resolve_node_fqn("/robot1", "audio io")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(asr::resolve_topic_name("/robot1", "~/capture")),
                     std::invalid_argument);
    }

} // namespace
