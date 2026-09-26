using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;

namespace MusicBar.Settings;

public partial class MainWindow : Window
{
    private DateTime _loadedStamp;

    [StructLayout(LayoutKind.Sequential)]
    private struct Margins
    {
        public int Left;
        public int Right;
        public int Top;
        public int Bottom;
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(
        IntPtr window, int attribute, ref int value, int valueSize);

    [DllImport("dwmapi.dll")]
    private static extern int DwmExtendFrameIntoClientArea(IntPtr window, ref Margins margins);

    public MainWindow()
    {
        InitializeComponent();
        ConfigPathText.Text = SettingsStore.FilePath;
        ConfigPathText.ToolTip = SettingsStore.FilePath;
        NavTaskbar.IsChecked = true;
        LoadSettings();
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        UpdateTitleBarTheme(((App)Application.Current).IsLightTheme);
        EnableSystemBackdrop();
    }

    internal void UpdateTitleBarTheme(bool light)
    {
        var window = new WindowInteropHelper(this).Handle;
        if (window == IntPtr.Zero) return;
        var dark = light ? 0 : 1;
        _ = DwmSetWindowAttribute(window, 20, ref dark, sizeof(int));
    }

    private void EnableSystemBackdrop()
    {
        // Windows 11 22H2 draws Mica behind the whole window; older systems use WindowBrush.
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22621)) return;

        var window = new WindowInteropHelper(this).Handle;
        var backdrop = 2; // DWMSBT_MAINWINDOW
        if (DwmSetWindowAttribute(window, 38, ref backdrop, sizeof(int)) < 0) return;

        var margins = new Margins { Left = -1, Right = -1, Top = -1, Bottom = -1 };
        if (DwmExtendFrameIntoClientArea(window, ref margins) < 0) return;

        if (PresentationSource.FromVisual(this) is HwndSource source)
        {
            source.CompositionTarget.BackgroundColor = Colors.Transparent;
            Background = Brushes.Transparent;
        }

        var corner = 2; // DWMWCP_ROUND
        _ = DwmSetWindowAttribute(window, 33, ref corner, sizeof(int));
    }

    private void NavigationChecked(object sender, RoutedEventArgs e)
    {
        if (PageTaskbar is null || PageLyrics is null || PageAudio is null ||
            sender is not RadioButton button) return;

        var page = button.Tag as string;
        PageTaskbar.Visibility = page == "taskbar" ? Visibility.Visible : Visibility.Collapsed;
        PageLyrics.Visibility = page == "lyrics" ? Visibility.Visible : Visibility.Collapsed;
        PageAudio.Visibility = page == "audio" ? Visibility.Visible : Visibility.Collapsed;

        (PageTitle.Text, PageDescription.Text) = page switch
        {
            "lyrics" => ("歌词", "选择歌词的来源，让正在播放的歌曲更容易被识别。"),
            "audio" => ("音频频谱", "决定频谱跟随当前播放器，还是响应系统中的所有声音。"),
            _ => ("任务栏", "决定音乐栏如何显示和响应操作。")
        };
        SettingsScrollViewer.ScrollToTop();
    }

    private RadioButton[] SourceButtons =>
        [SourceAuto, SourceNetEase, SourceQQ, SourceKugou, SourceSoda, SourceLrclib];

    private RadioButton[] CaptureButtons => [CapturePlayer, CaptureSystem];

    private void LoadSettings()
    {
        var loaded = SettingsStore.TryLoad(out var settings, out var error);
        Display(settings);
        _loadedStamp = SettingsStore.FileStamp;
        StatusText.Text = loaded
            ? "已读取设置；保存后运行中的 MusicBar 会自动更新。"
            : error ?? "设置文件无法读取。";
    }

    private void Display(SettingsModel settings)
    {
        ShowLyricsToggle.IsChecked = settings.ShowLyrics;
        PositionLockedToggle.IsChecked = settings.PositionLocked;
        foreach (var button in SourceButtons)
            button.IsChecked = (string)button.Tag == settings.LyricsSource;
        foreach (var button in CaptureButtons)
            button.IsChecked = (string)button.Tag == settings.FftCaptureMode;
    }

    private SettingsModel ReadControls()
    {
        return new SettingsModel
        {
            ShowLyrics = ShowLyricsToggle.IsChecked == true,
            PositionLocked = PositionLockedToggle.IsChecked == true,
            LyricsSource = SourceButtons.FirstOrDefault(button => button.IsChecked == true)?
                .Tag.ToString() ?? "auto",
            FftCaptureMode = CaptureButtons.FirstOrDefault(button => button.IsChecked == true)?
                .Tag.ToString() ?? "player"
        };
    }

    private void ReloadClicked(object sender, RoutedEventArgs e) => LoadSettings();

    private void ResetClicked(object sender, RoutedEventArgs e)
    {
        Display(new SettingsModel());
        StatusText.Text = "已选择默认值，点击“保存设置”后生效。";
    }

    private void SaveClicked(object sender, RoutedEventArgs e)
    {
        if (SettingsStore.FileStamp != _loadedStamp)
        {
            StatusText.Text = "设置文件已在其他窗口修改，请先重新读取。";
            return;
        }

        try
        {
            SettingsStore.Save(ReadControls());
            _loadedStamp = SettingsStore.FileStamp;
            StatusText.Text = "设置已保存，MusicBar 会在一秒内读取。";
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException)
        {
            StatusText.Text = "保存失败：" + exception.Message;
        }
    }
}
