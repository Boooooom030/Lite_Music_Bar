using System.Windows.Media.Imaging;

namespace MusicBar.Models;

/// <summary>
/// Immutable snapshot of the currently playing media's metadata.
/// Replaced wholesale on each update from the media session service.
/// </summary>
public record MediaInfo(
    string Title,
    string Artist,
    BitmapImage? AlbumArt,
    bool IsPlaying,
    TimeSpan Position,
    TimeSpan Duration)
{
    /// <summary>Sentinel value representing "no media is playing".</summary>
    public static MediaInfo Empty { get; } =
        new(string.Empty, string.Empty, null, false, TimeSpan.Zero, TimeSpan.Zero);

    /// <summary>True when a media session with meaningful metadata is active.</summary>
    public bool HasMedia => !string.IsNullOrEmpty(Title);
}
