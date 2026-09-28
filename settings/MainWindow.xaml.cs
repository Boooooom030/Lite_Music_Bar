using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;

namespace MusicBar.Settings;

public partial class MainWindow : Window
{
    private bool _isReady;
    private bool _updatingControls;

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
        NavTaskbar.IsChecked = true;
        LoadSettings();
        _isReady = true;
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

        PageTitle.Text = page switch
        {
            "lyrics" => "歌词",
            "audio" => "音频频谱",
            _ => "任务栏"
        };
        SettingsScrollViewer.ScrollToTop();
    }

    private void SearchTextChanged(object sender, TextChangedEventArgs e)
    {
        if (NavTaskbar is null || NavLyrics is null || NavAudio is null ||
            SearchHint is null || NoSearchResults is null) return;

        var query = SearchBox.Text.Trim();
        SearchHint.Visibility = query.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        bool Matches(string terms) => query.Length == 0 ||
            terms.Contains(query, StringComparison.CurrentCultureIgnoreCase);

        NavTaskbar.Visibility = Matches("任务栏 音乐栏 位置 锁定 拖动")
            ? Visibility.Visible : Visibility.Collapsed;
        NavLyrics.Visibility = Matches("歌词 显示 来源 网易云 QQ 酷狗 汽水 LRCLIB")
            ? Visibility.Visible : Visibility.Collapsed;
        NavAudio.Visibility = Matches("音频 频谱 FFT 播放器 系统混音")
            ? Visibility.Visible : Visibility.Collapsed;

        NoSearchResults.Visibility = NavTaskbar.Visibility == Visibility.Collapsed &&
            NavLyrics.Visibility == Visibility.Collapsed && NavAudio.Visibility == Visibility.Collapsed
            ? Visibility.Visible : Visibility.Collapsed;

        if (NavTaskbar.IsChecked == true && NavTaskbar.Visibility == Visibility.Collapsed ||
            NavLyrics.IsChecked == true && NavLyrics.Visibility == Visibility.Collapsed ||
            NavAudio.IsChecked == true && NavAudio.Visibility == Visibility.Collapsed)
        {
            var next = new[] { NavTaskbar, NavLyrics, NavAudio }
                .FirstOrDefault(button => button.Visibility == Visibility.Visible);
            if (next is not null) next.IsChecked = true;
        }
    }

    private void LoadSettings()
    {
        var loaded = SettingsStore.TryLoad(out var settings, out var error);
        Display(settings);
        SetStatus(loaded ? string.Empty : error ?? "设置文件无法读取。", !loaded);
    }

    private void Display(SettingsModel settings)
    {
        _updatingControls = true;
        try
        {
            ShowLyricsToggle.IsChecked = settings.ShowLyrics;
            PositionLockedToggle.IsChecked = settings.PositionLocked;
            SourcePicker.SelectedValue = settings.LyricsSource;
            CapturePicker.SelectedValue = settings.FftCaptureMode;
        }
        finally
        {
            _updatingControls = false;
        }
    }

    private void SettingChanged(object sender, RoutedEventArgs e)
    {
        if (!_isReady || _updatingControls) return;

        var stamp = SettingsStore.FileStamp;
        if (!SettingsStore.TryLoad(out var settings, out var error))
        {
            Display(settings);
            SetStatus(error ?? "设置文件无法读取。", true);
            return;
        }

        if (SettingsStore.FileStamp != stamp)
        {
            LoadSettings();
            SetStatus("设置文件刚被其他窗口修改，请重试。", true);
            return;
        }

        if (sender == ShowLyricsToggle) settings.ShowLyrics = ShowLyricsToggle.IsChecked == true;
        else if (sender == PositionLockedToggle) settings.PositionLocked = PositionLockedToggle.IsChecked == true;
        else if (sender == SourcePicker) settings.LyricsSource = SourcePicker.SelectedValue as string ?? "auto";
        else if (sender == CapturePicker) settings.FftCaptureMode = CapturePicker.SelectedValue as string ?? "player";
        else return;

        SaveSettings(settings);
    }

    private void ReloadClicked(object sender, RoutedEventArgs e) => LoadSettings();

    private void ResetClicked(object sender, RoutedEventArgs e)
    {
        SaveSettings(new SettingsModel());
    }

    private void SaveSettings(SettingsModel settings)
    {
        try
        {
            SettingsStore.Save(settings);
            Display(settings);
            SetStatus(string.Empty, false);
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException)
        {
            LoadSettings();
            SetStatus("保存失败：" + exception.Message, true);
        }
    }

    private void SetStatus(string message, bool error)
    {
        StatusText.Text = message;
        StatusText.Visibility = error ? Visibility.Visible : Visibility.Collapsed;
        StatusText.SetResourceReference(TextBlock.ForegroundProperty,
            error ? "ErrorBrush" : "MutedBrush");
    }
}
