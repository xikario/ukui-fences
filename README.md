# ukui-fences v0.4.0

面向 **UKUI / 银河麒麟桌面操作系统（X11）** 的桌面分区管理工具，使用 Qt 5 + C++17 实现，并集成系统资源监控与“智能空间”本地文件检索能力。

> 当前仓库对应源码包版本：**v0.4.0，整理日期 2026-08-24**。

## 功能概览

- 桌面围栏：创建、拖动、缩放、折叠、锁定、排序、独立字体与半透明背景。
- 桌面图标：网格布局、吸附、排序、复制/剪切/粘贴、回收站和打开方式。
- X11 桌面层：与 Peony 共存，支持 Win+D、桌面窗口 hints 和单实例 D-Bus 控制。
- 壁纸能力：多种适配模式、主色提取、区域取色、外部主题导入、壁纸轮廓磁吸。
- 系统监控：CPU、内存、磁盘、趋势、Top 进程及可选 DeepSeek 兼容 AI 诊断。
- 智能空间：全文索引、OCR、文件预览、保存空间、Provider、增量/空闲全量索引、本地知识检索与可选 AI 排序。

## 仓库结构

```text
.
├── 01-ukui-fences-main-source/   # 完整主工程，可构建
├── 02-system-monitor-widget/     # 系统监控小组件源码摘出
├── 03-smart-space-widget/        # 智能空间源码、脚本、文档和测试摘出
└── docs/CODE_OVERVIEW.md         # 代码架构解析与维护说明
```

`02-*` 与 `03-*` 目录是为了查阅、移植和二次开发而整理的功能快照；**实际完整构建以 `01-ukui-fences-main-source/` 为准**。

## 快速构建

依赖：CMake 3.16+、C++17 编译器、Qt 5（Core / Gui / Widgets / DBus）、Zlib、X11 开发库。

```bash
cd 01-ukui-fences-main-source
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

典型 Debian/Ubuntu/银河麒麟开发环境可先安装：

```bash
sudo apt install build-essential cmake \
  qtbase5-dev qtbase5-dev-tools libqt5dbus5 \
  libx11-dev zlib1g-dev
```

智能空间的 PDF/OCR/旧格式文档能力可能还需要 Poppler、Tesseract 及相应语言包，详见主工程 README。

## 测试

进入主工程后：

```bash
cd 01-ukui-fences-main-source
python3 -m unittest -v \
  tests/test_smart_space_indexer.py \
  tests/test_smart_space_knowledge.py
```

2026-08-24 对当前源码包执行结果：**25 / 25 tests passed**。

本次整理环境缺少 Qt5 开发包，因此 CMake 在配置阶段因 `Qt5Config.cmake` 不存在而停止；这不等同于源码编译失败。正式发布前建议在银河麒麟 V10 SP1 / UKUI X11 或等价 Qt5/X11 开发环境执行完整 C++ 构建及 Xvfb UI smoke tests。

## 运行与控制

程序使用 Session D-Bus 保持单实例：

```text
Service:   org.ukui.fences
Path:      /ukuiFences
Interface: org.ukui.fences
```

启动器支持常用参数：

```bash
ukui-fences-launcher --hide
ukui-fences-launcher --edit
ukui-fences-launcher --system-monitor
ukui-fences-launcher --smart-space
ukui-fences-launcher --autostart
ukui-fences-launcher --quit
```

## AI 相关配置

系统监控的 AI 诊断默认使用 DeepSeek 兼容接口。仓库中**不保存 API Key**：

```bash
export DEEPSEEK_API_KEY="your-key"
export DEEPSEEK_API_URL="https://api.deepseek.com/chat/completions"  # 可选
```

智能空间的 AI 筛选设计为“先本地索引/召回，再进行可选远端排序”，并尽量减少路径和正文上传。使用前仍建议结合实际部署要求审查隐私策略与外网访问策略。

## 代码阅读

更完整的模块关系、数据流、验证状态和维护建议见：

- [`docs/CODE_OVERVIEW.md`](docs/CODE_OVERVIEW.md)
- [`01-ukui-fences-main-source/README.md`](01-ukui-fences-main-source/README.md)
- [`01-ukui-fences-main-source/docs/SMART_SPACE.md`](01-ukui-fences-main-source/docs/SMART_SPACE.md)

## License

源码包现有元数据声明为 **GPL-3.0-or-later**。当前包未附带独立的 `LICENSE` 正文文件；对外正式发布前建议补充完整许可证文件，并确认所有新增代码、图片资源与第三方依赖的授权兼容性。
