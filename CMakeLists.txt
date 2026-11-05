cmake_minimum_required(VERSION 3.22)
project(asr VERSION 0.1.0)

option(BUILD_EXE "Build asr_node executable" ON)
option(BUILD_TESTING "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(SherpaOnnx REQUIRED)

# 通用 ASR 库。其他功能包可以链接本库，并把外部 rclcpp::Node 传入 AsrNode。
add_library(asr SHARED
        include/asr/linear_resampler.hpp
        src/linear_resampler.cpp
)
target_compile_features(asr PUBLIC cxx_std_23)
set_target_properties(asr PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(asr PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(asr PUBLIC rclcpp std_msgs)

if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    ament_add_gtest(test_linear_resampler test/test_linear_resampler.cpp)
    target_link_libraries(test_linear_resampler asr)
endif ()

install(TARGETS asr
        EXPORT asrTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)
install(DIRECTORY include/
        DESTINATION include
)

ament_export_include_directories(include)
ament_export_dependencies(rclcpp std_msgs)
ament_export_targets(asrTargets HAS_LIBRARY_TARGET)
ament_package()
