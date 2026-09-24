using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace MusicBar.Services;

/// <summary>
/// Taskbar docking service: embeds the music bar window directly into
/// the Windows Taskbar (Shell_TrayWnd) as a native child window.
/// Ported and refined from AF-Media-Bar / FluentFlyout architecture.
/// </summary>
public class TaskbarDockService
{
    private const int GWL_STYLE = -16;
    private const int GWL_EXSTYLE = -20;
    private const int WS_POPUP = unchecked((int)0x80000000);
    private const int WS_CHILD = 0x40000000;
    private const int WS_VISIBLE = 0x10000000;
    private const int WS_EX_TOOLWINDOW = 0x00000080;

    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOMOVE = 0x0002;
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_SHOWWINDOW = 0x0040;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
        public int Width => Right - Left;
        public int Height => Bottom - Top;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll", EntryPoint = "FindWindowW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr FindWindow(string lpClassName, string? lpWindowName);

    [DllImport("user32.dll", EntryPoint = "FindWindowExW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr FindWindowEx(IntPtr parentHandle, IntPtr childAfter, string? className, string? windowName);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetParent(IntPtr hWndChild, IntPtr hWndNewParent);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    private static extern bool ScreenToClient(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateRectRgn(int x1, int y1, int x2, int y2);

    [DllImport("user32.dll")]
    private static extern bool SetWindowRgn(IntPtr hWnd, IntPtr hRgn, bool bRedraw);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public IntPtr TaskbarHandle { get; private set; }
    public bool IsDocked { get; private set; }
    public double DpiScale { get; private set; } = 1.0;
    public RECT TaskbarRect { get; private set; }

    /// <summary>
    /// Finds the main taskbar handle (Shell_TrayWnd).
    /// </summary>
    public IntPtr GetTaskbarHandle(bool forceRefresh = false)
    {
        if (!forceRefresh && TaskbarHandle != IntPtr.Zero) return TaskbarHandle;
        TaskbarHandle = FindWindow("Shell_TrayWnd", null);
        return TaskbarHandle;
    }

    /// <summary>
    /// Updates the DPI scaling factor and taskbar bounds.
    /// </summary>
    public bool RefreshTaskbarInfo()
    {
        var tb = GetTaskbarHandle(true);
        if (tb == IntPtr.Zero) return false;

        if (!GetWindowRect(tb, out RECT tr)) return false;
        TaskbarRect = tr;

        try
        {
            uint dpi = GetDpiForWindow(tb);
            DpiScale = dpi > 0 ? dpi / 96.0 : 1.0;
        }
        catch
        {
            DpiScale = 1.0;
        }

        return true;
    }

    /// <summary>
    /// Docks the window into the Windows Taskbar as a child window,
    /// sized to match the entire taskbar canvas.
    /// </summary>
    public bool Dock(Window window)
    {
        if (!RefreshTaskbarInfo())
        {
            Debug.WriteLine("[TaskbarDockService] Shell_TrayWnd not found.");
            return false;
        }

        var tb = TaskbarHandle;
        var helper = new WindowInteropHelper(window);
        var hWnd = helper.Handle;
        if (hWnd == IntPtr.Zero)
        {
            helper.EnsureHandle();
            hWnd = helper.Handle;
        }

        if (hWnd == IntPtr.Zero) return false;

        // 1. Size the WPF window to cover the entire taskbar
        window.Width = TaskbarRect.Width / DpiScale;
        window.Height = TaskbarRect.Height / DpiScale;

        // 2. Modify window style to WS_CHILD
        int style = GetWindowLong(hWnd, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLong(hWnd, GWL_STYLE, style);

        // 3. Make child of taskbar
        SetParent(hWnd, tb);

        // 4. Position window covering the taskbar (0, 0, width, height)
        SetWindowPos(hWnd, IntPtr.Zero, 0, 0, TaskbarRect.Width, TaskbarRect.Height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        IsDocked = true;
        Debug.WriteLine($"[TaskbarDockService] Docked window to Shell_TrayWnd ({TaskbarRect.Width}x{TaskbarRect.Height})");
        return true;
    }

    /// <summary>
    /// Calculates the default placement for the 280x36 bar.
    /// Placed to the left of the notification tray area.
    /// </summary>
    public (double LeftDip, double TopDip) CalculateDefaultBarPosition(double barWidthDip = 280, double barHeightDip = 36)
    {
        var tb = GetTaskbarHandle();
        int tbWidthPx = TaskbarRect.Width;
        int tbHeightPx = TaskbarRect.Height;

        double barWidthPx = barWidthDip * DpiScale;
        double barHeightPx = barHeightDip * DpiScale;

        double topDip = ((tbHeightPx / DpiScale) - barHeightDip) / 2.0;

        IntPtr trayNotify = FindWindowEx(tb, IntPtr.Zero, "TrayNotifyWnd", null);
        if (trayNotify != IntPtr.Zero && GetWindowRect(trayNotify, out RECT trayRect))
        {
            POINT pt = new POINT { X = trayRect.Left, Y = trayRect.Top };
            ScreenToClient(tb, ref pt);
            double leftPx = Math.Max(10, pt.X - barWidthPx - (12 * DpiScale));
            return (leftPx / DpiScale, topDip);
        }

        // Fallback: ~250px from right edge
        double fallbackLeftPx = Math.Max(10, tbWidthPx - barWidthPx - (240 * DpiScale));
        return (fallbackLeftPx / DpiScale, topDip);
    }

    /// <summary>
    /// Applies an input region (SetWindowRgn) so that only the media bar rectangle
    /// is visible and clickable. The rest of the taskbar remains transparent and interactive.
    /// </summary>
    public void ApplyBarInputRegion(Window window, double barLeftDip, double barTopDip, double barWidthDip = 280, double barHeightDip = 36)
    {
        var helper = new WindowInteropHelper(window);
        var hWnd = helper.Handle;
        if (hWnd == IntPtr.Zero) return;

        int rLeft = (int)Math.Round(barLeftDip * DpiScale);
        int rTop = (int)Math.Round(barTopDip * DpiScale);
        int rRight = rLeft + (int)Math.Round(barWidthDip * DpiScale);
        int rBottom = rTop + (int)Math.Round(barHeightDip * DpiScale);

        IntPtr rgn = CreateRectRgn(rLeft, rTop, rRight, rBottom);
        SetWindowRgn(hWnd, rgn, true);
    }

    /// <summary>
    /// Hides the input region completely (when media is inactive and auto-hide is on).
    /// </summary>
    public void ClearInputRegion(Window window)
    {
        var helper = new WindowInteropHelper(window);
        var hWnd = helper.Handle;
        if (hWnd == IntPtr.Zero) return;

        IntPtr rgn = CreateRectRgn(0, 0, 0, 0);
        SetWindowRgn(hWnd, rgn, true);
    }

    /// <summary>
    /// Undocks the window from the Taskbar.
    /// </summary>
    public void Undock(Window window)
    {
        if (!IsDocked) return;
        var helper = new WindowInteropHelper(window);
        var hWnd = helper.Handle;
        if (hWnd == IntPtr.Zero) return;

        SetWindowRgn(hWnd, IntPtr.Zero, true);
        SetParent(hWnd, IntPtr.Zero);

        int style = GetWindowLong(hWnd, GWL_STYLE);
        style = (style & ~WS_CHILD) | WS_POPUP | WS_VISIBLE;
        SetWindowLong(hWnd, GWL_STYLE, style);

        IsDocked = false;
        Debug.WriteLine("[TaskbarDockService] Undocked window.");
    }
}
