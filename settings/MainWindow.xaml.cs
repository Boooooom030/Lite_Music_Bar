using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;

namespace MusicBar.Settings;

public partial class MainWindow : Window
{
    private DateTime _loadedStamp;

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(
        IntPtr window, int attribute, ref int value, int valueSize);

    public MainWindow()
    {
        InitializeComponent();
        ConfigPathText.Text = SettingsStore.FilePath;
        LoadSettings();
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        UpdateTitleBarTheme(((App)Application.Current).IsLightTheme);
    }

    internal void UpdateTitleBarTheme(bool light)
    {
        var window = new WindowInteropHelper(this).Handle;
        if (window == IntPtr.Zero) return;
        var dark = light ? 0 : 1;
        _ = DwmSetWindowAttribute(window, 20, ref dark, sizeof(int));
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
