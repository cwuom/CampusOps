# 🎮 击杀反馈与主菜单验收

本轮依据用户提供的 COD 战斗 HUD 与大厅截图调整信息层级。参考图中的弹幕、浏览器弹窗、播放器控件不属于游戏 UI。

## 🛠️ 实现

- 服务端向有效击杀者可靠发送受害者姓名，底部显示“击杀了 XXX”，仅保留文字和骷髅图标，无黑色底板或下划线
- 爆头、第一滴血、双杀、三杀、远距离击杀、复仇、五连杀、近战击杀使用八种图案，在屏幕上方居中显示
- 同一击杀的成就先收集 100 ms，再按优先级展示；待显示队列最多四项，同类可合并，超过六秒丢弃，声音随实际展示播放
- 回合制的胜利击杀在结算画面上保留姓名和成就，下一回合清空旧反馈
- 主菜单采用中央人物、左上紧凑对局信息、底部横向模式卡片；设置和 LAN 地址按需展开，保留配装、枪匠、技能、干员、自定义规则和设置入口
- 主要面板只使用一层强度 2.5 的背景模糊，配置低质量替代；辅助卡片使用静态底色
- 三张模式图来自本项目原生 3840×2160 运行截图，不含 HUD，导入时最长边上限 2048，保留完整 mip 和三线性过滤；不增加实时场景捕获
- 修正窄按钮裁切、固定高度卡片文字越界和枪匠优缺点文字裁切

## 🧪 重现

先构建 `CampusOpsEditor Win64 Development`。更新模式卡片图片时运行：

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\game-project\CampusOps.uproject" -run=CampusBootstrap -menuartonly -unattended -nopause -nosplash
```

该入口只导入 `SourceArt/Menu` 中的三张图片。原始拍摄视角保存在 `Shots_MenuArtCampus.txt` 和 `Shots_MenuArtHotel.txt`，使用对应地图、原生 3840×2160、Ultra、无机器人及帧生成。

| 脚本 | 地图 | 覆盖 |
| --- | --- | --- |
| `Shots_MenuRefresh.txt` | 默认主菜单 | 主界面、展开设置、LAN、配装、枪匠、附件、干员、设置、自定义、技能 |
| `Shots_CombatAwards.txt` | `Map_Campus?game=TDM?Bots=1?Weather=0?Time=1` | 实际姓名、第一滴血、爆头、双杀、三杀、五连杀与淡出 |
| `Shots_CombatAwardRound.txt` | `Map_Campus?game=Gunfight?TeamSize=1?Bots=1?Weather=0?Time=1` | 胜利击杀结算与下一回合 |

运行参数沿用 `-game -windowed -benchmark -fps=30 -CampusScriptFile=E:/game-project/Tools/<脚本>`。验证使用独立 `-UserDir`，避免改动用户配置与存档。

## ✅ 已验证，2026-10-17

- Editor Development 构建成功，最后一次日志 `Saved/Logs/ui_attachment_build.log`
- 自动化报告 `Saved/ExperienceTests/20261017-011822/index.json`：16 通过、0 失败、0 未执行；包括七项成就队列和 Unicode 姓名回归测试
- 三张纹理导入成功，0 error、8 项现有引擎或插件 warning；未引入新的可修复源码 warning
- 实际检查 1280×720 与 1920×1080 菜单截图，以及 TDM 成就和 1V1 结算截图
- 截图目录为 `Saved/PlaylistReviewUser/Saved/AutoShots`、`Saved/PlaylistReviewHD/Saved/AutoShots`；战斗与结算图为 `Saved/UIReviewUser/Saved/AutoShots/ui_award_*.png`

## 📦 最终 Windows 包

- `BuildCookRun -build -cook -stage -pak -archive` 成功，最终运行约 101 秒，退出码 0，日志 `Saved/Logs/ui_playlist_package_final.log`
- 直接更新 `E:/game-project/Build/Windows`，入口 `Build/Windows/CampusOps.exe`；原有用户存档和配置保留，旧 `Windows.7z` 未重新压缩
- 包内游戏 exe 与本次构建 SHA-256 一致，五个资源容器均与 staging 一致，记录 `Saved/UIPackageFinalHashes.json`
- 从包内入口运行完整菜单截图脚本、学校 TDM 和酒店雨天 TDM；两张地图均带九个 bots，脚本正常退出，未发现 Fatal、断言、未处理异常或资源加载错误
- 学校采集九条 bot 状态、酒店两个时点共十八条状态，记录 `Saved/UIPackageSmokeResults.json`；原始日志 `Saved/Logs/pkg_ui_final_menu.log`、`pkg_ui_final_school.log`、`pkg_ui_final_hotel.log`
- 包内截图已检查新主菜单、枪匠完整属性文本、无黑底姓名、成就弹出与酒店雨天远景；位于 `Saved/PackageUIFinal/Saved/AutoShots`
- 烟测使用独立 `-UserDir=E:/game-project/Saved/PackageUIFinal`，测试存档实际生成在其 `Saved/SaveGames` 下；测试游戏进程全部正常退出，临时脚本与 PID 记录已清理

## ⚠️ 验收边界

截图、源码复核和自动化覆盖了本次本机 UI 行为；未完成跨机器联机、全部输入设备和完整 3A 美术验收。本轮未重新测量目标低端 GPU 帧率，不将减少模糊层数和静态卡片预算等同于实际 FPS 提升。
