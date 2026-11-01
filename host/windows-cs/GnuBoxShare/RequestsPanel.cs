// RequestsPanel.cs - the phone's requests: a live list (newest at the bottom) with a filter,
// and the details of the selected one (headers, timing, sizes). GPL v2 or later.
using System.Text;

namespace GnuBoxShare;

sealed class RequestsPanel : UserControl
{
    sealed class FastListView : ListView
    {
        public FastListView() { DoubleBuffered = true; }
    }

    readonly FastListView _list = new()
    {
        Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, VirtualMode = true, HideSelection = false,
        MultiSelect = false, Font = new Font("Segoe UI", 9f),
    };
    readonly TextBox _details = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false,
        Font = new Font("Consolas", 9f),
    };
    readonly TextBox _filter = new() { Width = 220, PlaceholderText = "Filter (URL, status, type...)" };
    readonly CheckBox _hideDns = new() { Text = "Hide DNS", AutoSize = true, Checked = true, Margin = new Padding(10, 5, 3, 3) };
    readonly CheckBox _follow = new() { Text = "Scroll to new", AutoSize = true, Checked = true, Margin = new Padding(10, 5, 3, 3) };
    readonly Label _count = new() { AutoSize = true, Margin = new Padding(10, 7, 3, 3) };
    readonly SplitContainer _split = new() { Dock = DockStyle.Fill, Orientation = Orientation.Vertical };
    Traffic? _traffic;
    RequestInfo[] _all = Array.Empty<RequestInfo>();
    RequestInfo[] _shown = Array.Empty<RequestInfo>();
    RequestInfo? _selected;
    int _selectedVersion = -1;
    long _lastId = -1;
    string _lastFilter = "";
    bool _lastHideDns = true;

    public RequestsPanel()
    {
        _list.Columns.Add("Time", 62);
        _list.Columns.Add("Method", 52);
        _list.Columns.Add("Status", 80);
        _list.Columns.Add("Size", 62, HorizontalAlignment.Right);
        _list.Columns.Add("ms", 48, HorizontalAlignment.Right);
        _list.Columns.Add("URL", 420);
        _list.Columns.Add("Type", 110);
        _list.Columns.Add("Server", 120);
        _list.RetrieveVirtualItem += (_, e) => e.Item = MakeItem(e.ItemIndex < _shown.Length ? _shown[e.ItemIndex] : null);
        _list.SelectedIndexChanged += (_, _) =>
        {
            _selected = _list.SelectedIndices.Count > 0 && _list.SelectedIndices[0] < _shown.Length ? _shown[_list.SelectedIndices[0]] : null;
            _selectedVersion = -1;
            ShowDetails();
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Copy URL", null, (_, _) => { if (_selected != null) Clipboard.SetText(_selected.Url); });
        menu.Items.Add("Copy details", null, (_, _) => { if (_details.TextLength > 0) Clipboard.SetText(_details.Text); });
        _list.ContextMenuStrip = menu;

        var clear = new Button { Text = "Clear", AutoSize = true, Margin = new Padding(10, 1, 3, 1) };
        clear.Click += (_, _) => { _traffic?.Clear(); _lastId = -1; _selected = null; _details.Clear(); UpdateView(true); };
        _filter.TextChanged += (_, _) => UpdateView(true);
        _hideDns.CheckedChanged += (_, _) => UpdateView(true);

        var bar = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = false, Padding = new Padding(0, 0, 0, 4) };
        bar.Controls.AddRange(new Control[] { _filter, _hideDns, _follow, clear, _count });

        _split.Panel1.Controls.Add(_list);
        _split.Panel2.Controls.Add(_details);
        Controls.Add(_split);
        Controls.Add(bar);
        _split.FixedPanel = FixedPanel.None;
    }

    bool _splitSet;

    protected override void OnLayout(LayoutEventArgs e)
    {
        base.OnLayout(e);
        // the list gets about 60% of the width once the real width is known
        if (!_splitSet && _split.Width > 500)
        {
            _splitSet = true;
            _split.SplitterDistance = _split.Width * 60 / 100;
        }
    }

    public void Attach(Traffic t) { _traffic = t; UpdateView(true); }

    static string Bytes(long b) => b >= 1048576 ? $"{b / 1048576.0:0.0} MB" : b >= 1024 ? $"{b / 1024.0:0.#} KB" : $"{b} B";

    static ListViewItem MakeItem(RequestInfo? r)
    {
        if (r == null) return new ListViewItem(new[] { "", "", "", "", "", "", "", "" });
        string status = r.Status.Length > 0 ? r.Status : r.Done ? "" : "...";
        string type = r.ContentType;
        int semi = type.IndexOf(';');
        if (semi > 0) type = type[..semi];
        var it = new ListViewItem(new[]
        {
            r.Start.ToString("HH:mm:ss"), r.Method, status, Bytes(r.Down + r.Up),
            r.Ms is { } ms ? ms.ToString("0") : "...", r.Url, type, r.Remote,
        });
        if (r.Status.StartsWith('4') || r.Status.StartsWith('5') || r.Status is "not found" or "could not connect" or "no response")
            it.ForeColor = Application.IsDarkModeEnabled ? Color.FromArgb(248, 81, 73) : Color.FromArgb(198, 40, 40);
        else if (!r.Done)
            it.ForeColor = SystemColors.GrayText;
        return it;
    }

    bool Matches(RequestInfo r, string f)
    {
        if (_hideDns.Checked && r.Kind == RequestKind.Dns) return false;
        if (f.Length == 0) return true;
        return r.Url.Contains(f, StringComparison.OrdinalIgnoreCase) || r.Status.Contains(f, StringComparison.OrdinalIgnoreCase)
            || r.Method.Contains(f, StringComparison.OrdinalIgnoreCase) || r.ContentType.Contains(f, StringComparison.OrdinalIgnoreCase)
            || r.Remote.Contains(f, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Called twice a second by the window while the pane is visible.</summary>
    public void UpdateView(bool force)
    {
        if (_traffic == null) return;
        long last = _traffic.LastId;
        string f = _filter.Text.Trim();
        if (force || last != _lastId || f != _lastFilter || _hideDns.Checked != _lastHideDns || _all.Length == 0)
        {
            _lastId = last;
            _lastFilter = f;
            _lastHideDns = _hideDns.Checked;
            _all = _traffic.Snapshot();
            _shown = _all.Where(r => Matches(r, f)).ToArray();
            _list.VirtualListSize = _shown.Length;
            int sel = _selected != null ? Array.IndexOf(_shown, _selected) : -1;
            if (sel < 0 && _list.SelectedIndices.Count > 0) _list.SelectedIndices.Clear();
            else if (sel >= 0 && (_list.SelectedIndices.Count == 0 || _list.SelectedIndices[0] != sel))
            {
                _list.SelectedIndices.Clear();
                _list.SelectedIndices.Add(sel);
            }
            if (_follow.Checked && _shown.Length > 0) _list.EnsureVisible(_shown.Length - 1);
            int open = _all.Count(r => !r.Done);
            _count.Text = $"{_shown.Length} shown, {_all.Length} recorded, {open} in progress";
        }
        _list.Invalidate();                                        // sizes and states change while loading
        if (_selected != null && _selected.Version != _selectedVersion) ShowDetails();
    }

    void ShowDetails()
    {
        var r = _selected;
        if (r == null) { _details.Clear(); return; }
        _selectedVersion = r.Version;
        var sb = new StringBuilder();
        sb.AppendLine($"{r.Method} {r.Url}");
        sb.AppendLine();
        sb.AppendLine($"Status:      {(r.Status.Length > 0 ? r.Status : r.Done ? "-" : "waiting...")}");
        if (r.ContentType.Length > 0) sb.AppendLine($"Type:        {r.ContentType}");
        sb.AppendLine($"Server:      {r.Remote}" + (r.Conn > 0 ? $"   (TCP connection #{r.Conn})" : ""));
        sb.AppendLine($"Started:     {r.Start:HH:mm:ss.fff}");
        if (r.FirstByte is { } fb) sb.AppendLine($"First byte:  {(fb - r.Start).TotalMilliseconds:0} ms");
        sb.AppendLine(r.End is { } end ? $"Finished:    {(end - r.Start).TotalMilliseconds:0} ms" : "Finished:    still in progress");
        sb.AppendLine($"Down:        {r.Down:N0} bytes   ({Bytes(r.Down)})");
        sb.AppendLine($"Up:          {r.Up:N0} bytes   ({Bytes(r.Up)})");
        if (r.End is { } e2 && r.Down > 0 && (e2 - r.Start).TotalSeconds > 0.2)
            sb.AppendLine($"Speed:       {r.Down / 1024.0 / (e2 - r.Start).TotalSeconds:0.0} kB/s down");
        if (r.Note.Length > 0) sb.AppendLine($"Note:        {r.Note}");
        switch (r.Kind)
        {
            case RequestKind.Https:
                sb.AppendLine().AppendLine("HTTPS is encrypted between the phone and the server: only the server name");
                sb.AppendLine("(when the phone sends it) and the amount of data are visible here.");
                break;
            case RequestKind.Dns:
                sb.AppendLine().AppendLine("--- Answer ---").AppendLine(r.ResponseHeaders.Length > 0 ? r.ResponseHeaders : "(no addresses)");
                break;
        }
        if (r.RequestHeaders.Length > 0) sb.AppendLine().AppendLine("--- Request headers ---").AppendLine(r.RequestHeaders);
        if (r.Kind == RequestKind.Http && r.ResponseHeaders.Length > 0) sb.AppendLine().AppendLine("--- Response headers ---").AppendLine(r.ResponseHeaders);
        string text = sb.ToString().Replace("\r\n", "\n").Replace("\n", "\r\n");
        if (_details.Text != text) _details.Text = text;
    }
}
