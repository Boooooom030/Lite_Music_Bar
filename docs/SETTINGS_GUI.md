# 设置窗口与配置协议

设置窗口位于 `settings/`，是独立的 WPF 8 程序 `MusicBar.Settings.exe`。任务栏主程序 `MusicBar.exe` 始终是原生 C++，不加载 .NET；找不到设置程序时，托盘中的“设置”入口禁用，其余功能照常运行。

当前机器只有 .NET 8 SDK，因此界面先使用 WPF 自定义的 Fluent 风格控件，并跟随 Windows 应用亮暗主题。WPF 官方内置 Fluent 主题从 .NET 9 开始；未来升级目标框架时可替换样式，不改变下述设置协议。

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

WPF 程序将新内容写入同目录临时文件并原子替换原文件；保存前检查文件时间戳，避免覆盖设置窗口打开后由托盘菜单写入的更改。C++ 托盘菜单也通过临时文件替换写入。主程序每秒检查文件时间戳，在 UI 线程上应用更改；歌词来源改变时重新请求当前歌曲歌词，FFT 模式改变时重新连接音频采集。

## 构建与分发

在仓库根目录执行：

```powershell
dotnet publish settings/MusicBar.Settings.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=None -o build-settings
```

输出 `build-settings/MusicBar.Settings.exe` 是单文件设置程序，但仍依赖目标机器安装 .NET 8 Desktop Runtime。将它放在 `MusicBar.exe` 旁边即可启用托盘“设置”。GitHub Actions 把它作为独立的可选 ZIP 发布，不增加主程序 ZIP 的运行时依赖。

设置页目前覆盖已经存在的四类开关。后续如需设备预览、实时 FFT 预览或更复杂的设置交互，再设计进程间通信；当前文件协议足以完成保存与热更新。

## 参考资料

- [WPF 官方 Fluent 主题（.NET 9+）](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/whats-new/net90)
- [.NET 单文件部署](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview)
