# Fence Shader Lensing Demo

目标版本：`codex/liquid-glass-v0.5.0`

这个 Demo 只增强 Fence，不改 SystemMonitor、SmartSpace 或现有 KWin Blur 路径。现有 QWidget/QPainter 液态玻璃继续作为底层和失败回退，新增的 `DesktopLensingOverlay` 只在 Fence 外缘叠加实时 Shader 折射、高光和轻微色散。

## 设计目标

- 单个全桌面 `QOpenGLWidget`，所有 Fence 共用一个 OpenGL context 和一张壁纸纹理，避免每个 Fence 各自复制 20 MiB 级纹理。
- 只渲染 Fence 边缘约 24 px 的 lensing band；中间区域保持透明，继续显示 v0.5.0 现有的 StackBlur + tint + GlassStyle。
- Fence 移动速度会动态提高折射位移，停止后平滑回落。
- 鼠标位置参与局部 specular highlight，高光不是固定的顶部渐变。
- 轻量 RGB 采样偏移模拟边缘色散，但幅度很小，避免“彩虹玻璃”。
- Shader/OpenGL 初始化失败时隐藏 Overlay，原有玻璃路径不受影响。

## 构建 Demo

默认构建不启用实验层，避免影响现有自动化 UI 测试和无 OpenGL 环境。

```bash
cmake -S . -B build-demo \
  -DCMAKE_BUILD_TYPE=Release \
  -DUKUI_FENCES_ENABLE_LENSING_DEMO=ON
cmake --build build-demo -j"$(nproc)"
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
- `drawn_fences`：当前参与折射渲染的可见 Fence 数量。
- `current_max_velocity_px_s`：写日志时 Fence 的当前最大移动速度。
- `max_velocity_px_s`：5 秒统计窗口内捕获到的最大移动速度，短拖动也不会丢失。
- `max_lens_strength_px`：统计窗口内的最大动态折射位移强度。
- `fences[].velocity_px_s`：每个 Fence 的移动速度。
- `fences[].lens_strength_px`：动态折射位移强度。
- `fences[].peak_velocity_px_s` / `peak_lens_strength_px`：每个 Fence 在统计窗口内的峰值。

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
- 稳态 `avg_submit_ms` 约 0.38–0.60 ms；首次纹理/窗口热身阶段出现约 26–27 ms 的单帧峰值。
- 自动拖动实验捕获到 361.6 px/s 峰值速度，动态折射强度由 7.5 px 提升至 8.90 px，停止后平滑回落。
- `UKUI_FENCES_LENSING_DEMO=0` 时不创建 Overlay，原 v0.5.0 玻璃路径保持可用。

## 当前已知限制

- Demo 使用圆角矩形 SDF，暂未复刻 Fence 的“壁纸磁吸异形轮廓”；磁吸形状仍由原 QWidget 路径负责。
- Overlay 是全桌面透明 QOpenGLWidget，适合验证视觉和 FTG340 性能，不代表最终生产架构。
- 当前记录的是 CPU submit 时间，不是 GPU timer query；若 Demo 验证通过，下一阶段再加入 `GL_TIME_ELAPSED` 或平台侧 GPU 性能统计。
- 标题栏顶部折射刻意减弱，避免影响现有标题文字可读性。

## 下一阶段判定

若 FTG340 上 3~6 个 Fence 的视觉效果达到预期且日志没有明显帧耗时尖峰，再把 Shader 逻辑收敛为正式 `GlassRenderer`：支持任意 Fence mask / magnetic contour、按脏区更新、统一折射参数，并评估是否下沉到 SystemMonitor / SmartSpace。
