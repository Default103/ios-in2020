# IN2020 第一阶段 probe 分析 — 2026-11-01

结论：GPU/Mesa 自检通过，但用户确认启动阶段等待了 15–20 分钟。已定位 SpringBoard 等待 LaunchServices 重建数据库的同步调用链。先保留现有数据重复启动同一个 probe，确认是否仅首次建库慢；暂缓 ca-bridge。无需重刷 userdata。

## 数据可信度

源目录 logs/perf-phone-graphics-probe-20261101。独立核对 sweetlog.bin SHA256 db4b5409c6f4d70baeb956d299ed66412f3e59752f89fd8dea487c1aeb67f697、环形头 total=426927、未回卷；重新解码与 sweetlog.txt 一致，重新计算与 summary.json 一致。日志含 3 个 NUL，文本搜索需 rg -a，普通 rg 会把后续内容当二进制而略过。271 个性能窗口，总计 1355.261 秒（约 22.6 分钟）。

## 已确认的结果

| 项目 | 实测结果 | 含义 |
|---|---|---|
| CPU | cpu0..7 SGI 启动成功，所有窗口 online=8 | 八核正常 |
| 原生 GPU 测试 | 64 次提交，0 个错误回读字，提交+等待平均 75 微秒 | 用户进程可提交 GPU 命令并等到正确结果 |
| Mesa | freedreno / FD650 / OpenGL ES 3.2 Mesa 25.0.7 | 使用硬件驱动，非 softpipe |
| 三角形测试 | 像素检查通过，GL error=0 | 绘制输出正确 |
| Mesa 连续测试 | 100 帧 256×256，平均 201 微秒；首次 74061 微秒 | 离屏测试成功，不是手机桌面帧率 |
| GPU | 268 个 ready 样本，fault 全为 0；最终 submitted=completed=167，rptr=wptr=2628 | 采样未显示 GPU 卡死或未完成命令 |
| PosterBoard | 保护已安装，实际跳过一次重复首次初始化；未出现该进程 CRASH | 保护命中此前断言路径，本次未复现原崩溃 |
| assetsd | 异常记录器已启用；无该进程 CRASH 或 original ObjC throw 记录 | 本次未复现，不证明根因永久修复 |

## 如何解读性能

实际 boot-args 仅向 PosterBoard/assetsd 注入兼容库，没有向 backboardd 注入 sweet-ca。因此 probe 保留软件桌面渲染。

GPU 提交在自检期间从 65 增至 167，之后 265 个 GPU 样本均停在 submitted=completed=167。summary 中 fence_advanced=true 来自这些自检，不能当作桌面加速证据；自检结束后静止符合此版本设计。

全程仅 763 次 present，平均 0.563 次/秒，包含大量无 present 的启动等待窗口，不能解读为 0.563 FPS。按与前一轮相同 swaps>=20/5秒 规则筛选，有 12 个活动窗口、60.010 秒、741 次 present，约 12.35 次/秒；最高单窗口约 21.20 次/秒。平均 present 时间 2.354 毫秒、最大 7.330 毫秒。操作负载与上一轮不完全相同，不能据此判定性能进退；这些依然是调用/尝试计数，不是物理面板 FPS。

## 剩余问题

- Mesa 输出 os_same_file_description 比较 DRM 文件描述时 Operation timed out，随后采用“同一文件可能共享描述”的兼容假设。这条消息不是 GPU fence 超时，出现在自检成功之后，且后续采样 fault=0、提交完成。但多上下文的正确性与性能影响尚未验证，第二阶段需留意。
- driverkitd 有一次 SIGKILL（9）；activityawardsd、mediaserverd 各一次 SIGABRT（6）。不能因 GPU 自检成功就宣称全部服务已修复。SIGABRT 的现有尾栈不足以确定原始原因，另列后续诊断，暂不据此编造修复。
- 壁纸是否实际显示、补齐 App 是否可用，不能仅靠本日志确认。

## 启动慢的具体证据

用户确认这次是在启动阶段等待约 15–20 分钟，不是进入桌面后静置。日志中的八组连续线程快照显示相同阻塞：

- SpringBoard 主线程：CCSModuleRepository 更新控制中心模块信息 → LSApplicationRecord → LaunchServices Database Context → _LSContextInitCommon → os_unfair_lock → ulock_wait。
- SpringBoard 另一线程：_LSContextInitCommon → _LSCopyServerStore → **LAUNCH_SERVICES_IS_RESEEDING_ITS_DATABASE_AND_MAY_BLOCK** → NSXPCConnection 同步调用 → 等待回复。
- GPU/Mesa 测试在累计采样约 15/30 秒时已经完成；PosterBoard 到约 1000 秒才启动，首次触摸约 1025 秒。这些时间相对性能采样起点，不是上电绝对时刻。

使用本机相同 20H392 原始共享缓存、扣除该次 slide 0xb420000 后符号化，结果存于 startup-symbols.json。明确可见的是 LaunchServices 数据库初始化/重建等待；尚未捕获 lsd 内部为什么迟迟不回复，不能断言数据库损坏。此前补齐 32 个 App，首次注册/建库是合理线索，但尚不能仅凭相关性认定根因。

## 下一步：同一 probe 二次启动，保留当前根镜像

不要重新刷 userdata，也不先升级 ca-bridge，以免清掉已建立的数据库或同时引入另一项变化。手动进入 Fastboot 后执行：

```powershell
fastboot boot "M:\ios-in2020\builds\xnu-41329071725\in2020-graphics-tests\gpu-probe\in2020-ios16-gpu-probe-boot.img"
```

从执行命令开始计时，记录 Apple Logo/进度条持续时间与进入可操作桌面的时间。

1. 若恢复至几分钟内正常进入桌面，更支持首次注册/建库开销。保留这次日志，再按上一份修复文档进入 ca-bridge 测试；无需重刷根镜像或重新构建。
2. 若仍等待 15–20 分钟，暂缓桌面加速测试。下一轮应针对 lsd、installd/MobileInstallationHelper 增加日志及线程快照，确定数据库重建、同步服务回复或超时中的具体阻塞点。不能通过跳过数据库锁/删除数据库来掩盖问题。

手动返回 root TWRP 后，在电脑执行并保留新目录：

```powershell
Set-Location M:\ios-in2020
python scripts/pull-in2020-perf-log.py --out logs/perf-phone-graphics-probe-repeat-20261101
python scripts/analyze-in2020-perf.py logs/perf-phone-graphics-probe-repeat-20261101/sweetlog.txt --out logs/perf-phone-graphics-probe-repeat-20261101/summary.json
```

将这次实际启动耗时与新日志一起记录。当前 ca-bridge 文件已核对 SHA256 45ef2b33c49dcd7edf8e2d727a992035f2f11309e98465dacca8d17a4b921821，与现有图形根镜像绑定一致，但本轮暂不作为立即下一步。

本次仅分析本地日志、符号化和核对本地镜像，未操作手机。
