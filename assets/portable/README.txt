MusicBar 便携版

请先解压整个 MusicBar 文件夹，再运行 MusicBar.exe。
程序目录需要可写；推荐放在自己的下载目录或工具目录。

设置保存在 EXE 旁的 data/settings.json。
歌词、封面和渲染缓存只在内存中，退出后自动释放，不写入磁盘。
字体、图标、Qt 和 Fluent-Qt 已编译进 EXE；无需安装额外运行库。

更新：先从托盘退出 MusicBar，再替换 MusicBar.exe，保留 data 目录即可。
重置设置：退出程序后删除 data 目录。
删除程序：从托盘退出，然后删除整个 MusicBar 文件夹，配置也会一起删除。
仅删除下载的 ZIP 不会删除已经解压的文件。

第三方许可在 licenses 目录，构建版本与校验值在 build-info.json。
