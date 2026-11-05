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

if (NOT ROS2_WS_SOURCE_BUILD)
    find_package(common_cpp REQUIRED)
    find_package(audio_io_interface REQUIRED)
    find_package(asr_interface REQUIRED)
endif ()

# 通用 ASR 库。其他功能包可以链接本库，并把外部 rclcpp::Node 传入 AsrNode。
add_library(asr SHARED
        include/asr/asr_node.hpp
        include/asr/linear_resampler.hpp
        include/asr/path_utils.hpp
        include/asr/sense_voice_recognizer.hpp
        include/asr/vad_segmenter.hpp
        src/asr_node.cpp
        src/linear_resampler.cpp
        src/sense_voice_recognizer.cpp
        src/vad_segmenter.cpp
)
target_compile_features(asr PUBLIC cxx_std_23)
get_filename_component(SherpaOnnx_LIBRARY_DIR "${SherpaOnnx_C_API_LIBRARY}" DIRECTORY)
# 预编译 sherpa-onnx 不带 CMake config；把实际库目录写入 RPATH，避免
# 每次运行都必须手工设置 LD_LIBRARY_PATH。部署环境不同可用 LD_LIBRARY_PATH 覆盖。
set_target_properties(asr PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH "${SherpaOnnx_LIBRARY_DIR}"
        INSTALL_RPATH "${SherpaOnnx_LIBRARY_DIR}"
)
target_include_directories(asr PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(asr PUBLIC rclcpp std_msgs)
target_link_libraries(asr PRIVATE SherpaOnnx::c-api)

# common_cpp 只提供日志宏/格式化接口；ROS 日志桥接由 asr_node 安装。
if (ROS2_WS_SOURCE_BUILD)
    if (NOT TARGET common_cpp_logging_interface)
        message(FATAL_ERROR
                "CLion workspace configuration requires the in-tree common_cpp_logging_interface target.")
    endif ()
    target_link_libraries(asr PUBLIC common_cpp_logging_interface)
else ()
    target_link_libraries(asr PUBLIC common_cpp::common_cpp_logging_interface)
endif ()

# 消息接口：源码聚合构建使用 in-tree target，独立构建使用已安装包。
if (ROS2_WS_SOURCE_BUILD)
    if (NOT TARGET audio_io_interface OR NOT TARGET asr_interface)
        message(FATAL_ERROR
                "CLion workspace configuration requires the in-tree audio_io_interface and asr_interface targets.")
    endif ()
    include("${audio_io_interface_SOURCE_DIR}/cmake_include.cmake")
    include("${asr_interface_SOURCE_DIR}/cmake_include.cmake")
else ()
    ament_target_dependencies(asr PUBLIC audio_io_interface asr_interface)
endif ()

if (BUILD_EXE)
    add_executable(asr_node src/main.cpp)
    target_compile_features(asr_node PRIVATE cxx_std_23)
    target_link_libraries(asr_node asr)
    ament_target_dependencies(asr_node rclcpp)
    set_target_properties(asr_node PROPERTIES
            BUILD_RPATH "${SherpaOnnx_LIBRARY_DIR}"
            INSTALL_RPATH "$ORIGIN/..;${SherpaOnnx_LIBRARY_DIR}"
    )

    # rcutils 输出处理器是进程全局对象，只能由 main.cpp 安装。
    if (ROS2_WS_SOURCE_BUILD)
        if (NOT TARGET common_cpp_ros_logging)
            message(FATAL_ERROR
                    "CLion workspace configuration requires the in-tree common_cpp_ros_logging target.")
        endif ()
        target_link_libraries(asr_node common_cpp_ros_logging)
    else ()
        target_link_libraries(asr_node common_cpp::common_cpp_ros_logging)
    endif ()
endif ()

if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    ament_add_gtest(test_asr_linear_resampler test/test_linear_resampler.cpp)
    target_link_libraries(test_asr_linear_resampler asr)

    ament_add_gtest(test_asr_sense_voice_recognizer test/test_sense_voice_recognizer.cpp)
    target_link_libraries(test_asr_sense_voice_recognizer asr SherpaOnnx::c-api)

    ament_add_gtest(test_asr_vad_segmenter test/test_vad_segmenter.cpp)
    target_link_libraries(test_asr_vad_segmenter asr SherpaOnnx::c-api)
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

install(FILES README.md
        DESTINATION share/${PROJECT_NAME}
)

if (BUILD_EXE)
    install(TARGETS asr_node
            DESTINATION lib/${PROJECT_NAME}
    )
    install(DIRECTORY config launch tools
            DESTINATION share/${PROJECT_NAME}
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
    )
endif ()

ament_export_include_directories(include)
ament_export_dependencies(rclcpp std_msgs audio_io_interface asr_interface common_cpp)
ament_export_targets(asrTargets HAS_LIBRARY_TARGET)
ament_package()
