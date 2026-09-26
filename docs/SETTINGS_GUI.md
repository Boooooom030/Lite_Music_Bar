# 设置页面技术评估

## 结论

可以使用 C# 开发设置页面。建议让任务栏中的 `MusicBar.exe` 继续作为独立的原生 C++ 进程运行，并在用户打开设置时启动单独的 `MusicBar.Settings.exe`。这样关闭设置后，WPF / .NET 不会留在任务栏常驻进程中，也不会改变当前的窗口裁剪、点击穿透和媒体逻辑。设置程序是可选的独立交付物；如果未来要求整个产品只有一个原生 EXE 且无额外运行时，就需要改用 Win32 / Direct2D 实现设置窗口。

## 框架选择

| 方案 | 适合的情况 | 代价 |
| --- | --- | --- |
| C# WPF + 官方 Fluent 主题 | 快速实现表单、绑定和主题切换；现有 C# 经验可以复用 | 设置程序需要 .NET Desktop Runtime，或打包自包含运行时；界面仅在打开设置时占用资源 |
| C# WinUI 3（Windows App SDK） | 需要更完整的现代 Windows / Fluent 控件和交互 | 引入 Windows App SDK 的项目、打包和部署工作，设置程序体积及维护成本更高 |
| C++ Win32 / Direct2D | 必须坚持整个产品都是原生、无额外运行时 | 表单、无障碍、绑定和主题控件需要自行实现，开发成本较高 |

针对当前以轻量任务栏常驻为核心的项目，优先推荐 **C# WPF + 官方 Fluent 主题** 作为独立设置程序。这里的“Fluent”指设计语言和 WPF 主题；若需求特指 WinUI 3，应单独评估其部署方式。不要把旧 WPF 任务栏窗口重新嵌入 C++ 主程序。

## 推荐的集成边界

1. C++ 主程序仍负责所有任务栏交互、媒体控制、音频和歌词。托盘菜单提供“设置”入口，只在点击时启动设置程序。
2. 将可配置项定义成有版本号的共享配置格式，保存在用户配置目录。C++ 侧负责默认值、范围检查和兼容旧版本；设置页面以原子替换方式保存文件，主程序重新读取。若托盘菜单也要保存设置，再约定单一写入方或并发写入规则。不要直接从 C# 修改当前 C++ 内存状态。
3. 设置保存后可先用进程内轮询或文件变更通知刷新；如果需要立即获取当前设备、预览 FFT 等双向交互，再加同用户权限的命名管道。不要在初版引入复杂 IPC。
4. C# 设置程序不能成为 `MusicBar.exe` 的启动前置条件；即使设置程序未安装、崩溃或 .NET 运行时缺失，任务栏栏位也应正常工作。

当前代码尚未实现持久化配置或设置入口。开始制作 GUI 前，先确定设置项清单及默认值，再实现上述配置边界。

## 参考资料

- [WPF 官方 Fluent 主题（.NET 9+）](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/whats-new/net90)
- [WinUI 3 与 Windows 10 1809 兼容性](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/)
- [.NET 单文件部署及自包含体积](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview)
- [Windows 桌面应用进程通信](https://learn.microsoft.com/en-us/windows/apps/develop/communication/interprocess-communication)
