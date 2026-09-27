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

        SetBrush("WindowBrush", light ? "#F4F5F7" : "#202124");
        SetBrush("SidebarBrush", light ? "#18FFFFFF" : "#10000000");
        SetBrush("CardBrush", light ? "#D9FFFFFF" : "#D9333539");
        SetBrush("InputBrush", light ? "#BFFFFFFF" : "#B9383A3F");
        SetBrush("FlyoutBrush", light ? "#F9FAFC" : "#303236");
        SetBrush("TextBrush", light ? "#1B1B1B" : "#F5F5F5");
        SetBrush("MutedBrush", light ? "#61666E" : "#B8BDC5");
        SetBrush("BorderBrush", light ? "#26777D84" : "#34FFFFFF");
        SetBrush("AccentBrush", light ? "#0F6CBD" : "#72B4F8");
        SetBrush("SelectedBrush", light ? "#BFFFFFFF" : "#40FFFFFF");
        SetBrush("HoverBrush", light ? "#80FFFFFF" : "#25FFFFFF");
        SetBrush("TrackBrush", light ? "#8B8B8B" : "#777C82");
        SetBrush("ThumbBrush", "#FFFFFF");
        SetBrush("ErrorBrush", light ? "#B3261E" : "#FFB4AB");
        if (MainWindow is MainWindow window)
            window.UpdateTitleBarTheme(light);
    }

    private void SetBrush(string key, string hex)
    {
        Resources[key] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
    }
}
