# IN2020 GPU blit / 绘制目标 / Poster CPU 预览修复包 — 2026-11-01

状态：修复代码、内核/用户态云构建、独立根镜像离线检查和稀疏包展开校验通过。真实帧率、应用黑屏及壁纸恢复效果待手机验证。保持八核、60 Hz；本轮未运行任何手机命令。

## 本版修改

1. **减少全帧回读**：Mesa 增加显式的 BGRA plane0 视图标记，只用于桥接产生的 b3a8 目标；普通未标记的原生 RGB10+A 表面继续拒绝该解释。GPU blit 写入第一平面，保持现有纹理中间目标、着色器/颜色修正和显示栅栏同步，避免直接绘制引起的历史颜色/残缺问题。导入前检查格式、步长、偏移和容量；导入后检查 FBO 完整性，不完整则回退原拷贝路径。
2. **平面对齐和接口修正**：允许覆盖逻辑尺寸的对齐填充平面，仍验证起点和内存边界。新内核0117按实际20H392框架访问位置填入 plane+0x10 行步长，修复此前 API 返回零的接口问题。保留被拒绝目标的详细布局日志，用于排查尚未覆盖的情况。
3. **PosterBoard 预览回退**：用本机原始缓存确认 _imageForView: 将 newCommandQueue 结果放入字典，该队列缺失导致 objects[1]=nil。仅在这一方法检测到缺少 Metal 命令队列时，改用 UIGraphicsImageRenderer + CALayer renderInContext 生成 CPU 预览图，并匹配原 UIImage 的像素尺寸。方法签名不符不安装，Metal 队列正常则调用原方法；原异常记录保留。

这不是完整 Metal 驱动；CPU 预览也不能替代任意 Metal 图层。Maps/FaceTime 自身渲染、相机/网络等仍可能有独立问题。不能在手机验证前宣称壁纸、所有黑屏和目标帧率都已解决。

## 构建与离线验证

- 用户态 [CI41338508982](https://github.com/Default103/ios-in2020/actions/runs/41338508982) 成功，源码4b5eab425ecfab67616ab0db983aa9bee4d0be5f。最初预览库缺少 Foundation/CoreGraphics 显式链接，已修正并重建成功；Mesa、GL桥接、测试程序及预览库签名/依赖检查通过。
- 内核 [CI41338683465](https://github.com/Default103/ios-in2020/actions/runs/41338683465) 成功，源码cc91f2fe9ba1cec534895cfd72e1be50d14085ff；完整 Apple 编译/链接及 ZAP 保留区检查通过。
- 内核 SHA256：be27612a6383d04c8dfef7301ce7ffee9746e384a06e14c48b4a06e2d584c9fd。
- 本机 VM 对实际 Mesa 接受条件做视图标记/步长/容量/溢出回归，实际内核序列化函数做平面步长/alpha偏移回归，均通过；0117与精确源文件匹配。CPU预览的真实 UIKit 绘制只能在手机验证。
- 根镜像由前版 a492f72dd247cb917ccd740ea5cd615a5d50ee28cf410319d4269975df17126c 独立复制；更新前核对全部旧文件哈希，再替换六个产物。前后 fsck、只读复核通过；32个App的3840个文件、主程序哈希、壁纸配置及两个一次性任务保留。
- RAW/FILL 稀疏包独立展开 SHA256/CRC 通过，DONT_CARE=0；原始/稀疏/boot文件均冻结只读。三个 boot 均绑定本次根镜像，载荷/冻结loader/原DTB逐字节检查通过。

## 产物

根镜像目录：M:/ios-in2020/builds/ios16-root-20H392-in2020-gpu-blit-preview-20261101/

- ios16-root-20H392-in2020-gpu-blit-preview.img，16GiB，SHA256 **5b74ea9fe1659700a667cbf677046ec214a081d6cc0c841600271ed192516629**。
- ios16-root-20H392-in2020-gpu-blit-preview.simg，12349747928字节，SHA256 **0819cbc270076cb91a8df20b382816f2859a81b040181d50a69f5d3308e15757**。
- verification.json / sparse-verification.json / graphics-v2-manifest.json / graphics-v2-run.log。

启动镜像目录：M:/ios-in2020/builds/xnu-41338683465/in2020-blit-preview-tests/

| 子目录/用途 | boot SHA256 |
|---|---|
| gpu-probe-v2：软件桌面 + 新内核/兼容库/新增平面GPU测试 | db6230ea08e2555e025102dd8bd4c61e3b672bd3f50242e45df29da9f53c9e5d |
| ca-blit-preview：开启GPU blit和CPU预览回退 | d14d0eb8eedbd443e6fa9edfa59ead7ac9af8b3f10c55929b32f00aa5174e107 |
| ca-copy-preview：相同修复，但 glcopy=1 强制旧拷贝输出，供对照/回退 | 444edf7b55f7290452253ff73589e459a631a5b802cd36d6ba828b909e6f9f04 |

文件名均为 in2020-ios16-子目录名-boot.img。三版 cmd 分别505/549/558字节，小于608；从冻结八核/60Hz基线保留供电/ZAP/触摸等参数，新增 Maps/FaceTime 的 sweetnull 日志通道。完整 cmd 见各自 verification.json。

## 第一阶段：新根镜像 + 新增回归自检

**需刷本次新根镜像，旧 root 中没有这些用户态修复。** 刷 userdata 会覆盖当前 iOS 文件系统和数据；本镜像包含之前补齐的32个App，但不包含你手机本轮使用后新增的状态。先备份需要的数据。更换后首次启动仍可能像之前一样等待15–20分钟，不据此认定故障。

以下均由用户手动执行，手机先进入 Fastboot。只写已有 userdata，不格式化、不改GPT、不刷boot/recovery/modem等分区：

```powershell
fastboot -S 512M flash userdata "M:\ios-in2020\builds\ios16-root-20H392-in2020-gpu-blit-preview-20261101\ios16-root-20H392-in2020-gpu-blit-preview.simg"
fastboot boot "M:\ios-in2020\builds\xnu-41338683465\in2020-blit-preview-tests\gpu-probe-v2\in2020-ios16-gpu-probe-v2-boot.img"
```

此次 sweet-gl-test 在原三角形/100帧测试之后新增真实表面回归：逻辑65×65、填充80×80的双平面表面，拒绝未标记原生视图，再通过显式视图GPU blit，CPU核对BGRA颜色和独立alpha平面未被覆盖。**需要新增这一项也通过，不能只看旧的 MESA DRAWS WITH THE GPU。**

手动进入 root TWRP，电脑抓日志：

```powershell
Set-Location M:\ios-in2020
python scripts/pull-in2020-perf-log.py --out logs/perf-phone-graphics-v2-probe-20261101
```

要求看到：

- A PROGRAM DRIVES THE GPU，0 words wrong。
- FD650 / MESA DRAWS WITH THE GPU。
- **padded b3a8 plane0 GPU blit: PASSED**，native view rejected=1。

若第三项 FAILED，先保留日志，不进入 blit 版。Mesa 的 os_same_file_description 清理提示可能仍存在；它不是该测试的成功标记，也不等于 GPU fence 超时。

## 第二阶段：GPU blit 与视觉验证

第一阶段全部通过后，手动 Fastboot 临时启动，不重复刷根镜像：

```powershell
fastboot boot "M:\ios-in2020\builds\xnu-41338683465\in2020-blit-preview-tests\ca-blit-preview\in2020-ios16-ca-blit-preview-boot.img"
```

进入桌面、一次性测试结束后，静置30秒、连续滑动/打开文件夹60秒、再静置30秒。打开壁纸预览；分别打开 Maps/FaceTime，记录整个窗口黑还是仅地图/视频区域黑，并观察原已修正的颜色和毛玻璃是否保持。

手动进入 root TWRP 后：

```powershell
Set-Location M:\ios-in2020
python scripts/pull-in2020-perf-log.py --out logs/perf-phone-graphics-v2-ca-20261101
python scripts/analyze-in2020-perf.py logs/perf-phone-graphics-v2-ca-20261101/sweetlog.txt --out logs/perf-phone-graphics-v2-ca-20261101/summary.json
```

验收重点：

- 桌面输出出现 GPU blit/共享表面计数，常见帧从1picture copied out切换为drawn in place；copying out显著降低，而非只看GPU计数上涨。
- 自检结束后桌面操作仍有正常提交/完成，GPU无fault/超时；present仍是调用/尝试数，不是物理FPS。
- 若出现 GPU blit target incomplete，则已回退拷贝；保留该布局/FBO状态，不能算性能优化成功。
- PosterBoard 出现 missing-Metal-queue CPU preview hook installed，触发预览后 created；不再出现同一 nil commandQueue 字典异常。是否显示真实壁纸仍看屏幕结果。
- 未支持表面会打印完整布局，供继续定位0x506。没有App自身崩溃不等于App已完整工作。

## 对照与回退

如GPU blit仍花屏/更慢，可在相同根镜像下临时启动强制拷贝版，用相同操作抓独立日志：

```powershell
fastboot boot "M:\ios-in2020\builds\xnu-41338683465\in2020-blit-preview-tests\ca-copy-preview\in2020-ios16-ca-copy-preview-boot.img"
```

若两种CA版本都无法进入桌面，可回新 gpu-probe-v2，或冻结的旧八核/60Hz基线临时启动，先保留日志，不立即重刷：

```powershell
fastboot boot "M:\ios-in2020\builds\baselines\in2020-cpus8-fb60-working-20261101\in2020-ios16-cpus8-fb60-boot.img"
```

旧根镜像和此前成功启动包均保留。用户手动操作偏好持续有效；agent没有连接或修改手机。
