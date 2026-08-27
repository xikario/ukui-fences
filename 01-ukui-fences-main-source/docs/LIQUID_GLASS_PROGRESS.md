# ukui-fences 液态玻璃实施进度

更新日期：2026-08-27

## 当前结论

v0.5.0 已完成 Tier 0、Tier 1 与 Tier 2：Fence、SystemMonitor 和 SmartSpaceWidget 共享静态模糊缓存与统一玻璃质感层；SmartSpace 置顶窗口和所有统一样式的 QMenu 在 KWin Blur 可用时请求实时合成器模糊，不可用时自动保留半透明 tint 与 Tier 0 回退。

FTG340 的 `QOpenGLWidget` 真机实验仍保持通过，但 v0.5.0 生产路径不依赖 Shader。OpenGL 仅保留为独立实验目标，避免给普通 QWidget 桌面路径增加驱动风险。

## 任务清单

### 前置验证实验

- [x] 实验 1：FTG340 OpenGL 能力检测（OpenGL 4.6 / GLSL 4.60）
- [x] 实验 2：StackBlur 性能基准测试
- [x] 实验 3：ukui-kwin Blur 支持检测
- [x] 实验 4：QOpenGLWidget 最小 Shader 编译测试
- [x] 实验 5：FenceWidget 模糊壁纸切片视觉预评审

实验 4 真机结果：

```text
context.valid=true
context.version=4.6
gl.vendor=Phytium Technology Co., Ltd.
gl.renderer=Phytium FTG340
gl.version=4.6 V1.1.7
gl.glsl=4.60
shader.vertex=pass
shader.fragment=pass
shader.link=pass
```

实验 5 产物：`docs/images/liquid-glass-visual-preview.png`。预评审中壁纸结构在三个玻璃区域内均能保持位置连续，28px 模糊、四向高光/内阴影、微噪点和 1px 边框可正常合成；浅色区域的 tint 对比度已在 Tier 1 统一参数中完成校准。

### 第一阶段：Tier 0 — 静态毛玻璃预缓存

- [x] StackBlur 算法实现（纯 C++，无图像处理依赖）
- [x] DesktopCanvas 模糊壁纸缓存引擎集成
- [x] 统一的全局坐标壁纸切片绘制接口
- [x] FenceWidget 玻璃背景替换
- [x] SystemMonitor Glass / Wallpaper 皮肤玻璃背景
- [x] SmartSpaceWidget 玻璃背景与半透明面板
- [x] 壁纸缓存失效时同步清空模糊缓存
- [x] StackBlur 正确性测试

### 第二阶段：Tier 1 — 玻璃质感增强

- [x] 顶部高光渐变
- [x] 底部内阴影
- [x] 1px 半透明高光边框
- [x] 标题栏加深 tint（Fence）
- [x] 左侧高光与右侧内阴影
- [x] 微噪点预生成 tile（64×64，确定性、预乘 Alpha）
- [x] 右键菜单玻璃样式增强
- [x] 统一三类组件的 tint/对比度参数

### 第三阶段：Tier 2 — 合成器委托模糊

- [x] KWin Blur X11 协议封装函数
- [x] SmartSpaceWidget 置顶模式实时模糊
- [x] QMenu 弹出菜单实时模糊
- [x] 合成器兼容性回退测试

Tier 2 真机协议探针：

```text
kwin.blur.supported=true
kwin.blur.requested=true
kwin.blur.property=present
kwin.blur.menu_property=present
kwin.blur.clear=pass
kwin.blur.removed=true
```

Xvfb 无合成器回退探针：

```text
kwin.blur.supported=false
kwin.blur.requested=false
kwin.blur.property=absent
kwin.blur.menu_property=absent
kwin.blur.clear=pass
kwin.blur.removed=true
```

## 性能记录

测试环境：FTG340 / ARM64，Release，2880×1800，StackBlur 半径 28，全分辨率 RGB32。

| 实现 | 连续三次耗时 |
|---|---:|
| 单线程首轮实现 | 141 ms（单次） |
| 最多 8 线程的行/列并行实现 | 66 / 58 / 52 ms |

结论：实际真机数据略高于早期 26–41ms 估算，但仍在规划的 50–200ms 一次性启动/壁纸重建预算内。运行时不执行卷积。2880×1800 RGB32 模糊缓存本体约 19.8 MiB；生成期间临时图像会带来约 19.8 MiB 的短时峰值。

## 构建与验证

- `ukui-fences`、`qopenglwidget_shader_probe`、`glass_visual_preview`、`kwin_blur_probe` 构建通过。
- `glass_style`、`kwin_blur_codec`、`stack_blur`、`office_document_factory`、`smart_space_indexer`、`smart_space_knowledge` 和 `smart_space_new_features_smoke` 测试通过。
- 完整 CTest：10 项中 7 项通过；3 项既有 UI/资源集成测试未通过：
  - `smart_space_ui_smoke`：Xvfb/DBus 会话提前断开并触发外部桌面应用。
  - `smart_space_policy_smoke`：xdotool 未在固定坐标打开设置窗口。
  - `smart_space_resource_benchmark`：25,000 文件 fast-full 流程超时。

这三项历史失败尚未归因到玻璃绘制改动；v0.5.0 的玻璃模块、KWin 属性编解码、真机协议和无合成器回退均有独立验证覆盖。

## 收尾结论

Tier 0–2 已在 v0.5.0 分支收尾。后续若继续探索 Tier 3，只应针对小面积折射/色散元素单独启用，并继续保留当前 QWidget/QPainter 与 KWin Blur 回退链路。
