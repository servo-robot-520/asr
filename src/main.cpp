#include <exception>
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "asr/asr_node.hpp"
#include "common_cpp/logging/log_interface/log_macro.hpp"
#include "common_cpp/logging/rclcpp_log_handler.hpp"

int main(const int argc, char** argv) {
    rclcpp::init(argc, argv);

    const auto node = std::make_shared<rclcpp::Node>("asr");
    // rcutils 输出处理器是进程级对象，只能在可执行文件入口安装一次。
    if (!logging::ros::RclcppLogHandler::try_init_from_node(node)) {
        return 1;
    }

    try {
        asr::AsrNode asr_node(node);
        asr_node.start();

        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
        executor.remove_node(node);

        asr_node.stop();
    }
    catch (const std::exception& exception) {
        LOG_FATAL("asr terminated: {}", exception.what());
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    }
    catch (...) {
        LOG_FATAL("asr terminated with an unknown error");
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    }

    logging::ros::RclcppLogHandler::shutdown();
    rclcpp::shutdown();
    return 0;
}
