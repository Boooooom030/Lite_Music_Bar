# MusicBar 原生 Fluent 设置窗口实施报告

> **2026-10-12 选型更新：本文已停止作为实施依据。** 用户发现系统 UWP 控件的外观不符合预期，现已授权使用 Qt Widgets / Fluent-Qt，并将 Release EXE 体积上限放宽至 100 MB。新的 [设置重写实施报告](FLUENT_QT_SETTINGS_IMPLEMENTATION.md) 已直接选定常规控件、布局和接入方案；请以新报告为准。本文保留为旧 XAML 路线的技术记录，不应继续按下文的“已选定”要求迁移。

日期：2026-10-12

状态：方案已选定；本文是实施与验收交接，尚未迁移设置窗口。
目标读者：接手开发的 AI／贡献者，以及后续验收者。

## 1. 已确定的目标

将设置窗口从 Direct2D 自绘的 Fluent 风格界面，迁移为 Windows 自带的真实 UWP XAML 控件，通过 **UWP XAML Islands + C++/WinRT** 嵌入现有原生进程。

用户已经选择这条路线。执行者无需再次确认技术选型，应先完成最小接入验证，再逐步迁移和交付证据。

成功后的行为：

- 仍由音乐栏和托盘菜单打开同一份设置窗口。
- 设置窗口、任务栏栏、媒体、音频和歌词仍运行在 `MusicBar.exe` 内。
- 使用系统的导航、开关、下拉框、按钮、布局、焦点与无障碍能力。
- 继续即时应用并保存现有设置，配置版本和字段保持不变。
- Windows 10 1903+ 使用 XAML；Windows 10 1809 和 XAML 不可用的环境使用现有原生回退窗口。
- 分发仍只需要程序本体这一个 EXE，另附现有许可文本；不安装额外界面运行时。

这里的“真实 Fluent”指系统提供的 `Windows.UI.Xaml` 控件，**不是最新版 WinUI 3**，也不承诺与 Windows 11 系统设置界面逐像素相同。微软支持在 Win32 C++ 窗口内托管系统 UWP 控件，正式支持起点为 Windows 10 1903。[系统控件接入](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/host-standard-control-with-xaml-islands-cpp)、[支持范围](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/xaml-islands)

## 2. 项目边界与禁止扩展的范围

以根目录 `AGENTS.md` 为准，并落实以下边界：

| 项目 | 本次要求 |
| --- | --- |
| 语言与构建 | C++20、现有 CMake、MSVC Release 静态运行库 `/MT` |
| 分发 | 不新增设置 EXE、运行时 DLL、外置 XAML／PRI／WinMD 等必需文件 |
| UI 运行时 | 使用 Windows 系统 XAML；不引入 WinUI 2/3、Windows App SDK、WPF、.NET、WebView2、Electron |
| 应用类型 | 继续是普通未打包 Win32 程序，不改为 UWP 或依赖 MSIX 安装身份 |
| 设置数据 | 继续版本 1 JSON、现有路径、默认值、验证与原子保存 |
| 任务栏 | 保留 GPU 渲染、全任务栏宿主、区域裁剪和点击穿透 |
| 媒体／音频／歌词 | 保留当前实现；本次只接入设置窗口及必要的消息、生命周期支持 |
| 资源占用 | XAML 按需初始化；提供测量，不把系统控件等同于更少内存 |
| 原有资源目标 | EXE < 2.5 MB 等仍是待测目标；不能修改文档数字来宣称达标 |

系统 DLL 随 Windows 提供，属于允许依赖。开发时的 SDK 头文件、代码生成工具不等于新增运行时依赖，但必须检查最终产物。如果接入确实需要额外组件，应在最小验证阶段记录事实，不能默默换成另一套框架。

“单 EXE”验收不能只看输出文件数量：不得通过首启下载／解包私有 UI 运行时，或要求目标机器预装 Windows App SDK，来绕过以上要求。

## 3. 实施基线：必须保留当前工作树

本报告检查时，Git HEAD 为 `ea4cc7a74f1674068913f7d13c1e6d86c4c1ff56`，**工作树已有未提交改动**。当前工作树才是实施基线，单独检出这个 HEAD 会丢失后续成果。

已有成果包括：

- `Direct2DRenderer` 改用 D3D11／Direct2D DeviceContext／DirectComposition。
- 任务栏宿主使用 `WS_EX_NOREDIRECTIONBITMAP`；黑底、悬停和右键已由用户确认恢复。
- 交换链缩到音乐栏尺寸，普通帧直接绘入 back buffer，悬停才使用中间纹理。
- `LyricsTimeline.hpp` 在 UI 文档快照上计算时间轴，避免每帧复制整首歌词。
- 间奏空行保留上一句歌词，新增时间轴回归检查。
- 当前 CTest 为五项，包含新增 `Direct2DRendererTests`。

开始前执行 `git status --short`、`git diff --stat` 并保存基线证据。保留 `REPORT.md`、`dcomp-attempt.diff`、截图、未跟踪的歌词时间轴头文件和 GPU 测试。禁止用 `git reset --hard`、整目录覆盖或恢复旧渲染器来开始本次工作。提交时分清先前改动和本次设置迁移，不伪称全部由本次实现。

当前已测 Release EXE 为 **1,031,168 字节**；此前 Release 构建及 5/5 CTest 通过。这些只作为迁移前证据，不代表 XAML 方案已经验证。

### 3.1 现有接入点

| 文件／接口 | 当前行为 | 迁移要求 |
| --- | --- | --- |
| `src/ui/SettingsWindow.hpp/.cpp` | 自绘窗口、导航、开关、下拉框、读取／重置 | 保留对外入口，内部增加系统 XAML 后端与旧版回退 |
| `App::OpenSettings()` | 调用 `Show`，回调执行 `ApplySettings` 和 `PersistSettings` | 延续该数据流 |
| `App::ApplySettings()` | 更新歌词、位置锁定、音频采集，再调用 `SetSettings` | 程序性刷新不得再次触发保存回调 |
| `App::ReloadSettingsIfChanged()` | 任务栏 watchdog 每秒检查配置文件变化 | 设置窗口打开时须同步刷新控件 |
| `App::Run()` | `GetMessage` → `TranslateMessage` → `DispatchMessage` | 增加 XAML 消息预处理及清理所需配合 |
| `src/main.cpp` | 主 UI 线程已通过 `CoInitializeEx` 初始化 STA | 保持 STA，明确 WinRT 初始化与成对清理 |
| `App::HandleMessage` 的 `WM_DESTROY` | 停表、停媒体和音频后立即 `PostQuitMessage` | 核查 XAML 异步清理，不能照旧直接截断所需消息处理 |
| `assets/app.manifest` | 已有 PerMonitorV2、普通用户权限和兼容声明 | 补充 XAML Islands 所需目标版本声明，嵌入 EXE |
| `CMakeLists.txt` | 已链接 `windowsapp`，设置窗口属于主目标 | 增加必要源码，维持 `/MT` 与单程序包装 |
| `.github/workflows/build-windows.yml` | Windows Server 2022 构建，只打包 EXE 和许可 | 保持产物边界；服务器不能代替客户端 XAML 人工验收 |

## 4. 技术路线和最小验证门槛

采用 `Windows.UI.Xaml.*`，不要混入 `Microsoft.UI.Xaml.*` 命名空间。后一套并非本方案的系统控件后端。

建议从 C++ 代码直接创建标准控件树，不先引入编译 XAML、自定义 WinRT 控件库或外置资源索引。这样可以优先验证当前 SDK、CMake、静态运行库和单 EXE 的组合。[微软标准控件示例](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/host-standard-control-with-xaml-islands-cpp)

当前机器的 SDK 已存在 `winrt/Windows.UI.Xaml.Hosting.h`。可按实际需要使用：

```cpp
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
```

实现者应核对构建机器 SDK 的投影和接口，不通过下载界面运行库弥补误用命名空间。微软示例的 NuGet／MSBuild 步骤不能机械照搬到当前 CMake 项目；需要的清单内容应显式落实。

### 4.1 第一阶段必须证明的事项

在迁移全部控件前，以最小系统 `Button` 或 `ToggleSwitch` 验证：

1. x64 Release、`/MT`、未打包 Win32 可以激活并显示标准系统控件。
2. 鼠标和键盘输入有效，而不只是 `CreateWindow` 或编译成功。
3. 关闭／再次打开多次不会使主程序退出、卡死或丢失输入。
4. 1809／能力缺失时可以进入原有窗口，主程序仍运行。
5. EXE 移至空目录，在没有 VS 和 Windows App SDK 的客户端系统上可独立打开设置。
6. 不需要程序旁边的 XAML、PRI、WinMD 或运行时 DLL。

这些是接入门槛，不是已经完成的事实。若无法证明单 EXE 或可重开，先修复接入；不要先完成大量布局后再补部署条件。

## 5. 推荐代码结构

保留 `SettingsWindow` 作为门面，使 `App` 无需了解两个后端的控件和布局。

```text
src/ui/
  SettingsWindow.hpp/.cpp       # 现有公共入口；选择后端、消息预处理和生命周期协调
  XamlSettingsWindow.hpp/.cpp   # 标准系统控件、原生顶层宿主、XAML child、事件绑定
  LegacySettingsWindow.hpp/.cpp # 从现有 SettingsWindow 提取的完整原生回退实现
  XamlThreadRuntime.hpp/.cpp    # 如确有必要：UI 线程框架初始化与退出清理
```

这是建议分工，允许合并最后一组文件，但不要让 WinRT 激活、主题、数据写入和任务栏渲染堆在 `App.cpp` 中。

继续提供：

```cpp
void Show(HINSTANCE, const AppSettings&,
          std::function<void(const AppSettings&)> onChanged);
void SetSettings(const AppSettings&);
bool IsOpen() const;
```

另增加必要的 `PreTranslateMessage` 与显式退出清理入口。`Show` 已打开窗口时只恢复并激活，不创建第二份设置窗口。

头文件可使用 PImpl 隔离大批 XAML 类型。回退实现尽量保持原行为；不要为了迁移顺便更换配置服务或扩充设置格式。

### 5.1 HWND 与控件树

```text
MusicBar.exe 主 UI 线程
  ├─ 任务栏宿主：现有 GPU 渲染与区域裁剪
  └─ 设置顶层窗口：普通 WS_OVERLAPPEDWINDOW
       └─ DesktopWindowXamlSource 创建的原生 child HWND
            └─ 系统 XAML 根布局
                 ├─ NavigationView：任务栏／歌词／音频频谱
                 ├─ 当前页面内容：TextBlock、ToggleSwitch、ComboBox
                 └─ Button：重新读取、恢复默认
```

设置顶层窗口不附着到 `Shell_TrayWnd`，不复制任务栏的裁剪区域或透明宿主样式。XAML child 由框架创建；通过托管接口取出 HWND 并调整尺寸，不另造替代 child 来冒充托管成功。

使用 `DesktopWindowXamlSource`、`IDesktopWindowXamlSourceNative::AttachToWindow` 和 `get_WindowHandle` 接入；宿主 HWND 必须属于同一线程。[托管 API](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api)

## 6. 线程、框架与窗口生命周期

### 6.1 初始化范围

保留主 UI 线程的 STA，不改成 MTA，也不让媒体 MTA 工作线程创建控件。设置控件、原生 HWND、事件撤销和 XAML 清理均在拥有它们的 UI 线程执行。

现有主线程 COM 初始化不等于已经完成新的 XAML／WinRT 初始化工作。实施者须明确 `RoInitialize`／C++/WinRT 初始化与原 COM 初始化的关系，检查 HRESULT，并分别配对需要的反初始化。`RPC_E_CHANGED_MODE` 是失败，不能视作初始化完成。[WinRT 初始化契约](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-roinitialize)

首次打开设置时才激活 XAML。投影对象成员初始化为 `{nullptr}`，不要在 `App`／`SettingsWindow` 构造阶段通过控件默认构造或全局静态对象提前加载框架。

### 6.2 分清两种关闭

| 对象 | 建议生命周期 |
| --- | --- |
| UI 线程 COM／WinRT apartment | 与线程生命周期协调 |
| `WindowsXamlManager` | 首次需要时创建；同一线程上保留到应用最终退出 |
| 设置顶层 HWND、`DesktopWindowXamlSource`、控件树 | 每次开窗创建；普通关窗时撤销事件并关闭／释放 |
| 当前设置值和业务回调 | 由门面／App 管理；重新打开时同步当前值 |

微软明确限制同一线程上关闭 `WindowsXamlManager` 后再次初始化。**普通设置关窗不能关闭线程级 manager**。否则第一次能打开、第二次出错就是未完成迁移。[线程级生命周期](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api#best-practices)

普通关窗应依次：设置关闭状态、撤销事件／焦点订阅、解除内容引用、关闭 source、释放控件及 interop 引用、完成 HWND 销毁和句柄清空。按实际接口契约落实顺序；系统创建的 child 不作为私有窗口重复销毁。

### 6.3 最终退出

`WindowsXamlManager` 的清理需要继续处理 Windows 消息。应调整现有 `WM_DESTROY` → 立即 `PostQuitMessage` 的退出流程，明确何时关闭 source、manager、何时完成必要的消息排空，以及最后何时反初始化 apartment。

不要仅把 manager 放进 `App` 析构函数，就认定清理已经正确：该析构目前发生在主消息循环结束后。也不要用固定 Sleep 或强制退出代替清理。参考微软样例实现并在交付说明中解释实际顺序。[生命周期说明](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api#best-practices)、[官方样例仓库](https://github.com/microsoft/Xaml-Islands-Samples)

退出期间屏蔽设置事件和新开窗请求；保证同一资源只关闭一次。异常路径也要可退出，不能为了清理引入无限等待。

### 6.4 初始化失败与回退

区分“尚未尝试”“框架可用”“本次进程禁用 XAML”“正在退出”。能力检测失败时不要构造控件；部分初始化失败时撤销已经注册的事件、关闭 source，保留或清理线程级对象须遵守生命周期规则。

回退窗口要可用且设置值正确。记录失败阶段和 HRESULT 供本地诊断，不弹出反复的依赖安装提示，也不在每个动画 tick 中重试激活。WinRT 异常不得穿过 Win32 WndProc 或业务回调边界。

## 7. 消息循环与键盘焦点

### 7.1 消息预处理

主消息循环在 `TranslateMessage`／`DispatchMessage` 之前调用 `IDesktopWindowXamlSourceNative2::PreTranslateMessage`。只有接口调用成功且返回“已处理”时，才停止后续转发；接口错误不能导致消息被无条件吞掉。[接口契约](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.xaml.hosting.desktopwindowxamlsource/nf-windows-ui-xaml-hosting-desktopwindowxamlsource-idesktopwindowxamlsourcenative2-pretranslatemessage)

示意逻辑，不是完整清理实现：

```cpp
MSG msg{};
for (;;) {
    const BOOL status = GetMessageW(&msg, nullptr, 0, 0);
    if (status == 0) break;
    if (status == -1) return -1; // 实际实现先完成必要的退出清理。
    if (m_settingsWindow.PreTranslateMessage(msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
}
```

按微软要求向活动 source 提供消息；不要只依据 `MSG.hwnd == 设置顶层 HWND` 过滤，因为实际输入涉及 child 和弹出层。source 不存在或已关闭时，门面应安全返回未处理。任务栏 `WM_APP`、动画 tick、媒体和歌词通知不能因此被丢弃。

### 7.2 焦点进入和离开

处理 `NavigateFocus`、`TakeFocusRequested` 等焦点边界，验证正反向 Tab 导航。纯 XAML 内容和原生宿主的焦点交接仍是宿主职责。[键盘与焦点接入](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/advanced-scenarios-xaml-islands-cpp)

验收包括：导航项、开关、下拉框、读取和重置均可仅用键盘操作；Space 操作开关；下拉框方向键、Enter、Esc 正常；Esc 优先关闭展开的下拉／弹层；Alt+F4 只关设置；重新打开时焦点可用。不得继续用旧自绘坐标命中模拟系统控件交互。

## 8. 清单、系统版本与真正的单 EXE

### 8.1 清单

当前 `assets/app.manifest` 尚无 `maxversiontested`。未打包的 XAML Islands 应显式补充对应声明，保留 `supportedOS`、PerMonitorV2、`asInvoker` 和现有 Common Controls 依赖。

微软给出的声明位置与大小写如下；具体 Id 应与实际目标和测试记录对应，不把 SDK 版本冒充已测试 OS：

```xml
<compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
  <application>
    <maxversiontested Id="10.0.18362.1"/>
    <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
  </application>
</compatibility>
```

`maxversiontested` 不是最低 OS 版本；不能据此删除 1809 回退。清单要嵌入 EXE，而不依赖旁边的 `.manifest`。[清单规范](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests#maxversiontested)

### 8.2 能力检测

先检测所需系统类型／接口和支持范围，再初始化。类型存在不保证所有激活步骤成功，运行时激活、Attach 和取 HWND 仍须检查。不能只看“Windows 11”字符串，也不能在缺少 XAML 的 Windows Server 上强行创建。

1809 使用现有回退。现代客户端上不能静默回退后把它记为 XAML 成功；验证记录必须明确最终使用的后端及失败原因。仅供测试的强制回退入口可以采用非持久化开发开关或依赖注入，不添加用户设置字段。

### 8.3 部署证据

使用 VS 开发者环境的 `dumpbin /dependents`、`dumpbin /imports` 等检查静态和延迟导入；提取嵌入清单并核对。由于 WinRT 激活可能动态加载组件，导入表不是完整证据，还要记录打开设置前后的模块及路径。

最有说服力的验证是：把最终 EXE 和许可文本放入空目录，在无 VS、无额外 Windows App SDK 安装的客户端 VM／机器上启动，打开、关闭、重开设置并修改选项。确认没有首启下载、解包或依赖开发机文件的行为。

普通配置文件写入仍允许；禁止新增必须随 EXE 携带的界面资源文件。CI 打包清单要维持当前边界，测试程序可以独立构建，但不进入分发包。

## 9. 控件与设置行为迁移

| 位置／功能 | 系统控件 | 字段或动作 | 必须保留的行为 |
| --- | --- | --- | --- |
| 左侧三类导航 | `NavigationView`／`NavigationViewItem` | 任务栏、歌词、音频频谱 | 选择切页、当前项可见；关闭多余的内置设置入口 |
| 锁定音乐栏位置 | `ToggleSwitch` | `positionLocked` | 立即生效；正在拖动时锁定能结束拖动 |
| 显示同步歌词 | `ToggleSwitch` | `showLyrics` | 立即生效，值可通过托盘和文件同步 |
| 歌词来源 | `ComboBox`／`ComboBoxItem` | `lyricsSource` | 六个现有来源，来源变化触发既有歌词请求 |
| 频谱音频来源 | `ComboBox` | `fftCaptureMode` | 当前播放器／系统声音，使用既有音频重连行为 |
| 重新读取 | `Button` | 读取当前配置 | 无文件时使用默认；无效文件显示错误并保留现有有效值 |
| 恢复默认 | `Button` | 默认 `AppSettings{}` | 立即应用并保存，与当前行为一致 |
| 说明／错误文字 | `TextBlock` 等系统元素 | 文案和状态 | 中文可读，错误不能被布局裁掉 |

不要误用 WinUI 2／3 才有的 `InfoBar`、社区 `SettingsCard`／`SettingsExpander` 等控件。设置行可用系统 `Grid`、`Border`、`StackPanel` 组合，交互控件保留系统默认模板。

### 9.1 设置协议

路径继续为 `%LOCALAPPDATA%\MusicBar\settings.json`：

```json
{
  "version": 1,
  "showLyrics": true,
  "positionLocked": false,
  "lyricsSource": "auto",
  "fftCaptureMode": "player"
}
```

歌词来源的值继续为 `auto`、`netease`、`qq`、`kugou`、`soda`、`lrclib`；音频采集继续为 `player`、`system`。UI 中文标签不作为持久化值。用显式映射或 Tag 关联枚举，不让本地化文案、内置导航项或排序变化改变映射。

默认：歌词显示开启、位置未锁定、歌词来源自动、音频来源当前播放器。保留 `SettingsStore` 的未知版本／非法枚举／错误类型处理和原子保存。

### 9.2 防止回调重入

真实控件的程序性赋值可能触发事件。初始化、`SetSettings`、外部文件同步和恢复页面时，用作用域保护的 `m_updatingView` 等机制屏蔽用户修改回调；离开作用域自动恢复。

需要处理这条同步链：

```text
用户操作控件
  → 生成完整 AppSettings
  → onChanged
  → App::ApplySettings
  → SettingsWindow::SetSettings（仅更新显示）
  → App::PersistSettings（一次保存）
```

不得发生 Toggled → ApplySettings → SetSettings → Toggled 的递归；第一次打开设置不应因为控件初始化重写配置、发起歌词请求或重连音频。

保存失败不能伪装为已保存。可以延续现有错误反馈，不在此任务中擅自改变“内存已应用、磁盘保存失败”的处理约定。

### 9.3 事件撤销

保存 event token／合适的 revoker，在关窗和失败清理时显式撤销。避免控件、闭包和宿主形成强引用环。普通 C++ `SettingsWindow` 不自动拥有 WinRT `get_weak()`，不要照抄适用于 `winrt::implements` 的写法。

关闭后不得再执行旧窗口的设置回调；重新打开不能叠加订阅，造成一次用户操作保存多次。

## 10. 布局、DPI、主题与背景

### 10.1 布局

保持三类导航与现有设置项。使用自适应布局和必要的 `ScrollViewer`，减少固定绝对坐标。参考现有默认客户区约 940×610 DIP、最小约 720×480 DIP，具体可按真实控件间距适当调整；小窗口不得丢失操作。

现有窗口在同进程关窗重开时保留所选导航页。新实现应在门面或后端状态中保留该行为，不为此新增持久化字段；重新构建控件树时同步页选择，并屏蔽初始化事件。

不重写 ToggleSwitch、ComboBox、Button 的 ControlTemplate 来追求旧截图一致。系统控件的尺寸、字体、动画与旧自绘界面有合理差异，验收看控件真实性、可读性和功能完整性。

### 10.2 DPI

Win32 HWND／`SetWindowPos` 使用物理像素，XAML 布局使用 DIP。`WM_SIZE` 将 child 填满客户区；`WM_DPICHANGED` 应用系统建议矩形后同步尺寸，不能把字体和整个 XAML 根布局再次乘一次 DPI 比例。

PerMonitorV2 已在清单中。明确根元素约束／Measure／Arrange 或 Width／Height 的实际接入方式，用 100%、125%、150%、200% 和跨屏切换证明没有双重缩放、裁剪或命中错位。[尺寸和 DPI 指南](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/advanced-scenarios-xaml-islands-cpp#handle-dpi-changes)

### 10.3 主题、中文与无障碍

- 明暗主题跟随应用主题，窗口打开时切换也应更新。
- 优先使用系统主题资源；高对比度不能被固定颜色覆盖。
- 保持简体中文文案；检查导航、开关状态和下拉项，不要求机械复用旧 DirectWrite 字体映射。
- 给控件提供有意义的可访问名称和关联说明；验证 Narrator／UIA 能读出角色、值和状态。
- 不用“只可点击的 Border + 自绘文字”替代原生可交互控件。
- 默认设置窗口不常驻 60 FPS 自绘计时器；由系统控件处理动画和输入。

### 10.4 Mica 与不透明回退

Mica、圆角、标题栏明暗继续由适用的系统能力提供，是在控件接入成功后的增强项。先证明正常主题下的不透明背景可用，再检查透明根背景与宿主组合；不支持背景材质时使用正确主题的纯色。

不能仅凭设置了 DWM 属性，就宣称画面确有 Mica；要检查背景是否被 XAML 不透明面板盖住，或反过来出现黑底。高对比度及不支持的 OS 必须有可读背景。不要从系统应用复制未公开 DLL 或私有控件来取得外观。

## 11. 分阶段实施与提交

| 阶段 | 工作 | 完成证据 |
| --- | --- | --- |
| P0：保存基线 | 阅读 AGENTS／架构／设置协议；记录当前差异、产物与测试 | 基线说明，既有 GPU 和歌词成果未丢失 |
| P1：最小宿主 | 系统标准控件、清单、STA／WinRT、单文件部署试验 | 真正显示并可操作；重开与退出通过；依赖证据 |
| P2：后端分离 | 门面、XAML 后端、现有回退、消息和生命周期 | 支持与不支持环境走正确路径 |
| P3：行为迁移 | 三页、四个设置字段、读取／重置、事件保护 | UI／托盘／文件同步、一次操作一次保存 |
| P4：系统体验 | 键盘、焦点、DPI、主题、高对比度、错误显示 | 对应截图和人工记录 |
| P5：回归与资源 | 完整 Release、CTest、部署和资源测量 | 原功能未回退，测量有方法和原始样本 |
| P6：交付 | 更新文档、整理差异和待验项 | 验收者可直接复核，未测项标明 |

每阶段保持可编译；适合分提交，不要求把全部改动塞入一次提交。发现不符合运行时依赖边界的问题要在 P1 解决，不能在后期通过扩大包装掩盖。

## 12. 构建与测试要求

使用仓库现有构建树对应的生成器，不用 VS 2022 生成器覆盖 VS 2026 的缓存。当前本机 `build-vs` 为 Visual Studio 18 2026；仓库 CI 使用 Visual Studio 17 2022。

```powershell
# 本机已配置的构建树
cmake --build build-vs --config Release --parallel
ctest --test-dir build-vs -C Release --output-on-failure

# 需要隔离构建时可另开目录，不影响正在运行的原产物
cmake -S . -B build-fluent -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON
cmake --build build-fluent --config Release --parallel
ctest --test-dir build-fluent -C Release --output-on-failure
```

若 PATH 中旧 CMake 不支持 VS 18，可使用本机已安装的：

```text
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

CI 继续验证 VS 2022 构建，不把本机绝对路径写入项目。必要的 SDK 要求须说明，并给出明确构建错误，而不是依赖开发机隐式文件。

### 12.1 自动测试

保留现有五项测试：LyricScroll、FFT、LyricsParser、SettingsStore、Direct2DRenderer。设置迁移必须继续通过持久化测试和完整主程序构建。

按实际实现增加有价值的验证，重点覆盖：

- 用户操作与程序性赋值的区别，外部同步不会产生重复业务回调。
- 既有枚举映射和默认值；重新读取失败不覆盖有效状态。
- 若实现可运行集成测试：创建真实 XAML source、操作控件、关窗／重开和退出清理。
- 若加入强制能力缺失的测试：验证回退路径仍可更新设置。

测试采用临时配置目录，不修改用户真实 `settings.json`；当前 SettingsStoreTests 已通过测试进程自身的 LOCALAPPDATA 重定向实现隔离。

XAML 集成测试可能要求客户端 OS／交互桌面。CI 环境缺少该能力时，必须明确标为跳过及原因，不能 catch 所有异常后报告通过。现代客户端上的异常应是失败；跳过不代替人工验收。

### 12.2 人工功能矩阵

| 类别 | 场景 | 通过条件 |
| --- | --- | --- |
| 后端真实性 | 支持系统上打开设置 | 真实系统控件显示；不是悄悄走自绘回退 |
| 重复开关 | 同进程开／关 20 次，含最小化后恢复 | 不崩溃、不失去输入、不产生多个窗口／重复事件 |
| 退出 | 设置打开、关闭、下拉展开时分别退出程序 | 能正常退出，无残留设置进程、无清理卡死 |
| 设置 | 全部选项、重置、读取、重启程序 | 即时效果、落盘和恢复正确 |
| 同步 | 设置打开时修改托盘选项、外部配置 | 显示及时同步，业务操作不会循环触发 |
| 键盘 | Tab／Shift+Tab、方向键、Space、Enter、Esc、Alt+F4 | 所有选项可达；弹层和宿主焦点正确 |
| DPI | 100／125／150／200%，可用时混合 DPI 双屏 | 无双重缩放、裁剪、错位、异常大小 |
| 主题 | 明、暗、窗口打开时切换、高对比度 | 文本、状态、焦点框可读，背景无黑块 |
| 兼容 | Win10 1809、1903+、Win11 目标版本 | 1809 走回退，支持客户端走系统控件 |
| 部署 | 空目录、无 VS／额外 App SDK 客户端 | 单 EXE 可用，无隐藏依赖或下载 |
| 原功能 | 播放、悬停、封面按钮、右键、拖动、位置锁定、音量／设备菜单 | 任务栏与设置互不干扰 |
| 现有修复 | 歌词长间奏、回退进度、播放帧调度 | 保留现有成果，无回退到旧 GPU／歌词代码 |

其他 Windows 版本、不同主题或多屏缺少测试条件时，标记“未测”，不要标记通过。Explorer 重启和自动隐藏可作为已有任务栏待验证项继续记录，不在设置迁移中声称已全部解决。

## 13. 资源占用测量

原生 XAML 是系统框架，但首次加载仍可能增加内存和线程。关闭设置窗口后，框架／驱动缓存也可能驻留；不能承诺工作集立即回到首次启动值，更不能强制卸载系统 XAML DLL。

按相同 OS、主题、DPI、播放状态、歌曲和测试时段比较：

| 状态 | 最少记录 |
| --- | --- |
| 冷启动，从未打开设置 | 工作集、Private Bytes、线程、句柄、CPU |
| 首次打开后稳定 10 秒 | 上述指标、首次显示耗时 |
| 关闭后稳定 10 秒 | 驻留增量、是否仍持续绘制／轮询 |
| 连续 20 次开／关后 | 资源是否趋稳；不能每次持续增长 |
| 播放中打开／关闭设置 | 任务栏绘帧耗时、提交频率与 CPU 变化 |

建议 CPU 采样至少 10–15 秒，明确一个核心还是全机百分比。记录 EXE 字节数，并检查是否仍低于项目 2.5 MB 目标；超出时给出实际数字和原因。

不要把 5/5 CTest 当成内存达标，不把暂停样本当成播放占用，也不要把 Render 次数当成屏幕实际呈现的不同帧数。当前 REPORT 的最终播放 CPU 复测仍未完成；不得引用那个待测结果来证明设置迁移更省资源。

可接受的常驻增长需要实测评估。至少必须满足：不在从未开设置时激活 XAML，不引入空闲持续重绘；多次开／关后对象、事件和句柄不持续累积。不要通过清空工作集等手段美化测量。

## 14. 使用者正在运行程序时的协作要求

用户正在使用 MusicBar。只读检查、文档和隔离目录编译可以直接进行；需要停止、重启或切换运行中的程序时，**提前通知测试时间和影响**。

播放性能测试应先让用户选好带歌词的歌曲并确认播放已经开始，测量中保持播放和一致的悬停状态，结束后通知并恢复正式版本。没有实际连续播放样本时报告未完成，不反复把暂停数据当作结果。

自动化测试使用隔离配置，不直接写用户真实配置。需要人工操作正式设置时，事先保存有效配置，记录测试改动并恢复；主题等需要用户配合时说明具体动作，不擅自更改系统主题或隐私权限。

## 15. 交付与最终验收材料

执行 AI 应提交以下材料，而不是仅交一句“已改成 Fluent”：

1. 实现摘要：哪些模块迁移、最终使用 `Windows.UI.Xaml` 哪些控件，回退如何选择。
2. 当前基线、相关提交／差异、改动文件清单；说明既有未提交成果如何保留。
3. 完整 Release 构建与 CTest 输出，编译器、SDK、配置和产物路径。
4. 单 EXE 证据：字节数、包装清单、导入／嵌入清单检查、动态依赖路径和干净客户端验证。
5. 生命周期说明：首次初始化、普通关窗、重新打开、失败回退、最终退出各自的对象状态和顺序。
6. 明暗截图、关键 DPI 截图、真实系统控件与键盘／无障碍操作记录。
7. 四个设置字段、托盘／文件同步、默认／读取／错误处理的结果。
8. 内存、Private Bytes、CPU、线程、句柄的前后数据和测量方法。
9. 人工功能矩阵：通过／失败／未测，不用“应该没问题”代替证据。
10. 更新 `SETTINGS_GUI.md`、`ARCHITECTURE.md`、`ROADMAP.md`；新增依赖若仅用于开发，也要交代来源和是否进入分发。

最终验收优先检查：单 EXE 是否真实成立、支持系统是否真走 XAML、重复开关与退出、回调重入、1809 回退、任务栏原功能、资源是否趋稳。任一核心条件失败，应修复或明确报告，不能把自绘换一套颜色后当成完成系统控件迁移。

## 16. 给实施 AI 的任务摘要

> 请以当前工作树为基线，把 MusicBar 设置窗口迁移为系统 `Windows.UI.Xaml` 的真实 UWP XAML 控件，通过 C++/WinRT 和 XAML Islands 托管在同一个原生 EXE 内。先完成最小系统控件、可重开、可退出、单 EXE 干净客户端验证，再迁移三页和现有四个设置字段。保留 `/MT`、版本 1 配置、1809 原生回退，以及现有 GPU 渲染和歌词修复。重点处理线程级 manager 生命周期、消息预处理、焦点、DPI、主题、事件撤销和程序性赋值重入。按本文分阶段实现，完成完整构建、CTest、人工矩阵和资源测量，并提交可复核的验收材料。用户已选择此路线；不要改成 WinUI 3／WPF／WebView2，也不要重新确认已经确定的选型。测试会重启正在运行的程序时先通知用户。

## 17. 查阅资料

本文的项目结构、迁移步骤和验收标准为针对当前代码的实施建议。系统 API 的契约以当前 SDK 和以下微软资料为准，接入与单 EXE 可行性仍须通过 P1 验证：

- [UWP XAML Islands 支持范围](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/xaml-islands)
- [Win32 C++ 托管标准系统控件](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/host-standard-control-with-xaml-islands-cpp)
- [托管 API 与线程级生命周期](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api)
- [键盘、焦点、布局与 DPI](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/advanced-scenarios-xaml-islands-cpp)
- [PreTranslateMessage 接口](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.xaml.hosting.desktopwindowxamlsource/nf-windows-ui-xaml-hosting-desktopwindowxamlsource-idesktopwindowxamlsourcenative2-pretranslatemessage)
- [RoInitialize 契约](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-roinitialize)
- [应用清单与 maxversiontested](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests#maxversiontested)
- [微软 XAML Islands 样例](https://github.com/microsoft/Xaml-Islands-Samples)
- 项目内：[AGENTS.md](../AGENTS.md)、[架构](ARCHITECTURE.md)、[设置协议](SETTINGS_GUI.md)、[待办](ROADMAP.md)、[现有 GPU／歌词修复报告](../REPORT.md)。
