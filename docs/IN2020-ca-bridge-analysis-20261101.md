# IN2020 ca-bridge 实测分析 — 2026-11-01

结论：桌面 GPU 合成已实际启用，颜色/花屏和毛玻璃改善由用户确认；目前存在严重的帧回读瓶颈、未完整支持的绘制目标，以及新的 PosterBoard 预览生成异常。不能把 GPU 无 fault 等同整条图形路径正常。

用户明确说明此前 15–20 分钟等待与更换固件后首次启动一致，可直接测试 CA。因此首次 LaunchServices 建库等待不再作为本轮阻塞项，不再要求重复 probe 来解锁 CA。

## 数据与实际 GPU 活动

日志目录 logs/perf-phone-graphics-ca-20261101；raw SHA256 4d8c046ea47a95858ebf68fcdc0ae80b81901053c9ac2fd947a62132ba984394，total343473，未回卷；原始哈希、解码文本、重新计算 summary 均一致。51 个窗口，255.063 秒，全程八核。

backboardd 中 sweet-ca 已加载 libsweet-gl，EAGL 的 8 个方法全部替换；桌面上下文明确是 OpenGL ES 3.2 Mesa25.0.7 / FD650。GPU/Mesa 自检仍通过，自检结束后桌面提交/完成数继续增长，最终 submitted=completed=3277，rptr=wptr=2502，48 个 ready 样本全部 fault0。这次有真实桌面 GPU 活动。

## 为什么视觉改善，帧率却下降

用户确认颜色错误/花屏消失、毛玻璃正常。当前桥接除接入 GPU 外，还替换部分 CoreAnimation 着色器与混合路径；这些视觉正确性进展应保留。

但桥接并未走通共享显示表面：800 帧汇总中，0 个 IOSurface 成为纹理共享内存、87 个回退为拷贝，696 个 GL 错误。平均每帧119635微秒，其中112221微秒记在 copying out，约93.8%；后期许多单帧回读约186–195毫秒。该阶段包括 GPU 提交/完成等待、资源映射/转换及拷贝，不能把全部时间断言为 CPU memcpy，也不能据此认定 GPU 执行本身足够快。

最终 framebuffer present 平均2.463毫秒、最大7.409毫秒，远小于桥接回读路径的耗时。优化重点应放在桥接输出，继续增加 CPU 节点或只改最后一次 framebuffer 拷贝并不针对已观察到的主要瓶颈。

按 swaps>=20 的一致规则筛选活动窗口：CA 为24窗口/120.024秒/768次present，约6.40次/秒；probe约12.35次/秒。工作负载不完全相同，不能作为严格的帧率对照，但方向与用户体感一致。全程平均3.223次/秒及这些活动计数都不是物理面板FPS。

源码对应关系：

- userland/ca-bridge/eagl.c 的 surface_mirrored / surface_shared 排除了 deep 表面。
- 当前桌面使用 b3a8、两平面、1125×2436，行长4544；因而 attachImage 日志明确是 a copy。
- userland/mesa/patches/0001-freedreno-darwin.patch 的 osmesa_surface_resource 仅接受 BGRA、planes=0；无法直接导入上述桌面表面。
- target_to_surface 回退 glReadPixels，将渲染结果读回 IOSurface；已有 GPU blit/fence 通路未被这些显示表面使用。

下一版应优先给桥接产生的显示表面建立安全的 BGRA plane0 GPU blit 视图，保留当前修正后的着色器/颜色，以及 GPU 完成与显示同步。不能简单把所有原生 b3a8 当 BGRA：真实 b3a8 是 RGB10+独立alpha；当前桥接/显示驱动的约定才是将 BGRA8 结果放入第一平面。必须限定布局、生产者和使用范围，检查尺寸/步长/偏移/容量并保持生命周期与栅栏。

## 应用黑屏

Maps 与 FaceTime 均出现 exec，日志没有这两个进程的 CRASH；不能描述为应用崩溃。backboardd 中出现多次 b3a8 attachImage: not done，随后 glDrawElements: error0x506，即 INVALID_FRAMEBUFFER_OPERATION。可见日志有60条该错误，累计错误计数更多。这说明至少存在未完整支持的表面/绘制目标，是黑屏的具体排查方向。

当前注入只覆盖 backboardd 的合成，以及 PosterBoard/assetsd 兼容。不能因为合成器用了 FD650，就宣称所有应用内部的 Metal/OpenGL、相机、网络地图等功能已经适配。现有日志没有 Maps/FaceTime 自身的详细渲染错误，尚不能把它们黑屏全部归到同一个 FBO 错误。下一步需记录被拒绝表面的完整布局、FBO状态和所属上下文；若合成目标修复后应用仍黑，再分别分析应用内部渲染。

## 壁纸的新证据

PosterBoard 本次产生一次 SIGABRT6。兼容库记录到原始异常：NSInvalidArgumentException，构造 NSDictionary 时 objects[1] 为 nil。

原始 throw 栈以本机相同20H392共享缓存、扣除本轮slide0x2f3c8000符号化：

- PBFHomeScreenPreviewImageBuilder _imageForView: +428，unslid0x1c17ccaa8。
- homeScreenImageForConfiguration:unlockedPosterSnapshot:screen:imageScaleRelativeToScreen:options:completion: 的回调。

这是预览图生成路径的新异常，区别于上一轮已处理的 datastore 重复初始化断言。现有栈只证明某个字典值缺失，尚未证明具体是 image、元数据还是其他对象。不能直接跳过所有 nil 字典值或宣称它就是壁纸缺失的唯一原因。壁纸资源补齐不等于 Poster 渲染/快照链路完整。

本轮另有 driverkitd SIGKILL9；未记录 assetsd、activityawardsd、mediaserverd CRASH。单轮未复现不等于全部根因修复。

## 后续优先级

1. 修复显示表面 GPU blit/共享路径，减少同步全帧回读；同一操作对照 copying out 时间、共享/拷贝表面数、帧节奏和颜色/毛玻璃。
2. 补齐被拒绝的表面布局与 FBO 完整性检查，排查 Maps/FaceTime 黑屏，避免用全局忽略 GL 错误掩盖问题。
3. 沿 PosterBoard _imageForView 的原始异常查清 nil 来源，再修预览/壁纸路径。

无需再次重刷现有固件来解释这些已经可定位的图形错误。两套 boot/root 基线继续保留。本次分析没有修改手机或运行 adb/fastboot；诊断结果存 reviewed-analysis.json、poster-symbols.json。
