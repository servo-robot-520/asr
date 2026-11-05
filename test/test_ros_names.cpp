#include <stdexcept>

#include <gtest/gtest.h>

#include "asr/ros_names.hpp"

namespace {

    TEST(RosNames, KeepsDefaultNodeInRootNamespace) {
        EXPECT_EQ(asr::qualify_node_fqn("/", "/audio_io"), "/audio_io");
    }

    TEST(RosNames, QualifiesDefaultNodeWithNamespace) {
        EXPECT_EQ(asr::qualify_node_fqn("/robot1", "/audio_io"), "/robot1/audio_io");
        EXPECT_EQ(asr::qualify_node_fqn("/robot1/", "/audio_io"), "/robot1/audio_io");
        EXPECT_EQ(asr::qualify_node_fqn("/robot1", "/audio_io///"), "/robot1/audio_io");
    }

    TEST(RosNames, PreservesMultiSegmentDefaultNode) {
        EXPECT_EQ(asr::qualify_node_fqn("/robot1", "/audio/io"), "/robot1/audio/io");
    }

    TEST(RosNames, RejectsInvalidDefaultNode) {
        EXPECT_THROW(static_cast<void>(asr::qualify_node_fqn("/robot1", "")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(asr::qualify_node_fqn("/robot1", "audio_io")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(asr::qualify_node_fqn("/robot1", "/")),
                     std::invalid_argument);
    }

    TEST(RosNames, RejectsInvalidNamespace) {
        EXPECT_THROW(static_cast<void>(asr::qualify_node_fqn("robot1", "/audio_io")),
                     std::invalid_argument);
    }

} // namespace
