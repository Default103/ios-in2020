# IN2020 图形修复构建与手动验证 — 2026-11-01

状态：源码、云端构建、离线检查及最终稀疏展开 SHA256/CRC 校验全部通过；手机实测待用户执行。保持八核、60 Hz；不把 present 调用数当作屏幕 FPS。

## 修复内容与边界

上一轮实际日志表明八核已在线，但 GPU 初始化之后提交数不再增加。本次接入 Mesa/Freedreno、EAGL/CoreAnimation 桥接及 GPU IOSurface 导入/同步，补上用户态到现有 SweetGPU 的路径。只移植图形相关内容，不改变触摸、供电、ZAP 或物理内存参数。源码来自固定的公开 ios-apollo 提交 0784a28634a08b17fb4cb6c3d5fa3c3c2422fa9d；Mesa 25.0.7 下载有哈希约束。

PosterBoard 的旧日志已用本机原始共享缓存定位：去除 slide 后 PC 0x1c1778268，是 PBFPosterExtensionDataStore 首次初始化中 !_stateLock_switcherConfiguration 断言。本次兼容库在已有配置时保留它并跳过重复首次初始化；没有配置则调用原方法。方法/ivar 不匹配时不安装保护，不全局屏蔽断言。真实运行效果仍待验证。

assetsd 的旧栈只保留终止过程，原始异常已丢失，因此本次仅加入原始 ObjC 异常说明与抛出栈记录，保留原异常行为。若问题是纯 C++ 异常而非 ObjC 异常，这个记录钩子不能给出原始 C++ throw 原因。不能宣称 assetsd 已修复，也不能保证所有 App 或壁纸功能恢复。

## 已完成的验证

- 内核 CI [41329071725](https://github.com/Default103/ios-in2020/actions/runs/41329071725) 成功；源码 4669fc8c2b781dd209924b00edc17bdaadebc129。
- 用户态 CI [41329886029](https://github.com/Default103/ios-in2020/actions/runs/41329886029) 成功；源码 84baaf27eafaae8ed4d463058ff525ba271e7c20；arm64、签名及非系统动态依赖检查通过。
- 实际内核注入参数解析函数的本地 C 测试通过；实际 PosterBoard 重复初始化保护函数的本地 C 测试通过。它们不能代替手机测试。
- 内核 SHA256：5292ace8f54b525949d031d955cae66f9eb1b610f97c5dca9189cebc746f18e9。
- 新根镜像从上一轮 32 App/壁纸镜像独立复制。安装四个库、两个测试程序，均哈希核对；fsck 修复/只读检查和安装后只读复核通过。32 个 App 的 3840 个文件、主程序哈希及壁纸配置保留。
- 测试任务只在每次启动时运行一次，不自动重启；stdout/stderr 使用 /dev/null，经 sweetnull 写入持久日志。现有补丁 0048 已记录直接使用 /dev/console 可能导致 launchd 任务无法启动，因此这里不使用该路径。
- Android boot 头、原始 DTB、冻结 loader 与八核 ADT 保留；打包工具逐字节检查内核容器/载荷/DTB。两版实际 cmd 长度 491/535 字节，均小于 608 字节。

最终稀疏包 SHA256：50f6d28bc2d1b35f794d39cd53c15845184eded922a7c803f4f1483a435e7605，大小 12349747928 字节。

最终原始根镜像 SHA256：a492f72dd247cb917ccd740ea5cd615a5d50ee28cf410319d4269975df17126c。稀疏包哈希和展开校验详见同目录 verification.json、SHA256SUMS 和 sparse-verification.json；稀疏包使用 RAW/FILL，禁止 DONT_CARE，独立展开哈希及 CRC 必须通过。

## 文件

根镜像目录：M:/ios-in2020/builds/ios16-root-20H392-in2020-gpu-bridge-20261101/

- ios16-root-20H392-in2020-gpu-bridge.img：16 GiB 原始 HFSX。
- ios16-root-20H392-in2020-gpu-bridge.simg：供 fastboot 分块发送的稀疏包。
- graphics-manifest.json、graphics-run.log、graphics-stdio-fix.log：安装、复核、任务日志路径修正及 fsck 记录。

启动镜像目录：M:/ios-in2020/builds/xnu-41329071725/in2020-graphics-tests/

| 版本 | 路径 | SHA256 |
|---|---|---|
| GPU 自检，桌面保留软件渲染 | gpu-probe/in2020-ios16-gpu-probe-boot.img | 526e8d25fa1de7cc6625641aeff13187bac3081350a134eb22b2fe99482129f7 |
| 启用 CoreAnimation 桥接 | ca-bridge/in2020-ios16-ca-bridge-boot.img | 45ef2b33c49dcd7edf8e2d727a992035f2f11309e98465dacca8d17a4b921821 |

两版均 cpus=8 sweetfb_hz=60 sweetperf=5 sweetlog=1；保留原有 sweetgpu=22 sweetgpuzap=3 sweetgpukeep=1。probe 仅向 PosterBoard/assetsd 注入兼容库；ca-bridge 另向 backboardd 注入 libsweet-ca。完整 cmd 见各自 verification.json。不要把 scripts/build-in2020-gpu-userland.py 的旧 cmd 直接覆盖到这里。

## 手动测试，第一阶段

以下命令由用户执行；本轮 agent 没有访问或操作手机。刷 userdata 会覆盖当前 iOS 根文件系统及其数据；先保留需要的数据。不要执行 erase/format、修改 GPT 或刷其他分区。新根镜像已包含上一轮 32 个 App，无需先刷上一轮补齐镜像。

**先刷对应的新根镜像，再临时启动。旧根镜像缺少注入库，不能搭配这两个启动版本。**

在 Windows PowerShell、手机处于 Fastboot 时：

```powershell
fastboot -S 512M flash userdata "M:\ios-in2020\builds\ios16-root-20H392-in2020-gpu-bridge-20261101\ios16-root-20H392-in2020-gpu-bridge.simg"
fastboot boot "M:\ios-in2020\builds\xnu-41329071725\in2020-graphics-tests\gpu-probe\in2020-ios16-gpu-probe-boot.img"
```

先等待最多约 3 分钟，给 GPU 服务和一次性测试留出时间。若能进入桌面，静置 30 秒、连续滑动/打开文件夹 60 秒、再静置 30 秒，记录是否黑屏、卡住、壁纸/照片是否异常。然后手动进入 TWRP，在项目根目录执行：

```powershell
Set-Location M:\ios-in2020
python scripts/pull-in2020-perf-log.py --out logs/perf-phone-graphics-probe-20261101
python scripts/analyze-in2020-perf.py logs/perf-phone-graphics-probe-20261101/sweetlog.txt --out logs/perf-phone-graphics-probe-20261101/summary.json
```

pull 脚本仅在 root TWRP 读取 userdata 尾部日志；若该输出目录已存在，使用新名称。保留 sweetlog.bin、sweetlog.txt 和 capture.json。预期应看到：

- sweet-gpu-test: ... A PROGRAM DRIVES THE GPU，且 0 words wrong。
- sweet-gl-test: renderer 为实际 Adreno/Freedreno，末行 MESA DRAWS WITH THE GPU；若提示 FAILED (software, not the GPU)，不算成功。
- sweet-compat 的 guard installed / duplicate setup skipped / original ObjC throw。layout mismatch 应保留原日志，不能视为保护已经生效。

**先查看第一阶段结果，再决定第二阶段；probe 就黑屏或测试失败时不要直接升级到 ca-bridge。**

## 手动测试，第二阶段

第一阶段确认可启动且 GPU/Mesa 自检成功后，手动回到 Fastboot，不必重新刷根镜像：

```powershell
fastboot boot "M:\ios-in2020\builds\xnu-41329071725\in2020-graphics-tests\ca-bridge\in2020-ios16-ca-bridge-boot.img"
```

重复相同操作与 TWRP 抓日志，输出另存 logs/perf-phone-graphics-ca-20261101。需要确认 sweet-ca 真正加载 libsweet-gl，且一次性测试结束后，滑动桌面期间 GPU 提交/完成 fence 仍持续增长，无 fault/超时；对照活动窗口的 swap/present 时间与主观流畅度。

仅“GPU 计数增加”不够：这版 root 自带两个 GPU 测试，它们本身会增加计数。必须区分自检期间与自检结束后的桌面操作。present 是调用/尝试次数，可能包含等待 GPU 的延后处理，仍不能直接等同物理屏幕 FPS。GPU MHz 是请求值，非实测时钟。

## 回退

如新版本卡住，手动进入 Fastboot，先用冻结的已知八核/60Hz 启动镜像：

```powershell
fastboot boot "M:\ios-in2020\builds\baselines\in2020-cpus8-fb60-working-20261101\in2020-ios16-cpus8-fb60-boot.img"
```

该基线不注入新增库。若根镜像本身造成启动问题，再按之前备份/恢复流程手动恢复旧根镜像。不要把实验镜像刷入 boot_a/boot_b。

## 待完成

实际桌面 GPU 加速、PosterBoard 保护效果、assetsd 原始异常、壁纸和 App 可用性均需上述手机日志确认。没有进行手机操作，也没有上传固件、共享缓存或备份。
