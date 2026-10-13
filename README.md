# MusicBar

MusicBar 是一个嵌入 Windows 任务栏的原生音乐栏。左侧显示封面，中间显示歌曲信息和同步歌词，右侧显示音频频谱。悬停封面可播放或暂停；中间可切换输出设备、上一首和下一首；点击频谱可调节音量。

右键音乐栏或托盘图标，可勾选“锁定位置”防止误拖动；再次点击即可解锁。锁定状态会保存在本地设置中。

![MusicBar 任务栏效果](assets/screenshot.png)

面向 Windows 10 1809+ 和 Windows 11。Release 构建将 MSVC 运行库静态链接到 `MusicBar.exe`，无需另外安装 .NET 或 Visual C++ 运行库。歌词需要联网获取。双击 EXE 启动，可从托盘菜单退出。

自动歌词会优先查询当前播放器对应的平台。FFT 默认采集当前播放器进程的声音；在不支持按进程采集的旧版 Windows 上，继续使用系统混音采集。右键音乐栏或托盘图标打开“设置”，可手动切换到系统混音模式。

## 下载与构建

仓库发布后，可在 [Releases](../../releases) 下载 `MusicBar-Windows-x64.zip`，先解压整个 `MusicBar` 文件夹，再运行其中的 `MusicBar.exe`。任务栏音乐栏和设置窗口都在这一个 EXE 中；压缩包另附使用说明、构建信息和第三方许可文本。无需安装 .NET 或 Visual C++ 运行库。推送 `v` 开头的版本标签后，GitHub Actions 会自动构建、测试、打包，并创建带 ZIP 和 SHA-256 校验文件的 Release；普通分支推送和 PR 不触发构建。发布命令、依赖准备、缓存与本地复现见 [CI 构建说明](docs/CI_BUILD.md)。

配置保存在 EXE 旁的 `data/settings.json`，不写入用户 AppData，也不读取旧版配置。歌词、封面和渲染缓存只在内存中，退出即释放。请将整个程序目录放到可写的位置；从快捷方式或其他工作目录启动也会使用 EXE 所在目录。更新时先退出程序，替换 EXE 并保留 `data`；删除时从托盘退出，然后删除整个解压目录，配置会一并删除。仅删除下载的 ZIP 不会清理已解压的文件。

需要 MSVC C++ 工具集、Windows SDK、CMake 3.28+、Ninja，以及 Qt 6.11.2 / Fluent-Qt v1.10.0 静态 Release SDK。Qt Gui 必须启用 HarfBuzz。SDK 默认位于 `~/Storage/tool/Qt-FluentQt`，准备方法和路径见 [环境记录](docs/FLUENT_QT_ENVIRONMENT.md)。

```powershell
cmd /d /c scripts\build-qt-release.cmd
ctest --test-dir build-qt-release --output-on-failure
```

输出位于 `build-qt-release/MusicBar.exe`。设置静态链接 Qt Widgets，任务栏仍使用 C++20、Win32、Direct2D、DirectComposition、C++/WinRT 和 WASAPI。开发时生成的 SettingsUiTests.exe 仅用于界面验证，不随产品分发。

构建脚本会初始化本机 MSVC x64 环境。可通过 `MUSICBAR_BUILD_DIR` 指定独立输出目录，避免链接正在使用的 EXE；脚本只构建，不会启动或终止程序：

```powershell
$env:MUSICBAR_BUILD_DIR = Join-Path (Get-Location) 'build-qt-review'
cmd /d /c scripts\build-qt-release.cmd
ctest --test-dir build-qt-review --output-on-failure
```

也可通过构建脚本说明中的环境变量覆盖 SDK prefix 和 toolchain。

设置窗口内置于主程序，左侧分类导航、右侧分组显示四项设置，修改后立即保存到 EXE 旁的 `data/settings.json`。界面跟随系统明暗／高对比主题，中文优先使用 Microsoft YaHei UI。可用 `MusicBar.exe --settings` 直接打开设置；已有实例时会通知已有实例打开窗口。关闭设置继续运行，退出程序仍使用托盘菜单。

## 项目文档

- [当前架构与源码目录](docs/ARCHITECTURE.md)
- [待办与人工验证](docs/ROADMAP.md)
- [设置窗口与配置协议](docs/SETTINGS_GUI.md)
- [第三方组件与许可](docs/THIRD_PARTY.md)
- [GitHub Actions 构建与便携 ZIP](docs/CI_BUILD.md)

项目自身目前未设置开源许可证；第三方组件仍遵循各自许可。
