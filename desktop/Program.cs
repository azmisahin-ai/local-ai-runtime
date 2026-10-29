using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
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

        if (DeferStartupForInterruptedDesktopUpdate()) return;
        Application.Run(new DashboardForm());
    }

    private static bool DeferStartupForInterruptedDesktopUpdate()
    {
        var installRoot = Path.GetFullPath(AppContext.BaseDirectory).TrimEnd(Path.DirectorySeparatorChar);
        var dataRoot = Path.GetFullPath(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LocalAIRuntime"));

        // The updater deliberately marks the process it launches. That process
        // must be allowed to start and satisfy the health check while its parent
        // updater is still alive. Any other launch encountering an incomplete
        // update hands control to the recovery helper before loading the EXE
        // that may need to be restored.
        var activeHelperPid = 0;
        int.TryParse(Environment.GetEnvironmentVariable("LOCAL_AI_RUNTIME_DESKTOP_UPDATE_HELPER_PID"), out activeHelperPid);
        if (activeHelperPid > 0)
        {
            try
            {
                using var helper = Process.GetProcessById(activeHelperPid);
                var expectedStage = Environment.GetEnvironmentVariable("LOCAL_AI_RUNTIME_DESKTOP_UPDATE_STAGE");
                var updatesRoot = Path.Combine(dataRoot, "updates");
                if (helper.ProcessName.Equals("powershell", StringComparison.OrdinalIgnoreCase) &&
                    !string.IsNullOrWhiteSpace(expectedStage) && Directory.Exists(expectedStage) &&
                    Path.GetDirectoryName(Path.GetFullPath(expectedStage))!.Equals(updatesRoot, StringComparison.OrdinalIgnoreCase) &&
                    (File.GetAttributes(expectedStage) & FileAttributes.ReparsePoint) == 0)
                {
                    var statusPath = Path.Combine(expectedStage, "desktop-update-status.json");
                    if (File.Exists(statusPath) && (File.GetAttributes(statusPath) & FileAttributes.ReparsePoint) == 0)
                    {
                        using var statusDocument = JsonDocument.Parse(File.ReadAllText(statusPath));
                        var status = statusDocument.RootElement;
                        var recordedHelperPid = status.TryGetProperty("helper_pid", out var helperElement) &&
                            helperElement.TryGetInt32(out var parsedHelperPid) ? parsedHelperPid : 0;
                        var recordedInstallRoot = status.TryGetProperty("install_root", out var rootElement) ? rootElement.GetString() : null;
                        var recordedDataRoot = status.TryGetProperty("data_root", out var dataElement) ? dataElement.GetString() : null;
                        var recordedState = status.TryGetProperty("state", out var stateElement) ? stateElement.GetString() : null;
                        if (recordedHelperPid == activeHelperPid &&
                            Path.GetFullPath(recordedInstallRoot ?? string.Empty).TrimEnd(Path.DirectorySeparatorChar).Equals(installRoot, StringComparison.OrdinalIgnoreCase) &&
                            Path.GetFullPath(recordedDataRoot ?? string.Empty).TrimEnd(Path.DirectorySeparatorChar).Equals(dataRoot, StringComparison.OrdinalIgnoreCase) &&
                            (recordedState is "waiting_for_shutdown" or "installing" or "awaiting_health" or "rollback_failed"))
                        {
                            return false;
                        }
                    }
                }
            }
            catch { /* A stale or unverified helper marker must not bypass recovery inspection. */ }
        }

        var updatesRoot = Path.Combine(dataRoot, "updates");
        if (!Directory.Exists(updatesRoot)) return false;
        try
        {
            if ((File.GetAttributes(updatesRoot) & FileAttributes.ReparsePoint) != 0)
            {
                MessageBox.Show("The desktop update recovery folder is a symbolic link or junction. For safety, automatic recovery was not started.",
                    WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
                return true;
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show($"The desktop update recovery folder could not be inspected. Automatic recovery was not started.\n\n{ex.Message}",
                WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return true;
        }
        string? selectedStage = null;
        string? selectedState = null;
        DateTimeOffset selectedAt = DateTimeOffset.MinValue;
        DateTime selectedFileTime = DateTime.MinValue;
        foreach (var stage in Directory.EnumerateDirectories(updatesRoot))
        {
            var statusPath = Path.Combine(stage, "desktop-update-status.json");
            if (!File.Exists(statusPath)) continue;
            try
            {
                if ((File.GetAttributes(stage) & FileAttributes.ReparsePoint) != 0 ||
                    (File.GetAttributes(statusPath) & FileAttributes.ReparsePoint) != 0) continue;
                using var document = JsonDocument.Parse(File.ReadAllText(statusPath));
                var status = document.RootElement;
                var recordedRoot = status.TryGetProperty("install_root", out var rootElement) ? rootElement.GetString() : null;
                var recordedDataRoot = status.TryGetProperty("data_root", out var dataRootElement) ? dataRootElement.GetString() : null;
                var recordedTag = status.TryGetProperty("tag_name", out var tagElement) ? tagElement.GetString() : null;
                var previousVersion = status.TryGetProperty("previous_version", out var previousElement) ? previousElement.GetString() : null;
                var targetVersion = status.TryGetProperty("target_version", out var targetElement) ? targetElement.GetString() : null;
                var runtimePort = status.TryGetProperty("runtime_port", out var portElement) && portElement.TryGetInt32(out var port) ? port : 0;
                var state = status.TryGetProperty("state", out var stateElement) ? stateElement.GetString() : null;
                var stageTag = Path.GetFileName(stage);
                if (!Regex.IsMatch(stageTag, @"^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$") ||
                    !string.Equals(recordedTag, stageTag, StringComparison.Ordinal) ||
                    string.IsNullOrWhiteSpace(recordedRoot) || string.IsNullOrWhiteSpace(recordedDataRoot) ||
                    !Path.GetFullPath(recordedRoot).TrimEnd(Path.DirectorySeparatorChar).Equals(installRoot, StringComparison.OrdinalIgnoreCase) ||
                    !Path.GetFullPath(recordedDataRoot).TrimEnd(Path.DirectorySeparatorChar).Equals(dataRoot, StringComparison.OrdinalIgnoreCase) ||
                    state is not ("waiting_for_shutdown" or "installing" or "awaiting_health" or "succeeded" or "rolled_back" or "failed" or "rollback_failed")) continue;
                var needsRecovery = state is "waiting_for_shutdown" or "installing" or "awaiting_health" or "rollback_failed";
                if (needsRecovery &&
                    (!Regex.IsMatch(previousVersion ?? string.Empty, @"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$") ||
                     !Regex.IsMatch(targetVersion ?? string.Empty, @"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$") ||
                     runtimePort is < 1 or > 65535)) continue;
                var timestamp = status.TryGetProperty("generated_utc", out var timeElement) &&
                    DateTimeOffset.TryParse(timeElement.GetString(), out var parsed) ? parsed : File.GetLastWriteTimeUtc(statusPath);
                var fileTime = File.GetLastWriteTimeUtc(statusPath);
                if (selectedStage is null || timestamp > selectedAt || (timestamp == selectedAt && fileTime > selectedFileTime))
                {
                    selectedStage = stage;
                    selectedState = state;
                    selectedAt = timestamp;
                    selectedFileTime = fileTime;
                }
            }
            catch { /* Ignore malformed historical records; never trust their paths. */ }
        }
        // Terminal records are part of ordering. An older interrupted stage
        // must never roll back an installation after a newer update succeeded.
        if (selectedStage is null || selectedState is not ("waiting_for_shutdown" or "installing" or "awaiting_health" or "rollback_failed")) return false;

        try
        {
            var stageAttributes = File.GetAttributes(selectedStage);
            if ((stageAttributes & FileAttributes.ReparsePoint) != 0 ||
                !Path.GetDirectoryName(Path.GetFullPath(selectedStage))!.Equals(updatesRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The selected update stage is not a direct, ordinary directory under the update root.");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"The interrupted desktop update stage could not be trusted. Automatic recovery was not started.\n\n{ex.Message}",
                WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return true;
        }

        var helperScript = Path.Combine(selectedStage, "Apply-LocalAIRuntimeDesktopUpdate.ps1");
        var powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
            @"WindowsPowerShell\v1.0\powershell.exe");
        if (!File.Exists(helperScript) || !File.Exists(powershell) ||
            (File.GetAttributes(helperScript) & FileAttributes.ReparsePoint) != 0)
        {
            MessageBox.Show(
                "An interrupted desktop update needs recovery, but its verified recovery helper is missing. Preserve the update backup under %LOCALAPPDATA%\LocalAIRuntime\updates and reinstall from the official release.",
                WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return true;
        }

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = powershell,
                WorkingDirectory = dataRoot,
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            };
            foreach (var argument in new[]
            {
                "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                "-File", helperScript,
                "-InstallRoot", installRoot,
                "-DataRoot", dataRoot,
                "-StageRoot", selectedStage,
                "-DesktopPid", Environment.ProcessId.ToString(),
                "-RecoverInterrupted"
            }) startInfo.ArgumentList.Add(argument);
            using var recovery = Process.Start(startInfo) ?? throw new InvalidOperationException("Windows could not start the desktop update recovery helper.");
            return true; // The helper waits for this process to exit before restoring files.
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"The interrupted desktop update could not start its recovery helper. Preserve the update backup and inspect desktop-update.log.\n\n{ex.Message}",
                WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return true;
        }
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
                Path.Combine("scripts", "Apply-LocalAIRuntimeDesktopUpdate.ps1"),
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
    private readonly string _dataRoot = Path.GetFullPath(Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LocalAIRuntime"));
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
    private bool _updateCheckInProgress;
    private int _runtimePort;
    private string _runtimeVersion = string.Empty;
    private string _releaseApiUrl = "https://api.github.com/repos/azmisahin-ai/local-ai-runtime/releases?per_page=20";
    private string _desktopInstallerAssetName = "local-ai-runtime-windows-x64-setup.exe";

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
        core.WebMessageReceived += async (_, e) =>
        {
            if (!IsLocalDashboardUri(e.Source)) return;
            if (string.Equals(e.TryGetWebMessageAsString(), "check-for-desktop-updates", StringComparison.Ordinal))
            {
                if (_updateCheckInProgress || _isExiting)
                {
                    try { core.PostWebMessageAsString("desktop-update-check-finished"); } catch { }
                    return;
                }
                await CheckForUpdatesAsync();
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
            "-NoBrowser", "-NoPrompts", "-DataRoot", _dataRoot
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
        _runtimePort = port;
        var url = new Uri($"http://127.0.0.1:{port}/");

        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(2) };
        using (var versionResponse = await client.GetAsync(new Uri(url, "version")))
        {
            versionResponse.EnsureSuccessStatusCode();
            using var versionDocument = JsonDocument.Parse(await versionResponse.Content.ReadAsStringAsync());
            var root = versionDocument.RootElement;
            _runtimeVersion = root.TryGetProperty("version", out var versionElement) ? versionElement.GetString() ?? string.Empty : string.Empty;
            if (root.TryGetProperty("releases_api_url", out var releaseApiElement))
                _releaseApiUrl = releaseApiElement.GetString() ?? _releaseApiUrl;
            if (root.TryGetProperty("desktop_installer_asset_name", out var installerElement))
                _desktopInstallerAssetName = installerElement.GetString() ?? _desktopInstallerAssetName;
        }
        if (string.IsNullOrWhiteSpace(_runtimeVersion)) throw new InvalidDataException("The runtime did not report its installed version.");
        var healthy = false;
        for (var attempt = 0; attempt < 20; attempt++)
        {
            try
            {
                using var response = await client.GetAsync(new Uri(url, "health"));
                if (response.IsSuccessStatusCode)
                {
                    using var doc = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                    if (doc.RootElement.TryGetProperty("status", out var status) && status.GetString() == "ok")
                    {
                        healthy = true;
                        break;
                    }
                }
            }
            catch (HttpRequestException) { }
            catch (TaskCanceledException) { }
            await Task.Delay(250);
        }
        if (!healthy) throw new TimeoutException("The local runtime did not become healthy after the launcher completed. The dashboard was not opened.");

        // Never display a healthy but unrelated/stale runtime instance just
        // because it answered on the configured port. The launcher passes the
        // exact persistent data-root identity to the runtime, so UI/catalog and
        // installed engine/model status must refer to the same user data.
        var instanceIdPath = Path.Combine(_dataRoot, "runtime-instance.id");
        if (!File.Exists(instanceIdPath) || !Guid.TryParse((await File.ReadAllTextAsync(instanceIdPath)).Trim(), out var expectedInstanceId))
            throw new InvalidDataException("The persistent runtime identity is missing or invalid. Your settings were not changed; close any older runtime window and reopen Local AI Runtime.");
        using var identityResponse = await client.GetAsync(new Uri(url, "v1/system/identity"));
        if (!identityResponse.IsSuccessStatusCode)
            throw new InvalidOperationException("The runtime on the selected port cannot confirm which user-data directory it is using. Close older Local AI Runtime instances and reopen this app.");
        using var identityDocument = JsonDocument.Parse(await identityResponse.Content.ReadAsStringAsync());
        var reportedId = identityDocument.RootElement.TryGetProperty("data_root_id", out var idElement) ? idElement.GetString() : null;
        if (!Guid.TryParse(reportedId, out var reportedInstanceId) || reportedInstanceId != expectedInstanceId)
            throw new InvalidOperationException("A different or older Local AI Runtime instance answered on the selected port. No process was stopped. Close the older instance from its notification-area menu and reopen this app.");
        return url;
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
        if (_updateCheckInProgress || _isExiting) return;
        _updateCheckInProgress = true;
        var updateHandedOff = false;
        ShowDashboard();
        try
        {
            if (_runtimePort is < 1 or > 65535 ||
                !Regex.IsMatch(_runtimeVersion, @"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$") ||
                _runtimeVersion.Contains("-dev", StringComparison.OrdinalIgnoreCase))
            {
                SetUpdateProgress("Desktop update unavailable", "This is a development/preview build or the local runtime is still starting. Install a tagged desktop release before using the in-app updater.");
                return;
            }
            SetUpdateProgress("Checking official releases…", "Local inference remains available while update metadata is checked.");
            // Installer assets can be hundreds of MiB on slower links. Bound
            // a stalled/abandoned request without imposing a 30-second total
            // timeout on a legitimate streamed download.
            using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(20) };
            client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            client.DefaultRequestHeaders.UserAgent.ParseAdd("LocalAIRuntime-Desktop-Updater/1.0");
            using var releaseResponse = await client.GetAsync(_releaseApiUrl);
            releaseResponse.EnsureSuccessStatusCode();
            using var releaseDocument = JsonDocument.Parse(await releaseResponse.Content.ReadAsStringAsync());
            if (releaseDocument.RootElement.ValueKind != JsonValueKind.Array)
                throw new InvalidDataException("The official release service returned an unexpected response.");

            var installedIsPrerelease = _runtimeVersion.Contains('-');
            JsonElement? latest = null;
            foreach (var release in releaseDocument.RootElement.EnumerateArray())
            {
                if (release.TryGetProperty("draft", out var draft) && draft.GetBoolean()) continue;
                var prerelease = release.TryGetProperty("prerelease", out var pre) && pre.GetBoolean();
                if (!installedIsPrerelease && prerelease) continue;
                var tag = release.TryGetProperty("tag_name", out var tagElement) ? tagElement.GetString() : null;
                if (string.IsNullOrWhiteSpace(tag) || CompareReleaseTags(tag, _runtimeVersion) <= 0) continue;
                if (latest is null || CompareReleaseTags(tag, latest.Value.GetProperty("tag_name").GetString() ?? "") > 0)
                    latest = release;
            }

            if (latest is null)
            {
                SetUpdateProgress("You’re up to date", $"This desktop installation is running {_runtimeVersion}. No newer release is available in the selected channel.");
                MessageBox.Show($"Local AI Runtime {_runtimeVersion} is up to date.", WindowTitle,
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            var latestRelease = latest.Value;
            var tagName = latestRelease.GetProperty("tag_name").GetString() ?? throw new InvalidDataException("The release tag is missing.");
            var releaseUrl = latestRelease.TryGetProperty("html_url", out var html) ? html.GetString() : null;
            if (!Regex.IsMatch(tagName, @"^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$") ||
                releaseUrl != $"https://github.com/azmisahin-ai/local-ai-runtime/releases/tag/{tagName}")
                throw new InvalidDataException("The official release tag or release URL did not match the expected repository.");
            JsonElement? installerAsset = null;
            JsonElement? checksumAsset = null;
            if (latestRelease.TryGetProperty("assets", out var assets) && assets.ValueKind == JsonValueKind.Array)
            {
                foreach (var asset in assets.EnumerateArray())
                {
                    if (!asset.TryGetProperty("name", out var name)) continue;
                    var assetName = name.GetString();
                    if (string.Equals(assetName, _desktopInstallerAssetName, StringComparison.Ordinal)) installerAsset = asset;
                    else if (string.Equals(assetName, "SHA256SUMS.txt", StringComparison.Ordinal)) checksumAsset = asset;
                }
            }
            if (installerAsset is null)
            {
                var answer = MessageBox.Show(
                    $"Release {tagName} is available, but it does not contain the official Windows desktop installer. Open the release page?",
                    WindowTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Information);
                if (answer == DialogResult.Yes && !string.IsNullOrWhiteSpace(releaseUrl)) OpenExternalUri(releaseUrl);
                return;
            }

            var assetInfo = installerAsset.Value;
            var digest = assetInfo.TryGetProperty("digest", out var digestElement) ? digestElement.GetString() ?? "" : "";
            var assetUrl = assetInfo.TryGetProperty("browser_download_url", out var urlElement) ? urlElement.GetString() ?? "" : "";
            var assetName = assetInfo.TryGetProperty("name", out var assetNameElement) ? assetNameElement.GetString() ?? "" : "";
            var size = assetInfo.TryGetProperty("size", out var sizeElement) ? sizeElement.GetInt64() : 0;
            var expectedUrl = $"https://github.com/azmisahin-ai/local-ai-runtime/releases/download/{tagName}/{_desktopInstallerAssetName}";
            if (assetName != _desktopInstallerAssetName || assetUrl != expectedUrl || size < 1024 || size > 512L * 1024 * 1024)
            {
                var answer = MessageBox.Show(
                    $"Release {tagName} is available, but its installer does not have the expected official URL, asset name or supported size. Open the official release page to review it manually?",
                    WindowTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
                if (answer == DialogResult.Yes && !string.IsNullOrWhiteSpace(releaseUrl)) OpenExternalUri(releaseUrl);
                return;
            }
            if (!Regex.IsMatch(digest, "^sha256:[a-fA-F0-9]{64}$"))
            {
                // GitHub's release asset `digest` field may be absent on older
                // or imported releases. The project's own checksum manifest is
                // still bound to the exact official tag URL and asset filename.
                if (checksumAsset is null)
                    throw new InvalidDataException("The release has neither a GitHub SHA-256 digest nor the official SHA256SUMS.txt asset.");
                var checksum = checksumAsset.Value;
                var checksumName = checksum.TryGetProperty("name", out var checksumNameElement) ? checksumNameElement.GetString() ?? "" : "";
                var checksumUrl = checksum.TryGetProperty("browser_download_url", out var checksumUrlElement) ? checksumUrlElement.GetString() ?? "" : "";
                var checksumSize = checksum.TryGetProperty("size", out var checksumSizeElement) ? checksumSizeElement.GetInt64() : 0;
                var expectedChecksumUrl = $"https://github.com/azmisahin-ai/local-ai-runtime/releases/download/{tagName}/SHA256SUMS.txt";
                if (checksumName != "SHA256SUMS.txt" || checksumUrl != expectedChecksumUrl || checksumSize is < 1 or > 65536)
                    throw new InvalidDataException("The official SHA256SUMS.txt release asset failed URL/name/size validation.");
                using var checksumResponse = await client.GetAsync(checksumUrl, HttpCompletionOption.ResponseHeadersRead);
                checksumResponse.EnsureSuccessStatusCode();
                var checksumText = await ReadBoundedTextAsync(checksumResponse.Content, 65536);
                var checksumMatch = Regex.Match(checksumText,
                    @"(?m)^([a-fA-F0-9]{64})[ \t]+\*?" + Regex.Escape(_desktopInstallerAssetName) + @"[ \t]*$");
                if (!checksumMatch.Success)
                    throw new InvalidDataException("SHA256SUMS.txt does not contain a checksum entry for the Windows desktop installer.");
                digest = "sha256:" + checksumMatch.Groups[1].Value;
            }

            var sizeMiB = size / 1024d / 1024d;
            var confirmation = MessageBox.Show(
                $"Local AI Runtime {_runtimeVersion} can be updated to {tagName}.\n\nThe complete Windows desktop installer ({sizeMiB:F1} MiB) will be downloaded and verified against GitHub’s SHA-256 digest. The desktop window and local engines will close briefly. Settings, engines, downloaded models and logs are stored separately and will be preserved. A full application-folder backup will be retained for rollback.\n\nInstall this update now?",
                WindowTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Information);
            if (confirmation != DialogResult.Yes)
            {
                SetUpdateProgress("Update postponed", $"{tagName} remains available from the official release page.");
                return;
            }

            var updatesRoot = Path.GetFullPath(Path.Combine(_dataRoot, "updates"));
            var stageRoot = Path.GetFullPath(Path.Combine(updatesRoot, tagName));
            Directory.CreateDirectory(updatesRoot);
            Directory.CreateDirectory(stageRoot);
            foreach (var directory in new[] { updatesRoot, stageRoot })
            {
                if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException($"The desktop update folder cannot be a symbolic link or junction: {directory}");
            }
            var installerPath = Path.Combine(stageRoot, _desktopInstallerAssetName);
            var partialPath = installerPath + ".part";
            var manifestPath = Path.Combine(stageRoot, "desktop-update-manifest.json");
            var helperPath = Path.Combine(stageRoot, "Apply-LocalAIRuntimeDesktopUpdate.ps1");
            foreach (var existingPath in new[] { installerPath, partialPath, manifestPath, helperPath })
            {
                if (File.Exists(existingPath) && (File.GetAttributes(existingPath) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException($"A staged desktop update file is a symbolic link or junction: {existingPath}");
            }
            var expectedHash = digest.Substring("sha256:".Length).ToLowerInvariant();
            var alreadyStaged = File.Exists(installerPath) && new FileInfo(installerPath).Length == size &&
                string.Equals(await ComputeSha256Async(installerPath), expectedHash, StringComparison.Ordinal);
            if (!alreadyStaged)
            {
                if (File.Exists(partialPath)) File.Delete(partialPath);
                SetUpdateProgress($"Downloading {tagName}…", $"0 of {sizeMiB:F1} MiB downloaded. The installed application has not been changed.");
                using var downloadTimeout = new CancellationTokenSource(TimeSpan.FromMinutes(20));
                using var downloadResponse = await client.GetAsync(assetUrl, HttpCompletionOption.ResponseHeadersRead, downloadTimeout.Token);
                downloadResponse.EnsureSuccessStatusCode();
                await using var input = await downloadResponse.Content.ReadAsStreamAsync(downloadTimeout.Token);
                await using var output = new FileStream(partialPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 128, useAsync: true);
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                var buffer = new byte[1024 * 128];
                long downloaded = 0;
                int read;
                while ((read = await input.ReadAsync(buffer.AsMemory(0, buffer.Length), downloadTimeout.Token)) > 0)
                {
                    downloaded += read;
                    if (downloaded > size || downloaded > 512L * 1024 * 1024)
                        throw new InvalidDataException("The downloaded installer exceeded the size advertised by GitHub.");
                    hash.AppendData(buffer, 0, read);
                    await output.WriteAsync(buffer.AsMemory(0, read), downloadTimeout.Token);
                    if (downloaded == size || downloaded % (1024 * 1024) < buffer.Length)
                        SetUpdateProgress($"Downloading {tagName}…", $"{downloaded / 1024d / 1024d:F1} of {sizeMiB:F1} MiB downloaded. The installed application has not been changed.");
                }
                await output.FlushAsync(downloadTimeout.Token);
                if (downloaded != size) throw new InvalidDataException($"The installer size was {downloaded} bytes; GitHub advertised {size} bytes.");
                var actualHash = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
                if (!string.Equals(actualHash, expectedHash, StringComparison.Ordinal))
                    throw new InvalidDataException("The downloaded installer SHA-256 does not match GitHub release metadata.");
                output.Close();
                File.Move(partialPath, installerPath, overwrite: true);
            }

            var manifest = new
            {
                verified = true,
                tag_name = tagName,
                asset_name = _desktopInstallerAssetName,
                size_bytes = size,
                sha256 = expectedHash,
                release_url = releaseUrl
            };
            await File.WriteAllTextAsync(manifestPath, JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));

            var sourceHelper = Path.Combine(_installRoot, "scripts", "Apply-LocalAIRuntimeDesktopUpdate.ps1");
            if (!File.Exists(sourceHelper) || (File.GetAttributes(sourceHelper) & FileAttributes.ReparsePoint) != 0)
                throw new FileNotFoundException("The desktop update helper is missing or not a regular installed file. Reinstall from the latest official setup package.", sourceHelper);
            File.Copy(sourceHelper, helperPath, overwrite: true);

            var powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            if (!File.Exists(powershell)) throw new FileNotFoundException("Windows PowerShell 5.1 is required by the desktop installer hand-off.", powershell);
            var helperStart = new ProcessStartInfo
            {
                FileName = powershell,
                WorkingDirectory = _dataRoot,
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            };
            foreach (var argument in new[]
            {
                "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                "-File", helperPath,
                "-InstallRoot", _installRoot,
                "-DataRoot", _dataRoot,
                "-StageRoot", stageRoot,
                "-InstallerPath", installerPath,
                "-ManifestPath", manifestPath,
                "-TagName", tagName,
                "-ExpectedSha256", expectedHash,
                "-CurrentVersion", _runtimeVersion,
                "-DesktopPid", Environment.ProcessId.ToString(),
                "-Port", _runtimePort.ToString()
            }) helperStart.ArgumentList.Add(argument);
            using var updater = Process.Start(helperStart) ?? throw new InvalidOperationException("Windows could not start the verified desktop update helper.");
            updateHandedOff = true;
            SetUpdateProgress($"Installing {tagName}…", "The local runtime is shutting down. The complete desktop installation will be replaced and checked before the app reopens.");
            _isExiting = true;
            _allowClose = true;
            _tray.Visible = false;
            Close();
        }
        catch (Exception ex)
        {
            SetUpdateProgress("Update could not be installed", "Your current application and user data have not been intentionally changed. Review the local desktop-update.log if the problem continues.");
            MessageBox.Show(
                $"The desktop update could not be prepared. Your current installation remains available.\n\n{ex.Message}",
                WindowTitle, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            _updateCheckInProgress = false;
            if (!updateHandedOff)
            {
                try { _webView.CoreWebView2?.PostWebMessageAsString("desktop-update-check-finished"); }
                catch { /* The dashboard may still be starting or already closed. */ }
            }
        }
    }

    private void SetUpdateProgress(string title, string detail)
    {
        if (IsDisposed || Disposing) return;
        _loadingTitle.Text = title;
        _loadingDetail.Text = detail;
        try
        {
            _webView.CoreWebView2?.PostWebMessageAsJson(JsonSerializer.Serialize(new
            {
                type = "desktop-update-progress",
                title,
                detail
            }));
        }
        catch { /* The WebView may not be initialized or may be navigating. */ }
        if (_tray.Visible && !detail.Contains("MiB downloaded", StringComparison.OrdinalIgnoreCase))
        {
            _tray.BalloonTipTitle = title;
            _tray.BalloonTipText = detail.Length > 240 ? detail[..240] : detail;
            _tray.ShowBalloonTip(2500);
        }
    }

    private static async Task<string> ReadBoundedTextAsync(HttpContent content, int maximumBytes)
    {
        if (content.Headers.ContentLength is long contentLength && (contentLength < 1 || contentLength > maximumBytes))
            throw new InvalidDataException("The release checksum manifest has an unsupported size.");
        await using var input = await content.ReadAsStreamAsync();
        using var output = new MemoryStream();
        var buffer = new byte[4096];
        int read;
        while ((read = await input.ReadAsync(buffer.AsMemory(0, buffer.Length))) > 0)
        {
            if (output.Length + read > maximumBytes)
                throw new InvalidDataException("The release checksum manifest exceeded its maximum size.");
            output.Write(buffer, 0, read);
        }
        return Encoding.UTF8.GetString(output.ToArray());
    }

    private static async Task<string> ComputeSha256Async(string path)
    {
        await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 128, useAsync: true);
        var digest = await SHA256.HashDataAsync(stream);
        return Convert.ToHexString(digest).ToLowerInvariant();
    }

    private static int CompareReleaseTags(string left, string right)
    {
        static (int Major, int Minor, int Patch, string Pre) Parse(string value)
        {
            var match = Regex.Match(value, @"^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$");
            if (!match.Success) return (-1, -1, -1, "");
            return (int.Parse(match.Groups[1].Value), int.Parse(match.Groups[2].Value),
                int.Parse(match.Groups[3].Value), match.Groups[4].Value);
        }
        var a = Parse(left);
        var b = Parse(right);
        var compare = a.Major.CompareTo(b.Major);
        if (compare != 0) return compare;
        compare = a.Minor.CompareTo(b.Minor);
        if (compare != 0) return compare;
        compare = a.Patch.CompareTo(b.Patch);
        if (compare != 0) return compare;
        if (string.IsNullOrEmpty(a.Pre) && !string.IsNullOrEmpty(b.Pre)) return 1;
        if (!string.IsNullOrEmpty(a.Pre) && string.IsNullOrEmpty(b.Pre)) return -1;
        return StringComparer.OrdinalIgnoreCase.Compare(a.Pre, b.Pre);
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
