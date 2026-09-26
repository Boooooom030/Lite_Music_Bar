using System.IO;
using System.Text;
using System.Text.Json;

namespace MusicBar.Settings;

public static class SettingsStore
{
    public static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "MusicBar", "settings.json");

    public static DateTime FileStamp =>
        File.Exists(FilePath) ? File.GetLastWriteTimeUtc(FilePath) : DateTime.MinValue;

    public static bool TryLoad(out SettingsModel settings, out string? error)
    {
        settings = new SettingsModel();
        error = null;
        if (!File.Exists(FilePath)) return true;

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(FilePath, Encoding.UTF8));
            if (!document.RootElement.TryGetProperty("version", out var version) ||
                version.ValueKind != JsonValueKind.Number ||
                version.GetInt32() != 1)
            {
                error = "设置文件版本不受支持。";
                return false;
            }

            var loaded = document.RootElement.Deserialize<SettingsModel>();
            if (loaded is null ||
                loaded.LyricsSource is not ("auto" or "netease" or "qq" or "kugou" or "soda" or "lrclib") ||
                loaded.FftCaptureMode is not ("player" or "system"))
            {
                error = "设置文件包含未知选项。";
                return false;
            }

            settings = loaded;
            return true;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or JsonException or FormatException)
        {
            error = "读取设置失败：" + exception.Message;
            return false;
        }
    }

    public static void Save(SettingsModel settings)
    {
        var directory = Path.GetDirectoryName(FilePath)!;
        Directory.CreateDirectory(directory);
        var temporaryPath = FilePath + "." + Guid.NewGuid().ToString("N") + ".tmp";

        try
        {
            var json = JsonSerializer.Serialize(settings,
                new JsonSerializerOptions { WriteIndented = true }) + "\n";
            var bytes = new UTF8Encoding(false).GetBytes(json);
            using (var stream = new FileStream(temporaryPath, FileMode.CreateNew,
                FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }
            File.Move(temporaryPath, FilePath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
        }
    }
}
