# 第三方组件

本项目自身暂未设置开源许可证。`third_party/` 中的代码仍分别受其原作者的许可约束；不要把这些许可理解为覆盖 MusicBar 自有代码。

| 组件 | 本仓库文件 | 来源与许可 |
| --- | --- | --- |
| nlohmann/json 3.12.0 | `third_party/nlohmann/json.hpp` | [项目](https://github.com/nlohmann/json)、[本地 MIT 许可](../third_party/nlohmann/LICENSE.MIT)；头部含版权与 SPDX 标识 |
| miniz 2.2.0 | `third_party/miniz/miniz.c`、`miniz.h` | [项目](https://github.com/richgel999/miniz)、[本地 MIT 许可](../third_party/miniz/LICENSE.MIT)；vendored `miniz.c` 内也含版权和公有领域声明 |
| Qt 6.11.2 / Qt Base | 静态 SDK，由 `scripts/ci/vcpkg.json` 固定版本 | [Qt 许可说明](https://doc.qt.io/qt-6.11/licensing.html)；使用开源 Qt Base，适用各文件的 LGPLv3／GPL 等许可，见 SDK 的 `share/qtbase/copyright` |
| Fluent-Qt v1.10.0 | 静态 SDK，源码固定为 `4076a1612e5785757cfe0759436dabe3414e490e` | [项目源码](https://github.com/calvinhxx/Fluent-Qt/tree/4076a1612e5785757cfe0759436dabe3414e490e)、MIT；附带 Inter 字体的 SIL OFL、Fluent UI System Icons 的 MIT／NOTICE 和上游声明 |
| Qt 的静态依赖 | vcpkg 固定 baseline `e456309491fd875487bf02b8a0dca76c1b1b00cc` | HarfBuzz、FreeType、PNG、JPEG、zlib 等；逐个附上 SDK `share/<port>/copyright`，实际构建版本见固定的 vcpkg 清单和 baseline |

发布 ZIP 将上述许可文本置于 `MusicBar/licenses/` 目录，构建信息记录在 `MusicBar/build-info.json`。程序本身仍是单个可运行的 EXE。CI 只构建 FluentQt 核心库，不构建 Gallery、示例、QML、Spatial 或 PySide；不需要 Gallery 的 spdlog／QtSvg 依赖。

本项目实际静态链接 Qt，不能将 Fluent-Qt 上游声明中的“动态链接”描述直接套用。许可文本归档并不替代静态 Qt 的对应源码和重新链接要求；正式分发须按所选许可提供对应材料，详见 [Qt 官方 LGPL 说明](https://www.qt.io/development/open-source-lgpl-obligations)。固定源码版本、构建配方和本地复现命令见 [CI_BUILD.md](CI_BUILD.md)。

项目界面参考了 [AF-Media-Bar](https://github.com/Fervent-Tempo/AF-Media-Bar) 的交互效果。本仓库没有将它作为运行时依赖。
