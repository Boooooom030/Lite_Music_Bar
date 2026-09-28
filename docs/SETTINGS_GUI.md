# 设置窗口与配置协议

设置窗口由 `MusicBar.exe` 内的 `src/ui/SettingsWindow.*` 实现。右键音乐栏或托盘图标选择“设置”即可打开同进程窗口，不需要第二个 EXE 或 .NET 运行时。窗口使用 Win32、Direct2D 和 DirectWrite，左侧为任务栏、歌词、音频频谱分类，右侧为对应设置项。修改立即应用并保存。

文字使用 Segoe UI Variable；简体中文 Unicode 区间明确映射到等线（DengXian），并以 Microsoft YaHei UI 为备选。界面读取 Windows 应用亮暗主题；在支持的系统上请求 Mica 窗口背景，旧版使用同主题纯色。DPI 变化时窗口和绘制内容按比例调整。

## 版本 1 协议

配置文件固定为 `%LOCALAPPDATA%\MusicBar\settings.json`，UTF-8 编码：

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

缺失的已知字段使用默认值；未知字段忽略。未知版本、错误类型和非法枚举值会被拒绝，主程序保留当时设置。设置文件不存在时使用默认值。旧版 Windows 无法按进程采集时，`player` 模式退化到系统混音。

`SettingsStore` 使用临时文件和替换操作保存配置。设置窗口的修改直接调用 `App::ApplySettings`，随后保存；托盘菜单修改会同步到已打开的设置窗口。外部程序更新文件时，主程序每秒检查时间戳并在 UI 线程应用。歌词来源改变时重新请求当前歌曲歌词，FFT 模式改变时重新连接音频采集。左侧底部提供重新读取与恢复默认。

## 构建与分发

设置窗口随 CMake 主目标一起构建。发布包只有 `MusicBar.exe` 和第三方许可文本。设置文件位于用户目录，不需放在 EXE 旁边。

## 参考资料

- [Windows 应用设置页设计指南](https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings)
- [Windows Mica 材质指南](https://learn.microsoft.com/en-us/windows/apps/design/style/mica)
- [DirectWrite 字体回退](https://learn.microsoft.com/en-us/windows/win32/api/dwrite_2/nf-dwrite_2-idwritefontfallbackbuilder-addmapping)
