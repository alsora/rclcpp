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


### Test different IPC implementations

This repository contains different implementations for IPC. Mostly based on the use of buffers.
Moreover, there are different buffer library to choose.

You can make your choices by setting some preprocessor variables before building.

Go to `~/ros2_ws/src/rclcpp/rclcpp/include/rclcpp/intra_process_setting.hpp`


Lines from 23 to 31 provides you the possible values.

Set the chosen values at lines 37 and 38.

 - IPC_TYPE_DEFAULT: is the standard rclcpp IPC mechanism
 - IPC_TYPE_QUEUE_THREAD: each subscriber creates a thread where checks when new messages are added to its buffer
 - IPC_TYPE_QUEUE_SPIN: the presence of new messages in the buffer is notified to `rclcpp::spin()` using guard conditions.
 - IPC_TYPE_DIRECT_DISPATCH: the publisher direclty invokes the subscriptions' callbacks from its thread.

