# ukui-fences 液态玻璃实施进度

更新日期：2026-08-27

## 当前结论

Tier 0 已完成首轮代码集成：桌面壁纸在缓存重建时执行一次 StackBlur，Fence、SystemMonitor 和 SmartSpaceWidget 在绘制阶段按自身屏幕位置采样模糊缓存。组件移动时不重新模糊，只重新选择缓存切片。

FTG340 的 `QOpenGLWidget` 真机实验也已通过，后续可以在小面积组件上继续评估 Tier 3 Shader；当前生产路径仍保持纯 QWidget/QPainter，不引入 OpenGL 运行时依赖。

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

实验 5 产物：`docs/images/liquid-glass-visual-preview.png`。预评审中壁纸结构在三个玻璃区域内均能保持位置连续，28px 模糊、顶部高光、底部内阴影和 1px 边框可正常合成；浅色壁纸区域仍需在 Tier 1 阶段继续微调 tint 对比度。

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
- [ ] 左侧高光与右侧内阴影
- [ ] 微噪点预生成 tile
- [ ] 右键菜单玻璃样式增强
- [ ] 统一三类组件的 tint/对比度参数

### 第三阶段：Tier 2 — 合成器委托模糊

- [ ] KWin Blur X11 协议封装函数
- [ ] SmartSpaceWidget 置顶模式实时模糊
- [ ] QMenu 弹出菜单实时模糊
- [ ] 合成器兼容性回退测试

## 性能记录

测试环境：FTG340 / ARM64，Release，2880×1800，StackBlur 半径 28，全分辨率 RGB32。

| 实现 | 连续三次耗时 |
|---|---:|
| 单线程首轮实现 | 141 ms（单次） |
| 最多 8 线程的行/列并行实现 | 66 / 58 / 52 ms |

结论：实际真机数据略高于早期 26–41ms 估算，但仍在规划的 50–200ms 一次性启动/壁纸重建预算内。运行时不执行卷积。2880×1800 RGB32 模糊缓存本体约 19.8 MiB；生成期间临时图像会带来约 19.8 MiB 的短时峰值。

## 构建与验证

- `ukui-fences`、`qopenglwidget_shader_probe`、`glass_visual_preview` 构建通过。
- `stack_blur` 和 `office_document_factory` 测试通过。
- 完整 CTest：8 项中 5 项通过；3 项 UI/资源集成测试未通过：
  - `smart_space_ui_smoke`：Xvfb/DBus 会话提前断开并触发外部桌面应用。
  - `smart_space_policy_smoke`：xdotool 未在固定坐标打开设置窗口。
  - `smart_space_resource_benchmark`：25,000 文件 fast-full 流程超时。

这三项失败尚未归因到玻璃绘制改动，不能据此宣告全量回归通过；进入 Tier 2 前应单独稳定测试夹具并重跑。

## 下一步

1. 在实际桌面会话做一次人工视觉验收，确认深/浅壁纸下的 tint 和文字对比度。
2. 完成 Tier 1 剩余的左右边缘层、微噪点和菜单样式。
3. 封装 `_KDE_NET_WM_BLUR_BEHIND_REGION`，仅用于 SmartSpace 置顶窗口和 QMenu，并保留当前 Tier 0 回退。
