# New IPC Implementation for RCLCPP


This branch contains an alternative implementation for ROS2 Intra-Process Communication that does not use the underlying RMW.


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