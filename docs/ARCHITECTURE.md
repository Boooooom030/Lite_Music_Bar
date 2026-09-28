# MusicBar 架构

本页描述当前 C++ 实现。最初的设计计划和迭代记录仍可从 Git 历史中的 `PLAN.md`、`TODO.md` 查看；未验证的性能指标不作为已实现功能。

## 运行边界

`MusicBar.exe` 是 Windows 原生 C++20 程序。任务栏窗口、设置窗口、媒体控制、音频采集、歌词和渲染均在该进程内。Release 构建静态链接 MSVC 运行库；分发包中的程序本体只有一个 EXE，另附第三方许可文本。设置使用有版本号的 JSON 文件持久化；详见 [SETTINGS_GUI.md](SETTINGS_GUI.md)。

程序将窗口附着到 `Shell_TrayWnd`，窗口覆盖任务栏画布，再通过 `SetWindowRgn` 将绘制与鼠标区域裁剪到音乐栏。布局根据任务栏尺寸、通知区域和 DPI 计算，窗口内通过 Direct2D、DirectWrite 与 WIC 绘制封面、歌词、按钮和频谱。音量弹窗由独立的 Win32 窗口绘制，输出设备通过 Core Audio 枚举与切换。

## 数据流

`MediaSession` 监听 Windows GSMTC，选择有元数据的播放会话并提供标题、歌手、封面、时间轴和播放命令。`NetEaseMediaSource` 可在网易云音乐未公布合适的 GSMTC 会话时，从其进程读取当前歌曲 ID、播放状态与进度，再按 ID 获取元数据。`ProcessResolver` 将选中会话关联到播放器进程。

`LyricsService` 在歌曲变化时发起异步搜索。自动模式优先尝试匹配当前播放器的平台，再查询其余来源；可用来源为网易云、QQ、酷狗、汽水和 LRCLIB。结果按歌曲与来源缓存，`LyricsParser` 解析 LRC 及带逐字时间的格式。网络和解析失败不会阻塞 UI。

`WasapiLoopback` 在支持的 Windows 版本上采集选中播放器进程树的音频；旧版 Windows 保留系统混音路径。`FftAnalyzer` 使用 Hann 窗与 1024 点 FFT 生成 16 个频段，并做动态平滑。频谱代表音频能量，不用于推断歌曲名称或播放状态。

工作线程以消息或同步状态通知 UI 线程；Win32 消息循环拥有窗口与 Direct2D 资源。`App` 协调媒体、歌词、频谱、托盘菜单和交互。Explorer 重启后由任务栏重附着逻辑恢复窗口。

## 源码索引

| 目录 | 职责 |
| --- | --- |
| `src/core/` | 应用生命周期、消息处理、共享类型 |
| `src/ui/SettingsWindow.*` | 原生设置窗口，Direct2D 绘制、DirectWrite 字体和主题 |
| `src/shell/` | 任务栏附着、裁剪、托盘菜单 |
| `src/media/` | GSMTC、网易云来源、播放器进程解析 |
| `src/audio/` | WASAPI、FFT、音量和输出设备 |
| `src/lyrics/` | 歌词调度、解析、解密与来源实现 |
| `src/net/` | WinHTTP 客户端 |
| `src/ui/` | Direct2D 渲染、图片解码、歌词滚动、音量窗口 |
| `tests/` | FFT、歌词解析与滚动的回归测试 |
| `assets/` | 程序图标、manifest 和截图 |
| `third_party/` | vendored JSON 与 miniz 源码；见 [第三方说明](THIRD_PARTY.md) |

构建和运行方式见根目录 [README.md](../README.md)，待验证项见 [ROADMAP.md](ROADMAP.md)。
