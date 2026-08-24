# 系统监视小组件源码

本目录是从 ukui-fences v0.4.0 主工程中按组件摘出的源码，便于独立查阅和移植。

## 文件

- `src/SystemMonitor.h`
- `src/SystemMonitor.cpp`
- `src/DesktopCanvas.h`：当前组件调用的宿主接口参考
- `src/MenuStyle.h`：共享菜单样式

系统监视小组件读取 `/proc`、sysfs 和 `statvfs` 获取 CPU、内存、存储与进程数据，并由 Qt 5 Widgets 绘制界面。诊断功能还会使用 `QProcess`/`curl`。

## 集成说明

该摘出目录不是独立构建工程。组件由 `DesktopCanvas` 创建、显示和持久化，完整实现及 CMake 配置请以 `../01-ukui-fences-main-source` 为准。移植成独立程序时，需要为 `DesktopCanvas` 相关调用提供宿主接口，或按目标工程调整对应耦合点。
