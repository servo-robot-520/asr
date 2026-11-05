import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_config = os.path.join(
        get_package_share_directory("asr"), "config", "asr.yaml"
    )
    config = LaunchConfiguration("config")
    namespace = LaunchConfiguration("namespace")

    return LaunchDescription(
        [
            # 上层（例如 duck_asr）可以传入自己的参数文件；不传时使用包内默认配置。
            DeclareLaunchArgument(
                "config",
                default_value=default_config,
                description="Path to the asr parameter file",
            ),
            # 默认不设置 namespace，节点全名为 /asr。
            DeclareLaunchArgument(
                "namespace",
                default_value="",
                description="Optional ROS namespace; empty means no namespace",
            ),
            Node(
                package="asr",
                executable="asr_node",
                name="asr",
                namespace=namespace,
                output="screen",
                parameters=[config],
            ),
        ]
    )
