# 第三方组件

本项目自身暂未设置开源许可证。`third_party/` 中的代码仍分别受其原作者的许可约束；不要把这些许可理解为覆盖 MusicBar 自有代码。

| 组件 | 本仓库文件 | 来源与许可 |
| --- | --- | --- |
| nlohmann/json 3.12.0 | `third_party/nlohmann/json.hpp` | [项目](https://github.com/nlohmann/json)、[本地 MIT 许可](../third_party/nlohmann/LICENSE.MIT)；头部含版权与 SPDX 标识 |
| miniz 2.2.0 | `third_party/miniz/miniz.c`、`miniz.h` | [项目](https://github.com/richgel999/miniz)、[本地 MIT 许可](../third_party/miniz/LICENSE.MIT)；vendored `miniz.c` 内也含版权和公有领域声明 |

发布 ZIP 将上述许可文本置于 `licenses/` 目录。程序本身仍是单个可运行的 EXE。

项目界面参考了 [AF-Media-Bar](https://github.com/Fervent-Tempo/AF-Media-Bar) 的交互效果。本仓库没有将它作为运行时依赖。
