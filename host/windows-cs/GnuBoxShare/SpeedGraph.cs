// SpeedGraph.cs - live download/upload speed graph with peaks; hover shows the values at
// that moment, right-click chooses the time span. Follows the light/dark theme. GPL v2 or later.
using System.Drawing.Drawing2D;

namespace GnuBoxShare;

sealed class SpeedGraph : Control
{
    readonly List<(DateTime t, double down, double up)> _samples = new();
    const int MaxSamples = 3700;                     // a bit over an hour at one sample per second
    int _spanSeconds = 120;
    int? _mouseX;
    public double PeakDown { get; private set; }
    public double PeakUp { get; private set; }
    public DateTime? PeakDownAt { get; private set; }
    public DateTime? PeakUpAt { get; private set; }
    public double LastDown => _samples.Count > 0 ? _samples[^1].down : 0;
    public double LastUp => _samples.Count > 0 ? _samples[^1].up : 0;
    public event Action? SpanChanged;

    static readonly (int s, string text)[] Spans = { (60, "1 minute"), (120, "2 minutes"), (300, "5 minutes"), (900, "15 minutes"), (3600, "1 hour") };

    public SpeedGraph()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        Font = new Font("Segoe UI", 8.5f);
        var menu = new ContextMenuStrip();
        foreach (var (s, text) in Spans)
        {
            var item = new ToolStripMenuItem("Show the last " + text) { Tag = s };
            item.Click += (_, _) => SpanSeconds = s;
            menu.Items.Add(item);
        }
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Reset peaks", null, (_, _) => ResetPeaks());
        menu.Items.Add("Clear the graph", null, (_, _) => { _samples.Clear(); ResetPeaks(); });
        menu.Opening += (_, _) =>
        {
            foreach (var it in menu.Items.OfType<ToolStripMenuItem>())
                if (it.Tag is int s) it.Checked = s == _spanSeconds;
        };
        ContextMenuStrip = menu;
    }

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public int SpanSeconds
    {
        get => _spanSeconds;
        set { _spanSeconds = Math.Clamp(value, 30, 3600); Invalidate(); SpanChanged?.Invoke(); }
    }

    public void ResetPeaks() { PeakDown = PeakUp = 0; PeakDownAt = PeakUpAt = null; Invalidate(); }

    /// <summary>One measurement in kB/s (normally once a second).</summary>
    public void Add(double downKBs, double upKBs)
    {
        var now = DateTime.Now;
        _samples.Add((now, downKBs, upKBs));
        if (_samples.Count > MaxSamples) _samples.RemoveRange(0, _samples.Count - MaxSamples);
        if (downKBs > PeakDown) { PeakDown = downKBs; PeakDownAt = now; }
        if (upKBs > PeakUp) { PeakUp = upKBs; PeakUpAt = now; }
        Invalidate();
    }

    protected override void OnMouseMove(MouseEventArgs e) { _mouseX = e.X; Invalidate(); base.OnMouseMove(e); }
    protected override void OnMouseLeave(EventArgs e) { _mouseX = null; Invalidate(); base.OnMouseLeave(e); }

    static bool Dark => Application.IsDarkModeEnabled;
    static Color DownColor => Dark ? Color.FromArgb(88, 166, 255) : Color.FromArgb(9, 105, 218);
    static Color UpColor => Dark ? Color.FromArgb(255, 166, 87) : Color.FromArgb(207, 102, 0);

    static double NiceMax(double v)
    {
        if (v <= 4) return 4;
        double mag = Math.Pow(10, Math.Floor(Math.Log10(v)));
        foreach (var m in new[] { 1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10 })
            if (m * mag >= v) return m * mag;
        return 10 * mag;
    }

    static string Ago(double s) =>
        s < 1 ? "now" : s < 60 ? $"-{s:0} s" : s % 60 == 0 ? $"-{s / 60:0} min" : $"-{(int)s / 60}:{(int)s % 60:00}";

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var bg = SystemColors.Window;
        var fg = SystemColors.WindowText;
        var grid = Dark ? Color.FromArgb(60, 60, 60) : Color.FromArgb(225, 225, 225);
        var axis = SystemColors.GrayText;
        g.Clear(bg);

        using var fgBrush = new SolidBrush(fg);
        using var axisBrush = new SolidBrush(axis);
        using var downPen = new Pen(DownColor, 2f);
        using var upPen = new Pen(UpColor, 2f);
        using var downFill = new SolidBrush(Color.FromArgb(Dark ? 60 : 45, DownColor));
        using var gridPen = new Pen(grid);

        // legend
        float lx = 8, ly = 4;
        void Legend(Color c, string text)
        {
            using var b = new SolidBrush(c);
            g.FillRectangle(b, lx, ly + 4, 10, 10);
            g.DrawString(text, Font, fgBrush, lx + 14, ly);
            lx += 14 + g.MeasureString(text, Font).Width + 16;
        }
        Legend(DownColor, $"Down {LastDown:0.0} kB/s   peak {PeakDown:0.0}");
        Legend(UpColor, $"Up {LastUp:0.0} kB/s   peak {PeakUp:0.0}");
        string spanText = "last " + Spans.FirstOrDefault(x => x.s == _spanSeconds).text + "  (right-click: change)";
        var sw = g.MeasureString(spanText, Font).Width;
        if (lx + sw < Width - 8) g.DrawString(spanText, Font, axisBrush, Width - sw - 8, ly);

        var plot = new RectangleF(48, 26, Math.Max(10, Width - 48 - 12), Math.Max(10, Height - 26 - 22));
        var now = DateTime.Now;
        var from = now.AddSeconds(-_spanSeconds);
        var visible = _samples.Where(s => s.t >= from.AddSeconds(-2)).ToList();
        double max = NiceMax(visible.Count > 0 ? visible.Max(s => Math.Max(s.down, s.up)) * 1.1 : 0);

        // grid and labels
        for (int i = 0; i <= 4; i++)
        {
            float y = plot.Bottom - plot.Height * i / 4f;
            g.DrawLine(gridPen, plot.Left, y, plot.Right, y);
            string lab = (max * i / 4).ToString(max >= 20 ? "0" : "0.#");
            var sz = g.MeasureString(lab, Font);
            g.DrawString(lab, Font, axisBrush, plot.Left - sz.Width - 4, y - sz.Height / 2);
        }
        int ticks = 6;
        for (int i = 0; i <= ticks; i++)
        {
            float x = plot.Left + plot.Width * i / ticks;
            if (i > 0 && i < ticks) g.DrawLine(gridPen, x, plot.Top, x, plot.Bottom);
            string lab = Ago(_spanSeconds * (ticks - i) / (double)ticks);
            var sz = g.MeasureString(lab, Font);
            float tx = Math.Clamp(x - sz.Width / 2, plot.Left - 10, plot.Right - sz.Width + 6);
            g.DrawString(lab, Font, axisBrush, tx, plot.Bottom + 3);
        }

        if (visible.Count >= 1)
        {
            float X(DateTime t) => plot.Left + (float)((t - from).TotalSeconds / _spanSeconds) * plot.Width;
            float Y(double v) => plot.Bottom - (float)(Math.Min(v, max) / max) * plot.Height;
            var downPts = visible.Select(s => new PointF(Math.Max(plot.Left, X(s.t)), Y(s.down))).ToArray();
            var upPts = visible.Select(s => new PointF(Math.Max(plot.Left, X(s.t)), Y(s.up))).ToArray();
            var clip = g.Clip;
            g.SetClip(plot);
            if (downPts.Length >= 2)
            {
                using var area = new GraphicsPath();
                area.AddLine(downPts[0].X, plot.Bottom, downPts[0].X, downPts[0].Y);
                area.AddLines(downPts);
                area.AddLine(downPts[^1].X, downPts[^1].Y, downPts[^1].X, plot.Bottom);
                area.CloseFigure();
                g.FillPath(downFill, area);
                g.DrawLines(downPen, downPts);
                g.DrawLines(upPen, upPts);
            }
            // peak lines
            using var peakDown = new Pen(Color.FromArgb(140, DownColor), 1f) { DashStyle = DashStyle.Dash };
            using var peakUp = new Pen(Color.FromArgb(140, UpColor), 1f) { DashStyle = DashStyle.Dash };
            if (PeakDown > 0 && PeakDown <= max) g.DrawLine(peakDown, plot.Left, Y(PeakDown), plot.Right, Y(PeakDown));
            if (PeakUp > 0 && PeakUp <= max) g.DrawLine(peakUp, plot.Left, Y(PeakUp), plot.Right, Y(PeakUp));
            g.Clip = clip;

            // hover: the sample under the mouse
            if (_mouseX is int mx && mx >= plot.Left && mx <= plot.Right)
            {
                var t = from.AddSeconds((mx - plot.Left) / plot.Width * _spanSeconds);
                var s = visible.MinBy(v => Math.Abs((v.t - t).TotalSeconds));
                if (Math.Abs((s.t - t).TotalSeconds) < Math.Max(3, _spanSeconds / 100.0))
                {
                    float sx = X(s.t);
                    using var cross = new Pen(axis) { DashStyle = DashStyle.Dot };
                    g.DrawLine(cross, sx, plot.Top, sx, plot.Bottom);
                    using (var b = new SolidBrush(DownColor)) g.FillEllipse(b, sx - 3.5f, Y(s.down) - 3.5f, 7, 7);
                    using (var b = new SolidBrush(UpColor)) g.FillEllipse(b, sx - 3.5f, Y(s.up) - 3.5f, 7, 7);
                    string tip = $"{s.t:HH:mm:ss}  ({Ago((now - s.t).TotalSeconds)})\nDown {s.down:0.0} kB/s\nUp {s.up:0.0} kB/s";
                    var tsz = g.MeasureString(tip, Font);
                    float bx = sx + 10 + tsz.Width + 8 > plot.Right ? sx - tsz.Width - 18 : sx + 10;
                    var box = new RectangleF(bx, plot.Top + 6, tsz.Width + 8, tsz.Height + 6);
                    using var boxBrush = new SolidBrush(Color.FromArgb(235, Dark ? Color.FromArgb(45, 45, 48) : Color.White));
                    using var boxPen = new Pen(axis);
                    g.FillRectangle(boxBrush, box);
                    g.DrawRectangle(boxPen, box.X, box.Y, box.Width, box.Height);
                    g.DrawString(tip, Font, fgBrush, box.X + 4, box.Y + 3);
                }
            }
        }
        else
        {
            const string none = "No data yet - the graph starts when sharing starts.";
            var sz = g.MeasureString(none, Font);
            g.DrawString(none, Font, axisBrush, plot.Left + (plot.Width - sz.Width) / 2, plot.Top + (plot.Height - sz.Height) / 2);
        }
        using var border = new Pen(grid);
        g.DrawRectangle(border, plot.X, plot.Y, plot.Width, plot.Height);
    }
}
