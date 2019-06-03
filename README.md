# New IPC Implementation for RCLCPP


This branch contains an alternative implementation for ROS2 Intra-Process Communication that does not use the underlying RMW.


### Setup

This branch allows to test the new IPC implementation also in combination with RMW DPS.
However, at the moment, this middleware does not allow introspection. This means that you can't count the number of subscriptions to a topic, which is the method currently used to determine whether you have to publish intra-process, inter-process or both.

As a solution, you have to hardcode this decision before building the package.

Go to `~/ros2_ws/src/rclcpp/rclcpp/include/rclcpp/intra_process_setting.hpp`

At line 33 select one of the available options

- COMM_TYPE_INTRA_ONLY 1
- COMM_TYPE_INTER_ONLY 2
- COMM_TYPE_INTRA_INTER 3


### Build

Create a ROS2 workspace for master

```
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws
wget https://raw.githubusercontent.com/ros2/ros2/master/ros2.repos
vcs import src < ros2.repos
```

Substitute this repository to the default `rclcpp` one.

```
rm -rf ~/ros2_ws/src/ros2/rclcpp
git clone -b alsora/rebase_ipc https://github.com/alsora/rclcpp ~/ros2_ws/src/rclcpp
```

Build the workspace

```
cd ~/ros2_ws
colcon build
```