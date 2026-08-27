# Fence Shader Lensing Demo

目标版本：`codex/liquid-glass-v0.5.0`

这个 Demo 只增强 Fence，不改 SystemMonitor、SmartSpace 或现有 KWin Blur 路径。现有 QWidget/QPainter 液态玻璃继续作为底层和失败回退，新增的 `DesktopLensingOverlay` 只在 Fence 外缘叠加实时 Shader 折射、高光和轻微色散。

## 设计目标

- 单个全桌面 `QOpenGLWidget`，所有 Fence 共用一个 OpenGL context 和一张壁纸纹理，避免每个 Fence 各自复制 20 MiB 级纹理。
- GL 层是独立的透明输出窗口，使用 `WindowTransparentForInput` 让 X11 从窗口系统层面穿透鼠标、触摸和滚轮事件；不再使用会破坏 QWidget 层级的 `WA_AlwaysStackOnTop`。
- V2 使用 0–8 px 强折射 rim 与 8–28 px 反向 shoulder；中央可在 Shader 内混回 11% 原壁纸清晰度。
- Fence 移动速度会动态提高折射位移，停止后平滑回落；完全稳定后动画 Timer 停止。
- 鼠标位置参与局部 specular highlight，高光不是固定的顶部渐变。
- 轻量 RGB 采样偏移模拟边缘色散，但幅度很小，避免“彩虹玻璃”。
- Shader/OpenGL 初始化失败时隐藏 Overlay，原有玻璃路径不受影响。

## 构建 Demo

默认构建不启用实验层，避免影响现有自动化 UI 测试和无 OpenGL 环境。

```bash
cmake -S . -B build-demo \
  -DCMAKE_BUILD_TYPE=Release \
  -DUKUI_FENCES_ENABLE_LENSING_DEMO=ON
cmake --build build-demo -j2
```

启用该 CMake 选项后，Demo 运行时默认打开。临时关闭 Shader 层：

```bash
UKUI_FENCES_LENSING_DEMO=0 ./build-demo/ukui-fences
```

## 后端日志

默认日志：

```text
$XDG_DATA_HOME/ukui-fences/logs/liquid-glass-demo.jsonl
```

若 `XDG_DATA_HOME` 未设置，通常落在：

```text
~/.local/share/ukui-fences/logs/liquid-glass-demo.jsonl
```

可覆盖日志路径：

```bash
UKUI_FENCES_GLASS_LOG=/tmp/ukui-glass-demo.jsonl ./build-demo/ukui-fences
```

JSONL 事件包括：

- `demo_installed`：Demo 参数与日志路径。
- `overlay_created` / `overlay_destroyed`：OpenGL Overlay 生命周期。
- `shader_ready`：OpenGL vendor / renderer / version 与实际 context 版本。
- `shader_failed`：vertex / fragment / link 阶段及 shader log。
- `opengl_unavailable`：QOpenGLWidget context 无法建立，随后自动回退。
- `wallpaper_missing` / `wallpaper_render_failed`：壁纸采样源异常。
- `texture_uploaded`：壁纸纹理重建，记录尺寸和 source key。
- `render_metrics`：每 5 秒聚合一次渲染数据。

`render_metrics` 中的重点字段：

- `frames`：统计窗口内实际执行的 paintGL 次数。
- `avg_submit_ms` / `max_submit_ms`：CPU 侧一次 paintGL + draw submit 耗时，不等同 GPU 完整执行时间。
- `avg_gpu_ms` / `max_gpu_ms`：异步 `GL_TIME_ELAPSED` 样本，不主动等待 GPU。
- `timer_active`：动画 Timer 是否仍在运行；稳定窗口应为 `false`。
- `drawn_fences`：当前参与折射渲染的可见 Fence 数量。
- `current_max_velocity_px_s`：写日志时 Fence 的当前最大移动速度。
- `max_velocity_px_s`：5 秒统计窗口内捕获到的最大移动速度，短拖动也不会丢失。
- `max_lens_strength_px`：统计窗口内的最大动态折射位移强度。
- `fences[].velocity_px_s`：每个 Fence 的移动速度。
- `fences[].lens_strength_px`：动态折射位移强度。
- `fences[].peak_velocity_px_s` / `peak_lens_strength_px`：每个 Fence 在统计窗口内的峰值。

分析日志：

```bash
python3 experiments/analyze_lensing_log.py /tmp/liquid-glass-demo.jsonl
```

低功耗边缘-only 档：

```bash
export UKUI_FENCES_GLASS_CENTER_TRANSMISSION=0
export UKUI_FENCES_GLASS_SPECULAR_GAIN=1.15
export UKUI_FENCES_GLASS_ACTIVE_FRAME_MS=50
```

此模式使用 GL scissor 仅栅格化四条边带，避免透明中心仍消耗 FTG340 fragment 计算。

## Demo 验收建议

1. 深色、浅色、高对比线条三类壁纸各测试一次。
2. 将 Fence 横向快速拖过壁纸中的直线/建筑边缘，观察边缘是否产生可辨识但不过度的弯曲位移。
3. 快速拖动后停止，确认 lensing 强度平滑衰减，而不是瞬间跳变。
4. 鼠标沿 Fence 边缘移动，观察局部高光是否跟随。
5. 同屏放置 1 / 3 / 6 个 Fence，各运行至少 30 秒，比较 `render_metrics`。
6. 设置 `UKUI_FENCES_LENSING_DEMO=0` 再运行一次，确认完全回到 v0.5.0 原有视觉路径。
7. 人工破坏 OpenGL 环境或在不支持环境运行，确认应用仍可用且日志出现 `opengl_unavailable` / `shader_failed`。

## FTG340 真机验证

2026-08-27 在 FTG340 / ARM64 / UKUI X11、2880×1800（150% 缩放）环境完成 Release 构建与运行验证：

- OpenGL context：4.6，GLSL 4.60，Shader 编译和链接通过。
- GPU：`Phytium FTG340`，Overlay 获得 8-bit alpha buffer。
- 5 个 Fence 共用一张 1921×1201 壁纸纹理，拖动与点击不被透明 Overlay 截获。
- V2 默认档稳定 `avg_submit_ms` 约 0.35–0.57 ms；慢拖达到 10.25 px，快拖达到 12.52 px，5 个统计窗口中 4 个为 `timer_active=false`。
- 默认 11% 中央透射在 FTG340 上部分 GPU 窗口为 3–10 ms，未达到 2 ms 目标；建议该 GPU 使用低功耗档。
- 低功耗边缘-only/scissor 档动态窗口 CPU submit 约 0.63 ms、GPU 约 2.34 ms，峰值折射 11.74 px；GPU 已显著下降但仍略高于 2 ms 理想值。
- 关闭 GPU Timer Query 的对照组稳定 CPU 加权均值为 0.83 ms、动态窗口约 0.73 ms；Query 不是稳定 CPU 成本的主要来源。
- 折叠/展开自动化日志确认目标 Fence 高度 `154 → 34 → 154`，且每次事件结束后 Timer 均回到停止状态。
- 首次 context/纹理热身 CPU 峰值约 27–32 ms，单独记录且不计入稳定均值。
- `UKUI_FENCES_LENSING_DEMO=0` 时不创建 Overlay，原 v0.5.0 玻璃路径保持可用。

## 当前已知限制

- Demo 使用圆角矩形 SDF，暂未复刻 Fence 的“壁纸磁吸异形轮廓”；磁吸形状仍由原 QWidget 路径负责。
- Overlay 是系统级输入穿透的全桌面透明 QOpenGLWidget，适合验证视觉和 FTG340 性能，不代表最终生产架构。
- FTG340 的异步 Timer Query 已可用；GPU 耗时仍存在驱动/调度波动，不能与 CPU submit 相互替代。
- 标题栏顶部折射刻意减弱，避免影响现有标题文字可读性。

## 下一阶段判定

若 FTG340 上 3~6 个 Fence 的视觉效果达到预期且日志没有明显帧耗时尖峰，再把 Shader 逻辑收敛为正式 `GlassRenderer`：支持任意 Fence mask / magnetic contour、按脏区更新、统一折射参数，并评估是否下沉到 SystemMonitor / SmartSpace。
