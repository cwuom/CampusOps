# 🎮 战斗与雨天体验验收

本轮沿用 UE 5.8 现有程序化动画、材质和 Canvas HUD，重点处理穿模、接触反馈、酒店雨效和可测量开销。参考目标为 COD20 / Modern Warfare III；这份记录不代表完成整款游戏的 3A 美术验收。

## 🔎 参考资料

- [MWIII 官方多人演示资料](https://www.callofduty.com/ca/en/blog/2023/10/call-of-duty-next-modern-warfare-III-warzone-mobile-announcement-zombies-multiplayer)：动作响应、换弹取消、移动状态衔接
- [MWIII 官方发布更新说明](https://www.callofduty.com/patchnotes/2023/11/call-of-duty--modern-warfare-iii-launch-patch-notes)：姓名牌可读性、减少 HUD 遮挡、击杀确认层级
- [官方 Multiplayer Hype Trailer](https://www.youtube.com/watch?v=YZhKBAEzSwM)
- [武器检查与换弹动画视频检索结果](https://www.youtube.com/watch?v=ArWWq20PVPA)
- [慢速换弹展示，狙击枪章节 09:48](https://steamcommunity.com/sharedfiles/filedetails/?id=3089852666)

线上视频已检索到来源；当前网页工具无法直接播放 YouTube 正片，未据此声称逐帧观看或精确复刻。实现判断以官方文字、本地引擎源码、项目源码和实际运行截图为证据。

## 🛠️ 改动边界

- 轻刀 / 重刺在 0.10 / 0.22 秒到达击出姿态并进行权威接触判定；枪托接触为 0.20 秒。保留原有伤害和冷却，切枪取消未落下的攻击，墙体阻挡近战
- 换弹支援手绕开机匣；取消换弹沿外侧返回；切枪清除旧武器手部平滑状态
- 近墙按枪械、附件和第一人称实际渲染坐标做有限扫掠，先收枪再校正位置；离墙平滑恢复
- 腿部命中回落到已模拟的躯干施加受击冲量，避免向运动学骨骼施加无效冲量
- 雨滴无表面命中时不生成假地面；随风跨格重新确认落点，飞溅要求真实朝上的表面命中；酒店远景雨幕限制在相机附近
- 每个角色 5 Hz 读取雨罩，逐渐淋湿、遮雨后缓慢干燥；身体与第一人称手臂共享对应角色的湿度
- 雨滴和飞溅复用实例池与变换数组，限制射线和粒子预算；HUD 合并命中 / 受击反馈，四个图层共享角色名单
- `-weatheronly` 只重建本次修改的雨幕和人物材质，避免重写无关水体、地图和贴图

## 🧪 复现方式

自动化测试先构建 Editor，再运行：

```powershell
& Tools\TestExperience.ps1
```

脚本检查 JSON 报告中的通过、失败和未执行数量；UE 进程返回 0 本身不足以证明测试通过。报告和日志保存到 `Saved/ExperienceTests`、`Saved/Logs`。

运行截图使用 `UnrealEditor.exe CampusOps.uproject "<地图URL>" -game -windowed -ResX=1280 -ResY=720 -benchmark -fps=30 -CampusScriptFile=E:/game-project/Tools/<脚本>`：

| 脚本 | 地图 URL | 覆盖 |
| --- | --- | --- |
| `Shots_ExperienceWeapons.txt` | `/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=0?Time=1` | 6 倍镜换弹、取消、轻刀 / 重刺、真实周界墙的冲刺 / ADS / 换弹 |
| `Shots_ExperienceRain.txt` | `/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1` | 干湿手臂、远景斜俯视、遮雨、泳池停雨 |
| `Shots_ExperienceHUD.txt` | `/Game/Campus/Maps/Map_Campus?game=TDM?Bots=1?Custom=1?BotHP=1000?Weather=0?Time=1` | 固定目标连射、击杀 / 爆头、淡出、换弹 / 换枪 |

性能采样单独运行 `Shots_ExperiencePerf.txt`，使用酒店 `Weather=3?Time=2?Bots=0`、1920x1080，关闭帧生成和 VSync，使用 Low + TSR。不要添加 `-benchmark` 或固定帧率参数；不要与其它游戏实例、构建或 GPU 任务同时运行。

## ✅ 本机实测，2026-10-16

- `CampusOpsEditor Win64 Development`、`CampusOps Win64 Development` 构建成功
- `Tools/TestExperience.ps1` 实际检查报告：9 通过、0 失败、0 未执行；报告为 `Saved/ExperienceTests/20261016-223607/index.json`
- 测试覆盖近战接触延迟、重复请求、遮挡、死亡、真实收枪取消，以及挥刀姿态、换弹绕行、湿度累积、采样频率一致性和屋顶遮雨
- 最终武器、机器人 HUD 截图进程均退出 0；对应日志没有材质编译失败、Fatal 或原有腿骨 `AddImpulse` 警告
- 酒店干湿手臂、远景与室内雨效已实际运行检查；贴墙的冲刺、ADS、换弹三个采样点 `ReachErr=0.00`
- 连射截图 `hud_hit__003.png` 已确认单个白色命中 X；普通击杀、爆头确认及淡出已检查
- `-weatheronly` 生成成功，0 error、8 项现有引擎 / 插件 warning；仅保留雨幕、人物两个材质资产改动

性能为两次各 12 秒的单次采样，硬件为 i7-14700KF / RTX 5070 Ti，运行于 Editor 的 `-game` 模式，1920x1080 / Low / TSR，无机器人、无帧生成：

| 指标 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 平均 FPS | 337.64 | 352.13 |
| 游戏线程 | 2.06 ms | 1.92 ms |
| 渲染线程 | 2.96 ms | 2.83 ms |
| GPU | 1.65 ms | 1.61 ms |
| Hitches / min | 0.00 | 0.00 |

平均 FPS 在这次采样中约提高 4.3%。天气种子和系统噪声未固定，不能视为统计显著结论，也不能外推到其它 GPU。原始日志为 `Saved/Logs/experience_before.log`、`experience_after.log`；截图拼图位于 `Saved/AutoShots/small/experience_weapons.jpg`、`experience_rain.jpg`、`experience_knife.jpg`。这些本机验收文件不进入 Git。

## 📦 Windows 打包复验，2026-10-16

- 已执行仓库规定的 Development `BuildCookRun -build -cook -stage -pak -archive`，约 108 秒完成，`BUILD SUCCESSFUL`、退出码 0
- 已直接更新 `E:/game-project/Build/Windows`；入口为 `Build/Windows/CampusOps.exe`
- 包内游戏 exe 的 SHA-256 与本次源构建一致；五个 pak / ucas / utoc 文件的 SHA-256 均与本次 staging 产物一致
- 从上述入口分别启动学校和酒店雨天地图，均带 9 个 bots；每张地图两次状态快照均包含 9 个 bots，脚本执行后正常退出
- 两次日志均未发现 Fatal、未处理异常、资源加载 Error 或材质编译失败；学校换弹与酒店城市远景截图已检查
- 烟测使用 `-UserDir=E:/game-project/Saved/PackageSmokeUser`，测试配置和存档实际生成在该目录下，保留旧包的用户配置和存档
- 原有 `Build/Windows/Windows.7z` 保留，仍是旧压缩包，本次未重新压缩

原始记录：`Saved/Logs/experience_package.log`、`package_smoke_school.log`、`package_smoke_hotel.log`、`Saved/PackageSmokeResults.json`。截图位于 `Saved/PackageSmokeUser/Saved/AutoShots/pkg_updated_*.png`。

## ⚠️ 验收边界

单机截图、局部自动化、本机性能数据和打包启动烟测不能证明多人网络时延、所有角色 / 枪械 / 附件姿态、GTX 1650 目标帧率或完整发行验收。本轮保留已有模型、贴图和动作资源；完整角色动画美术、主菜单重设计和全平台性能仍需要独立验收。
