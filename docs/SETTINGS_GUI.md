# 设置窗口与配置协议

设置窗口位于 `settings/`，是独立的 WPF 8 程序 `MusicBar.Settings.exe`。任务栏主程序 `MusicBar.exe` 始终是原生 C++，不加载 .NET；找不到设置程序时，托盘中的“设置”入口禁用，其余功能照常运行。

当前机器只有 .NET 8 SDK，因此界面使用 WPF 自定义的 Windows 设置风格控件。设置按“任务栏 / 歌词 / 音频频谱”分为左侧导航和右侧详细设置：右侧采用大标题、分组标题、每项独立的设置行与右侧操作控件，左侧搜索可筛选类别。中文明确使用 Windows 自带的“Microsoft YaHei UI”，避免随系统语言回退到繁体或日文字形。窗口跟随 Windows 应用亮暗主题；Windows 11 22H2 及更新版本以系统 Mica 作为底层磨砂材质，旧版 Windows 使用同主题纯色背景。卡片和侧栏使用轻透明填充，不额外引入 Windows App SDK 运行时。WPF 官方内置 Fluent 主题从 .NET 9 开始；未来升级目标框架时可替换样式，不改变下述设置协议。

## 版本 1 协议

配置文件固定为 `%LOCALAPPDATA%\MusicBar\settings.json`，UTF-8 编码，字段如下：

```json
{
  "version": 1,
  "showLyrics": true,
  "positionLocked": false,
  "lyricsSource": "auto",
  "fftCaptureMode": "player"
}
```

| 字段 | 允许值 | 含义 |
| --- | --- | --- |
| `showLyrics` | `true` / `false` | 显示或隐藏同步歌词 |
| `positionLocked` | `true` / `false` | 阻止拖动音乐栏，保留按钮点击 |
| `lyricsSource` | `auto`、`netease`、`qq`、`kugou`、`soda`、`lrclib` | 自动优先当前播放器，或手动指定来源 |
| `fftCaptureMode` | `player`、`system` | 当前播放器进程音频，或系统混音 |

缺失的已知字段使用默认值；未知字段忽略。未知版本、错误类型和非法枚举值会被拒绝，主程序保留当时的设置。设置文件不存在时使用默认值。旧版 Windows 无法按进程采集时，`player` 模式仍退化到系统混音。

设置窗口修改选项后立即读取当前配置，只修改该选项，再写入同目录临时文件并原子替换原文件；读取期间若文件时间戳变化，则提示重试，避免覆盖托盘菜单刚写入的更改。重新读取与恢复默认位于左侧底部。C++ 托盘菜单也通过临时文件替换写入。主程序每秒检查文件时间戳，在 UI 线程上应用更改；歌词来源改变时重新请求当前歌曲歌词，FFT 模式改变时重新连接音频采集。

## 构建与分发

在仓库根目录执行：

```powershell
dotnet publish settings/MusicBar.Settings.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=None -o build-settings
```

输出 `build-settings/MusicBar.Settings.exe` 是单文件设置程序，但仍依赖目标机器安装 .NET 8 Desktop Runtime。将它放在 `MusicBar.exe` 旁边即可启用托盘“设置”。GitHub Actions 把它作为独立的可选 ZIP 发布，不增加主程序 ZIP 的运行时依赖。

设置页目前覆盖已经存在的四类开关。后续如需设备预览、实时 FFT 预览或更复杂的设置交互，再设计进程间通信；当前文件协议足以完成保存与热更新。

## 参考资料

- [Windows 应用设置页设计指南](https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings)
- [Windows Mica 材质指南](https://learn.microsoft.com/en-us/windows/apps/design/style/mica)
- [WPF 官方 Fluent 主题（.NET 9+）](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/whats-new/net90)
- [.NET 单文件部署](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview)
