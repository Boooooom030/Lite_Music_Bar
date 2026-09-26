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

        SetBrush("WindowBrush", light ? "#F4F6FA" : "#10151D");
        SetBrush("CardBrush", light ? "#FFFFFF" : "#1A212B");
        SetBrush("InputBrush", light ? "#F7F8FB" : "#252E3B");
        SetBrush("TextBrush", light ? "#1B2430" : "#F0F3F8");
        SetBrush("MutedBrush", light ? "#647083" : "#A5B0BF");
        SetBrush("BorderBrush", light ? "#DFE4EC" : "#354152");
        SetBrush("AccentBrush", light ? "#3176E4" : "#70A8FF");
        SetBrush("AccentHoverBrush", light ? "#245EBB" : "#4B91F6");
        SetBrush("AccentSoftBrush", light ? "#EAF2FF" : "#203856");
        if (MainWindow is MainWindow window)
            window.UpdateTitleBarTheme(light);
    }

    private void SetBrush(string key, string hex)
    {
        Resources[key] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
    }
}
