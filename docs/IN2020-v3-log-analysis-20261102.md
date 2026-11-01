# IN2020 v3 真机日志分析 — 2026-11-02

已从 Fastboot 临时启动本地 SHA256 验证的 TWRP，Sending/Booting 均 OKAY。通过 root ADB 只读提取 userdata 尾部 4100 KiB 日志区域；未刷写分区、未执行文件系统修复，userdata 未挂载。手机保持 TWRP。本机目录：M:/ios-in2020/logs/perf-phone-graphics-v3-ca-20261101。

## 捕获完整性

SWEETLOG 有效内容 913363 字节，75 次刷新，4 MiB 环形区未覆盖。原始捕获 SHA256：4d01250b043decb0ac675315b192763cecfc899d1300a10c7d483551c473c29c。独立重建文本与保存文本一致。另保存 TWRP recovery.log、挂载表、性能汇总、事件时间线和结构化复核结论；原始记录仅保留本地，不入 git。

## 已确认生效

- sweetlog.txt:137–141：更新器替换了四个有变化的文件，其余两个本来就与目标相同；最终六文件哈希全部通过，mode0。并非必须出现六条 updated 才算成功。
- :1752：原有 padded b3a8 plane0 GPU blit 自测通过。
- :1772–1782：b3a8 element1、w30r、wrapped half、RGBA half 四种真实目标全部通过，actual bridge target formats PASSED。
- 本轮未找到此前的表面拒绝/FBO incomplete/0x506 错误；多处 CA 累计统计报告 0 errors。不能据此宣称所有原生应用已正确绘制。
- :7634：PosterBoard CPU preview without Metal queue: 375x812 created。本轮 CPU 预览路径实际执行；上一轮没有调用。

## 壁纸：已定位到补丁 ABI 假设错误

:3388 明确记录 wallpaper service ABI mismatch; no request。因此原生静态颜色请求未发出，不能归因于“请求成功但显示失败”。

本机原始 20H392 缓存的 verbose ObjC 元数据及有界反汇编确认：

`+[SBSUIWallpaperService setWallpaperColor:darkColor:forLocations:]`

类型编码 `B40@0:8@16@24@32`：BOOL 返回、三个对象参数。当前补丁错误要求 void 返回和整数位置参数，保护检查拒绝调用。这是本次实现错误。下一版应按实际 ABI 修复，并从真实调用方核实 locations 对象的类型/内容以及颜色对象约定，记录 BOOL 返回；不能简单删掉检查，或继续把整数 3 当对象传入。此轮未修改手机壁纸配置，也没有声称视觉恢复。

## Maps / FaceTime：已经启动，尚不能确认界面恢复

Maps PID256：:4664 兼容库加载，:4776 UIApplicationDidFinishLaunching，随后有 scene 初始化。没有捕获 UIWindowDidBecomeVisible；该缺失是进一步排查线索，不足以单独证明窗口不存在。Maps 的 os_log 248 条，未触及 500 条限制。CloudKit 明确提示未登录 iCloud，另有蜂窝服务不可用；这些日志不能单独解释整个窗口全黑。

FaceTime PID289：:6726 兼容库加载，:6840 UIApplicationDidFinishLaunching，:6849 scene willConnect，:6865 UIWindow 可见，:6867–6869 窗口成为 key。其 os_log 155 条，未触及限制。没有捕获该进程原始 ObjC throw 或明确退出证据。如果实际仍全黑，应调查 rootViewController/view/layer 内容、场景提交和表面像素，而不是再次补装应用。Metal 缺失仍是候选因素，本轮没有确定因果证据。

SpringBoard os_log 达到约500条限制，后续该通道内容可能缺失；生命周期和显式兼容诊断使用独立输出。本轮没有用户画面反馈，因此不把窗口事件或测试通过等同于画面可用。

## 性能

149 个采样窗合计 745144 毫秒（约12.4分钟），8核持续在线，GPU 146 个 ready 样本，fault 样本0，提交/完成 fence 继续前进。present 7853次、平均调用耗时2192.74微秒；10.539次/秒是 present 调用率，绝非实测屏幕 FPS。

CA after7200frames 累计平均9153微秒，命令提交1318、GPU等待950、copyin约2127微秒；26个共享纹理、400个需复制的 IOSurface 导入记录。主显示读回仍为微秒量级，未回到旧版约112毫秒读回瓶颈。与 v2 的6613微秒累计平均相比，本次采样更长、操作与纹理不同，不能直接认定固定比例性能退化。下一步优化重点可放在纹理上传/转换，而不是撤销已经工作的 blit。

## 其他问题

assetsd 原始异常共11条：唯一键 ZGLOBALKEYVALUE.ZKEY 冲突2条、optimistic locking failure 7条、Constraint violation 2条。未见相应明确终止证据，可能由 CoreData 内部捕获/重试；不能把所有 throw 计为崩溃，也不能据此判断固件损坏。需独立分析迁移/并发写入，不应直接删除照片数据库。

## 下一步

1. 按原始 ABI 和真实调用方修复壁纸请求；保留返回值、配置读取和视觉结果区分。
2. 结合用户画面反馈，分别排查 Maps 场景/窗口建立、FaceTime 可见窗口的实际视图及表面内容；增加有限、可归属到具体窗口的像素/提交诊断。当前日志不足以断言应用黑屏根因。
3. 保留8核/60Hz、GPU blit与四类格式支持。assetsd 单独处理。

本次任务完成采集和分析，未自动构建/部署后续补丁；手机仍在 TWRP。
