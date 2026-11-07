# 一加 8 Pro IN2020 相机适配交接报告

更新日期：2026-11-07。项目：`Default103/ios-in2020`，工作分支：`in2020-current`。

本文供其他 agent 从现有进度继续工作。这里的“成功”分别指硬件采集、文件校验、用户态取帧和屏幕显示；这些环节不能互相替代。实际照片、RAW、固件模块、设备转储和完整日志未随报告上传。

## 1. 当前结论与任务优先级

| 对象 | 已有证据 | 尚未完成 |
| --- | --- | --- |
| 前摄 Sony IMX471 | XNU 原生上电、寄存器编程、CSI/IFE DMA、单帧导出成功；主机独立验证文件长度和 CRC；多轮预览可辨认真实场景 | 系统内可见预览、实时视频、彩色处理、曝光控制、方向处理 |
| 长焦 Samsung S5K3M5 | 原生 ID、寄存器表、8 次 DMA 完成、最后一帧导出及主机 CRC 校验通过；用户认为图像已正常 | 动态范围很窄，画质/曝光仍需研究；没有系统内长焦按钮成功取帧的实测记录 |
| 主摄 IMX689 MP、超广角 IMX586 | 原厂模块、模式表、板级连接和 C-PHY 配置已做离线准备 | 尚未完成 XNU 原生硬件取帧，不能宣称可用 |
| 系统内双摄测试入口 | SpringBoard 中能显示窗口；前摄 ioctl、读回、CRC、主线程图像赋值均成功；软件绘制回调也完成 | 用户仍看不到图像；后续显示提交停止；双摄切换和连续取景未验证 |
| 系统按钮取帧落盘 | 补丁 0149 已提交，本地编译及补丁检查通过 | 撰写时内核构建 42314712425 仍进行中，尚未启动验证 |

用户最新优先级是先把**现有前摄和长焦**放入系统内测试，再继续其他镜头。当前不是 AVFoundation 驱动，也不是原生 Camera.app：只是一个注入 SpringBoard 的 UIKit 单帧测试窗口。尚未实现持续视频流、ISP 图像处理、自动对焦/曝光或标准相机会话。

当前最后一次设备操作将手机留在 ROOT TWRP，iOS 所在 userdata 未挂载。每次操作前都应重新检查实际连接状态，不能依赖此快照。

## 2. 最重要的真机证据

### 2.1 最新独立读回的前摄画面

为了确认采集是否有效，使用已有 stage 2 镜像重新自动取了一张，而不是尝试恢复重启前的 RAM。

- 内核：Actions [42310641810](https://github.com/Default103/ios-in2020/actions/runs/42310641810)，源码 `02405be3481303143bd8ccc737a01ad5f8c65087`。
- 导出路径：`/private/var/tmp/in2020-front-raw10-865736161.bin`。
- RAW10：2328×1748，stride 2912，载荷 5,090,176 字节；前面另有 64 字节头。
- 内核与主机独立 CRC32 均为 `7c30a044`。
- RAW 载荷 SHA256：`f0dbada608d1b10d9da6a388b911c9933780060417d51e3f73ac9a995f703787`。
- DMA 完成约 49 ms；清理及 SMMU 恢复通过；文件写入 `error=0 residual=0`。
- 目视预览能辨认真实场景。原始像素范围 62–104；灰度预览使用单独的显示亮度拉伸，原始 RAW 未修改。颜色、Bayer 顺序和曝光尚未标定。

这证实前摄原生采集有效，**不证明系统内画面已显示**。同一天较早的系统按钮取帧仅保存在 RAM，重启后没有对应文件，不能把这张新照片当作那一帧。

### 2.2 长焦 TELE5

- 内核：[42279828601](https://github.com/Default103/ios-in2020/actions/runs/42279828601)，源码 `19a6434da2b5a24874bb560f84e4bf942e3fdbe8`。
- ID `0x30d5`；918 条初始化及 53 条模式记录；曝光 1000 行、模拟增益 `0x20`、帧长 1656 行，均做寄存器读回。
- 依次获得 8 次新的 WM 完成，导出第 8 帧；没有 CSI ECC/CRC 错误，清理及 SMMU 恢复通过。
- 导出 `/private/var/tmp/in2020-tele-raw10-896803105.bin`；2104×1560，stride 2640，载荷 4,118,400 字节；CRC32 `b5b3414b`。
- 用户确认图像看起来正常；主机统计范围只有 62–67，曝光/增益、动态范围和画质仍是后续工作，不应宣称完整画质适配完成。

### 2.3 系统预览问题的三轮证据

| 版本 | 改动 | 实测结果 |
| --- | --- | --- |
| UI v1 / SYSTEM CAMERA1 | UIImageView + 8 位灰度 CGImage | 窗口可见，两次前摄请求成功，用户无画面；灰度纹理上传被记录，但不能据此认定格式不支持 |
| UI v2 / SYSTEM CAMERA2 | 转为不透明 BGRA32，增加心跳和主线程日志 | 用户看到计数停在 326；实际日志继续到 350；采集、读回、CRC、图像赋值和主线程回调均完成，仍无可见图像 |
| UI v3 / SYSTEM CAMERA3 | 普通 UIView 的 drawRect 中 CGContextDrawImage，绕开 UIImageView 的直接图像内容路径 | 前摄 CRC `904ca8d6`；源 582×437，绘制目标约 339×254.541；raster complete；心跳继续到 60；仍无可见图像 |

v2/v3 的关键共同点：取帧后，后台心跳、触摸/系统日志、显示 timer ticks 仍存在；GPU 计数显示 submitted==completed、fault=0，但后续 swaps/presents 和 GPU 新提交停止。**主线程没有卡在取帧或 image 赋值中；软件 drawRect 完成也不等于最终扫描输出成功。**

v3 采集/绘制结束后的 GPU 计数固定在 609/609。根因仍未确定，不能把 UIImageView、BGRA、共享电源、GPU 或 CoreAnimation 中任一项直接定为根因。

## 3. 代码结构及文件作用

以下链接均相对于本报告所在的 `docs/` 目录，可在 GitHub 中直接打开。

### XNU 原生驱动

| 文件 | 作用与阅读入口 |
| --- | --- |
| [in2020_camera.c](../in2020/camera/in2020_camera.c) | `in2020_camera_init()` 解析启动参数；stage 1 PMIC/引脚观察；stage 2 启动单帧线程；stage 3 注册系统测试服务。PMIC 只读观察接口也在这里 |
| [in2020_camera_board.h](../in2020/camera/in2020_camera_board.h) | 前摄板级常量、TLMM 引脚、CSI/IFE 地址和 SID 定义 |
| [in2020_camera_sensor.c](../in2020/camera/in2020_camera_sensor.c) | 选择前摄/长焦的严格私有 ADT 模式；PMIC/电压/BOB 状态检查；复位和供电 GPIO；开漏 bit-bang I²C；ID、模式编程、stream on/off 和恢复 |
| [in2020_camera_clocks.c](../in2020/camera/in2020_camera_clocks.c) | GCC/CAMCC/GDSC/REFGEN/MIPI 电源、PLL0、时钟根和分支、MCLK 的读回与恢复 |
| [in2020_camera_dma.c](../in2020/camera/in2020_camera_dma.c) | Apps SMMUv500 的空闲 context/SMR 分配、页表、带 guard 的 wired buffer、未匹配流故障门控、缓存维护、所有权恢复和不确定状态隔离 |
| [in2020_camera_capture.c](../in2020/camera/in2020_camera_capture.c) | 核心 `capture()`；GIC/PHY/CSID/IFE 设置、WM 完成轮询、停流、CRC、`export_frame()`；stage 3 的 cdev/ioctl/read、版本化 UI 库安装也在此文件 |
| [in2020_camera_raw.h](../in2020/camera/in2020_camera_raw.h) | `cr_configuration`、尺寸/stride/IOVA、寄存器访问及模块间接口 |
| [in2020_camera_plan.h](../in2020/camera/in2020_camera_plan.h) | 可做主机测试的计划解析、边界/CRC/SMMU 条件验证；不要绕过其校验 |
| [in2020_camera_user.h](../in2020/camera/in2020_camera_user.h) | 内核与用户态共享的 snapshot ioctl ABI：传入 uint32，1=前摄，2=长焦 |
| [in2020_rpmh.c](../in2020/audio/in2020_rpmh.c) | 位于 audio 目录但相机也依赖它；串行化 RPMh TCS 事务、enable-only 相机 VRM 票和相机 ARC 票 |
| [in2020_arc.h](../in2020/audio/in2020_arc.h) | CMD DB 中 cx/mx/mmcx ARC 资源和精确等级解析 |

驱动源文件是审阅入口；真正构建 XNU 使用 `xnu/patches/` 顺序叠加的补丁。**修改源文件时必须同步追加补丁**，并通过累计一致性检查，否则可能“仓库源已改，实际内核未改”。

### iOS 用户态和图形排查

| 文件 | 作用 |
| --- | --- |
| [camera_ui.m](../userland/camera/camera_ui.m) | 注入 SpringBoard 后延迟 30 秒建立窗口；镜头选择、后台 ioctl/read、CRC、图像建立、主线程更新、收起窗口、心跳及 raster 日志。当前是 UI v3 的普通 UIView 软件绘制 |
| [preview.h](../userland/camera/preview.h) | 严格帧头检查、完整读取、独立 CRC、RAW10 解包、4×4 Bayer 块灰度缩小、百分位显示拉伸及不透明 BGRA 转换 |
| [preview_test.c](../userland/camera/preview_test.c) | 已知 RAW10 向量、CRC、头部约束和转换缓冲区边界测试；也可输入真实 native-frame.bin 校验 |
| [eagl.c](../userland/ca-bridge/eagl.c) | 现有 CoreAnimation/OpenGL ES 桥接：纹理、IOSurface、frame 日志和 GPU 提交/等待；显示问题的重要排查入口 |
| [0116 图形桥接补丁](../xnu/patches/0116-in2020-ca-gpu-surface-bridge.patch) | `sweet_exec_inject()`、dyld 策略以及 SweetFramebuffer/SweetSurface 等图形相关内核改动；后续相机补丁在这里建立的执行注入基础上追加 UI 库 |

当前库路径固定为 `/usr/local/lib/in2020-camera-ui-v3.dylib`，内核先验证 ADT 中的 SHA256，再安装并核对文件。已有不同内容的文件会被拒绝覆盖。注入保留原有 SpringBoard compat 库。以后更换 UI 二进制需要考虑版本路径/安装机制，不能仅替换打包输入后假定旧内核会接受。

### 模式准备、打包与读回工具

| 文件 | 作用 |
| --- | --- |
| [build-in2020-front-mode.py](../tools/build-in2020-front-mode.py) | 从固定摘要的原厂 IMX471 模块提取并严格验证 155+57 条模式记录，输出仅保存在本地 |
| [build-in2020-rear-mode.py](../tools/build-in2020-rear-mode.py) | 固定原厂模块、模式和寄存器数据宽度；长焦及主摄/超广角离线计划生成。Samsung 间接写入顺序必须保留 |
| [build-in2020-cphy-ops.py](../tools/build-in2020-cphy-ops.py) | 从固定版本 CSIPHY1.2.1 源生成三组 C-PHY trio 操作；只是离线准备，未验证主摄/超广角原生出帧 |
| [verify-in2020-camera-board.py](../tools/verify-in2020-camera-board.py)、[verify-in2020-rear-board.py](../tools/verify-in2020-rear-board.py) | 对照实测 FDT/pinctrl 校验前摄和后摄连接；后者区分 PMIC GPIO 与 TLMM GPIO |
| [build-in2020-camera-preflight.py](../tools/build-in2020-camera-preflight.py) | stage 1 观察镜像打包 |
| [build-in2020-camera-raw.py](../tools/build-in2020-camera-raw.py) | stage 2 镜像：`--sensor front` 或 `tele`，私有表/PHY 数据本地嵌入 ADT，自动有限取帧并导出 |
| [build-in2020-camera-system.py](../tools/build-in2020-camera-system.py) | stage 3 镜像：同时嵌入两份计划/PHY 操作和签名 UI 库；内部还生成前摄、长焦 stage 2 子目录 |
| [adt.py](../tools/adt.py)、[mkpayload.py](../tools/mkpayload.py) | ADT 解析/序列化与内核 payload 构造 |
| [read-in2020-native-raw.py](../tools/read-in2020-native-raw.py) | ROOT TWRP 下对未挂载 userdata 做有边界的只读 HFS+/HFSX 目录/extent 解析，从日志中的精确路径提取文件并校验帧头/长度/CRC。不是 Android 相机采集后端 |
| [read-in2020-native-raw-test.py](../tools/read-in2020-native-raw-test.py) | HFS 路径、索引、循环、extent、范围和 RAW 完整性拒绝测试；父 ID 索引优化使实际读回从分钟级降至秒级 |
| [preview-in2020-native-raw.py](../tools/preview-in2020-native-raw.py) | 再次校验 RAW，生成 16 位 mosaic、原灰度及单独的显示拉伸 PNG/统计。它不恢复标准相机色彩 |
| [check-in2020-camera.py](../tools/check-in2020-camera.py) | 检查相机源和累计 XNU 补丁是否完全一致 |
| [board_test.py](../in2020/camera/board_test.py)、[front_mode_test.py](../in2020/camera/front_mode_test.py)、[raw_plan_test.c](../in2020/camera/raw_plan_test.c)、[tele_plan_test.c](../in2020/camera/tele_plan_test.c)、[raw_arc_test.c](../in2020/camera/raw_arc_test.c) | 板级、固定模式、非法计划和 ARC 等主机回归验证；通过它们不等于真机成功 |

### 构建、历史与诊断入口

- [xnu.yml](../.github/workflows/xnu.yml)：macOS 全量 XNU 编译，产物 `xnu-kernel`、`xnu-build-log`、`xnu-buildtree`。
- [in2020-camera-userland.yml](../.github/workflows/in2020-camera-userland.yml)：iPhoneOS SDK 编译 arm64 UI 库、ad-hoc 签名及严格验证；运行 ASan/UBSan 预览测试；产物 `in2020-camera-ui`。
- [in2020-checks.yml](../.github/workflows/in2020-checks.yml)：主机边界检查/ASan/UBSan 和 QEMU 控制台检查；QEMU 不模拟本机相机或验证显示。
- [NOTES.md](../NOTES.md)：按时间倒序保存完整操作交接，开头优先，旧段落可能已被后续结果推翻。
- [既有硬件记录](IN2020-speaker-camera-20261102.md)：较早的原厂硬件探测资料，不能替代本报告中的最新原生结果。

## 4. 当前硬件参数与采集约束

| 参数 | 前摄 IMX471 | 长焦 S5K3M5 |
| --- | --- | --- |
| 原生传感器 ID / I²C 7 位地址 | 0x0471 / 0x10 | 0x30d5 / 0x2d |
| 输出尺寸 / RAW10 stride | 2328×1748 / 2912 | 2104×1560 / 2640 |
| CSI | 4-lane D-PHY，PHY3 | 4-lane D-PHY，PHY2 |
| PHY 基址 / GIC IRQ | 0x0ac70000 / 480 | 0x0ac6e000 / 511 |
| MCLK / reset / VANA GPIO | 97 / 78 / 24 | 96 / 90 / 139 |
| SDA / SCL GPIO | 107 / 108 | 105 / 106 |
| MCLK 根/分支偏移 | 0x5054 / 0x506c | 0x5038 / 0x5050 |
| VDIG | PM8009 L3，ldof3，1056 mV，subtype 0x6a | PM8009 L4，ldof4，1056 mV，subtype 0x6b |
| VIO | PM8009 L7，ldof7，1800 mV | 同前摄 |
| 有限采集 | 一帧完成 | 连续确认 8 次完成，导出第 8 帧 |

两者共享 CSID-lite480 `0x0acd9200` 和 IFE-lite480 `0x0acd9000`，数据通路为 RDI0/WM0 的 RAW10。MCLK 为 19.2 MHz，PLL0 为 1200 MHz，IFE/CSID 为 400 MHz，PHY timer 为 300 MHz。

Apps SMMUv500 基址 `0x15000000`，实测 87 个 SMR、74 个 context bank；IOVA `0x40000000`，数据从 IOVA+4096 开始。使用未被引用的关闭 context 和 ASID `0xfffe`，不得接管现有活动映射。实测 0x800/0x840 路由可发布，0x801/0x841 的有效发布被拒绝后精确回滚；只在严格 fault-on-unmatched 门控下允许已验证子集，绝不能启用全局 bypass。

内存 guard、故障状态、缓存维护、两次稳定 CRC、停流、引脚/时钟/SMMU 恢复是现有成功条件。只有精确清理后才能复用 wired 内存；不确定映射保持隔离直到重启。共享 BOB 不被改写。不写 EEPROM、OIS 或标定数据；原厂固定表中的特定记录由验证器单独约束，不能扩展成任意寄存器访问。

主摄/超广角需要 C-PHY，不应照搬前摄/长焦的 D-PHY 路径。超广角 VANA 连接 PM8009 GPIO2，不能误当 TLMM。相关供电和 C-PHY 参数仍需原生验证，尤其不能猜测 PMIC 地址后直接写入。

## 5. 启动模式、ABI 与持久化变化

- `in2020cam=1`：观察模式，不启动传感器、CSI/ISP 或 DMA。
- `in2020cam=2`：启动后延迟约 5 秒自动有限采集，并尝试文件导出，不依赖系统测试窗口。
- `in2020cam=3`：安装 UI/注册 `/dev/in2020-camera`，系统启动后按按钮发起有限采集。

stage 3 的 `_IOW('C', 1, uint32_t)` snapshot ioctl 在内核中同步完成有限采集，UIKit 从后台 dispatch queue 调用。随后 read 返回 64 字节 `I20FRAME` 帧头及 RAW 载荷。一次只允许一个客户端和一个操作；错误后禁止继续触碰硬件直到重启。

帧头布局为小端 `<8s8I3Q`：magic、version、width、height、stride、bits、crc、flags、reserved、time、reserved64×2。头部共 64 字节；RAW 载荷是 `stride*height`。CRC 只覆盖载荷。

**0149 之前 stage 3 并不写照片文件。** 老日志 `exported=1` 在 stage 3 只是完成验证的标志，不能当作已写盘证据，必须看到带精确路径的 `IN2020 RAW: export ... error=0 residual=0`。

0149 将 stage 3 的已验证帧也写入唯一的 `0600`、`O_EXCL` 文件：`/private/var/tmp/in2020-{front|tele}-raw10-<machtime>.bin`，并将日志区分为 `validated` 和 `exported`。磁盘导出失败仍可向 UI 返回有效帧，失败原因需读 export 日志；stage 2 的成功仍要求导出成功。该变化尚未真机验证。

## 6. 补丁和构建版本索引

| 补丁范围 | 用途 |
| --- | --- |
| 0131–0132 | 前摄观察入口与完整原生有限 RAW 路径 |
| 0133–0138 | SMMU TCR2/ASID、可写 SMR、真实 FDT SID、发布/回滚与故障约束修正 |
| 0139 | IFE-lite Bus v3 突发长度读回修正，前摄实际取帧成功的关键之一 |
| 0140 | 长焦计划、供电、引脚和 PHY2 接入 |
| 0141–0143 | 时钟失败日志、MIPI 连续稳定状态判断、长焦 L4 实测 subtype 修正 |
| 0144 | 长焦固定曝光及第 8 次完成后取帧 |
| 0145–0146 | 系统 cdev/UI、双配置重复采集、wired 及 MMIO 映射复用 |
| 0147 | BGRA UI v2；包含一次补丁格式修正，见下文陷阱 |
| 0148 | 普通 UIView 软件绘制 UI v3 |
| 0149 | 系统按钮取帧持久 RAW 导出，待真机验证 |

| 构建 | 用途/状态 |
| --- | --- |
| [42229170609](https://github.com/Default103/ios-in2020/actions/runs/42229170609) | 首次已确认真实场景的前摄 RAW8 |
| [42279828601](https://github.com/Default103/ios-in2020/actions/runs/42279828601) | 长焦 TELE5，第 8 帧导出与 CRC 已核验 |
| [42300548139](https://github.com/Default103/ios-in2020/actions/runs/42300548139) | 首个系统双摄窗口，UI v1；两次前摄请求成功但用户无图像 |
| [42304009524](https://github.com/Default103/ios-in2020/actions/runs/42304009524) | **拒绝使用**：CI 显示 success，但 0147 被跳过，实际仍是 UI v1 路径，不能与 UI v2 配对 |
| [42306380957](https://github.com/Default103/ios-in2020/actions/runs/42306380957) | 修正补丁后的 UI v2 内核，真机显示问题仍存在 |
| [42310641810](https://github.com/Default103/ios-in2020/actions/runs/42310641810) | 已验证 UI v3 配对；系统软件绘制完成但无可见图像；其 stage 2 子镜像成功导出最新实际前摄照片 |
| [42310641719](https://github.com/Default103/ios-in2020/actions/runs/42310641719) | 已通过构建/签名/预览 sanitizers 的 UI v3，可与最新内核复用 |
| [42314712425](https://github.com/Default103/ios-in2020/actions/runs/42314712425) | 0149 内核；源码 `efbe8515ec0ece6244c704cd8522e64d32505e34`，撰写检查时 in_progress，尚未真机验证 |

已验证 UI v3 内核 SHA256：`67771574f335a5a2da5f7d37ecd46a56dbbaad2bc43e0e0132c971453f0276f8`。
UI v3 库 SHA256：`012795a93c51f8c6d53c83c9e56c4301dcb9233c9eb398ce83a95bca1e1e5c17`。
SYSTEM CAMERA3 启动镜像 SHA256：`f3e82220bef10d5e5a84e19ac3089efec3a2024aba79ee72b459654f7e9cf53d`。

## 7. 接手后的可复现流程

### 源码和构建

```sh
git switch in2020-current
python tools/check-in2020-camera.py
python tools/read-in2020-native-raw-test.py
# 新补丁同时用真实 git apply --check 对匹配的旧版源检查；仅做文本比较不够。
gh workflow run xnu.yml --ref in2020-current \
  -f runner=macos-15-intel -f macos_version=13.5 \
  -f kernel_config=DEVELOPMENT -f machine_config=SWEET -f keep_going=true
# 只有 UI 源更改时才需要新 UI 产物：
gh workflow run in2020-camera-userland.yml --ref in2020-current
```

下载 `xnu-kernel` 与 `xnu-build-log`，核对 SHA256SUMS、源 commit 和所有新补丁实际应用情况，再检查内核中 UI 安装/注入路径与 UI 二进制版本一致。**不要只检查 Actions 的绿色 success。**

曾发生无上下文 unified diff 被 git apply 拒绝，但构建脚本打印 `Skipping patch ... malformed` 后继续构建。原先的自制零 fuzz 文本检查未能发现 git apply 的上下文要求。补丁已改为有上下文版本；今后新补丁也应通过真实 git apply 检查，并在日志中查找跳过和失败记录。

```sh
python tools/build-in2020-camera-system.py \
  --workspace <本地私有工作区根目录> \
  --kernel <kernel.development.sweet> --kernel-sha256 <已核验摘要> \
  --kernel-run <run_id> --kernel-commit <实际源码commit> \
  --ui <in2020-camera-ui-v3.dylib> --ui-sha256 <已核验摘要> \
  --out <不存在的新输出目录>
```

此构建器不是只靠 Git 仓库就能运行：它还依赖私有工作区中的原厂模块、固定版本 PHY 源、已验证的 keys-on ADT/镜像和 baseline loader/boot 镜像。依赖路径及摘要门控见构建器与 `build-in2020-camera-raw.py`；不可删除断言以绕过缺失依赖。远端 agent 可以先做源码/静态研究；没有私有资料和实体设备时，不能声称完成重现。

### 真机和读回

1. 核对唯一设备、TWRP/root 或 Fastboot 的实际身份；Fastboot product 应为 `kona`。使用临时 `fastboot boot`，不要误用 flash。
2. stage 2 自动取帧；stage 3 等系统窗口出现后测试前摄两次、长焦一次、再切回前摄，分别记录状态。
3. 当前没有已验证的远程返回 Fastboot/TWRP 通道，需用户实体按键返回。
4. 临时启动已核验的 TWRP，等待 ADB；常需 30–60 秒。保持 iOS userdata 未挂载，从尾部 SWEETLOG 环形区域只读取日志。
5. 只使用本轮日志里成功 export 的精确路径读回，并独立验证 CRC；然后生成预览。

```sh
python tools/read-in2020-native-raw.py \
  --adb <adb可执行文件> --serial <当前设备序列号> \
  --path /private/var/tmp/in2020-front-raw10-<本轮日志时间>.bin \
  --out <新的本地帧目录>
python tools/preview-in2020-native-raw.py <帧目录>/native-frame.bin \
  --out <新的本地预览目录>
```

完整日志采集辅助脚本当前在本地工作区 `scripts/pull-in2020-perf-log.py`，不是仓库中的 `tools/` 文件。它核验 userdata 身份/容量，只读尾部 SWEETLOG。使用前应读脚本，不应推测偏移去写分区。构建 helper/本地编译器也不都随仓库发布。

## 8. 建议下一步与尚未证明的方向

1. 先完成 0149 的产物核验，复用 UI v3，真机确认**系统按钮**产生的 RAW 能落盘并独立读回；前摄和长焦分别测试。这样后续显示异常不会再次丢失证据。
2. 将“摄像头采集影响共享资源”与“向 CA 提交图像触发问题”分开验证：可考虑只采集不赋图、明确标注的离线真实 RAW 回放、同一窗口普通绘制等对照。离线回放不能冒充当前实时帧。
3. 同步记录 SpringBoard main/raster/heartbeat、backboardd 线程和 CA transaction/IOSurface、图形桥接 frame、GPU submit/complete、Framebuffer swaps/presents，找出第一处停止推进的位置。不要再单凭屏幕不动判断内核死锁。
4. 排查共享资源时核对相机操作前后 ARC、REFGEN、GCC/CAMCC、GIC 和 SMMU 所有权。特别是当前 `in2020_rpmh_camera_arc(false)` 会发送 cx/mx/mmcx 的 0 票，是否影响显示所需的共享状态**尚未证明**；不能未经对照就认定、绕过或粗暴改写共享资源。
5. 预览修复后再做可见双摄切换、持续流、色彩/曝光/方向和标准相机框架接入。主摄/超广角的 C-PHY 实测是另外一项待办。

## 9. 私有证据位置与操作边界

以下为原工作机本地路径索引，仅帮助同工作区的接手者定位，文件内容未上传：

- 工作区根：`M:/ios-in2020`；仓库：`projects/ios-in2020-main`。
- `logs/camera-front-readback-20261107/`：最新独立导出日志、RAW、CRC 元数据和实际场景预览。
- `logs/camera-system1-20261106/`、`camera-system2-20261107/`、`camera-system3-20261107/`：三轮系统显示问题日志。
- `logs/camera-native-tele5-20261106/`：长焦最后一轮 RAW/预览与证据。
- `builds/xnu-42310641810/in2020-system-camera3/`：已测系统镜像，以及 `front-profile/`、`tele-profile/` 自动 RAW 子镜像。
- `builds/camera-ui-42310641719/`：UI v3 库与摘要清单。
- `firmware/in2020-camera-stock-20261102/`：原厂传感器模块；`projects/ios-in2020/camera-reference/kona/`：固定版本原厂源码/参考清单；均需在原工作区验证存在。

使用临时启动，保留 GPT、bootloader、recovery、modem、persist、标定和备份；不要为显示排障刷写这些分区。不得接管活动 SMMU context、启用全局 bypass、忽略 CRC/guard/读回失败或覆盖不匹配的已安装 UI 库。不要把原厂固件、私人照片、RAW 或设备转储提交到仓库。

用户已授权本次项目的源码 push 和 macOS 构建；此报告中的历史授权不应被远端 agent 当作其自身环境的额外凭据或越权权限。继续工作仍须遵守当前会话和执行环境的权限边界。
