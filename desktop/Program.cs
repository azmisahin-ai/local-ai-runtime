using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace LocalAIRuntime.Desktop;

internal static class Program
{
    private const string MutexName = @"Local\LocalAIRuntimeDesktopApp";
    internal const string WindowTitle = "Local AI Runtime";

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr FindWindow(string? className, string? windowName);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    private static extern bool ShowWindowAsync(IntPtr window, int command);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetForegroundWindow(IntPtr window);

    private const int RestoreWindow = 9;

    private static bool FocusExistingWindow()
    {
        // The form remains alive but hidden while its tray icon is active.
        // Retry briefly because the existing instance may still be starting.
        for (var attempt = 0; attempt < 15; attempt++)
        {
            var window = FindWindow(null, WindowTitle);
            if (window != IntPtr.Zero)
            {
                ShowWindowAsync(window, RestoreWindow);
                // Windows may deny foreground activation when another process
                // currently owns focus. Restoring the window is still useful;
                // avoid showing a misleading duplicate-instance message box.
                SetForegroundWindow(window);
                return true;
            }
            Thread.Sleep(100);
        }
        return false;
    }

    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Any(arg => arg.Equals("--verify-install", StringComparison.OrdinalIgnoreCase)))
        {
            Environment.ExitCode = VerifyInstall() ? 0 : 2;
            return;
        }
        using var singleInstance = new Mutex(initiallyOwned: true, name: MutexName, createdNew: out var isFirstInstance);
        if (!isFirstInstance)
        {
            if (!FocusExistingWindow())
            {
                MessageBox.Show(
                    "Local AI Runtime is already running. Its window may still be starting; use its notification-area icon if it is hidden.",
                    "Local AI Runtime is running",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
            }
            return;
        }

        Application.Run(new DashboardForm());
    }

    private static bool VerifyInstall()
    {
        try
        {
            var root = AppContext.BaseDirectory;
            var required = new[]
            {
                "LocalAIRuntime.Desktop.exe",
                "local-ai-runtime.exe",
                "local-ai-runtime.ico",
                Path.Combine("web", "index.html"),
                Path.Combine("scripts", "Start-LocalAIRuntime.ps1"),
                Path.Combine("scripts", "Apply-LocalAIRuntimeUpdate.ps1"),
                Path.Combine("config", "engines.example.json")
            };
            if (required.Any(file => !File.Exists(Path.Combine(root, file)))) return false;

            using var process = Process.Start(new ProcessStartInfo
            {
                FileName = Path.Combine(root, "local-ai-runtime.exe"),
                WorkingDirectory = root,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                ArgumentList = { "--version" }
            });
            if (process is null || !process.WaitForExit(5000))
            {
                try { process?.Kill(entireProcessTree: true); } catch { }
                return false;
            }
            var output = process.StandardOutput.ReadToEnd().Trim();
            return process.ExitCode == 0 && System.Text.RegularExpressions.Regex.IsMatch(output, @"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$");
        }
        catch
        {
            return false;
        }
    }
}

internal sealed class DashboardForm : Form
{
    private readonly string _installRoot = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
    private readonly string _dataRoot = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LocalAIRuntime");
    private readonly WebView2 _webView = new() { Dock = DockStyle.Fill, Visible = false };
    private readonly Panel _loadingPanel = new() { Dock = DockStyle.Fill, BackColor = Color.FromArgb(17, 25, 37) };
    private readonly Label _loadingTitle = new()
    {
        AutoSize = false,
        Dock = DockStyle.Top,
        Height = 48,
        Text = "Starting Local AI Runtime…",
        TextAlign = ContentAlignment.MiddleCenter,
        ForeColor = Color.White,
        Font = new Font("Segoe UI", 16, FontStyle.Bold)
    };
    private readonly Label _loadingDetail = new()
    {
        AutoSize = false,
        Dock = DockStyle.Top,
        Height = 54,
        Text = "Preparing your local dashboard. Your models and settings stay on this device.",
        TextAlign = ContentAlignment.TopCenter,
        ForeColor = Color.FromArgb(192, 204, 220),
        Font = new Font("Segoe UI", 10, FontStyle.Regular)
    };
    private readonly ProgressBar _progress = new()
    {
        Style = ProgressBarStyle.Marquee,
        MarqueeAnimationSpeed = 25,
        Width = 280,
        Height = 18,
        Anchor = AnchorStyles.None
    };
    private readonly NotifyIcon _tray;
    private Uri? _dashboardUri;
    private bool _allowClose;
    private bool _startupFailed;
    private bool _isExiting;

    public DashboardForm()
    {
        Text = Program.WindowTitle;
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(960, 640);
        Size = new Size(1420, 920);
        BackColor = Color.FromArgb(17, 25, 37);
        ShowInTaskbar = true;

        var iconPath = Path.Combine(_installRoot, "local-ai-runtime.ico");
        if (File.Exists(iconPath)) Icon = new Icon(iconPath);

        var loadingLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 4,
            BackColor = _loadingPanel.BackColor,
            Padding = new Padding(24)
        };
        loadingLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        loadingLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        loadingLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        loadingLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));
        loadingLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        loadingLayout.Controls.Add(new Panel(), 0, 0);
        loadingLayout.Controls.Add(_loadingTitle, 0, 1);
        loadingLayout.Controls.Add(_loadingDetail, 0, 2);
        var progressPanel = new Panel { Dock = DockStyle.Fill };
        _progress.Location = new Point(0, 8);
        progressPanel.Controls.Add(_progress);
        progressPanel.Resize += (_, _) => _progress.Left = Math.Max(0, (progressPanel.ClientSize.Width - _progress.Width) / 2);
        loadingLayout.Controls.Add(progressPanel, 0, 3);
        _loadingPanel.Controls.Add(loadingLayout);

        Controls.Add(_webView);
        Controls.Add(_loadingPanel);

        var menu = new ContextMenuStrip();
        menu.Items.Add("Open Local AI Runtime", null, (_, _) => ShowDashboard());
        menu.Items.Add("Check for updates", null, async (_, _) => await CheckForUpdatesAsync());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit and stop local engines", null, async (_, _) => await ExitAndStopAsync());
        _tray = new NotifyIcon
        {
            Text = "Local AI Runtime is running locally",
            Icon = Icon ?? SystemIcons.Application,
            ContextMenuStrip = menu,
            Visible = true
        };
        _tray.DoubleClick += (_, _) => ShowDashboard();

        Shown += async (_, _) => await StartDesktopAsync();
        FormClosing += OnFormClosing;
    }

    private async Task StartDesktopAsync()
    {
        try
        {
            _loadingTitle.Text = "Preparing the desktop window…";
            await InitializeWebViewAsync();
            _loadingTitle.Text = "Starting your local runtime…";
            _loadingDetail.Text = "This can take a few seconds. No terminal window is needed.";
            _dashboardUri = await EnsureRuntimeAsync();
            _webView.CoreWebView2.Navigate(_dashboardUri.ToString());
        }
        catch (Exception ex)
        {
            _startupFailed = true;
            _allowClose = true;
            var logPath = Path.Combine(_dataRoot, "logs", "runtime.log");
            MessageBox.Show(
                $"Local AI Runtime could not start. No unrelated process was stopped.\n\n{ex.Message}\n\nIf the problem continues, check:\n{logPath}",
                "Local AI Runtime could not start",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            Close();
        }
    }

    private async Task InitializeWebViewAsync()
    {
        var webViewData = Path.Combine(_dataRoot, "WebView2");
        Directory.CreateDirectory(webViewData);
        var environment = await CoreWebView2Environment.CreateAsync(
            browserExecutableFolder: null,
            userDataFolder: webViewData);
        await _webView.EnsureCoreWebView2Async(environment);

        var core = _webView.CoreWebView2;
        core.Settings.IsStatusBarEnabled = false;
        core.Settings.AreDevToolsEnabled = false;
        core.Settings.IsPasswordAutosaveEnabled = false;
        core.Settings.IsGeneralAutofillEnabled = false;
        core.NavigationStarting += (_, e) =>
        {
            if (IsLocalDashboardUri(e.Uri)) return;
            e.Cancel = true;
            OpenExternalUri(e.Uri);
        };
        core.NewWindowRequested += (_, e) =>
        {
            e.Handled = true;
            if (Uri.TryCreate(e.Uri, UriKind.Absolute, out var uri))
            {
                if (IsLocalDashboardUri(uri)) core.Navigate(uri.ToString());
                else OpenExternalUri(uri.ToString());
            }
        };
        core.NavigationCompleted += (_, e) =>
        {
            if (e.IsSuccess)
            {
                _loadingPanel.Visible = false;
                _webView.Visible = true;
                _webView.BringToFront();
            }
            else
            {
                _webView.Visible = false;
                _loadingPanel.Visible = true;
                _loadingTitle.Text = "The dashboard could not be loaded";
                _loadingDetail.Text = $"WebView2 reported {e.WebErrorStatus}. Close and reopen Local AI Runtime to retry.";
            }
        };
        core.ProcessFailed += (_, e) =>
        {
            _loadingPanel.Visible = true;
            _webView.Visible = false;
            _loadingTitle.Text = "The dashboard needs to restart";
            _loadingDetail.Text = $"The embedded browser reported {e.ProcessFailedKind}. Close and reopen Local AI Runtime to recover.";
        };
    }

    private async Task<Uri> EnsureRuntimeAsync()
    {
        var launcher = Path.Combine(_installRoot, "scripts", "Start-LocalAIRuntime.ps1");
        var runtimeExe = Path.Combine(_installRoot, "local-ai-runtime.exe");
        var configPath = Path.Combine(_dataRoot, "config", "engines.json");
        if (!File.Exists(launcher) || !File.Exists(runtimeExe))
            throw new FileNotFoundException("The installed runtime files are incomplete. Reinstall Local AI Runtime from the official project website.");

        var powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
            @"WindowsPowerShell\v1.0\powershell.exe");
        var startInfo = new ProcessStartInfo
        {
            FileName = powershell,
            WorkingDirectory = _installRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (var arg in new[]
        {
            "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", launcher,
            "-NoBrowser", "-NoPrompts"
        }) startInfo.ArgumentList.Add(arg);

        using var process = Process.Start(startInfo) ?? throw new InvalidOperationException("Windows could not start the internal runtime launcher.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        try
        {
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(45));
        }
        catch (TimeoutException)
        {
            try { process.Kill(entireProcessTree: true); } catch { }
            throw new TimeoutException("Runtime startup took longer than 45 seconds. No unrelated program was stopped. Check the runtime log and try again.");
        }
        var output = (await stdout + Environment.NewLine + await stderr).Trim();
        if (process.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(output)
                ? $"The internal launcher exited with code {process.ExitCode}."
                : output);

        if (!File.Exists(configPath)) throw new FileNotFoundException("The user settings were not created by the launcher.");
        using var config = JsonDocument.Parse(await File.ReadAllTextAsync(configPath));
        var port = config.RootElement.GetProperty("server").GetProperty("port").GetInt32();
        if (port is < 1 or > 65535) throw new InvalidDataException("The configured local port is outside the valid TCP range.");
        var url = new Uri($"http://127.0.0.1:{port}/");

        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(2) };
        for (var attempt = 0; attempt < 20; attempt++)
        {
            try
            {
                using var response = await client.GetAsync(new Uri(url, "health"));
                if (response.IsSuccessStatusCode)
                {
                    using var doc = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                    if (doc.RootElement.TryGetProperty("status", out var status) && status.GetString() == "ok") return url;
                }
            }
            catch (HttpRequestException) { }
            catch (TaskCanceledException) { }
            await Task.Delay(250);
        }
        throw new TimeoutException("The local runtime did not become healthy after the launcher completed. The dashboard was not opened.");
    }

    private bool IsLocalDashboardUri(string value)
    {
        return Uri.TryCreate(value, UriKind.Absolute, out var uri) && IsLocalDashboardUri(uri);
    }

    private bool IsLocalDashboardUri(Uri uri)
    {
        return _dashboardUri is not null &&
            uri.Scheme.Equals(Uri.UriSchemeHttp, StringComparison.OrdinalIgnoreCase) &&
            uri.Host.Equals("127.0.0.1", StringComparison.OrdinalIgnoreCase) &&
            uri.Port == _dashboardUri.Port;
    }

    private static void OpenExternalUri(string uriText)
    {
        if (!Uri.TryCreate(uriText, UriKind.Absolute, out var uri) ||
            (uri.Scheme != Uri.UriSchemeHttps && uri.Scheme != Uri.UriSchemeHttp)) return;
        try { Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true }); }
        catch (Exception ex)
        {
            MessageBox.Show($"Could not open the link in your default browser.\n\n{ex.Message}", "Open link", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    private void ShowDashboard()
    {
        Show();
        WindowState = FormWindowState.Normal;
        ShowInTaskbar = true;
        Activate();
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
    }

    private async Task CheckForUpdatesAsync()
    {
        ShowDashboard();
        try
        {
            if (_webView.CoreWebView2 is not null)
                await _webView.CoreWebView2.ExecuteScriptAsync("document.getElementById('check-updates')?.click();");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Open the dashboard and use Check for updates.\n\n{ex.Message}", "Check for updates", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private async Task ExitAndStopAsync()
    {
        if (_isExiting) return;
        _isExiting = true;
        try
        {
            if (_dashboardUri is not null)
            {
                using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
                using var body = new StringContent("{}", System.Text.Encoding.UTF8, "application/json");
                using var response = await client.PostAsync(new Uri(_dashboardUri, "v1/runtime/shutdown"), body);
                if (!response.IsSuccessStatusCode)
                    throw new HttpRequestException($"The runtime returned HTTP {(int)response.StatusCode} to the shutdown request.");
                await Task.Delay(350);
            }
            _allowClose = true;
            Close();
        }
        catch (Exception ex)
        {
            _isExiting = false;
            var answer = MessageBox.Show(
                $"The runtime did not confirm a graceful stop. Local engines may still be running.\n\n{ex.Message}\n\nKeep the app open so you can retry?",
                "Runtime did not stop",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning);
            if (answer == DialogResult.No)
            {
                _allowClose = true;
                Close();
            }
        }
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_allowClose || _startupFailed) return;
        if (e.CloseReason == CloseReason.WindowsShutDown || e.CloseReason == CloseReason.TaskManagerClosing)
        {
            try
            {
                if (_dashboardUri is not null)
                    _ = new HttpClient().PostAsync(new Uri(_dashboardUri, "v1/runtime/shutdown"),
                        new StringContent("{}", System.Text.Encoding.UTF8, "application/json"));
            }
            catch { }
            _allowClose = true;
            return;
        }

        e.Cancel = true;
        Hide();
        ShowInTaskbar = false;
        _tray.Visible = true;
        _tray.BalloonTipTitle = "Local AI Runtime is still running";
        _tray.BalloonTipText = "It is safely minimized to the notification area. Choose ‘Exit and stop local engines’ to stop it completely.";
        _tray.ShowBalloonTip(3500);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _tray.Visible = false;
        _tray.Dispose();
        base.OnFormClosed(e);
    }
}
