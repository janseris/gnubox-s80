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
    Status _status = new();
    CancellationTokenSource? _cts;
    Task? _task;
    bool _exiting, _trayHintShown;
    LinkState _lastState = LinkState.Stopped;
    (DateTime t, long rx, long tx) _last = (DateTime.UtcNow, 0, 0);

    readonly Label _state = new() { AutoSize = true, Font = new Font("Segoe UI", 13f, FontStyle.Bold), Text = "Stopped" };
    readonly Label _stats = new() { AutoSize = true, Font = new Font("Segoe UI", 9.5f) };
    readonly Button _go = new() { Text = "Start sharing", AutoSize = true, MinimumSize = new Size(150, 34) };
    readonly CheckBox _chkAuto = new() { Text = "Start sharing when GnuBox Share opens", AutoSize = true };
    readonly CheckBox _chkBoot = new() { Text = "Start with Windows (in the tray)", AutoSize = true };
    readonly CheckBox _chkTray = new() { Text = "Keep running in the tray when the window is closed", AutoSize = true };
    readonly CheckBox _chkVerbose = new() { Text = "Log every connection", AutoSize = true };
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
        ClientSize = new Size(760, 600);
        MinimumSize = new Size(640, 480);
        Padding = new Padding(10);

        BuildLayout();
        BuildTray();

        _chkAuto.Checked = _settings.StartSharingOnOpen;
        _chkTray.Checked = _settings.KeepInTray;
        _chkVerbose.Checked = _settings.Verbose;
        _chkBoot.Checked = IsStartWithWindows();
        _chkAuto.CheckedChanged += (_, _) => { _settings.StartSharingOnOpen = _chkAuto.Checked; _settings.Save(); };
        _chkTray.CheckedChanged += (_, _) => { _settings.KeepInTray = _chkTray.Checked; _settings.Save(); };
        _chkVerbose.CheckedChanged += (_, _) => { _settings.Verbose = Log.Verbose = _chkVerbose.Checked; _settings.Save(); };
        _chkBoot.CheckedChanged += (_, _) => SetStartWithWindows(_chkBoot.Checked);
        _go.Click += (_, _) => { if (Running) StopSharing(); else StartSharing(); };

        _timer.Tick += (_, _) => Poll();
        _timer.Start();
        UpdateStateView();
        Log.Info("GnuBox Share 1.0 - press Start sharing, then Connect in GnuBox on the 9300.");
    }

    bool Running => _task is { IsCompleted: false };

    // ---------------------------------------------------------------- layout
    void BuildLayout()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 6 };
        for (int i = 0; i < 5; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
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
        _stats.Margin = new Padding(6, 2, 3, 8);
        root.Controls.Add(_stats);

        // options
        var opts = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
        opts.Controls.AddRange(new Control[] { _chkAuto, _chkBoot, _chkTray, _chkVerbose });
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

        // log
        var logBox = new GroupBox { Text = "Log", Dock = DockStyle.Fill, Padding = new Padding(6) };
        logBox.Controls.Add(_log);
        root.Controls.Add(logBox);

        Controls.Add(root);
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
        _status = new Status();
        _last = (DateTime.UtcNow, 0, 0);
        _cts = new CancellationTokenSource();
        var ct = _cts.Token;
        _task = Task.Run(() => Runner.RunAsync(BluetoothTransport.OpenAsync, _status, null, ct));
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
            if (running && st is LinkState.Up or LinkState.Negotiating)
            {
                int n = _status.Connections;
                var s = $"Down {down:0.0} kB/s   Up {up:0.0} kB/s   |   {n} connection{(n == 1 ? "" : "s")}   |   " +
                        $"{Mb(_status.Tx)} down, {Mb(_status.Rx)} up";
                if (_status.Since is { } since) s += $"   |   connected {(DateTime.Now - since):h\\:mm\\:ss}";
                _stats.Text = s;
                tip += $" {down:0.0} kB/s";
            }
            else _stats.Text = running ? $"Bluetooth: {_status.Where}" : "";
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

    static string Mb(long b) => b >= 1048576 ? $"{b / 1048576.0:0.0} MB" : $"{b / 1024} KB";
}
