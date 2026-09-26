using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace MusicBar.Settings;

public partial class App : Application
{
    internal bool IsLightTheme { get; private set; } = true;

    protected override void OnStartup(StartupEventArgs e)
    {
        ApplySystemTheme();
        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        base.OnExit(e);
    }

    private void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
    {
        Dispatcher.BeginInvoke(new Action(ApplySystemTheme));
    }

    private void ApplySystemTheme()
    {
        var value = Registry.GetValue(
            @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
            "AppsUseLightTheme", 1);
        var light = value is not int numeric || numeric != 0;
        IsLightTheme = light;

        SetBrush("WindowBrush", light ? "#F5F6F8" : "#202124");
        SetBrush("SidebarBrush", light ? "#C8F5F6F8" : "#C81B1C20");
        SetBrush("CardBrush", light ? "#D9FFFFFF" : "#D92B2D31");
        SetBrush("InputBrush", light ? "#AAFFFFFF" : "#AA34363A");
        SetBrush("TextBrush", light ? "#202124" : "#F5F5F5");
        SetBrush("MutedBrush", light ? "#626A73" : "#B1B6BE");
        SetBrush("BorderBrush", light ? "#2A858D98" : "#3FFFFFFF");
        SetBrush("AccentBrush", light ? "#0F6CBD" : "#72B4F8");
        SetBrush("AccentHoverBrush", light ? "#115EA3" : "#91C5FA");
        SetBrush("AccentTextBrush", light ? "#FFFFFF" : "#10263A");
        SetBrush("AccentSoftBrush", light ? "#DDE9F3FC" : "#55305577");
        SetBrush("HoverBrush", light ? "#90FFFFFF" : "#33FFFFFF");
        if (MainWindow is MainWindow window)
            window.UpdateTitleBarTheme(light);
    }

    private void SetBrush(string key, string hex)
    {
        Resources[key] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
    }
}
