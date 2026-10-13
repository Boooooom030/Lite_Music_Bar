# MusicBar 架构

本页描述当前 C++ 实现。最初的设计计划和迭代记录仍可从 Git 历史中的 `PLAN.md`、`TODO.md` 查看；未验证的性能指标不作为已实现功能。

## 运行边界

`MusicBar.exe` 是 Windows 原生 C++20 程序。任务栏窗口、设置窗口、媒体控制、音频采集、歌词和渲染均在该进程内。Release 构建静态链接 MSVC 运行库；便携 ZIP 中的程序本体只有一个 EXE，另附使用说明、构建信息和第三方许可文本。设置使用有版本号的 JSON 文件持久化到 EXE 旁的 `data/settings.json`，原子保存的临时文件也只在该目录内；不读取旧版用户目录配置，不回退写入 AppData。歌词、封面和渲染缓存仍只在内存中，没有新增磁盘缓存或日志。详见 [SETTINGS_GUI.md](SETTINGS_GUI.md) 和 [CI_BUILD.md](CI_BUILD.md)。

设置窗口是同一进程内的 C++ Qt Widgets 界面，使用静态链接的 Qt 6.11.2 与 Fluent-Qt v1.10.0 控件库，Qt 类型只出现在 `src/ui/qt/` 和入口 `main()`。任务栏、媒体、音频、歌词与音量弹窗仍走 Windows 系统 API 和 Direct2D，不经过 Qt 绘制。

程序将窗口附着到 `Shell_TrayWnd`，窗口覆盖任务栏画布，再通过 `SetWindowRgn` 将绘制与鼠标区域裁剪到音乐栏。布局根据任务栏尺寸、通知区域和 DPI 计算，窗口内通过 Direct2D、DirectWrite 与 WIC 绘制封面、歌词、按钮和频谱。音量弹窗由独立的 Win32 窗口绘制，输出设备通过 Core Audio 枚举与切换。

任务栏宿主使用 `WS_EX_NOREDIRECTIONBITMAP`，由 D3D11 上的 Direct2D `ID2D1DeviceContext` 绘制，DirectComposition 的预乘 alpha 交换链交给 DWM 合成。窗口依旧覆盖整个任务栏，但交换链及中间纹理只分配音乐栏尺寸，通过 visual offset 定位。普通帧直接绘入 back buffer；悬停时才经过中间位图和 GPU GaussianBlur，随后叠加按钮。每次 `BeginDraw` 都配对 `EndDraw`，呈现及调整尺寸前解除目标引用；设备错误会释放资源并在下一次刷新重建。硬件设备创建失败时使用系统 WARP 回退。

歌词与频谱更新由 `AnimationClock` 的高精度可等待定时器发送 UI 消息，最多保留一个未处理 tick；`SetTimer` 只作为启动失败的后备。`Present(1, 0)` 设置合成呈现节奏，不能将其理解为每次调用都阻塞到下一个 vblank。播放和动画结束后停止重复绘制，稳定悬停也不持续重绘；时钟仍会检查媒体和音频状态。

## 数据流

`MediaSession` 监听 Windows GSMTC，选择有元数据的播放会话并提供标题、歌手、封面、时间轴和播放命令。`NetEaseMediaSource` 可在网易云音乐未公布合适的 GSMTC 会话时，从其进程读取当前歌曲 ID、播放状态与进度，再按 ID 获取元数据。`ProcessResolver` 将选中会话关联到播放器进程。

`LyricsService` 在歌曲变化时发起异步搜索。自动模式优先尝试匹配当前播放器的平台，再查询其余来源；可用来源为网易云、QQ、酷狗、汽水和 LRCLIB。结果按歌曲与来源缓存，`LyricsParser` 解析 LRC 及带逐字时间的格式。网络和解析失败不会阻塞 UI。

网易云响应明确给出 `pureMusic: true` 时，文档通过 `isInstrumental` 表示已确认纯音乐，歌词行允许为空。该结果会正常缓存与发布，并停止来源回退；UI 在歌词区域显示固定的“纯音乐，请欣赏”，不参与逐句／逐字着色。缺失歌词、失败响应和空文本不会被推断为纯音乐；关闭歌词显示也会隐藏这条提示。网易云的空 YRC 不再覆盖有效 LRC。

歌词加载完成或歌曲、来源改变时，UI 获取一份歌词文档；每次绘帧由 `LyricsTimeline` 更新这份文档的行号、逐字索引和进度，避免每秒约 60 次完整复制整首歌词。空白时间戳仍决定演唱时长，但不会清除上一句可见歌词：间奏和尾奏保留上一句，到下一句非空歌词开始才切换。进度到演唱末尾后钳制到 1，拖动回前奏或更换文档时重新计算，不继承未来或上一首的着色状态。

首句尚未开始时，显示层通过 `LyricDisplayIndex` 预览第一条非空歌词，跳过前奏中的空白标记；演唱时间轴仍保持未开始状态，文字未着色、不提前滚动，也不对首句触发换句动画。到原始时间戳后开始渐进着色，拖回前奏仍显示首句未唱状态。空文档与纯音乐保持各自处理，不会显示上一首的首句。

普通 LRC 按当前句的时间区间渐进着色，逐字歌词按真实音节时间戳着色。换句动画在下一句起点前最多 180 毫秒开始：旧句向上淡出，新句从下方以未着色状态进入，到起点首帧已经就位并开始着色。间隔较短时过渡长度最多占两句起点间隔的一半。动画按播放位置计算，暂停冻结，拖动进度直接对齐目标帧；空白标记不触发退场，长间奏保留旧句到下一句临近。长句横向滚动仍平滑，下一句文本布局在预览时缓存并在切换时复用。`Direct2DRendererTests` 读回 GPU 歌词像素，覆盖渐进着色、间奏保留、旧句上移淡出、暂停过渡、准时就位、回退进度及逐字时间戳。

`WasapiLoopback` 在支持的 Windows 版本上采集选中播放器进程树的音频；旧版 Windows 保留系统混音路径。`FftAnalyzer` 使用 Hann 窗与 1024 点 FFT 生成 16 个频段，并做动态平滑。频谱代表音频能量，不用于推断歌曲名称或播放状态。

工作线程以消息或同步状态通知 UI 线程；入口运行唯一的 `QApplication` 事件循环，同时拥有任务栏窗口、其 WndProc 与 Direct2D 资源。入口将事件循环和退出回调交给 App，核心模块不引用 Qt 类型。后台线程发出的 `PostMessageW` 仍由原 WndProc 消费，Qt 不接管任务栏绘制。`App` 协调媒体、歌词、频谱、托盘菜单和交互。Explorer 重启后由任务栏重附着逻辑恢复窗口。

设置窗口首次打开时创建，关闭时隐藏并复用同一个顶层窗口；隐藏不结束进程，只有托盘“退出”才结束事件循环并清理。退出路径为：停止后台活动 → 释放设置窗口及其 Qt 对象 → `QApplication` 析构 → 反初始化 COM。

## 源码索引

| 目录 | 职责 |
| --- | --- |
| `src/core/` | 应用生命周期、原生消息处理、入口提供的事件循环回调、共享类型 |
| `src/ui/SettingsWindow.*` | 设置窗口门面（PIMPL），保持既有调用边界 |
| `src/ui/qt/` | Fluent-Qt 设置窗口：左侧分类导航、响应式卡片、四项绑定、系统 Acrylic 背板 |
| `src/shell/` | 任务栏附着、裁剪、托盘菜单 |
| `src/media/` | GSMTC、网易云来源、播放器进程解析 |
| `src/audio/` | WASAPI、FFT、音量和输出设备 |
| `src/lyrics/` | 歌词调度、解析、解密与来源实现 |
| `src/net/` | WinHTTP 客户端 |
| `src/ui/` | Direct2D 渲染、图片解码、歌词滚动、音量窗口 |
| `scripts/` | Qt Release 构建与启动脚本 |
| `tests/` | FFT、歌词解析与滚动的回归测试 |
| `assets/` | 程序图标、manifest 和截图 |
| `third_party/` | vendored JSON 与 miniz 源码；见 [第三方说明](THIRD_PARTY.md) |

构建和运行方式见根目录 [README.md](../README.md)，待验证项见 [ROADMAP.md](ROADMAP.md)。设置窗口已按 [Fluent-Qt 实施报告](FLUENT_QT_SETTINGS_IMPLEMENTATION.md) 重写为 Qt Widgets，当前布局以 [设置说明](SETTINGS_GUI.md) 为准，外观尚待用户审阅。本地 SDK 见 [环境记录](FLUENT_QT_ENVIRONMENT.md)。旧 UWP XAML 设置后端和旧自绘设置窗口已从源码和构建中移除。
