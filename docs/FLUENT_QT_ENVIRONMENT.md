# Fluent-Qt 开发环境准备记录

日期：2026-10-12。状态：通用 Qt / Fluent-Qt 环境安装完成，SDK 构建、独立消费者构建、官方预检和单 EXE 启动验证通过。

用户授权本阶段安装依赖和验证开发环境。设置窗口的软件代码由其他 AI 编写；用户随后授权直接选择常规控件，新的 [设置重写实施报告](FLUENT_QT_SETTINGS_IMPLEMENTATION.md) 已完成。本页记录本机开发环境；MusicBar 的 Qt 设置实现仍待开发和验收。

## 当前边界

- 设置窗口计划使用 C++ Qt Widgets / Fluent-Qt，仍运行于 MusicBar.exe 同一进程。
- Release 程序本体保持单个原生 EXE，体积上限为 100 MB。
- 任务栏继续使用现有 Win32 / Direct2D / DirectComposition；本阶段不替换或重启用户正在运行的 MusicBar。
- 不引入 Qt QML、WebEngine、Python 绑定或 3D 扩展。
- 保留其他 AI 已完成的部分 XAML 代码；旧 [XAML 报告](FLUENT_SETTINGS_IMPLEMENTATION.md) 不再作为当前迁移依据。

## 版本和安装目录

通用开发环境位于 `C:\Users\huban\Storage\tool\Qt-FluentQt`（`~/Storage/tool/Qt-FluentQt`），可供其他 C++ 项目复用，不绑定 MusicBar。脚本从自身位置确定根目录，不改系统 PATH，不向主工程复制大型库，也不修改 Visual Studio 自带的 vcpkg。迁移前的生成文件保存在环境内 `relocation-archive/`，新构建不使用这些旧路径。

| 项目 | 版本或路径 |
| --- | --- |
| Fluent-Qt | v1.10.0，commit `4076a1612e5785757cfe0759436dabe3414e490e` |
| vcpkg registry | commit `e456309491fd875487bf02b8a0dca76c1b1b00cc` |
| vcpkg tool | 2026-09-26 |
| Qt Base / SVG | 6.11.2 |
| 编译器 | MSVC 14.51.36231，Visual Studio 2026 x64 |
| Windows SDK | 本机有 10.0.26100.0 和 10.0.28000.0 |
| 构建配置 | Release、x64、静态库、静态 MSVC CRT（`/MT`） |
| 依赖 manifest | `C:\Users\huban\Storage\tool\Qt-FluentQt\manifest\vcpkg.json` |
| 自定义 triplet | `C:\Users\huban\Storage\tool\Qt-FluentQt\triplets\x64-windows-static-release.cmake` |
| Qt 与基础依赖 prefix | `C:\Users\huban\Storage\tool\Qt-FluentQt\installed\x64-windows-static-release` |
| Fluent-Qt 固定版本源码 | `C:\Users\huban\Storage\tool\Qt-FluentQt\Fluent-Qt` |
| Fluent-Qt SDK prefix | `C:\Users\huban\Storage\tool\Qt-FluentQt\sdk\FluentQt` |
| Fluent-Qt 构建目录 | `C:\Users\huban\Storage\tool\Qt-FluentQt\build-fluent` |
| 消费者工具链 | `C:\Users\huban\Storage\tool\Qt-FluentQt\toolchains\msvc-x64-static-release.cmake` |
| 通用说明 | `C:\Users\huban\Storage\tool\Qt-FluentQt\README.md` |

Qt Base 仅选择 Widgets / Gui、Network、PNG、JPEG 及其必需依赖；SVG 和 spdlog 用于官方 Gallery。它们不意味着 MusicBar 必须链接 Network、SVG 或 spdlog，后续主程序应按实际使用选择目标。

自定义 triplet 将 host 和 target 都设为同一 x64 静态配置，只构建 Release。使用此 SDK 的应用也必须选 Release 和静态 CRT；不能直接混用 Debug 或 `/MD` 配置。

## 重建入口

安装脚本会在自己的进程中初始化 MSVC 环境，设置独立 vcpkg 根目录。

```powershell
cmd /d /c C:\Users\huban\Storage\tool\Qt-FluentQt\install-dependencies.cmd
cmd /d /c C:\Users\huban\Storage\tool\Qt-FluentQt\build-fluent.cmd
cmd /d /c C:\Users\huban\Storage\tool\Qt-FluentQt\verify-sdk.cmd
python C:\Users\huban\Storage\tool\Qt-FluentQt\verify-runtime.py
```

第一条从固定 registry 与 manifest 安装依赖；第二条构建 Fluent-Qt、官方 Hello World、官方 Gallery，并安装可供 `find_package(FluentQt CONFIG REQUIRED)` 使用的 SDK；第三条运行官方环境预检，并以独立消费者工程验证已安装 SDK。重复执行会复用下载、已安装依赖和编译产物。

首次 Qt 源码下载遇到默认镜像 HTTP/2 传输失败，安装脚本现先调用 `cache-qt-sources.ps1`，通过 Qt 官方列出的日本 JAIST 镜像、HTTP/1.1 和断点续传下载，并按固定 vcpkg registry 中的 SHA512 校验后放入缓存。首次安装还利用 FAU / JAIST / 20i 镜像分段补齐已有下载，最终对完整文件统一校验。此处理不修改系统代理。

消费者应把 Qt prefix 和 Fluent-Qt SDK prefix 加入本机构建的 `CMAKE_PREFIX_PATH`，并使用上表的 `CMAKE_TOOLCHAIN_FILE`，不要把本机绝对路径写死到共享源码。该工具链复用已安装依赖并关闭消费者的 vcpkg manifest 自动安装；静态 ZLIB 等依赖需要 vcpkg 的查找包装器，因此仅添加 prefix 不够。脚本固定 MSVC，避免本机 Strawberry 附带的 GCC 被自动选择。主工程的消息循环、COM/WinRT、DPI 和窗口生命周期接入详见 [设置重写实施报告](FLUENT_QT_SETTINGS_IMPLEMENTATION.md)；环境准备阶段没有修改入口函数。

Qt 构建复用本机 Perl 5.42.2 与 Python 3.14.6，路径由安装脚本配置并通过 triplet 传入。下载及新的二进制缓存均保存在本环境中。

## 验证结果

- Fluent-Qt v1.10.0 固定版本已下载。
- Qt Base 源码大小为 50,582,668 字节，分段合并后 SHA512 与固定 vcpkg registry 相同；SHA256 为 `5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22`，也与 Qt 官方镜像列表一致。
- Qt SVG 源码大小为 2,341,860 字节，SHA512 与固定 vcpkg registry 相同。
- 两份 Qt 源码均已置于 `C:\Users\huban\Storage\tool\Qt-FluentQt\vcpkg\downloads`，后续安装脚本可直接使用缓存。
- Qt Base / SVG、fmt 和 spdlog 静态 Release 安装完成；Fluent-Qt 核心、官方 Gallery、Hello World 构建成功，SDK 安装完成。
- 官方 `doctor --profile cpp`：3 项通过、0 项失败、0 项警告。
- 独立 Hello World 工程通过 `find_package(FluentQt)` 使用安装后的 SDK 编译链接成功，EXE 为 **26,541,056 字节**（约 26.5 MB）。
- Gallery EXE 为 **32,652,800 字节**（约 32.7 MB）。`verification/portable/` 内只有此 EXE；清除 Qt 环境变量并将 PATH 限制为 Windows 系统目录后，以 Windows 原生平台运行明暗主题按钮预览，两次均退出码 0。PNG 已检查，按钮和文字正常显示。
- `dumpbin /DEPENDENTS` 未发现 Qt DLL 或 MSVC 可再分发运行库 DLL；列出的依赖为 Windows 系统接口。此结果结合单文件运行证据支持本机无需附带 Qt DLL，而非未经测试的新系统兼容性保证。
- 验证主机：Windows 11 Pro，build 26200。没有在 Windows 10 或干净虚拟机上运行；没有对 MusicBar 的新设置界面进行功能、内存或 CPU 验收。

| 验证产物 | 环境内相对路径 |
| --- | --- |
| 可直接打开的本地 Gallery | `verification/portable/fluent_qt_gallery.exe` |
| SDK 独立示例 | `build-consumer/fluentqt_hello_world.exe` |
| SDK 构建日志 | `build-fluent.log` |
| 官方预检及独立消费者日志 | `verify-sdk.log` |
| 单文件运行记录 | `verification/runtime-summary.json` |
| DLL 依赖记录 | `verification/gallery-dependencies.txt`、`verification/hello-world-dependencies.txt` |
| 明暗主题预览及 Inspector | `verification/artifacts/button-light.*`、`button-dark.*` |

## 本机兼容性处理

Fluent-Qt 核心保持 v1.10.0 原版。官方 Gallery 的 `GalleryApplicationController.cpp` 增加 `QT_CONFIG(sessionmanager)` 条件，仅在 Qt 提供可选会话管理接口时调用 `isSavingSession()`；补丁保存在 `patches/gallery-optional-sessionmanager.patch`。这使当前精简 Qt 配置可以构建 Gallery，未修改 MusicBar 的业务代码。

本机中文 MSVC 的包含文件前缀被 CMake 误解码，消费者工具链通过 `normalize-msvc-includes.cmake` 修正 Ninja 的头文件依赖识别，编译警告仍正常输出。

示例通过不能视为 MusicBar 的设置迁移已经完成，也不能作为常驻内存、CPU 或 Windows 10 兼容性验收。

## 来源与许可

- [Fluent-Qt 固定版本](https://github.com/calvinhxx/Fluent-Qt/tree/v1.10.0)：自身代码为 MIT；随库资源有独立许可，安装时保留上游许可文本。
- [Qt Windows 静态部署](https://doc.qt.io/qt-6/windows-deployment.html#static-linking)、[静态插件](https://doc.qt.io/qt-6/plugins-howto.html#static-plugins)：Qt 库、平台插件及必需资源均需纳入链接或资源编译。
- [Qt 开源许可义务](https://www.qt.io/development/open-source-lgpl-obligations)：当前只做本机开发环境准备；公开发布时需按选用的 Qt 许可提供对应材料。静态链接不自动免除重新链接等要求。
- [vcpkg triplet 变量](https://learn.microsoft.com/en-us/vcpkg/users/triplets)：分别固定库、CRT 链接方式和 Release 构建配置。
