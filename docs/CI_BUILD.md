# Windows CI 与便携分发

工作流为 `.github/workflows/build-windows.yml`，仅在推送 `v*` 版本标签时自动运行：构建、执行 CTest、打包 ZIP，成功后创建 GitHub Release 并上传 ZIP 和 SHA-256 附件。普通分支推送、PR 和手动触发均不运行该工作流。使用 Windows Server 2022 runner、MSVC x64、Ninja、静态 `/MT` Release，与 `scripts/build-qt-release.cmd` 共用应用构建入口。

发布时，先提交并推送要发布的代码，再在该提交上创建新的版本标签。例如：

```powershell
git tag v0.1.0
git push origin v0.1.0
```

将 `v0.1.0` 换成实际版本号。标签必须指向包含当前工作流的提交；仅在本地创建标签不会触发 CI。触发配置遵循 [GitHub 的标签过滤规则](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#onpushbranchestagsbranches-ignoretags-ignore)。

`scripts/ci/initialize-msvc.ps1` 使用系统 Visual Studio Installer 和 `vcvars64.bat` 初始化 x64 工具链，只导出编译器／SDK 环境到 `GITHUB_ENV`。避免依赖旧 Node 20 的 MSVC 初始化 action；不导出凭据或 GitHub runner 控制变量。

## 依赖准备

旧工作流未安装 Qt／Fluent-Qt，直接配置 Visual Studio 工程，无法满足当前 `find_package`。现在由 `scripts/ci/prepare-qt-sdk.ps1` 在 `build-ci-sdk` 中准备以下固定依赖：

| 依赖 | 固定版本 |
| --- | --- |
| vcpkg / registry baseline | `e456309491fd875487bf02b8a0dca76c1b1b00cc` |
| Qt Base | 6.11.2；Widgets、Gui、HarfBuzz、Network、PNG、JPEG；启用 `FEATURE_cxx20` |
| Fluent-Qt | v1.10.0，`4076a1612e5785757cfe0759436dabe3414e490e` |
| CRT / 库 / 构建类型 | static `/MT` / static / Release only |

target 与 host 均使用 `x64-windows-static-release` 自定义 triplet。FluentQt 只构建／安装核心静态库与资源，关闭 Gallery、测试、示例及可选运行时。脚本确认源码 commit、Qt 版本、静态 LIB 和 HarfBuzz；后者避免 Fluent 图标加载成功却无法绘出的问题。

Qt 必须通过 triplet 的 `VCPKG_CMAKE_CONFIGURE_OPTIONS` 显式启用 `FEATURE_cxx20=ON`。Qt 自己选择语言标准，单独传 `CMAKE_CXX_STANDARD=20` 无效；MSVC 19.4x 下默认 C++17 Qt 的 WinRT 实现与 C++20 程序产生 `_COROUTINE_ABI` 的 LNK2038 冲突。准备脚本检查实际生成的 `qconfig.h` 与 SDK receipt，拒绝不一致的缓存。相关设置见 [vcpkg triplet 文档](https://learn.microsoft.com/en-us/vcpkg/users/triplets#vcpkg_cmake_configure_options) 与 [对应问题的复现记录](https://github.com/microsoft/vcpkg/discussions/50515)。

缓存保存 Qt 安装目录、FluentQt SDK／版本记录和 vcpkg 二进制缓存。缓存键包含 MSVC 工具集、Windows SDK 与构建配方哈希；没有跨工具链的模糊匹配。命中后校验 SDK 完整性并复用。新 SDK 在应用构建之前保存，因此应用测试失败不会丢失已成功构建的依赖。首次无缓存会从源码编译 Qt，耗时明显更长，job 超时设为 120 分钟。

应用和测试显式关闭 `VCPKG_APPLOCAL_DEPS`：它们使用静态 SDK，不复制 vcpkg DLL，也不在链接后调用新 checkout 中不存在的 `vcpkg.exe`。这保证完整 SDK 缓存命中后无需重新 bootstrap vcpkg；打包脚本仍独立检查 PE 导入表。

## 测试与打包

应用输出为 `build-ci/MusicBar.exe`，不是旧 Visual Studio 的 `build-ci/Release/MusicBar.exe`。CTest 使用 `--no-tests=error`，保留设置窗口与 GPU 像素回归；GPU 初始化失败时由现有 renderer 使用 WARP。失败时上传 CMake、CTest、界面快照与依赖编译诊断。

`scripts/ci/package-release.ps1` 在 MSVC 环境中检查 EXE 小于 100,000,000 字节，并用 `dumpbin` 拒绝 Qt／FluentQt／VC++ 运行库 DLL 依赖。它只选择主 EXE，不能将开发测试程序、旧数据或其他构建文件混入发布包；输出目录已存在时拒绝复用。附上 json／miniz、Qt 及其依赖、FluentQt、字体与图标许可，以及源码版本、EXE 大小、导入表和 SHA-256。

ZIP 布局：

```text
MusicBar-Windows-x64.zip
└── MusicBar/
    ├── MusicBar.exe
    ├── README.txt
    ├── build-info.json
    └── licenses/
```

首次保存配置后生成 `MusicBar/data/settings.json`。保存临时文件也位于 `data`，不使用工作目录或 AppData。内存缓存不落盘，不新增磁盘日志／缓存，也不迁移旧配置。用户先退出程序再删除整个 `MusicBar` 解压目录即可清理应用自身文件；删除 ZIP 本身不会清理已经解压的目录。

标签发布 job 显式设置 `GH_REPO`，使无 Git checkout 的 Ubuntu job 能正确调用 `gh release`。保留重复标签构建时替换附件的行为。

## 本地复现

已有通用 SDK 时，在普通 PowerShell 中构建至独立目录；避免链接正在运行的程序：

```powershell
$env:MUSICBAR_BUILD_DIR = Join-Path (Get-Location) 'build-ci-validation'
cmd /d /c scripts\build-qt-release.cmd
ctest --test-dir build-ci-validation --output-on-failure --no-tests=error
```

完整依赖准备需要在 x64 Native Tools for VS 环境中运行 PowerShell，并具备 Git、CMake、Ninja、Perl 与 Python。将 vcpkg 和 Fluent-Qt 分别 checkout 到 `<SDK root>/vcpkg`、`<SDK root>/Fluent-Qt`，切到表格中的固定 commit，然后执行：

```powershell
./scripts/ci/prepare-qt-sdk.ps1 -SdkRoot ./build-ci-sdk
$env:QT_FLUENT_ROOT = Join-Path (Get-Location) 'build-ci-sdk'
$env:MUSICBAR_BUILD_DIR = Join-Path (Get-Location) 'build-ci'
cmd /d /c scripts\build-qt-release.cmd
ctest --test-dir build-ci --output-on-failure --no-tests=error
./scripts/ci/package-release.ps1 -BuildDirectory ./build-ci -SdkRoot $env:QT_FLUENT_ROOT -OutputDirectory ./release
```

`-ReuseInstalledDependencies` 仅用于本机复用已经验证的 Qt 依赖并重新构建 FluentQt，CI 冷启动不使用该开关。`-UseCachedSdk` 要求完整 SDK 与 receipt，CI 仅在缓存精确命中时使用。脚本不启动或终止 MusicBar。

## 验证记录

2026-10-13，本机使用已构建的 Qt 6.11.2 依赖，在独立 SDK 目录重新构建 FluentQt 核心，再由标准脚本构建应用。最终八项 CTest 和 actionlint 1.7.12 检查通过，配置测试包括中文路径、工作目录变化和被阻止的保存路径；确认不存在 AppData 写入。额外使用没有 `vcpkg.exe` 的全新 vcpkg checkout 模拟完整 SDK 缓存，独立构建与八项 CTest 通过；本机模拟原生程序返回 `-1`，确认 CMD 构建脚本返回失败且不打印成功输出。依赖清单另以 vcpkg dry-run 确认能解析 Qt 6.11.2 和 HarfBuzz，没有以 dry-run 声称完成冷启动编译。

最终 EXE 为 29,237,760 字节，ZIP 为 12,984,042 字节；归档检查确认只有一个 EXE，没有 DLL／测试程序或开发机数据，包含 34 个说明、许可与运行文件。ZIP SHA-256 与旁附校验文件一致。本机构建树更新并启动了新的便携版 MusicBar；旧用户目录清理被自动审批拦截，详见 ROADMAP。

2026-10-13 已将当前源码推送到 `Boooooom030/Lite_Music_Bar` 的 `main`。首次 [真实 CI](https://github.com/Boooooom030/Lite_Music_Bar/actions/runs/38375858961) 成功构建／缓存 SDK，但链接 MusicBar 时发现上述协程 ABI 冲突；本机 MSVC 19.5x 的通过结果不能替代 MSVC 19.4x CI 验证。

修正后的提交 `a65c9d7` 在 [C++20 SDK 冷启动运行](https://github.com/Boooooom030/Lite_Music_Bar/actions/runs/38381039523) 中成功完成依赖准备、应用构建、全部八项 CTest、ZIP 打包和上传。已下载 Actions 产物检查：EXE 为 28,945,408 字节，便携 ZIP 为 12,915,971 字节，31 个文件，只有一个运行 EXE，没有 DLL 或用户数据；`build-info.json` 的源码 commit 和 Qt C++20 标记正确，ZIP SHA-256 与旁附文件一致。

提交 `2f4c9fd` 的 [缓存命中运行](https://github.com/Boooooom030/Lite_Music_Bar/actions/runs/38385970099) 也成功完成应用构建、全部八项 CTest、ZIP 打包和上传。日志确认精确命中 SDK cache、读取通过 C++20 检查的 receipt 并直接复用，系统 MSVC 初始化选中同一套 MSVC 14.44.35207 / Windows SDK 10.0.26100.0，不再出现旧初始化 action 的 Node 20 弃用警告。该运行从初始化到上传约三分钟。标签发布仍需另行验证。
