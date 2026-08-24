# 智能空间小组件源码

本目录是从 ukui-fences v0.4.0 主工程中按组件摘出的源码与配套文件，便于独立查阅和移植。

## 目录

```text
src/       SmartSpaceWidget C++ UI 与宿主接口参考
scripts/   本地文件索引与 SQLite 知识库脚本
assets/    智能空间图标和菜单资源
docs/      功能、知识库、UI 与测试说明
tests/     Python 单测和 Xvfb UI/策略/资源测试脚本
```

根目录中的 `resources.qrc` 是智能空间资源映射参考。

## 集成说明

该摘出目录不是独立构建工程。组件由 ukui-fences 的 `DesktopCanvas` 托管，完整 D-Bus 控制、安装规则、共享代码和 CMake 配置请以 `../01-ukui-fences-main-source` 为准。

智能空间索引默认在本地执行；文档解析、PDF 预览和 OCR 的可选运行时依赖详见 `docs/SMART_SPACE.md` 和主工程 README。
