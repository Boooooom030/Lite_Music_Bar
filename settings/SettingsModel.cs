using System.Text.Json.Serialization;

namespace MusicBar.Settings;

public sealed class SettingsModel
{
    [JsonPropertyName("version")]
    public int Version { get; set; } = 1;

    [JsonPropertyName("showLyrics")]
    public bool ShowLyrics { get; set; } = true;

    [JsonPropertyName("positionLocked")]
    public bool PositionLocked { get; set; }

    [JsonPropertyName("lyricsSource")]
    public string LyricsSource { get; set; } = "auto";

    [JsonPropertyName("fftCaptureMode")]
    public string FftCaptureMode { get; set; } = "player";
}
