# MusicBar 设置窗口重写实施报告（Fluent-Qt）

> **2026-10-13 实施更新：** 用户要求恢复左侧栏，并采用 Windows 设置的视觉层次和明显的毛玻璃效果。当前使用 Fluent-Qt NavigationView / ListView，默认显示全部四项设置，分类可过滤任务栏、歌词和音频卡片；背景请求系统 Acrylic，移除覆盖材质的滚动区域底色。默认 1040 × 740、最小 600 × 480，补齐标题栏按钮、系统中文字体与窄窗口排版；Qt Gui 已补齐 HarfBuzz。当前实现以 [设置说明](SETTINGS_GUI.md) 为准，外观待用户审阅，下文原始三页设计保留为交接历史。

日期：2026-10-12。状态：控件、布局和接入路线已选定，交给执行模型开发；MusicBar 的 Qt 设置代码尚未实施或验收。

用户已授权直接选择常规控件、先重写现有设置。执行者按本报告实现，无需再次向用户征求控件或框架选择。本报告替代旧 [XAML 实施报告](FLUENT_SETTINGS_IMPLEMENTATION.md)；依赖安装情况见 [环境记录](FLUENT_QT_ENVIRONMENT.md)。

## 1. 本轮交付目标

把现有设置窗口完整改为 **C++ Qt Widgets + calvinhxx/Fluent-Qt v1.10.0**。保持三个分类、四个设置项、重新读取和恢复默认，修改立即应用并保存。用户打开设置时应看到该库提供的现代 Fluent 风格控件、焦点和交互。

- 设置和音乐栏在同一个 `MusicBar.exe` 内，Release 静态链接 Qt、Fluent-Qt、必要平台插件和资源，EXE **小于 100,000,000 字节**。
- 任务栏继续采用现有 Win32 / Direct2D / DirectComposition；全任务栏 HWND、`SetWindowRgn`、透明合成和鼠标穿透保持原有约束。
- 媒体、音频、歌词、网络和音量弹窗维持现有实现。Qt 负责设置控件和主线程事件分发。
- 配置路径、版本、字段、枚举值、旧版系统的音频回退行为保持兼容。
- 使用已安装的通用 SDK，不把 Qt 源码、SDK 或 Gallery 源码复制进 MusicBar 仓库。

这里的 Fluent-Qt 是第三方 Qt Widgets 控件库，其控件由库内的 Qt 绘制代码呈现；它提供 Fluent / WinUI 风格，**不是微软官方 WinUI 控件，也不保证设置窗口全部采用 Direct2D 或 GPU 绘制**。任务栏现有 GPU 路径继续保留。上游定位见 [Fluent-Qt](https://github.com/calvinhxx/Fluent-Qt/tree/v1.10.0)。

本轮不增加主题选择、歌词偏移、字体、动画速率、自动启动、设备管理等新设置；不接入 QML、Qt Quick、WebEngine、Python、Spatial 或 Gallery 整套应用壳。最终运行路径只保留 Qt 设置实现，移除已被替代的 UWP XAML 和旧设置自绘代码。

## 2. 已选定的界面

### 2.1 窗口和布局

采用普通桌面设置窗口：左侧固定分类导航，右侧标题、说明和设置卡片。首开进入“任务栏”；本次进程内记住最后选择的页面，再次打开恢复该页，不写入配置文件。

| 部分 | 已定规格，单位为 Qt 逻辑像素 |
| --- | --- |
| 标题 | `MusicBar 设置`，使用现有应用图标 |
| 初始尺寸 | 960 × 640，放在当前操作所在显示器的可用工作区内 |
| 最小尺寸 | 720 × 480；小工作区优先保证窗口可操作 |
| 标题栏 | 使用 Fluent-Qt Window 默认的系统窗口管理，保留最小化、最大化、关闭；不自行实现拖动和缩放 |
| 导航区域 | 200 宽，固定左侧展开模式；不做自动折叠、汉堡菜单或顶部导航 |
| 内容边距 | 左右 28，上下 24；卡片间距 12 |
| 页面标题 | `Typography::FontRole::Title`，库内默认约 28 像素 |
| 设置标题、说明 | `BodyStrong` / `Body`；说明使用 Secondary 文本色并允许换行 |
| 设置行 | 最小高度 72，内边距 16；文字居左，控制项居右 |
| 下拉框 | 非编辑，首选宽度 220，高度遵循库的 sizeHint，避免截断中文 |
| 开关 | 默认大小，不人为放大；状态文字统一“开启”／“关闭” |
| 底部操作 | 导航区域底部竖排“重新读取”“恢复默认”，使用普通按钮 |
| 状态提示 | 内容区内简短文本，错误发生时显示，正常时隐藏，不遮挡控件 |

布局使用 `QVBoxLayout` / `QHBoxLayout`，让文字和卡片按内容增高。右侧页面可垂直滚动；普通宽度下保持一行左右布局，最小尺寸和高 DPI 下允许说明换行，必要时把控制项移到文字下方，不出现横向滚动或重叠。

```text
┌ MusicBar 设置                               最小化 最大化 关闭 ┐
│ MusicBar          │ 歌词                                     │
│ 设置              │ 配置任务栏歌词的显示和获取方式。             │
│                   │                                          │
│  任务栏           │ ┌ 显示歌词                    开启 [开关] ┐ │
│ >歌词             │ │ 在音乐栏中显示同步歌词。                 │ │
│  音频频谱         │ └────────────────────────────────────────┘ │
│                   │ ┌ 歌词来源                 [自动选择  ∨] ┐ │
│                   │ │ 自动模式优先使用当前播放器对应的平台。   │ │
│                   │ └────────────────────────────────────────┘ │
│                   │                                          │
│  [重新读取]       │ 更改会立即应用并保存。                      │
│  [恢复默认]       │                                          │
└───────────────────┴──────────────────────────────────────────┘
```

### 2.2 页面、文案与数据绑定

| 页面 | 标题／说明 | 控件 | 字段和默认值 |
| --- | --- | --- | --- |
| 任务栏 | **锁定位置**：禁止拖动音乐栏，播放控制按钮仍可使用。 | ToggleSwitch | `positionLocked = false` |
| 歌词 | **显示歌词**：在音乐栏中显示同步歌词。 | ToggleSwitch | `showLyrics = true` |
| 歌词 | **歌词来源**：自动模式优先使用当前播放器对应的平台。 | ComboBox | `lyricsSource = LyricsSource::Auto` |
| 音频频谱 | **音频采集范围**：选择频谱分析使用的声音来源；不支持按播放器采集的系统会使用系统声音。 | ComboBox | `fftCaptureMode = SpectrumCaptureMode::Player` |

歌词关闭时，歌词来源仍可选择并保存，不添加额外的禁用逻辑。音频设置只切换既有采集模式，不增加频谱显示开关。

| 歌词来源显示文字 | C++ 值 | JSON 值 |
| --- | --- | --- |
| 自动选择 | `LyricsSource::Auto` | `auto` |
| 网易云音乐 | `LyricsSource::NetEase` | `netease` |
| QQ 音乐 | `LyricsSource::QQMusic` | `qq` |
| 酷狗音乐 | `LyricsSource::Kugou` | `kugou` |
| 汽水音乐 | `LyricsSource::Soda` | `soda` |
| LRCLIB | `LyricsSource::Lrclib` | `lrclib` |

| 音频采集显示文字 | C++ 值 | JSON 值 |
| --- | --- | --- |
| 当前播放器 | `SpectrumCaptureMode::Player` | `player` |
| 系统声音 | `SpectrumCaptureMode::System` | `system` |

用 ComboBox 的 item data 存放明确的枚举值，通过 `findData()` 同步选中项。不要用中文显示文字解析设置，也不要让业务依赖界面行号的隐式转换。

### 2.3 操作规则

- 修改开关或下拉框：构造完整 `AppSettings`，经现有 App 回调立即应用并保存，不增加“应用／确定”按钮。
- “重新读取”：读同一个设置文件并应用；文件不存在时使用默认值，读取失败或 JSON 非法时保留当前值并提示“设置文件无法读取，请检查内容。”。保持现有成功读取后的回调／保存行为。
- “恢复默认”：将四项恢复为 `AppSettings{}`，一次性应用、保存并同步所有控件。沿用当前直接恢复行为，不增加多级确认流程。
- 点击关闭、Alt+F4 或窗口级 Escape：关闭下拉弹层后隐藏设置窗口，音乐栏、托盘、媒体和频谱继续运行。Escape 先交给当前控件，避免同时关闭弹层和整个窗口。
- 连续从托盘或音乐栏打开设置：只激活同一个窗口，已经最小化时还原，使用 `show()` / `raise()` / `activateWindow()`；不创建多个实例。
- Tab / Shift+Tab 可以遍历导航、开关、下拉框和操作按钮；导航上下键切页，开关支持空格，下拉框支持键盘选择。保留清楚的焦点标记，并设置可理解的 accessibleName。

## 3. 控件和真实 API 清单

以下已核对本机 **v1.10.0 安装后的公开头文件**。本报告不使用其他 Fluent 库的类名。头文件统一加 `FluentQt/` 前缀；不要包含上游 `app/` 私有实现。

| 用途 | 类型 | 已核对入口／头文件后缀 |
| --- | --- | --- |
| 顶层窗口 | `fluent::windowing::Window` | `setContentWidget()`、`setBackdropEffect()`；`components/windowing/Window.h` |
| 左右布局壳 | `fluent::navigation::NavigationView` | `setDisplayMode(DisplayMode::Left)`、`setExpandedPaneWidth(200)`、`setMainChromeWidget()`、`setFooterChromeWidget()`、`contentHost()`；`components/navigation/NavigationView.h` |
| 三个导航项 | `fluent::collections::ListView` | `setModel()`、`setSelectedIndex()`；`components/collections/ListView.h` |
| 页面承载 | `fluent::navigation::StackContentHost` | `insertPage()`、`setCurrentIndex()`、`setTransitionAnimationEnabled()`；`components/navigation/StackContentHost.h` |
| 页面滚动 | `fluent::scrolling::ScrollView` | `setContentWidget()`，沿用 QScrollArea 的内容自适应能力；`components/scrolling/ScrollView.h` |
| 设置卡片 | `fluent::layout::Card` | `setAppearance(Card::Layer)`，卡片直接安装普通 Qt layout；`components/layout/Card.h` |
| 设置开关 | `fluent::basicinput::ToggleSwitch` | `isOn()`、`setIsOn()`、`toggled(bool)`、`setOnContent()`、`setOffContent()`；`components/basicinput/ToggleSwitch.h` |
| 设置下拉框 | `fluent::basicinput::ComboBox` | 继承 QComboBox 的 `addItem()`、`currentData()`、`findData()`、`currentIndexChanged(int)`；`components/basicinput/ComboBox.h` |
| 操作按钮 | `fluent::basicinput::Button` | `clicked`，使用默认 Standard 外观；`components/basicinput/Button.h` |
| 标题和说明 | `fluent::textfields::Label` | `setFluentTypography()`、`setTextColorRole()`，继承 QLabel 的换行；`components/textfields/Label.h` |
| 资源初始化 | `fluent::prepareHighDpiApplication()` / `fluent::initializeResources()` | `FluentQt.h` |
| 主题同步 | `fluent::FluentElement::setTheme()` | `Light` / `Dark` / `HighContrast`；`components/foundation/FluentElement.h` |

`NavigationView` 负责窗格几何和内容承载，**不会替应用创建导航项或页面**。使用一个由导航控件持有 QObject parent 的 `QStandardItemModel` 放三项，单选并关闭编辑、拖动和重排；监听 selection model 的 `currentChanged` 切换页面，鼠标和键盘共用同一路径。仅连接 `itemClicked` 会漏掉键盘导航。关闭 StackContentHost 的页面过渡动画，保留库内开关和焦点的正常短动画。

`Card` 是普通容器，没有现成的 MusicBar 设置业务或 `SettingCard` 字段绑定。可以增加一个很小的 `SettingsRow` 组合帮助函数／类，负责 Label、布局和右侧控件；不能重新绘制开关、下拉框、按钮、命中测试或焦点效果。

使用有所有权参数的 hosting API 时，明确传 `fluent::WidgetOwnership::Owned`；模型设置 parent，页面和控件按 QObject 父子关系管理。已经交给 Qt 容器管理的子控件，不再用 `unique_ptr` 同时持有。

## 4. 现状和修改边界

接手前阅读根 [AGENTS.md](../AGENTS.md)、[架构](ARCHITECTURE.md)、[设置协议](SETTINGS_GUI.md) 和 [路线图](ROADMAP.md)。工作区已有 GPU、歌词优化和其他 AI 的部分 XAML 改动，应先保存清单、检查 diff，保留不相关工作，不执行整个工作区 reset 或覆盖。

| 现有位置 | 本轮处理 |
| --- | --- |
| `src/main.cpp` | 接入唯一 QApplication，处理入口、COM、初始化和销毁顺序 |
| `src/core/App.cpp` / `.hpp` | 将主事件循环接到 Qt，保留 Win32 WndProc、设置应用和托盘逻辑，明确退出路径 |
| `src/ui/SettingsWindow.cpp` / `.hpp` | 替换为 Qt 设置门面，移除旧 D2D 设置绘制、手写命中测试和 XAML 选择逻辑 |
| 建议新增 `src/ui/qt/SettingsQtWindow.*` | Window、导航、页面和四项绑定；可按需要拆出 SettingsRow，避免每项一个过度抽象模块 |
| `src/ui/XamlSettingsWindow.*`、`XamlThreadRuntime.*` | Qt 全部功能通过后，从构建和最终源码中移除被替代的设置后端 |
| `src/core/SettingsStore.*` | 保留版本 1 的纯 C++ 配置协议和原子保存，不改成 QSettings |
| `CMakeLists.txt` | Qt／FluentQt 查找、静态插件、MOC 和主目标接入，保留原生系统库和 CTest |
| `assets/app.manifest` | 保留 PerMonitorV2、权限和系统兼容声明；清理仅为 XAML 增加的注释／声明时说明理由，不扩大权限 |
| 构建脚本、CI、第三方文档 | 添加可复现的 Qt 构建路径、许可和验证说明 |

Qt 类型尽量留在 `ui/qt/` 和入口处。`SettingsWindow.hpp` 可以通过 PIMPL 保持轻量；保留 `Show(HINSTANCE, const AppSettings&, std::function<void(const AppSettings&)>)`、`SetSettings()`、`IsOpen()`、`Shutdown()` 这组调用边界。`PreTranslateMessage()` 随旧 XAML 循环退出而移除。

## 5. 主线程、消息循环和生命周期

### 5.1 选定的接入方式

**原 UI 主线程上创建唯一 QApplication，由 `QApplication::exec()` 驱动事件循环。** 任务栏原 HWND 和其 WndProc 仍由同一线程拥有，D2D 继续在该线程绘制。Qt 要求 GUI 应用只有一个 QApplication，并在 UI 对象创建前初始化，见 [QApplication](https://doc.qt.io/qt-6.11/qapplication.html)。

采用标准 `main(int argc, char** argv)` 配合 Qt 的 WIN32 executable 构建方式。将现有 `wWinMain` 中必要的桌面和 COM 初始化迁移到新入口，通过 `GetModuleHandleW(nullptr)` 得到 HINSTANCE。只保留一个用户入口，避免手写 WinMain 与 Qt EntryPoint 同时定义；不要临时拼出无效或提前释放的 argv。

初始化顺序：

1. 保留现有必要的 Windows 桌面选择逻辑，任何 Qt／原生窗口创建之前完成。
2. 主线程 COM STA 初始化，检查 HRESULT；由原有主体配对反初始化，Qt 自己的 OLE 初始化由 Qt 自己配对。
3. 调用 `fluent::prepareHighDpiApplication()`，保留 manifest 的 PerMonitorV2，不重复改变已创建窗口的 DPI awareness。
4. 创建 QApplication；立即 `setQuitOnLastWindowClosed(false)`，设置应用名和图标。
5. 调用 `fluent::initializeResources()` 并检查结果，设置正文 Typography；此时可以载入字体和图标资源。
6. 创建并初始化 MusicBar App、原生任务栏窗口、媒体与音频模块。
7. `App::Run()` 进入 Qt exec，不再并行运行旧 GetMessage 循环。

关闭最后一个 Qt 窗口默认可能退出应用，因此显式关闭此行为是必需项，见 [quitOnLastWindowClosed](https://doc.qt.io/qt-6.11/qguiapplication.html#quitOnLastWindowClosed-prop)。

不能通过 16 ms 定时调用 `processEvents()` 拼接两个消息循环，不能给设置另开 GUI 线程，也不能创建第二个 QApplication。

### 5.2 Win32 消息接入与退出

- 现有 `WM_APP + 102/103/104`、`WM_TIMER`、托盘、任务栏和鼠标消息继续发往原 HWND，由原 WndProc 消费；不要把后台线程改为直接操作 QWidget 或 D2D。
- 先验证 Qt 事件分发能正常派发这些 HWND 消息。必要的全局／线程消息才加 `QAbstractNativeEventFilter`，它返回 false 表示继续正常处理；不得二次 DispatchMessage 或吞掉全部消息。接口见 [Qt 原生事件过滤器](https://doc.qt.io/qt-6.11/qabstractnativeeventfilter.html)。
- 现有 `App::Run()` 中 XAML 的 PreTranslate 和 teardown pump 全部退出 Qt 运行路径。
- 托盘“退出”保留原有媒体、音频、时钟、托盘和任务栏清理，明确调用 Qt quit／exit，替换仅靠 `PostQuitMessage()` 的退出约定。
- 正常关闭设置不触发 App 的 WM_DESTROY、停止音频或 Qt quit。原生音量弹窗销毁同样不应退出。
- 系统注销／关闭和初始化失败也应收敛到可重复调用的清理路径；移除旧 XAML teardown 后清查所有遗留注释和退出调用。

销毁顺序：停止产生 UI 消息的后台活动、释放设置及 App 的原生资源，再销毁 QApplication，最后配对主线程 COM。使用局部作用域或 RAII 让所有 QWidget 先于 QApplication 销毁；不要在 COM 已释放后销毁 WinRT 对象。

### 5.3 设置窗口所有权

选定 **首次打开时创建，关闭隐藏，后续复用**：门面用 `std::unique_ptr` 持有一个无父 QWidget 的顶层窗口，子控件由 Qt parent 管理，显式 `setAttribute(Qt::WA_DeleteOnClose, false)`。退出时 `Shutdown()` 在 UI 线程释放顶层窗口，并且可重复调用。

`IsOpen()` 表示窗口当前可见，而不是指针非空。窗口隐藏时仍可同步四项数据；再次打开先读取 `App::CurrentSettings()`，不使用过期 UI 副本。隐藏时关闭弹层和停止应用新增的动画／定时任务，不持续刷新设置页。

连接 lambda 时提供有生命周期的 QObject receiver；不要让延迟回调捕获已销毁的 App、门面或控件。整个进程只维护一个设置回调和一个窗口。

该方式减少重复创建带来的复杂性，但 QApplication 和字体等缓存会常驻；关闭设置不代表 Qt 已卸载或内存完全回到首次打开前。这部分需要测量，不能用“仅在打开设置时才有 Qt 成本”描述实现。

## 6. 设置同步、主题和 DPI

### 6.1 单一业务入口

`App::ApplySettings()` 是应用设置的统一入口，负责歌词请求、音频模式切换、拖动锁定、设置窗口同步和音乐栏重绘；`App::PersistSettings()` 负责保存。Qt 控件只构造 AppSettings 并调用门面回调，不直接触碰 MediaSession、WasapiLoopback 或 LyricsService。

初始化控件、托盘修改、外部配置重载、恢复默认和打开窗口后的同步，必须用 `QSignalBlocker` 或明确的同步保护阻止重复提交。尤其注意 App::ApplySettings 会回调 SettingsWindow::SetSettings，避免形成 UI → App → UI → App 循环。

加载过程中不保存，只有用户真正改变值或现有重新读取／恢复默认动作才走对应回调。同步所有控件后再撤销 blocker，不出现半更新状态。非法下拉 item data 不提交。

配置仍为 `%LOCALAPPDATA%\MusicBar\settings.json`，UTF-8、`version: 1`；现有默认值、严格校验、原子保存和每秒文件时间戳检查保持原义。托盘和设置窗口显示一致，切换歌词来源仅发起该次有效变更所需的请求，切换采集模式不重复重启音频。

保存失败保留当前已应用的内存设置，沿用 App 现有失败警告并不伪报保存成功；无效文件不覆盖当前设置或原文件。窗口中的“更改会立即应用并保存”是说明文字，不能当成成功状态记录。

### 6.2 外观处理

- 跟随 Windows 应用亮暗主题：启动和系统主题变化时读取 `AppsUseLightTheme`，映射到 FluentElement Light／Dark，避免每帧查询注册表。已有 WM_SETTINGCHANGE／WM_THEMECHANGED 可用于同步。
- 高对比模式优先，检测系统状态并使用库的 HighContrast；恢复后回到系统亮暗主题。不添加新的主题持久化字段。
- 使用默认 Fluent token、Card 外观和控件状态；不覆写整套 QSS 或硬编码亮／暗两套背景、边框和文字色。
- 原生窗口创建并完成能力探测后，依据 `backdropCapabilities()` 请求 `BackdropEffect::Mica`，查看 `backdropState()` 确认实际后端；原生 Mica 不支持或申请失败时选 `Solid`。不另做软件实时模糊，不把 PaintedMaterial 回退称为系统原生 Mica。
- 保留库内字体／图标资源初始化。正文按 Typography 角色设置；汉字优先本机 `Microsoft YaHei UI`，通过 Qt 的 Han 字体回退配置处理缺失字符，不给整个界面统一覆盖一个固定字号。
- 卡片是非交互容器，真正开关、下拉框和按钮承担焦点与输入。普通文字导航即可，本轮不要求自制图标或复制 Gallery 的装饰。

DPI 由 Qt 处理设置窗口，MusicBar 原生任务栏仍按现有物理像素逻辑处理。不把 Qt 逻辑坐标直接作为 taskbar HWND 的像素矩形，不对 QWidget 手动重复乘 DPI。验证 100%、125%、150%、200% 和跨显示器移动；设置窗口不能因此改变任务栏裁剪或出现黑底。

## 7. SDK、构建与单 EXE

### 7.1 固定依赖

| 项目 | 固定值／路径 |
| --- | --- |
| 通用环境根目录 | `C:\Users\huban\Storage\tool\Qt-FluentQt` |
| Qt | Base / SVG 6.11.2，当前精简静态 Release；本轮主目标仅显式请求 Widgets |
| Fluent-Qt | v1.10.0，commit `4076a1612e5785757cfe0759436dabe3414e490e` |
| FluentQt SDK | 环境内 `sdk\FluentQt` |
| Qt prefix | 环境内 `installed\x64-windows-static-release` |
| CMake toolchain | 环境内 `toolchains\msvc-x64-static-release.cmake` |
| ABI | x64 / Release / MSVC / 静态 CRT `/MT`，不混用 `/MD` 或 Debug 库 |
| 当前实测工具链 | Visual Studio 2026 MSVC 14.51.36231，Windows SDK 26100／28000 |

Qt 6.11 官方支持列表包含 Windows 10 1809+ x64 和 Windows 11，列出的 MSVC 为 2022；本地 2026 工具链已构建环境示例，但完整产品还需在目标系统验证。见 [Qt 6.11 Windows 支持范围](https://doc.qt.io/qt-6.11/windows.html)。

复用现有 SDK，除非出现确切缺失模块，不重新安装整套 Qt。新消费者构建必须使用上述 toolchain，只有 CMAKE_PREFIX_PATH 不足以正确查找静态 ZLIB 等传递依赖。不要将本机绝对路径写死在 CMakeLists.txt；通过参数、本地未提交配置或构建脚本的环境根目录变量传入。

### 7.2 CMake 接入示意

保留原 SOURCES、Windows 链接库、C++20、警告和现有 CTest；从 SOURCES 去掉替代后的 XAML 文件并加入 Qt 设置实现。主目标示意：

```cmake
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Widgets)
find_package(FluentQt CONFIG REQUIRED)

qt_add_executable(MusicBar WIN32 ${SOURCES})
set_target_properties(MusicBar PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(MusicBar PRIVATE Qt6::Widgets FluentQt::FluentQt)
qt_import_plugins(MusicBar INCLUDE Qt6::QWindowsIntegrationPlugin)
# 原有 Windows 系统库、资源依赖和测试定义继续保留。
```

Qt 的 CMake 集成负责目标 finalization 和静态插件注册；本项目最低 CMake 3.28 可使用自动延迟 finalization，不同时手动生成重复的插件注册。见 [qt_add_executable](https://doc.qt.io/qt-6.11/qt-add-executable.html)、[qt_import_plugins](https://doc.qt.io/qt-6.11/qt-import-plugins.html)。

先使用默认合理插件集合加显式 Windows 平台插件，检查实际链接产物。需要额外图像插件时按确切用途加入；不要盲目 `NO_DEFAULT` 导致平台、字体或图像缺失。不要仅凭“Qt 库是 static”就认定单文件部署完成。

使用 `qt_add_resources()` 将应用 Qt 图标等资源加入 EXE。Fluent-Qt 已编入库的资源仍需 `initializeResources()` 激活；不得要求用户把资源目录、字体、qwindows.dll、Qt DLL 或开发环境一起复制到运行目录。

### 7.3 本机配置命令

在 **VS 2026 x64 Native Tools PowerShell／命令环境**中执行；确保 `cl`、CMake 和 Ninja 可用，且编译器为 MSVC。使用独立 `build-qt-release`，不要重新配置用户正在使用的旧 `build/`。

```powershell
$qtFluentRoot = Join-Path $env:USERPROFILE 'Storage\tool\Qt-FluentQt'
$qtPrefix = Join-Path $qtFluentRoot 'installed\x64-windows-static-release'
$fluentPrefix = Join-Path $qtFluentRoot 'sdk\FluentQt'
$qtToolchain = Join-Path $qtFluentRoot 'toolchains\msvc-x64-static-release.cmake'

cmake -S . -B build-qt-release -G Ninja `
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
  "-DCMAKE_TOOLCHAIN_FILE=$qtToolchain" `
  "-DCMAKE_PREFIX_PATH=$fluentPrefix;$qtPrefix"
cmake --build build-qt-release --parallel 6
ctest --test-dir build-qt-release --output-on-failure
```

可增加 `scripts/build-qt-release.cmd` 自动初始化本机工具，默认使用上述通用环境，允许环境变量覆盖；它只构建、不启动、不替换正在运行的 EXE。已有 `build-vs` 如已缓存非 Qt 配置，应使用新的生成目录，不能把配置成功误认为旧生成目录已完成迁移。

当前依赖只提供 Release。若未来需要 Debug，需另行构建匹配的 Qt 和 FluentQt SDK，不能通过修改主程序的 runtime 开关混用现有库。

## 8. 实施顺序和阶段退出条件

| 阶段 | 工作 | 完成证据 |
| --- | --- | --- |
| A：基线和最小接入 | 记录工作区差异与当前 EXE；使用安装 SDK 创建最小 settings Window，接入唯一 Qt 主循环和显式退出 | 主目标编译成功；任务栏正常；关闭设置程序仍运行；托盘退出正常 |
| B：界面迁移 | 按本报告完成三页、两开关、两下拉框和两按钮，加入资源、字体与主题 | 所有页面可操作，明暗截图与键盘验证，窗口重复打开只有一个 |
| C：业务联动 | 复用 AppSettings／SettingsStore，阻断同步信号回路，验证托盘和外部文件变化 | 字段映射正确、每次变更单次提交、非法文件保留当前值、保存失败有提示 |
| D：清理和回归 | 删除旧设置后端，清理入口和 manifest 的旧 XAML 内容，运行构建／CTest | 最终只有 Qt 设置路径，新警告已处理，原有五类测试通过 |
| E：部署与资源 | 单文件隔离运行、目标系统检查、播放／暂停／设置开关前后采样 | EXE 字节数、依赖列表、运行与资源记录、已测／未测平台明示 |
| F：交接 | 更新 README、ARCHITECTURE、SETTINGS_GUI、ROADMAP、THIRD_PARTY | 提供变更清单、证据路径和剩余限制，由验收者核查 |

每阶段完成后再进入下一阶段；不要仅交付独立 Gallery 或截图就宣称迁移完成。最小接入失败时优先修复入口、插件、资源和事件循环，不切回另一套未授权框架。

## 9. 验收要求

### 9.1 功能与回归

1. 托盘和音乐栏菜单打开同一窗口；连续点击不会多开。关闭、最小化、还原、再次打开、最后退出行为正确。
2. 四项设置的默认值、六个歌词来源和两种采集模式准确；恢复默认一次提交四项；重新读取成功／失败／文件不存在均符合协议。
3. 设置修改立即作用于音乐栏和配置文件；托盘操作、外部合法文件更新在现有检查周期内同步 Qt 控件，不发生递归提交。
4. 关闭后重新打开和重启程序保持配置；非法 JSON／版本／类型／枚举不破坏当前值；写入失败有真实提示。
5. 任务栏圆角透明、悬停按钮、右键、拖动锁定、音量弹窗正常；框外 Start、任务栏图标和托盘可点击。
6. Qt 窗口打开／隐藏时歌词继续刷新、频谱和播放控制可用；长间奏保留歌词的修复不退化。其他“部分歌曲不滚”问题不并入本轮。
7. 重复打开／关闭至少 50 次，切页和修改设置后退出，没有悬空回调、异常退出、持续新增窗口或显著持续增长的 handle／线程数。字体等首次缓存增长单独记账。
8. 明暗、高对比和各 DPI 下字体、边界、下拉弹层、焦点和中文显示正常；目标 Windows 10、Windows 11 的不同背景后端如实记录。

现有 CTest 包括 `LyricScrollTests`、`FftAnalyzerTests`、`LyricsParserTests`、`SettingsStoreTests`、`Direct2DRendererTests`，应全部通过。按实际绑定设计补充有意义的设置同步验证，重点检查外部 SetSettings 不触发 onChanged、枚举 item data 双向映射和用户修改只提交一次；不为像素位置或 trivial getter 堆测试。自动化可以通过现有工具或专用小测试完成，验收证据必须区分自动测试和人工观察。

### 9.2 单文件部署

- 对 Release EXE 记录字节数、SHA256 和 `dumpbin /DEPENDENTS`。不得依赖 Qt DLL、FluentQt DLL、VC++ 可再分发运行库 DLL；Windows 系统依赖可以保留。
- 复制到新目录，目录中仅放 EXE，清除 Qt 环境变量并使用不含 SDK 的 PATH 启动，验证实际设置和主程序功能，而非仅使用 offscreen 平台截图。
- 验证明暗窗口、开关、下拉框、中文、图标和托盘退出，确认资源确实编入程序。
- 干净目标 Windows 虚拟机上的启动是独立验证项；本机隔离目录不能代替干净系统和 Windows 10 验证。未测试的版本必须写“未测试”。

### 9.3 资源占用与测试协调

用户正在使用 MusicBar。执行者可以构建和静态检查；**任何关闭、替换、重启当前进程或需要歌曲播放的采样，都先通知用户安排测试时机**，不自行停止播放器或当前 MusicBar。

使用同一歌曲、播放区间、采集模式、DPI 和 FPS，对基线与新版本记录以下状态，每个稳定状态采样约 30 秒，注明进程、EXE、配置、系统、CPU 核心数和歌曲状态：

| 状态 | 要记录的问题 |
| --- | --- |
| 冷启动，未打开设置，暂停／播放 | QApplication 初始化的常驻成本，任务栏刷新是否正常 |
| 设置打开，停止交互，暂停／播放 | 设置是否引入持续重绘／额外定时器，CPU 与内存增量 |
| 设置关闭后，暂停／播放 | 隐藏窗口是否停止活动，缓存成本与基线差别 |
| 重复开关 50 次后的稳定状态 | 内存、句柄、线程是否持续增长 |

至少记录 Private Bytes、Working Set、进程 CPU 时间增量、handle 和线程数。CPU 单核等效百分比 = CPU 时间增量／墙钟时间 × 100%；按整机总算力计算时再除以逻辑处理器数，两种口径注明，不能混用。不要把暂停样本当作播放样本。

现有 AGENTS 中 <15 MB、<0.1%、暂停 0.0% 仍为历史目标，尚无达标证据；加入 Qt 后不得直接声称达标或资源更低。用户明确要求低占用，应消除持续设置重绘、重复实例和泄漏，并报告真实增量。明显回归应说明原因并处理，仍不能达到的指标列为剩余问题。

## 10. 分发资料和执行交接

单个 EXE 指运行程序本体；发布包仍可包含说明与许可文本。补充实际链接的 Qt、Fluent-Qt、字体／图标和传递依赖记录，不把 Fluent-Qt 的 MIT 理解为 Qt 及全部资源的统一许可证。

使用开源 Qt 静态分发时，在发布资料中落实所选许可证要求，包括对应源码和用户修改／重链接所需的材料；具体材料按实际选择的许可确认，不能只放一份 MIT 文本。技术实现可继续，不需要在本轮引入新运行文件。依据见 [Qt 官方 LGPL 义务说明](https://www.qt.io/development/open-source-lgpl-obligations)。本项目自有代码目前没有指定开源许可证，执行模型不要自行改许可证。

建议执行者新增 `docs/FLUENT_QT_SETTINGS_ACCEPTANCE.md`，提供版本、修改文件、构建／测试日志、三页明暗截图、部署证据、资源采样表、已测平台和剩余问题。更新路线图时，只有确实完成并验证的项才能打勾。

可直接交给执行模型的任务文本：

> 请在 MusicBar 仓库按 `docs/FLUENT_QT_SETTINGS_IMPLEMENTATION.md` 重写现有设置窗口。技术栈固定为本机已准备的 Qt Widgets 6.11.2 和 Fluent-Qt v1.10.0 静态 SDK，使用报告已经选好的常规控件和三页布局，不再要求用户选择。保留同进程单 EXE、100 MB 上限、现有四项 JSON v1 设置以及任务栏 Win32 / D2D / DComp 路径。先完成 Qt 主循环、生命周期和单窗口接入，再完成设置绑定、主题／DPI、旧 XAML 清理和部署／资源验收。保留工作区其他已有改动。构建使用独立目录；重启正在运行的程序或采样播放之前通知用户。交付可运行 Release EXE、测试记录和验收文档，不以 Gallery 成功代替 MusicBar 完成。
