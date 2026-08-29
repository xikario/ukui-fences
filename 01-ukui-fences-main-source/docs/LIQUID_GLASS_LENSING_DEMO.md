# Fence Shader Lensing Demo V3

目标分支：`codex/fence-lensing-demo-v0.5.0`

V3 保留现有 StackBlur、GlassStyle 和 KWin Blur 作为所有 Fence 的稳定底层，另用一个共享 `QOpenGLWidget` 为当前活动 Fence 的外缘绘制实时折射和高光。这一版主要解决 V2 的全屏置顶、透明层拦截输入、刷新后纹理丢失，以及折射不跟随磁吸轮廓的问题。

## 当前架构

- OpenGL 层是 `DesktopCanvas` 的内部子组件，不再是全屏顶层工具窗口，因此不会泄漏到 Chrome、Codex 或其他应用上方。
- 只保留一个 GL context 和一张桌面壁纸纹理；Overlay 的几何范围会收缩到当前活动 Fence 附近。
- Shader 只输出 Fence 外缘的 0–8 px 强折射 rim 和 8–28 px shoulder，中央始终为透明，不会再将整个区域糊上一层。
- `WA_TransparentForMouseEvents` 保证文件、Fence 和桌面右键菜单仍可正常操作。
- 普通 Fence 只栅格化四条外缘带；壁纸磁吸 Fence 使用 64 点轮廓查找表，Shader 折射会跟随不规则壁纸边缘。
- 动画为事件驱动；拖动、缩放和折叠时才唤醒，强度回落后 Timer 停止。
- 普通鼠标悬停不会移动或重建 `QOpenGLWidget`。这避免 FTG340/X11 下兄弟控件 backing store 花屏，并确保 Fence 内文件始终可以点击。
- F5/强制同步会通过 `wallpaperCacheChanged` 使旧纹理失效，并上传新壁纸纹理。
- Shader/context 失败时 Overlay 自动隐藏，原有 CPU 静态玻璃路径继续工作。

## 构建与运行

Demo 在 CMake 和运行时都是显式开启；不设环境变量时不创建 OpenGL Overlay。

```bash
cmake -S . -B build-demo \
  -DCMAKE_BUILD_TYPE=Release \
  -DUKUI_FENCES_ENABLE_LENSING_DEMO=ON
cmake --build build-demo -j2

export UKUI_FENCES_LENSING_DEMO=1
export UKUI_FENCES_GLASS_LOG=/tmp/liquid-glass-demo-v3.jsonl
./build-demo/ukui-fences
```

运行时关闭：

```bash
UKUI_FENCES_LENSING_DEMO=0 ./build-demo/ukui-fences
```

## 可调参数

```bash
export UKUI_FENCES_GLASS_BASE_LENS_PX=6.25
export UKUI_FENCES_GLASS_VELOCITY_BOOST_PX=10
export UKUI_FENCES_GLASS_VELOCITY_NORM_PX_S=700
export UKUI_FENCES_GLASS_RIM_BAND_PX=8
export UKUI_FENCES_GLASS_EDGE_BAND_PX=28
export UKUI_FENCES_GLASS_SPECULAR_GAIN=1.38
export UKUI_FENCES_GLASS_ACTIVE_FRAME_MS=33
export UKUI_FENCES_GLASS_GPU_TIMER=1
```

FTG340 低功耗档可把活动帧间隔改为 50 ms，并降低高光：

```bash
export UKUI_FENCES_GLASS_SPECULAR_GAIN=1.15
export UKUI_FENCES_GLASS_ACTIVE_FRAME_MS=50
```

## 日志与验收

默认 JSONL 日志位于 `~/.local/share/ukui-fences/logs/liquid-glass-demo.jsonl`，可用 `UKUI_FENCES_GLASS_LOG` 覆盖。关键事件：

- `demo_installed`：参数、日志路径和 `active_fence_edge_only` 渲染模式。
- `shader_ready` / `shader_failed`：GPU、OpenGL 版本和 Shader 编译结果。
- `wallpaper_cache_changed` / `texture_uploaded`：刷新后的缓存失效与纹理重传。
- `render_metrics`：`avg_submit_ms`、`max_submit_ms`、`avg_gpu_ms`、`max_gpu_ms`、`timer_active`、速度和动态折射强度。

```bash
python3 experiments/analyze_lensing_log.py /tmp/liquid-glass-demo-v3.jsonl
```

2026-08-28 在 FTG340 / ARM64 / UKUI X11、2880×1800（150% 缩放）的 Release 实机验证结果：

- OpenGL 4.6 / GLSL 4.60，8-bit alpha buffer，Shader 编译和链接通过。
- 排除首帧热身后，CPU submit 加权平均约 0.65 ms；普通窗口多为 0.61–0.85 ms，F5 纹理重传的一次性峰值约 26.27 ms。
- GPU 加权平均约 1.14 ms，普通窗口多为 0.22–1.03 ms，驱动调度窗口的实测峰值约 3.97 ms。
- 稳定窗口 `timer_active=false`，当前动态层只绘制 1 个活动 Fence。
- F5 后日志出现新的 `texture_uploaded`，桌面无黑屏、无特效丢失。
- Fence 内文件、桌面右键菜单可点击；Chrome 全屏不出现 Overlay 泄漏。

## 统一 Fences 设置

桌面右键菜单的 `Fences 设置…` 使用固定宽度的左侧导航，在同一屏显示全部模块：

1. 行为与布局
2. 玻璃与外观
3. 单个分区
4. 组件与同步
5. 维护

分类包含编辑模式、壁纸磁吸边缘、桌面图标布局、静态毛玻璃半径、透明度、主题、字体、Fence 壁纸、单个 Fence 属性、组件、同步和布局维护。

## 当前边界

- 动态 Shader 同时只增强当前活动 Fence；其他 Fence 仍使用完整静态玻璃背景。
- 缩放、多屏或驱动环境改变后仍建议复验 alpha 合成与 GPU Timer Query。
- 第一次 context/纹理热身可有数十毫秒 CPU 峰值，不代表稳态帧时。
