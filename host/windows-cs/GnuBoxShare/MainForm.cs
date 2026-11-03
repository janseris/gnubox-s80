// MainForm.cs - the window and the tray icon. GPL v2 or later.
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Text.Json;
using Microsoft.Win32;

namespace GnuBoxShare;

sealed class Settings
{
    public bool StartSharingOnOpen { get; set; }
    public bool KeepInTray { get; set; } = true;
    public bool Verbose { get; set; }
    public bool ShowRequests { get; set; }
    public bool ShortQueue { get; set; }
    public bool SaveLogs { get; set; } = true;
    public bool TraceSegments { get; set; }
    public int GraphSeconds { get; set; } = 120;

    static string Dir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "GnuBoxShare");
    static string File => Path.Combine(Dir, "GnuBoxShare.json");

    public static Settings Load()
    {
        try { return JsonSerializer.Deserialize<Settings>(System.IO.File.ReadAllText(File)) ?? new(); }
        catch { return new(); }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Dir);
            System.IO.File.WriteAllText(File, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { }
    }
}

sealed class MainForm : Form
{
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    const string RunValue = "GnuBox Share";

    readonly Settings _settings = Settings.Load();
    readonly bool _autostart;
    readonly ConcurrentQueue<string> _logQueue = new();
    readonly Traffic _traffic = new();                  // the requests list, kept across Start/Stop
    Status _status = new();
    CancellationTokenSource? _cts;
    Task? _task;
    bool _exiting, _trayHintShown;
    LinkState _lastState = LinkState.Stopped;
    (DateTime t, long rx, long tx) _last = (DateTime.UtcNow, 0, 0);

    readonly Label _state = new() { AutoSize = true, Font = new Font("Segoe UI", 13f, FontStyle.Bold), Text = "Stopped" };
    readonly Label _stats = new() { AutoSize = true, Font = new Font("Segoe UI", 9.5f) };
    readonly Label _diag = new() { AutoSize = true, Font = new Font("Segoe UI", 9f) };
    readonly ToolTip _tips = new() { AutoPopDelay = 30000 };
    readonly Queue<(DateTime t, long busy, long data, long wf, long qf)> _diagHist = new();
    readonly Button _go = new() { Text = "Start sharing", AutoSize = true, MinimumSize = new Size(150, 34) };
    readonly CheckBox _chkAuto = new() { Text = "Start sharing when GnuBox Share opens", AutoSize = true };
    readonly CheckBox _chkBoot = new() { Text = "Start with Windows (in the tray)", AutoSize = true };
    readonly CheckBox _chkTray = new() { Text = "Keep running in the tray when the window is closed", AutoSize = true };
    readonly CheckBox _chkVerbose = new() { Text = "Log every connection", AutoSize = true };
    readonly CheckBox _chkRequests = new() { Text = "Show the phone's requests", AutoSize = true };
    readonly CheckBox _chkShortQueue = new() { Text = "Test: short send queue", AutoSize = true };
    readonly CheckBox _chkLogs = new() { Text = "Save logs for analysis", AutoSize = true };
    readonly CheckBox _chkTrace = new() { Text = "Log TCP segments", AutoSize = true };
    readonly LinkLabel _openLogs = new() { Text = "open the logs folder", AutoSize = true, Margin = new Padding(0, 6, 3, 3) };
    DiagFiles? _files;
    readonly SpeedGraph _graph = new() { Dock = DockStyle.Fill, Margin = new Padding(3, 0, 3, 6) };
    readonly RequestsPanel _requests = new() { Dock = DockStyle.Fill };
    readonly SplitContainer _bottom = new() { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal };
    readonly TextBox _log = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Font = new Font("Consolas", 9f), Dock = DockStyle.Fill, WordWrap = true };
    readonly NotifyIcon _tray = new();
    readonly ToolStripMenuItem _trayStartStop = new("Start sharing");
    readonly System.Windows.Forms.Timer _timer = new() { Interval = 500 };
    readonly Icon _appIcon;
    readonly Dictionary<LinkState, Icon> _stateIcons = new();

    public MainForm(bool autostart)
    {
        _autostart = autostart;
        Log.Sink = line => _logQueue.Enqueue(line);
        Log.Verbose = _settings.Verbose;

        using (var s = typeof(MainForm).Assembly.GetManifestResourceStream("app.ico"))
            _appIcon = s != null ? new Icon(s) : SystemIcons.Application;
        Icon = _appIcon;
        Text = "GnuBox Share - internet for the Nokia 9300";
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(980, 840);
        MinimumSize = new Size(720, 640);
        Padding = new Padding(10);

        BuildLayout();
        BuildTray();

        _chkAuto.Checked = _settings.StartSharingOnOpen;
        _chkTray.Checked = _settings.KeepInTray;
        _chkVerbose.Checked = _settings.Verbose;
        _chkBoot.Checked = IsStartWithWindows();
        _chkRequests.Checked = _settings.ShowRequests;
        _bottom.Panel2Collapsed = !_settings.ShowRequests;
        _graph.SpanSeconds = _settings.GraphSeconds;
        _graph.SpanChanged += () => { _settings.GraphSeconds = _graph.SpanSeconds; _settings.Save(); };
        _requests.Attach(_traffic);
        _chkRequests.CheckedChanged += (_, _) => ShowRequestsPane(_chkRequests.Checked);
        _chkLogs.Checked = _settings.SaveLogs;
        _chkLogs.CheckedChanged += (_, _) => { _settings.SaveLogs = _chkLogs.Checked; _settings.Save(); SetLogFiles(_chkLogs.Checked); };
        _openLogs.LinkClicked += (_, _) => { if (_files != null) OpenUri(_files.Folder); else Log.Info("tick 'Save logs for analysis' first"); };
        _traffic.Completed += r => _files?.Request(r);
        SetLogFiles(_settings.SaveLogs);
        _chkTrace.Checked = Nat.TraceSegments = _settings.TraceSegments;
        _chkTrace.CheckedChanged += (_, _) =>
        {
            _settings.TraceSegments = Nat.TraceSegments = _chkTrace.Checked;
            _settings.Save();
            Log.Info(_chkTrace.Checked ? "TCP segments are written into the log file" : "TCP segments no longer logged");
        };
        _chkShortQueue.Checked = _settings.ShortQueue;
        _chkShortQueue.CheckedChanged += (_, _) =>
        {
            _settings.ShortQueue = _status.ShortQueue = _chkShortQueue.Checked;
            _settings.Save();
            Log.Info(_chkShortQueue.Checked ? "test: short send queue (4 packets)" : "normal send queue (12 packets)");
        };
        _chkAuto.CheckedChanged += (_, _) => { _settings.StartSharingOnOpen = _chkAuto.Checked; _settings.Save(); };
        _chkTray.CheckedChanged += (_, _) => { _settings.KeepInTray = _chkTray.Checked; _settings.Save(); };
        _chkVerbose.CheckedChanged += (_, _) => { _settings.Verbose = Log.Verbose = _chkVerbose.Checked; _settings.Save(); };
        _chkBoot.CheckedChanged += (_, _) => SetStartWithWindows(_chkBoot.Checked);
        _go.Click += (_, _) => { if (Running) StopSharing(); else StartSharing(); };

        _timer.Tick += (_, _) => Poll();
        _timer.Start();
        UpdateStateView();
        Log.Info("GnuBox Share 1.4 - press Start sharing, then Connect in GnuBox on the 9300.");
    }

    bool Running => _task is { IsCompleted: false };

    // ---------------------------------------------------------------- layout
    void BuildLayout()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 8 };
        for (int i = 0; i < 4; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 190));         // speed graph
        for (int i = 0; i < 2; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        // Bluetooth
        var bt = new GroupBox { Text = "Bluetooth", Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(8) };
        var btRow = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
        btRow.Controls.Add(new Label
        {
            AutoSize = true, MaximumSize = new Size(520, 0), Margin = new Padding(3, 6, 12, 3),
            Text = "This PC offers a Bluetooth Serial Port service; GnuBox on the 9300 connects to it. " +
                   "No COM port needed. Pair the phone with this PC once.",
        });
        var pair = new Button { Text = "Pair the phone...", AutoSize = true };
        pair.Click += (_, _) => OpenUri("ms-settings:bluetooth");
        btRow.Controls.Add(pair);
        bt.Controls.Add(btRow);
        root.Controls.Add(bt);

        // state + start/stop
        var stateRow = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2, Margin = new Padding(3, 10, 3, 0) };
        stateRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        stateRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        _state.Anchor = AnchorStyles.Left;
        _go.Anchor = AnchorStyles.Right;
        stateRow.Controls.Add(_state, 0, 0);
        stateRow.Controls.Add(_go, 1, 0);
        root.Controls.Add(stateRow);
        _stats.Margin = new Padding(6, 2, 3, 2);
        root.Controls.Add(_stats);
        _diag.Margin = new Padding(6, 0, 3, 6);
        _tips.SetToolTip(_diag,
            "Phone TCP window: how much the phone accepts before it must acknowledge (Symbian default 8 KB;\n" +
            "GnuBox Settings > Bigger TCP window raises it to 32 KB after a phone restart).\n" +
            "Full: share of the time data was waiting here only because that window was full.\n" +
            "Round trip: data sent -> phone's acknowledgement. Far above its minimum = data queues in front of Bluetooth.\n" +
            "Bluetooth busy: share of the time Windows' Bluetooth send was blocked (the link was full).\n" +
            "Frame: the serial channel (RFCOMM) frame size agreed with the phone.");
        root.Controls.Add(_diag);
        root.Controls.Add(_graph);

        // options
        var opts = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
        opts.Controls.AddRange(new Control[] { _chkAuto, _chkBoot, _chkTray, _chkVerbose, _chkRequests, _chkShortQueue, _chkLogs, _openLogs, _chkTrace });
        _tips.SetToolTip(_chkTrace, "Debugging: writes every TCP segment to and from the phone into the log file\n" +
            "(needs 'Save logs for analysis'). Many lines: turn it on only for a test.");
        _tips.SetToolTip(_chkLogs, "Writes the log (with every connection), one speed/diagnostics row per second,\n" +
            "and every request into the 'logs' folder next to GnuBoxShare.exe, one set of files per day\n" +
            "(kept 14 days). Claude can read them from there to analyse the speed.");
        _tips.SetToolTip(_chkShortQueue, "Keep at most 4 packets (instead of 12) waiting for Bluetooth.\nFor speed tests: compare the speed and the round trip with it on and off.");
        root.Controls.Add(opts);

        // phone steps
        var phone = new GroupBox { Text = "On the Nokia 9300 (GnuBox)", Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(8) };
        phone.Controls.Add(new Label
        {
            AutoSize = true, Dock = DockStyle.Fill,
            Text = "1. Device > choose this PC     2. Settings > Other device: PC (pppd)     3. Connect\n" +
                   "Apps use it without asking when GnuBox's Default menu is set to GnuBox BT.\n" +
                   "This PC itself (e.g. localserver on port 8000) is 192.168.137.1 or 10.64.64.1 for the phone.",
        });
        root.Controls.Add(phone);

        // log, and below it the requests (optional)
        var logBox = new GroupBox { Text = "Log", Dock = DockStyle.Fill, Padding = new Padding(6) };
        logBox.Controls.Add(_log);
        var reqBox = new GroupBox { Text = "The phone's requests (HTTPS: only the server name is visible)", Dock = DockStyle.Fill, Padding = new Padding(6) };
        reqBox.Controls.Add(_requests);
        _bottom.Panel1.Controls.Add(logBox);
        _bottom.Panel2.Controls.Add(reqBox);
        _bottom.FixedPanel = FixedPanel.Panel1;              // a bigger window grows the requests, not the log
        _bottom.Panel1MinSize = 70;
        _bottom.Panel2MinSize = 150;
        root.Controls.Add(_bottom);

        Controls.Add(root);
    }

    /// <summary>The 'logs' folder next to the exe (or in %LOCALAPPDATA% if that isn't writable).</summary>
    void SetLogFiles(bool on)
    {
        if (!on)
        {
            if (_files != null) { Log.Info("logs are no longer saved"); Log.FileSink = null; _files.Dispose(); _files = null; }
            return;
        }
        if (_files != null) return;
        foreach (var dir in new[]
        {
            Path.Combine(AppContext.BaseDirectory, "logs"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GnuBoxShare", "logs"),
        })
        {
            try
            {
                var probe = Path.Combine(dir, ".write-test");
                Directory.CreateDirectory(dir);
                File.WriteAllText(probe, "");
                File.Delete(probe);
                _files = new DiagFiles(dir);
                _files.Watch(_status);
                Log.FileSink = _files.Line;
                Log.Info($"logs are saved in {dir}");
                return;
            }
            catch { }
        }
        Log.Info("could not create a logs folder");
    }

    void ShowRequestsPane(bool show)
    {
        _settings.ShowRequests = show;
        _settings.Save();
        _bottom.Panel2Collapsed = !show;
        if (show)
        {
            // make room: the requests want most of the space below the graph
            var area = Screen.FromControl(this).WorkingArea;
            if (WindowState == FormWindowState.Normal && Height < 960)
            {
                int h = Math.Min(960, area.Height);
                Top = Math.Max(area.Top, Math.Min(Top, area.Bottom - h));
                Height = h;
            }
            SizeBottom();
            _requests.UpdateView(true);
        }
    }

    /// <summary>With the requests shown, the log keeps a few lines and the requests get the rest.</summary>
    void SizeBottom()
    {
        if (_bottom.Panel2Collapsed || _bottom.Height < 300) return;
        _bottom.SplitterDistance = Math.Clamp(_bottom.Height / 4, _bottom.Panel1MinSize, 150);
    }

    void BuildTray()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Open GnuBox Share", null, (_, _) => ShowFromTray());
        menu.Items.Add(_trayStartStop);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitApp());
        _trayStartStop.Click += (_, _) => { if (Running) StopSharing(); else StartSharing(); };
        _tray.ContextMenuStrip = menu;
        _tray.DoubleClick += (_, _) => ShowFromTray();
        _tray.Icon = TrayIcon(LinkState.Stopped);
        _tray.Text = "GnuBox Share";
        _tray.Visible = true;
    }

    /// <summary>The app icon with a coloured dot for the state.</summary>
    Icon TrayIcon(LinkState s)
    {
        if (_stateIcons.TryGetValue(s, out var ic)) return ic;
        var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            using var src = new Icon(_appIcon, 32, 32);
            g.DrawIcon(src, new Rectangle(0, 0, 32, 32));
            var col = s switch
            {
                LinkState.Up => Color.FromArgb(46, 160, 67),
                LinkState.Waiting or LinkState.Negotiating => Color.FromArgb(230, 150, 20),
                LinkState.Error => Color.FromArgb(220, 50, 50),
                _ => Color.FromArgb(140, 140, 140),
            };
            using var b = new SolidBrush(col);
            using var p = new Pen(Color.White, 2);
            g.FillEllipse(b, 18, 18, 13, 13);
            g.DrawEllipse(p, 18, 18, 13, 13);
        }
        ic = Icon.FromHandle(bmp.GetHicon());
        _stateIcons[s] = ic;
        return ic;
    }

    // ---------------------------------------------------------------- start / stop
    void StartSharing()
    {
        if (Running) return;
        _status = new Status { Traffic = _traffic, ShortQueue = _settings.ShortQueue };
        _diagHist.Clear();
        _files?.Watch(_status);
        Log.Info($"sharing started: GnuBox Share {Application.ProductVersion.Split('+')[0]}, {Environment.OSVersion}, .NET {Environment.Version}, " +
                 $"short send queue {(_settings.ShortQueue ? "on" : "off")}");
        _last = (DateTime.UtcNow, 0, 0);
        _graph.ResetPeaks();
        _cts = new CancellationTokenSource();
        var ct = _cts.Token;
        // tests only: GNUBOX_TEST_TCP=127.0.0.1:7777 serves a simulated phone over TCP instead of Bluetooth
        var test = Environment.GetEnvironmentVariable("GNUBOX_TEST_TCP");
        var rewrite = (Environment.GetEnvironmentVariable("GNUBOX_TEST_REWRITE") ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries)
            .Select(x => x.Split('=', 2)).Where(x => x.Length == 2).ToDictionary(x => x[0], x => x[1]);
        Func<CancellationToken, Task<ITransport>> open = test != null ? c => TcpTestTransport.AcceptAsync(test, c) : BluetoothTransport.OpenAsync;
        _task = Task.Run(() => Runner.RunAsync(open, _status, rewrite, ct));
        UpdateStateView();
    }

    void StopSharing()
    {
        _cts?.Cancel();
        _go.Enabled = false;
        _go.Text = "Stopping...";
    }

    public void ShowFromTray()
    {
        Show();
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate();
    }

    void ExitApp()
    {
        _exiting = true;
        Close();
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        SizeBottom();
        if (_autostart || _settings.StartSharingOnOpen) StartSharing();
        if (_autostart) BeginInvoke((Action)Hide);                       // started with Windows: straight to the tray
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!_exiting && e.CloseReason == CloseReason.UserClosing && _chkTray.Checked)
        {
            e.Cancel = true;
            Hide();
            if (!_trayHintShown)
            {
                _trayHintShown = true;
                _tray.ShowBalloonTip(3000, "GnuBox Share", "Still running in the tray. Right-click the icon to exit.", ToolTipIcon.Info);
            }
            return;
        }
        _timer.Stop();
        _cts?.Cancel();
        try { _task?.Wait(3000); } catch { }
        _tray.Visible = false;
        _tray.Dispose();
        Log.FileSink = null;
        _files?.Dispose();
        base.OnFormClosing(e);
    }

    // ---------------------------------------------------------------- start with Windows
    static bool IsStartWithWindows()
    {
        using var k = Registry.CurrentUser.OpenSubKey(RunKey);
        return k?.GetValue(RunValue) != null;
    }

    void SetStartWithWindows(bool on)
    {
        try
        {
            using var k = Registry.CurrentUser.CreateSubKey(RunKey);
            if (on) k.SetValue(RunValue, $"\"{Application.ExecutablePath}\" --autostart");
            else k.DeleteValue(RunValue, false);
            Log.Info(on ? "starts with Windows (in the tray)" : "no longer starts with Windows");
        }
        catch (Exception ex)
        {
            Log.Info("could not change the Windows startup setting: " + ex.Message);
        }
    }

    static void OpenUri(string uri)
    {
        try { Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true }); }
        catch (Exception ex) { Log.Info("could not open " + uri + ": " + ex.Message); }
    }

    // ---------------------------------------------------------------- polling the engine
    void Poll()
    {
        if (!_logQueue.IsEmpty)
        {
            var sb = new System.Text.StringBuilder();
            while (_logQueue.TryDequeue(out var line)) sb.Append(line).Append("\r\n");
            if (_log.TextLength > 300_000) _log.Text = _log.Text[^100_000..];
            _log.AppendText(sb.ToString());
        }
        UpdateStateView();
        if (!_bottom.Panel2Collapsed && Visible && WindowState != FormWindowState.Minimized) _requests.UpdateView(false);
    }

    void UpdateStateView()
    {
        bool running = Running;
        var st = running ? _status.State : LinkState.Stopped;
        bool dark = Application.IsDarkModeEnabled;
        (string text, Color color) = st switch
        {
            LinkState.Up => ("Sharing the internet with the phone", dark ? Color.FromArgb(63, 185, 80) : Color.FromArgb(26, 127, 55)),
            LinkState.Negotiating => ("Phone connected, starting PPP...", dark ? Color.FromArgb(210, 153, 34) : Color.FromArgb(154, 103, 0)),
            LinkState.Waiting => ("Waiting for the phone - press Connect in GnuBox", dark ? Color.FromArgb(210, 153, 34) : Color.FromArgb(154, 103, 0)),
            LinkState.Error => ("Bluetooth problem - retrying", dark ? Color.FromArgb(248, 81, 73) : Color.FromArgb(198, 40, 40)),
            _ => ("Stopped", SystemColors.GrayText),
        };
        if (_state.Text != text) _state.Text = text;
        _state.ForeColor = color;
        if (!running && !_go.Enabled) _go.Enabled = true;
        string btn = running ? (_go.Enabled ? "Stop sharing" : "Stopping...") : "Start sharing";
        if (_go.Text != btn) _go.Text = btn;
        _trayStartStop.Text = running ? "Stop sharing" : "Start sharing";

        var now = DateTime.UtcNow;
        double dt = (now - _last.t).TotalSeconds;
        string tip = "GnuBox Share: " + (st == LinkState.Up ? "sharing" : st == LinkState.Stopped ? "stopped" : "waiting");
        if (dt >= 1)
        {
            double down = (_status.Tx - _last.tx) / dt / 1024, up = (_status.Rx - _last.rx) / dt / 1024;
            _last = (now, _status.Rx, _status.Tx);
            if (running) _graph.Add(down, up);
            if (running && st is LinkState.Up or LinkState.Negotiating)
            {
                int n = _status.Connections;
                var s = $"Down {down:0.0} kB/s (peak {_graph.PeakDown:0.0})   Up {up:0.0} kB/s (peak {_graph.PeakUp:0.0})   |   " +
                        $"{n} connection{(n == 1 ? "" : "s")}   |   {Mb(_status.Tx)} down, {Mb(_status.Rx)} up";
                if (_status.Since is { } since) s += $"   |   connected {(DateTime.Now - since):h\\:mm\\:ss}";
                _stats.Text = s;
                tip += $" {down:0.0} kB/s";
            }
            else _stats.Text = running ? $"Bluetooth: {_status.Where}" : "";
            _diag.Text = running && st == LinkState.Up ? DiagText(down) : "";
            _tray.Text = tip.Length > 63 ? tip[..63] : tip;
        }
        if (st != _lastState)
        {
            _tray.Icon = TrayIcon(st);
            if (st == LinkState.Up && !Visible)
                _tray.ShowBalloonTip(3000, "GnuBox Share", "The Nokia 9300 is connected.", ToolTipIcon.Info);
            _lastState = st;
        }
    }

    /// <summary>The diagnostics line: what limits the speed, over the last ~5 seconds.</summary>
    string DiagText(double downKBs)
    {
        var d = _status.Diag;
        var now = DateTime.UtcNow;
        _diagHist.Enqueue((now, d.WriteBusyTicks, d.TicksData, d.TicksWindowFull, d.TicksQueueFull));
        while (_diagHist.Count > 6) _diagHist.Dequeue();
        var o = _diagHist.Peek();
        double secs = (now - o.t).TotalSeconds;
        long data = d.TicksData - o.data;
        int busy = secs > 0.5 ? (int)Math.Min(100, 100.0 * (d.WriteBusyTicks - o.busy) / System.Diagnostics.Stopwatch.Frequency / secs) : 0;
        int wfull = data > 0 ? (int)(100 * (d.TicksWindowFull - o.wf) / data) : 0;
        int qfull = data > 0 ? (int)(100 * (d.TicksQueueFull - o.qf) / data) : 0;
        static string Kb(int b) => b >= 1024 ? $"{b / 1024.0:0.#} KB" : $"{b} B";

        var parts = new List<string>();
        if (d.PhoneWindowMax > 0) parts.Add($"phone TCP window {Kb(d.PhoneWindowMax)}" + (data > 0 ? $" (full {wfull}%)" : ""));
        if (d.RttMs > 0) parts.Add($"round trip {d.RttMs} ms (min {d.RttMinMs})");
        parts.Add($"Bluetooth busy {busy}%" + (qfull > 0 ? $", queue full {qfull}%" : ""));
        if (d.RfcommMtu > 0) parts.Add($"frame {d.RfcommMtu} B");
        string line = "Link: " + string.Join("   ·   ", parts);

        string verdict = "";
        if (downKBs >= 3 && data >= 20)                 // only while a download is running
        {
            if (wfull >= 50)
                verdict = d.PhoneWindowMax <= 8192
                    ? "Limit: the phone's 8 KB TCP window. Try GnuBox Settings > Bigger TCP window (then restart the phone)."
                    : "Limit: the phone's TCP window.";
            else if (busy >= 70 || qfull >= 50 || (d.RttMinMs > 0 && d.RttMs > 2 * d.RttMinMs + 100))
                verdict = "Limit: the Bluetooth link itself (data waits in front of it).";
            else
                verdict = "Limit: not the link - the server or the phone app is slower.";
        }
        if (d.Compression.Length > 0) line += "\nThe phone asks for " + d.Compression + " (refused).";
        return verdict.Length > 0 ? line + "\n" + verdict : line;
    }

    static string Mb(long b) => b >= 1048576 ? $"{b / 1048576.0:0.0} MB" : $"{b / 1024} KB";
}
