# 🎵 MusicBar — Minimal Windows Music Bar

一款专为 Windows 10/11 设计的极简**原生任务栏内嵌媒体控制组件**。  
灵感源自 [AF-Media-Bar](https://github.com/Fervent-Tempo/AF-Media-Bar)，去除所有繁重子系统（歌词引擎、频谱分析、设备混音、主题换肤等），仅保留最核心功能：**看歌、控歌、与任务栏融为一体**。

---

## ✨ 核心特性

- 📌 **原生嵌入任务栏内部**：通过 Win32 API (`SetParent`) 将组件挂载为任务栏 (`Shell_TrayWnd`) 的子窗口，与任务栏图标、托盘处于同一水平线，真正镶嵌在任务栏里。
- 🧲 **任务栏内自由拖拽**：按住鼠标左键可在任务栏内部左右拖拽调整位置。
- 🎧 **原生 GSMTC 支持**：基于 Windows 官方 `Windows.Media.Control` API，支持 Spotify、网易云音乐、QQ音乐、Apple Music、Chrome、Edge 等所有播放器。
- 🖼️ **微型封面与曲目信息**：24×24 圆角专辑封面，紧凑显示歌曲名及歌手名。
- ⏯️ **微型播放控制**：上一首、播放/暂停、下一首按钮。
- 🖱️ **鼠标滚轮切歌**：滚轮上 = 上一首，滚轮下 = 下一首。
- ⏱️ **底部 2px 进度条**：`#1DB954` Spotify Green 进度条，实时跟随播放进度。
- 👻 **自适应隐藏**：托盘右键可开启"无播放时自动隐藏"。
- 📌 **系统托盘常驻**：右键菜单（显示/隐藏、重新停靠、退出），双击切换可见性。
- ⚡ **单实例防护**：全局互斥体防止重复启动。
- 🪶 **超低资源占用**：~30 MB 内存，无额外 CPU 消耗。

---

## 🚀 运行方式

### 方式 1：直接运行已发布的单文件免安装版（推荐）
```
MusicBar\bin\Release\net8.0-windows10.0.17763.0\win-x64\publish\MusicBar.exe
```
完全自包含单文件，无需安装 .NET 运行时，双击即可运行。

### 方式 2：从源码运行 / 开发调试
```powershell
cd MusicBar
dotnet run
```

---

## 🛠️ 构建与发布

- **系统要求**：Windows 10 1809 (Build 17763) 或更高版本 / Windows 11
- **开发工具**：.NET 8 SDK

```powershell
# 编译 Debug 版本
dotnet build

# 发布自包含单文件便携 EXE
dotnet publish -c Release -r win-x64 --self-contained -p:PublishSingleFile=true
```

---

## 📂 项目结构

```
MusicBar/
├── MusicBar.csproj              # 项目工程文件 (WPF + WinForms 托盘)
├── App.xaml / App.xaml.cs       # 应用入口、单实例互斥、系统托盘
├── MainWindow.xaml / .cs        # 任务栏内嵌 UI、拖拽、进度条、控制
├── Services/
│   ├── MediaSessionService.cs   # GSMTC 接口封装、会话监听与控制
│   └── TaskbarDockService.cs    # Win32 任务栏嵌入、DPI 适配、拖拽定位
├── Models/
│   └── MediaInfo.cs             # 媒体元数据不可变快照
└── Assets/
    └── icon.ico                 # 应用程序与托盘图标
```
