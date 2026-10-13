# 设置窗口与配置协议

设置窗口由 `MusicBar.exe` 内的 `src/ui/SettingsWindow.*`（门面）和 `src/ui/qt/SettingsQtWindow.*`（实现）组成。右键音乐栏或托盘图标选择“设置”即可打开同进程窗口，不需要第二个 EXE、.NET 运行时或随附的 Qt DLL。修改立即应用并保存，没有“应用／确定”按钮。

窗口是 C++ Qt Widgets 界面，控件来自静态链接的 Fluent-Qt v1.10.0。2026-10-13 按用户追加要求采用接近 Windows 设置的布局：左侧使用库内 NavigationView 与 ListView，提供“全部设置”“任务栏”“歌词”“音频”，带图标和选中指示条；右侧显示分类标题与分组卡片。默认打开“全部设置”，分类切换过滤同一组控件，不复制设置或提交配置。底部提供“重新读取”“恢复默认”。默认 1040 × 740，最小 600 × 480 逻辑像素；小工作区可进一步缩小。窄窗口下左栏缩到 180，控制项移到文字下方，正文可滚动，标题栏和底部操作保持可见。

窗口显式启用 Fluent-Qt 标题栏，包含图标、名称、最小化、最大化／还原和关闭按钮。外观跟随 Windows 应用亮暗主题，高对比模式优先；正文保留 Typography 的尺寸与字重，并优先使用 Microsoft YaHei UI。清除内置 Inter 的固定样式名，避免干扰系统字体的字重选择。Qt Gui 必须启用 HarfBuzz，保证补充平面的 Fluent 图标、下拉箭头及标题栏符号真正绘出；程序构建时检查该配置，UI 测试也检查实际图标像素。

请求 Fluent-Qt 的 `Acrylic` 背景；支持的 Windows 11 上由系统 DWM 合成真实模糊背景。NavigationView 保持透明左栏和带圆角的右侧内容层，卡片与输入控件由库正常绘制。ScrollView 的视口及其内容取消自动填充，避免不透明画布盖住材质；明暗主题变化后也保持这一约束。较旧系统由库回退到不透明材质，高对比模式明确请求 `Solid`。DPI 由 Qt 处理，任务栏仍按物理像素逻辑绘制。

浅色设置卡片通过库的局部主题覆盖，将 `bgLayer` 设为约 70% 不透明的白色，减轻窗口激活后纯白卡片与深色 Acrylic 的反差。此覆盖只作用于卡片底色，文字、控件以及深色／高对比主题继续使用原有样式；没有对整张卡片添加透明度效果。

[本机实际 Acrylic 预览](images/settings-acrylic-soft.jpg) 来自独立设置预览窗口的系统截图，包含浅色卡片反差调整；其余明暗／窄窗口 PNG 为纯色布局回归快照，不展示 DWM 材质。独立预览复用正式设置实现，使用临时配置，不创建任务栏窗口或启动音频／媒体模块；它只是开发测试目标，不随正式程序分发。

窗口首次打开时创建，关闭（关闭按钮、Alt+F4 或窗口级 Escape）只隐藏并保留同一个实例，再次打开时还原并激活，不会多开；隐藏时音乐栏、托盘、媒体和频谱继续运行。连续从托盘或音乐栏打开只激活已有窗口。

后续选型依据见 [设置重写实施报告](FLUENT_QT_SETTINGS_IMPLEMENTATION.md)。当前外观仍待用户审阅，功能回归通过不代表视觉验收通过。原 [XAML 实施报告](FLUENT_SETTINGS_IMPLEMENTATION.md) 保留为历史技术记录；对应的 XAML 后端已从源码和构建中移除。

## 版本 1 协议

配置文件固定为 `<MusicBar.exe 所在目录>/data/settings.json`，UTF-8 编码；以 EXE 的绝对路径定位，不使用当前工作目录，也不读取／迁移旧的 AppData 配置：

```json
{
  "version": 1,
  "showLyrics": true,
  "positionLocked": false,
  "lyricsSource": "auto",
  "fftCaptureMode": "player"
}
```

| 字段 | 允许值 | 含义 |
| --- | --- | --- |
| `showLyrics` | `true` / `false` | 显示或隐藏同步歌词 |
| `positionLocked` | `true` / `false` | 阻止拖动音乐栏，保留按钮点击 |
| `lyricsSource` | `auto`、`netease`、`qq`、`kugou`、`soda`、`lrclib` | 自动优先当前播放器，或手动指定来源 |
| `fftCaptureMode` | `player`、`system` | 当前播放器进程音频，或系统混音 |

缺失的已知字段使用默认值；未知字段忽略。未知版本、错误类型和非法枚举值会被拒绝，主程序保留当时设置。设置文件不存在时使用默认值。旧版 Windows 无法按进程采集时，`player` 模式退化到系统混音。

`SettingsStore` 使用同一 `data` 目录内的临时文件和替换操作保存配置，成功／失败后清理临时文件。目录不可写时返回失败并提示将程序目录放到可写位置，不回退到 AppData。设置窗口的修改直接调用 `App::ApplySettings`，随后保存；托盘菜单修改会同步到已打开的设置窗口。外部程序更新文件时，主程序每秒检查时间戳并在 UI 线程应用。歌词来源改变时重新请求当前歌曲歌词，FFT 模式改变时重新连接音频采集。窗口底部提供重新读取与恢复默认；重新读取失败时显示状态文本并保留当前设置。程序性同步使用 QSignalBlocker，用户修改的值与当前值相同时不重复提交。

## 构建与分发

设置窗口随 CMake 主目标一起构建，需要本机的 Qt 6.11.2 + Fluent-Qt v1.10.0 静态 SDK（见 [环境记录](FLUENT_QT_ENVIRONMENT.md)）。构建入口为 `scripts\build-qt-release.cmd`，输出 `build-qt-release\MusicBar.exe`；脚本只构建，不启动程序，也不替换正在运行的 EXE。

发布 ZIP 包含一个 `MusicBar` 目录，内含 `MusicBar.exe`、使用说明、构建信息和第三方许可文本。Qt、Fluent-Qt、平台／样式／图像插件、字体与图标资源都已静态链接或编译进 EXE。配置在 EXE 旁的 `data` 目录，首次保存时创建；内存中的歌词／封面／渲染缓存继续保持内存存储，程序没有新增磁盘缓存或日志。退出后删除整个程序目录即可清理程序自身数据。

## 参考资料

- [Windows 应用设置页设计指南](https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings)
- [Windows Mica 材质指南](https://learn.microsoft.com/en-us/windows/apps/design/style/mica)
- [Fluent-Qt v1.10.0](https://github.com/calvinhxx/Fluent-Qt/tree/v1.10.0)
- [QApplication](https://doc.qt.io/qt-6.11/qapplication.html)、[quitOnLastWindowClosed](https://doc.qt.io/qt-6.11/qguiapplication.html#quitOnLastWindowClosed-prop)
- [Qt Windows 静态部署](https://doc.qt.io/qt-6/windows-deployment.html#static-linking)、[静态插件](https://doc.qt.io/qt-6/plugins-howto.html#static-plugins)
