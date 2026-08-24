# ukui-fences v0.4.0 代码解析

本文面向希望阅读、维护或二次开发 `ukui-fences` 的开发者，说明项目的运行模型、核心模块、数据流和验证状态。

## 1. 项目定位

`ukui-fences` 是运行在 UKUI/X11 桌面上的 Qt 5 桌面分区管理程序。它并不是替换 Peony 的文件管理能力，而是在原生桌面之上维护一层 `_NET_WM_WINDOW_TYPE_DESKTOP` 窗口，提供围栏、桌面图标布局、系统监控和智能空间。

主工程目录：`01-ukui-fences-main-source/`。

`02-system-monitor-widget/` 与 `03-smart-space-widget/` 是从主工程抽出的功能源码快照，便于单独查看和移植；它们当前不是独立可执行项目，实际构建仍以主工程为准。

## 2. 启动与进程模型

入口位于 `src/main.cpp`：

1. 启用 Qt High-DPI；
2. 注册 Session D-Bus 服务 `org.ukui.fences`；
3. 若服务已存在，新进程通过 D-Bus 把命令转发给已运行实例后退出；
4. 创建 `DesktopCanvas` 作为桌面层控制器；
5. 注册 `/ukuiFences` D-Bus 对象并创建系统托盘；
6. 根据 `--hide`、`--edit`、`--system-monitor`、`--smart-space`、`--autostart` 等参数执行动作。

因此，项目本质上是“单实例桌面宿主 + 若干嵌入式桌面组件”的架构。

## 3. 核心模块

### DesktopCanvas

文件：`src/DesktopCanvas.h/.cpp`

它是主控制器，负责：

- X11 桌面窗口属性和层级；
- 壁纸加载、缓存、主题取色；
- 桌面松散图标的网格布局与排序；
- Fence 的创建、删除、持久化；
- 文件系统监控和桌面目录对账；
- 全局编辑模式；
- 撤销栈；
- 系统监控与智能空间小组件的托管；
- D-Bus 对外控制接口。

这是整个工程耦合度最高的模块，也是二次开发时最需要谨慎修改的部分。

### FenceWidget

文件：`src/FenceWidget.h/.cpp`

负责单个“围栏”的绘制与交互，包括拖动、八方向缩放、折叠、锁定、图标布局、文件拖放、排序、吸附对齐、壁纸轮廓磁吸和局部字体设置。

### DesktopIcon / DesktopItem

文件：`src/DesktopIcon.*`、`src/DesktopItem.h`

用于桌面文件/系统图标的数据表示、绘制、选择、拖放以及右键交互。

### FileClipboard

文件：`src/FileClipboard.*`

封装复制、剪切、粘贴等文件操作，并把结果提供给撤销系统。

### SystemMonitor

文件：`src/SystemMonitor.h/.cpp`

直接从 `/proc`、sysfs 与 `statvfs` 采集 CPU、内存、磁盘和进程信息，不依赖常驻监控守护进程。AI 诊断通过外部 `curl` 请求 DeepSeek 兼容接口；密钥从 `DEEPSEEK_API_KEY` 读取，接口地址可由 `DEEPSEEK_API_URL` 覆盖。

### SmartSpaceWidget

文件：`src/SmartSpaceWidget.h/.cpp`

负责智能空间 UI、保存规则、索引任务控制、文件预览、Provider 管理、AI 语义筛选和状态展示。

### smart_space_indexer.py

文件：`scripts/smart_space_indexer.py`

负责文件扫描、文本抽取、OCR、增量重用、快照原子替换，以及 Command/HTTP/D-Bus Provider 数据源接入。设计上对文件数、全文预算、OCR 页数和目录监视数设置资源保护。

### smart_space_knowledge.py

文件：`scripts/smart_space_knowledge.py`

负责本地知识索引构建、检索、持久化和中断恢复，为智能空间的本地语义召回提供基础。

## 4. 关键数据流

### 桌面文件变化

`QFileSystemWatcher` / 定时对账 → `DesktopCanvas` → 更新 `DesktopIcon` / `FenceWidget` → 布局持久化。

### 智能空间

用户配置范围/格式 → `SmartSpaceWidget` 启动索引器 → `smart_space_indexer.py` 生成/更新索引快照 → UI 加载结果 → 可选本地知识检索 → 可选 AI 排序。

### 系统诊断

`SystemMonitor` 定时采集 `/proc` / sysfs → 绘制趋势与 Top 进程 → 用户触发 AI 诊断 → 生成裁剪后的 JSON 遥测 → DeepSeek 兼容 API → 显示建议。

## 5. 持久化与配置

主布局使用 JSON 保存到：

```text
~/.config/kyfences/layout.json
```

系统监控和智能空间的部分配置通过 `QSettings` 持久化。布局写入采用临时文件再重命名的方式降低损坏风险。

## 6. 构建依赖

主程序使用 CMake 3.16+、C++17、Qt 5 Core/Gui/Widgets/DBus、Zlib 和 X11。

常见 Debian/Ubuntu/银河麒麟开发环境需要至少具备：

```bash
sudo apt install build-essential cmake \
  qtbase5-dev qtbase5-dev-tools libqt5dbus5 \
  libx11-dev zlib1g-dev
```

智能空间的文档解析/OCR能力还会按需使用 Python 3、Poppler、Tesseract 等外部工具，具体以主 README 为准。

## 7. 验证状态（2026-08-24）

已在源码包上执行：

```bash
python3 -m unittest -v \
  tests/test_smart_space_indexer.py \
  tests/test_smart_space_knowledge.py
```

结果：**25 / 25 通过**。

覆盖内容包括增量索引、删除/变更复用、OCR、扫描 PDF、格式白名单、资源预算、Command/HTTP/D-Bus Provider、异常 sidecar 隔离、快照恢复、本地知识搜索等。

当前检查环境没有 Qt5 开发包，CMake 在配置阶段因找不到 `Qt5Config.cmake` 停止，因此本次没有宣称完成 C++ 全量编译验证。建议在银河麒麟 V10 SP1 / UKUI X11 或等价 Qt5/X11 环境执行完整构建和 UI smoke tests。

## 8. 维护建议

1. `DesktopCanvas.cpp` 已超过 5,000 行，后续新增功能建议逐步拆分窗口层、桌面文件同步、布局持久化和主题逻辑，降低回归风险。
2. `SmartSpaceWidget.cpp` 同样超过 5,000 行，建议将 Provider、索引任务状态机、预览和设置页拆为独立类。
3. AI 功能应继续保持“密钥不入库、正文/路径最小上传”的原则，并在 README 中明确隐私边界。
4. `02-*` / `03-*` 是镜像摘出目录，长期维护时最好用脚本生成，避免与主工程源码出现漂移。
5. 正式发布前建议补充 GitHub Actions 或等价 CI，在 Qt5/X11 环境至少执行 CMake build + Python 单测。
