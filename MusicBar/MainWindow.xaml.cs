using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using MusicBar.Models;
using MusicBar.Services;
using MouseEventArgs = System.Windows.Input.MouseEventArgs;

namespace MusicBar;

/// <summary>
/// Taskbar-embedded music control bar.
/// Docks into the Windows taskbar and displays compact playback info.
/// Uses full-taskbar canvas with GDI SetWindowRgn clipping.
/// </summary>
public partial class MainWindow : Window
{
    private readonly MediaSessionService _service;
    private readonly TaskbarDockService _dockService;
    private readonly DispatcherTimer _timelineTimer;

    private MediaInfo _currentInfo = MediaInfo.Empty;

    // Bar dimensions
    private const double BarWidthDip = 280;
    private const double BarHeightDip = 36;

    // Dragging state inside taskbar
    private bool _isDraggingInTaskbar;
    private System.Drawing.Point _dragStartCursor;
    private double _dragStartLeft;
    private double _currentBarLeft;
    private double _currentBarTop;

    public bool AutoHideWhenIdle { get; set; } = false;
    public bool HasActiveMedia => _currentInfo.HasMedia;
    public bool IsTaskbarDocked => _dockService.IsDocked;

    public MainWindow(MediaSessionService service, TaskbarDockService dockService)
    {
        InitializeComponent();

        _service = service;
        _dockService = dockService;
        _service.MediaChanged += OnMediaChanged;

        _timelineTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _timelineTimer.Tick += OnTimelineTick;

        Loaded += OnLoaded;
        MouseWheel += OnMouseWheel;
    }

    // ────────────────────────────────────────────
    //  Taskbar Docking & Positioning
    // ────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        TryDockIntoTaskbar();
        ApplyMediaInfo(_service.CurrentMediaInfo);
    }

    private DispatcherTimer? _dockRetryTimer;

    /// <summary>
    /// Attempts to dock into the Windows Taskbar as a native child window.
    /// </summary>
    public void TryDockIntoTaskbar()
    {
        bool success = _dockService.Dock(this);
        if (success)
        {
            _dockRetryTimer?.Stop();

            // Calculate default position (near notification area)
            var (left, top) = _dockService.CalculateDefaultBarPosition(BarWidthDip, BarHeightDip);
            _currentBarLeft = left;
            _currentBarTop = top;

            Canvas.SetLeft(MainContainer, _currentBarLeft);
            Canvas.SetTop(MainContainer, _currentBarTop);

            // Apply GDI clipping region to the bar rectangle
            _dockService.ApplyBarInputRegion(this, _currentBarLeft, _currentBarTop, BarWidthDip, BarHeightDip);

            // Make visible at Win32 level
            Visibility = Visibility.Visible;
            MainContainer.Opacity = 1.0;
        }
        else
        {
            // Fallback: place above taskbar
            PositionFallbackAboveTaskbar();
            Visibility = Visibility.Visible;

            if (_dockRetryTimer == null)
            {
                _dockRetryTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
                _dockRetryTimer.Tick += (_, _) =>
                {
                    if (_dockService.Dock(this))
                    {
                        _dockRetryTimer.Stop();
                        var (l, t) = _dockService.CalculateDefaultBarPosition(BarWidthDip, BarHeightDip);
                        _currentBarLeft = l;
                        _currentBarTop = t;
                        Canvas.SetLeft(MainContainer, _currentBarLeft);
                        Canvas.SetTop(MainContainer, _currentBarTop);
                        _dockService.ApplyBarInputRegion(this, _currentBarLeft, _currentBarTop, BarWidthDip, BarHeightDip);
                    }
                };
                _dockRetryTimer.Start();
            }
        }
    }

    private void PositionFallbackAboveTaskbar()
    {
        var workArea = SystemParameters.WorkArea;
        Width = BarWidthDip;
        Height = BarHeightDip;
        Left = workArea.Left + (workArea.Width - BarWidthDip) / 2;
        Top = workArea.Bottom - BarHeightDip;
        Topmost = true;
        Canvas.SetLeft(MainContainer, 0);
        Canvas.SetTop(MainContainer, 0);
    }

    // ────────────────────────────────────────────
    //  Dragging inside Taskbar Canvas
    // ────────────────────────────────────────────

    private void OnBarMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Left)
        {
            if (_dockService.IsDocked)
            {
                _isDraggingInTaskbar = true;
                _dragStartLeft = _currentBarLeft;
                GetCursorPos(out _dragStartCursor);
                MainContainer.CaptureMouse();
            }
            else
            {
                DragMove();
            }
        }
    }

    private void OnBarMouseMove(object sender, MouseEventArgs e)
    {
        if (_isDraggingInTaskbar && _dockService.IsDocked)
        {
            GetCursorPos(out var currentCursor);
            double deltaXDip = (currentCursor.X - _dragStartCursor.X) / _dockService.DpiScale;
            double newLeft = _dragStartLeft + deltaXDip;

            // Clamp inside taskbar
            double maxLeft = (Width) - BarWidthDip - 10;
            newLeft = Math.Clamp(newLeft, 10, Math.Max(10, maxLeft));

            _currentBarLeft = newLeft;
            Canvas.SetLeft(MainContainer, _currentBarLeft);
            _dockService.ApplyBarInputRegion(this, _currentBarLeft, _currentBarTop, BarWidthDip, BarHeightDip);
        }
    }

    private void OnBarMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (_isDraggingInTaskbar)
        {
            _isDraggingInTaskbar = false;
            MainContainer.ReleaseMouseCapture();
        }
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool GetCursorPos(out System.Drawing.Point lpPoint);

    // ────────────────────────────────────────────
    //  Mouse Wheel for Quick Skip
    // ────────────────────────────────────────────

    private void OnMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (e.Delta > 0)
        {
            _ = _service.PreviousAsync();
        }
        else if (e.Delta < 0)
        {
            _ = _service.NextAsync();
        }
    }

    // ────────────────────────────────────────────
    //  Media Info Updates
    // ────────────────────────────────────────────

    private void OnMediaChanged(MediaInfo info)
    {
        Dispatcher.BeginInvoke(() => ApplyMediaInfo(info));
    }

    private void ApplyMediaInfo(MediaInfo info)
    {
        _currentInfo = info;

        if (!info.HasMedia)
        {
            _timelineTimer.Stop();

            if (AutoHideWhenIdle)
            {
                AnimateHide();
                return;
            }

            // Idle state: show compact placeholder in taskbar
            TitleText.Text = "Music Bar";
            ArtistText.Text = "未在播放";
            AlbumArt.Source = null;
            ArtPlaceholder.Visibility = Visibility.Visible;
            PlayPauseBtn.Content = "\uE768";
            PlayPauseBtn.ToolTip = "播放";
            BottomProgress.Value = 0;

            if (MainContainer.Opacity < 0.1)
            {
                AnimateShow();
            }
            return;
        }

        // Active media
        if (MainContainer.Opacity < 0.1)
        {
            AnimateShow();
        }

        TitleText.Text = info.Title;
        ArtistText.Text = string.IsNullOrEmpty(info.Artist) ? "未知歌手" : info.Artist;

        if (info.AlbumArt is not null)
        {
            AlbumArt.Source = info.AlbumArt;
            ArtPlaceholder.Visibility = Visibility.Collapsed;
        }
        else
        {
            AlbumArt.Source = null;
            ArtPlaceholder.Visibility = Visibility.Visible;
        }

        PlayPauseBtn.Content = info.IsPlaying ? "\uE769" : "\uE768";
        PlayPauseBtn.ToolTip = info.IsPlaying ? "暂停" : "播放";

        UpdateTimeline(info.Position, info.Duration, info.IsPlaying);
    }

    private void UpdateTimeline(TimeSpan position, TimeSpan duration, bool isPlaying)
    {
        if (duration > TimeSpan.Zero)
        {
            BottomProgress.Maximum = duration.TotalSeconds;
            BottomProgress.Value = position.TotalSeconds;
        }
        else
        {
            BottomProgress.Value = 0;
        }

        if (isPlaying)
            _timelineTimer.Start();
        else
            _timelineTimer.Stop();
    }

    private void OnTimelineTick(object? sender, EventArgs e)
    {
        var snapshot = _service.GetCurrentTimelineSnapshot();
        if (snapshot is null) return;

        UpdateTimeline(snapshot.Position, snapshot.Duration, snapshot.IsPlaying);

        PlayPauseBtn.Content = snapshot.IsPlaying ? "\uE769" : "\uE768";
        PlayPauseBtn.ToolTip = snapshot.IsPlaying ? "暂停" : "播放";
    }

    // ────────────────────────────────────────────
    //  Playback Controls
    // ────────────────────────────────────────────

    private async void OnPrevious(object sender, RoutedEventArgs e) => await _service.PreviousAsync();
    private async void OnPlayPause(object sender, RoutedEventArgs e) => await _service.PlayPauseAsync();
    private async void OnNext(object sender, RoutedEventArgs e) => await _service.NextAsync();

    // ────────────────────────────────────────────
    //  Show / Hide Animations
    // ────────────────────────────────────────────

    public void ToggleVisibility()
    {
        if (MainContainer.Opacity > 0.1)
        {
            AnimateHide();
        }
        else
        {
            AnimateShow();
        }
    }

    public void AnimateShow()
    {
        _dockService.ApplyBarInputRegion(this, _currentBarLeft, _currentBarTop, BarWidthDip, BarHeightDip);
        var startOpacity = MainContainer.Opacity < 0.05 ? 0.0 : MainContainer.Opacity;
        var anim = new DoubleAnimation(startOpacity, 1.0, TimeSpan.FromMilliseconds(200))
        {
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
        };
        MainContainer.BeginAnimation(UIElement.OpacityProperty, anim);
    }

    public void AnimateHide()
    {
        var anim = new DoubleAnimation(MainContainer.Opacity, 0.0, TimeSpan.FromMilliseconds(180))
        {
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn }
        };
        anim.Completed += (_, _) =>
        {
            if (MainContainer.Opacity < 0.05)
            {
                _dockService.ClearInputRegion(this);
            }
        };
        MainContainer.BeginAnimation(UIElement.OpacityProperty, anim);
    }
}
