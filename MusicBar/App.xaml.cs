using System.Diagnostics;
using System.Drawing;
using System.Windows;
using MusicBar.Services;
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox;

namespace MusicBar;

/// <summary>
/// Application entry point.
/// Handles single-instance enforcement, service initialization,
/// tray icon setup, and clean shutdown.
/// </summary>
public partial class App : Application
{
    private Mutex? _singleInstanceMutex;
    private MediaSessionService? _mediaService;
    private TaskbarDockService? _dockService;
    private MainWindow? _mainWindow;
    private System.Windows.Forms.NotifyIcon? _trayIcon;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // ── Single-instance guard ──────────────────────────
        _singleInstanceMutex = new Mutex(true, "MusicBar_SingleInstance_7F3A", out bool isNew);
        if (!isNew)
        {
            MessageBox.Show("Music Bar 已经在运行中。\n可以在任务栏或系统托盘找到它。", "Music Bar",
                            MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }

        // ── 1. Create and show window immediately ──────────
        _mediaService = new MediaSessionService();
        _dockService = new TaskbarDockService();
        _mainWindow = new MainWindow(_mediaService, _dockService);
        MainWindow = _mainWindow;
        _mainWindow.Show();

        // ── 2. Setup system tray icon ───────────────────────
        SetupTrayIcon();

        // ── 3. Initialize GSMTC in background without blocking UI ──
        _ = Task.Run(async () =>
        {
            try
            {
                await _mediaService.InitializeAsync();
                Debug.WriteLine("[App] MediaSessionService initialized successfully.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[App] MediaSessionService failed: {ex.Message}");
            }
        });

        Debug.WriteLine("[App] Music Bar started successfully.");
    }

    // ────────────────────────────────────────────
    //  System Tray
    // ────────────────────────────────────────────

    private void SetupTrayIcon()
    {
        var contextMenu = new System.Windows.Forms.ContextMenuStrip();

        var showHideItem = new System.Windows.Forms.ToolStripMenuItem("显示 / 隐藏 (Show / Hide)");
        showHideItem.Click += (_, _) => _mainWindow?.ToggleVisibility();
        contextMenu.Items.Add(showHideItem);

        var redockItem = new System.Windows.Forms.ToolStripMenuItem("重新停靠任务栏 (Re-dock Taskbar)");
        redockItem.Click += (_, _) => _mainWindow?.TryDockIntoTaskbar();
        contextMenu.Items.Add(redockItem);

        var autoHideItem = new System.Windows.Forms.ToolStripMenuItem("无播放时自动隐藏 (Auto-Hide When Idle)")
        {
            CheckOnClick = true,
            Checked = false
        };
        autoHideItem.Click += (_, _) =>
        {
            if (_mainWindow is not null)
            {
                _mainWindow.AutoHideWhenIdle = autoHideItem.Checked;
                if (autoHideItem.Checked && !_mainWindow.HasActiveMedia)
                {
                    _mainWindow.AnimateHide();
                }
            }
        };
        contextMenu.Items.Add(autoHideItem);

        contextMenu.Items.Add(new System.Windows.Forms.ToolStripSeparator());

        var exitItem = new System.Windows.Forms.ToolStripMenuItem("退出 (Exit)");
        exitItem.Click += (_, _) => Shutdown();
        contextMenu.Items.Add(exitItem);

        _trayIcon = new System.Windows.Forms.NotifyIcon
        {
            Text = "Music Bar",
            Icon = LoadEmbeddedIcon(),
            Visible = true,
            ContextMenuStrip = contextMenu
        };

        _trayIcon.DoubleClick += (_, _) => _mainWindow?.ToggleVisibility();
    }

    /// <summary>
    /// Loads a simple tray icon. Uses the app icon if available,
    /// otherwise falls back to a system default.
    /// </summary>
    private static Icon LoadEmbeddedIcon()
    {
        try
        {
            var iconPath = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Assets", "icon.ico");
            if (System.IO.File.Exists(iconPath))
            {
                return new Icon(iconPath);
            }

            var exePath = Environment.ProcessPath;
            if (exePath is not null)
            {
                var icon = Icon.ExtractAssociatedIcon(exePath);
                if (icon is not null) return icon;
            }
        }
        catch { /* fall through */ }

        // Fallback: use SystemIcons.
        return SystemIcons.Application;
    }

    // ────────────────────────────────────────────
    //  Shutdown
    // ────────────────────────────────────────────

    protected override void OnExit(ExitEventArgs e)
    {
        Debug.WriteLine("[App] Shutting down...");

        _trayIcon?.Dispose();
        _mediaService?.Dispose();
        _singleInstanceMutex?.ReleaseMutex();
        _singleInstanceMutex?.Dispose();

        base.OnExit(e);
    }
}
