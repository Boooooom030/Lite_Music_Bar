using System.Diagnostics;
using System.IO;
using System.Windows.Media.Imaging;
using MusicBar.Models;
using Windows.Media.Control;
using Windows.Storage.Streams;

namespace MusicBar.Services;

/// <summary>
/// Wraps the Windows GSMTC (Global System Media Transport Controls) API.
/// Discovers the active media session, reads metadata, and sends playback commands.
/// </summary>
public class MediaSessionService : IDisposable
{
    private GlobalSystemMediaTransportControlsSessionManager? _manager;
    private GlobalSystemMediaTransportControlsSession? _currentSession;
    private CancellationTokenSource? _debounceCts;
    private bool _disposed;

    /// <summary>Current media snapshot. Accessible anytime on any thread.</summary>
    public MediaInfo CurrentMediaInfo { get; private set; } = MediaInfo.Empty;

    /// <summary>Fired on the background thread whenever media info changes (or media disappears).</summary>
    public event Action<MediaInfo>? MediaChanged;

    private void SetCurrentMediaInfo(MediaInfo info)
    {
        CurrentMediaInfo = info;
        MediaChanged?.Invoke(info);
    }

    /// <summary>
    /// Initializes the GSMTC session manager and hooks up to the current media session.
    /// Must be called once at startup.
    /// </summary>
    public async Task InitializeAsync()
    {
        _manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
        _manager.CurrentSessionChanged += OnCurrentSessionChanged;

        // Pick up any session that's already active at launch.
        UpdateSession(_manager.GetCurrentSession());
    }

    // ────────────────────────────────────────────
    //  Session tracking
    // ────────────────────────────────────────────

    private void OnCurrentSessionChanged(
        GlobalSystemMediaTransportControlsSessionManager sender,
        CurrentSessionChangedEventArgs args)
    {
        UpdateSession(sender.GetCurrentSession());
    }

    private void UpdateSession(GlobalSystemMediaTransportControlsSession? session)
    {
        // Unsubscribe from previous session.
        if (_currentSession is not null)
        {
            _currentSession.MediaPropertiesChanged -= OnMediaPropertiesChanged;
            _currentSession.PlaybackInfoChanged -= OnPlaybackInfoChanged;
        }

        _currentSession = session;

        if (_currentSession is not null)
        {
            _currentSession.MediaPropertiesChanged += OnMediaPropertiesChanged;
            _currentSession.PlaybackInfoChanged += OnPlaybackInfoChanged;
            _ = DebouncedRefreshAsync();
        }
        else
        {
            SetCurrentMediaInfo(MediaInfo.Empty);
        }
    }

    // ────────────────────────────────────────────
    //  Metadata reading
    // ────────────────────────────────────────────

    private void OnMediaPropertiesChanged(
        GlobalSystemMediaTransportControlsSession sender, MediaPropertiesChangedEventArgs args)
    {
        _ = DebouncedRefreshAsync();
    }

    private void OnPlaybackInfoChanged(
        GlobalSystemMediaTransportControlsSession sender, PlaybackInfoChangedEventArgs args)
    {
        _ = DebouncedRefreshAsync();
    }

    /// <summary>
    /// Debounces rapid successive events (GSMTC often fires multiple events for a single track change).
    /// Waits 150 ms; if another event arrives in that window the previous read is cancelled.
    /// </summary>
    private async Task DebouncedRefreshAsync()
    {
        _debounceCts?.Cancel();
        var cts = new CancellationTokenSource();
        _debounceCts = cts;

        try
        {
            await Task.Delay(150, cts.Token);
            await RefreshMediaInfoAsync();
        }
        catch (TaskCanceledException)
        {
            // A newer event superseded this one — expected.
        }
    }

    private async Task RefreshMediaInfoAsync()
    {
        var session = _currentSession;
        if (session is null)
        {
            SetCurrentMediaInfo(MediaInfo.Empty);
            return;
        }

        try
        {
            var props = await session.TryGetMediaPropertiesAsync();
            var playback = session.GetPlaybackInfo();
            var timeline = session.GetTimelineProperties();

            var title = props?.Title ?? string.Empty;
            var artist = props?.Artist ?? string.Empty;
            var isPlaying = playback?.PlaybackStatus ==
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;

            CalculateTimeline(timeline, isPlaying, out var position, out var duration);

            BitmapImage? albumArt = null;
            if (props?.Thumbnail is not null)
            {
                albumArt = await LoadThumbnailAsync(props.Thumbnail);
            }

            var info = new MediaInfo(title, artist, albumArt, isPlaying, position, duration);
            SetCurrentMediaInfo(info);
        }
        catch (Exception ex) when (ex is System.Runtime.InteropServices.COMException
                                       or InvalidOperationException
                                       or UnauthorizedAccessException)
        {
            // Session disappeared while we were reading — treat as "no media".
            Debug.WriteLine($"[MediaSessionService] Read failed: {ex.Message}");
            SetCurrentMediaInfo(MediaInfo.Empty);
        }
    }

    /// <summary>
    /// Converts a WinRT <see cref="IRandomAccessStreamReference"/> thumbnail into
    /// a frozen WPF <see cref="BitmapImage"/> that's safe to use from the UI thread.
    /// </summary>
    private static async Task<BitmapImage?> LoadThumbnailAsync(IRandomAccessStreamReference thumbRef)
    {
        try
        {
            using var winrtStream = await thumbRef.OpenReadAsync();
            using var dotnetStream = winrtStream.AsStreamForRead();

            // Copy into a MemoryStream so we can seek (BitmapImage requires it).
            var memoryStream = new MemoryStream();
            await dotnetStream.CopyToAsync(memoryStream);
            memoryStream.Position = 0;

            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.StreamSource = memoryStream;
            bitmap.DecodePixelWidth = 120; // Limit decode size for performance.
            bitmap.EndInit();
            bitmap.Freeze(); // Required for cross-thread access.

            return bitmap;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[MediaSessionService] Thumbnail load failed: {ex.Message}");
            return null;
        }
    }

    // ────────────────────────────────────────────
    //  Timeline polling (for progress bar)
    // ────────────────────────────────────────────

    /// <summary>
    /// Reads the current timeline properties (position and duration).
    /// Called by the UI timer to update the progress bar.
    /// </summary>
    public MediaInfo? GetCurrentTimelineSnapshot()
    {
        var session = _currentSession;
        if (session is null) return null;

        try
        {
            var playback = session.GetPlaybackInfo();
            var timeline = session.GetTimelineProperties();
            var isPlaying = playback?.PlaybackStatus ==
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;

            CalculateTimeline(timeline, isPlaying, out var position, out var duration);

            // Return a partial snapshot — only timeline fields are current.
            // Title/Artist/AlbumArt are NOT refreshed here (too expensive).
            return new MediaInfo(string.Empty, string.Empty, null, isPlaying, position, duration);
        }
        catch
        {
            return null;
        }
    }

    private static void CalculateTimeline(
        GlobalSystemMediaTransportControlsSessionTimelineProperties? timeline,
        bool isPlaying,
        out TimeSpan position,
        out TimeSpan duration)
    {
        duration = timeline?.EndTime ?? TimeSpan.Zero;
        position = timeline?.Position ?? TimeSpan.Zero;

        if (isPlaying && timeline is not null && timeline.LastUpdatedTime != default)
        {
            var elapsed = DateTimeOffset.UtcNow - timeline.LastUpdatedTime;
            if (elapsed > TimeSpan.Zero)
            {
                position += elapsed;
                if (duration > TimeSpan.Zero && position > duration)
                {
                    position = duration;
                }
            }
        }
    }

    // ────────────────────────────────────────────
    //  Playback commands
    // ────────────────────────────────────────────

    public async Task PlayPauseAsync()
    {
        try
        {
            var session = _currentSession;
            if (session is not null)
            {
                await session.TryTogglePlayPauseAsync();
            }
        }
        catch { /* Session may have closed — ignore. */ }
    }

    public async Task NextAsync()
    {
        try
        {
            var session = _currentSession;
            if (session is not null)
            {
                await session.TrySkipNextAsync();
            }
        }
        catch { /* ignore */ }
    }

    public async Task PreviousAsync()
    {
        try
        {
            var session = _currentSession;
            if (session is not null)
            {
                await session.TrySkipPreviousAsync();
            }
        }
        catch { /* ignore */ }
    }

    public async Task SeekAsync(TimeSpan position)
    {
        try
        {
            var session = _currentSession;
            if (session is not null)
            {
                await session.TryChangePlaybackPositionAsync(position.Ticks);
            }
        }
        catch { /* ignore */ }
    }

    // ────────────────────────────────────────────
    //  Disposal
    // ────────────────────────────────────────────

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _debounceCts?.Cancel();

        if (_currentSession is not null)
        {
            _currentSession.MediaPropertiesChanged -= OnMediaPropertiesChanged;
            _currentSession.PlaybackInfoChanged -= OnPlaybackInfoChanged;
        }

        if (_manager is not null)
        {
            _manager.CurrentSessionChanged -= OnCurrentSessionChanged;
        }

        GC.SuppressFinalize(this);
    }
}
