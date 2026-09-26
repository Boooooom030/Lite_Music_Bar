# MusicBar

MusicBar 是一个嵌入 Windows 任务栏的原生音乐栏。左侧显示封面，中间显示歌曲信息和同步歌词，右侧显示音频频谱。悬停封面可播放或暂停；中间可切换输出设备、上一首和下一首；点击频谱可调节音量。

右键音乐栏或托盘图标，可勾选“锁定位置”防止误拖动；再次点击即可解锁。锁定状态会保存在本地设置中。

![MusicBar 任务栏效果](assets/screenshot.png)

面向 Windows 10 1809+ 和 Windows 11。Release 构建将 MSVC 运行库静态链接到 `MusicBar.exe`，无需另外安装 .NET 或 Visual C++ 运行库。歌词需要联网获取。双击 EXE 启动，可从托盘菜单退出。

自动歌词会优先查询当前播放器对应的平台。FFT 默认采集当前播放器进程的声音；在不支持按进程采集的旧版 Windows 上，继续使用系统混音采集。安装可选设置窗口后，也可手动切换到系统混音模式。

## 下载与构建

仓库发布后，可在 [Releases](../../releases) 下载 `MusicBar-Windows-x64.zip`，解压后运行其中的 `MusicBar.exe`。压缩包还附有第三方组件的许可文本；主程序运行只需 EXE。可选的 `MusicBar-Settings-Windows-x64.zip` 提供独立设置窗口，需要 .NET 8 Desktop Runtime；将其中的 `MusicBar.Settings.exe` 放到主程序旁边，右键音乐栏或托盘图标即可打开“设置”。GitHub Actions 在每次构建后保存这两个压缩包；推送 `v` 开头的版本标签会创建 Release。首次发布前请先确认 Windows 工作流成功运行。

需要 MSVC C++ 工具集、Windows SDK 和 CMake 3.28+。安装 Visual Studio 2022 时可直接使用：

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release --parallel
ctest --test-dir build-vs -C Release --output-on-failure
```

输出位于 `build-vs/Release/MusicBar.exe`，构建目录由 Git 忽略。核心程序使用 C++20、Win32、Direct2D、C++/WinRT 和 WASAPI。

如果只安装了 Visual Studio 2026，请在其 **x64 Native Tools Command Prompt** 中使用 Ninja：

```powershell
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-ninja --parallel
ctest --test-dir build-ninja --output-on-failure
```

此时输出位于 `build-ninja/MusicBar.exe`。

设置窗口使用 WPF 8，可单独构建为一个依赖 .NET 8 Desktop Runtime 的 EXE：

```powershell
dotnet publish settings/MusicBar.Settings.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=None -o build-settings
```

将 `build-settings/MusicBar.Settings.exe` 复制到 `MusicBar.exe` 所在目录后，托盘“设置”入口会启用。设置保存在 `%LOCALAPPDATA%\MusicBar\settings.json`；运行中的主程序约一秒内读取更改。设置程序未安装时，主程序仍可单独运行。

## 项目文档

- [当前架构与源码目录](docs/ARCHITECTURE.md)
- [待办与人工验证](docs/ROADMAP.md)
- [设置窗口与配置协议](docs/SETTINGS_GUI.md)
- [第三方组件与许可](docs/THIRD_PARTY.md)

项目自身目前未设置开源许可证；第三方组件仍遵循各自许可。
