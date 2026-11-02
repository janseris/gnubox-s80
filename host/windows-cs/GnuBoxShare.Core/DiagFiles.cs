// DiagFiles.cs - writes what is needed to analyse a session afterwards, one set of files per day:
//   gnubox-share-<date>.log    every log line (connection details too), with milliseconds
//   speed-<date>.csv           one row per second while the phone is connected
//   requests-<date>.jsonl      every finished request (timing, sizes, status, TCP window, headers)
// Files older than 14 days are deleted. Writing happens on its own thread. GPL v2 or later.
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace GnuBoxShare;

public sealed class DiagFiles : IDisposable
{
    readonly BlockingCollection<(string kind, string line)> _q = new(20000);
    readonly Thread _writer, _sampler;
    readonly CancellationTokenSource _stop = new();
    volatile Status? _status;
    public string Folder { get; }

    public DiagFiles(string folder)
    {
        Folder = folder;
        Directory.CreateDirectory(folder);
        try
        {
            foreach (var f in Directory.GetFiles(folder))
                if (File.GetLastWriteTime(f) < DateTime.Now.AddDays(-14)) File.Delete(f);
        }
        catch { }
        _writer = new Thread(WriteLoop) { IsBackground = true, Name = "diag files" };
        _writer.Start();
        _sampler = new Thread(SampleLoop) { IsBackground = true, Name = "diag sampler" };
        _sampler.Start();
    }

    /// <summary>The status of the current sharing session (a new one at every Start).</summary>
    public void Watch(Status status) => _status = status;

    public void Line(string line) => _q.TryAdd(("log", line));

    static string Cut(string s, int max) => s.Length <= max ? s : s[..max] + "...";

    public void Request(RequestInfo r)
    {
        var t = r.Tcp;
        var o = new Dictionary<string, object?>
        {
            ["id"] = r.Id, ["kind"] = r.Kind.ToString(), ["start"] = r.Start.ToString("yyyy-MM-ddTHH:mm:ss.fff"),
            ["ms"] = r.Ms is { } ms ? Math.Round(ms) : null,
            ["first_byte_ms"] = r.FirstByte is { } fb ? Math.Round((fb - r.Start).TotalMilliseconds) : null,
            ["method"] = r.Method, ["url"] = r.Url, ["status"] = r.Status, ["type"] = r.ContentType,
            ["down"] = r.Down, ["up"] = r.Up, ["remote"] = r.Remote, ["conn"] = r.Conn, ["note"] = r.Note,
            ["phone_win_max"] = t?.PhoneWindowMax, ["rtt_ms"] = t?.RttMs, ["rtt_min_ms"] = t?.RttMinMs,
            ["rtt_samples"] = t?.RttSamples,
            ["window_full_pct"] = t is { TicksData: > 0 } ? (int)(100 * t.TicksWindowFull / t.TicksData) : null,
            ["req_headers"] = r.RequestHeaders.Length > 0 ? Cut(r.RequestHeaders, 2000) : null,
            ["resp_headers"] = r.ResponseHeaders.Length > 0 ? Cut(r.ResponseHeaders, 2000) : null,
        };
        _q.TryAdd(("requests", JsonSerializer.Serialize(o)));
    }

    const string SpeedHeader = "time,state,down_Bps,up_Bps,connections,queued_B,bt_busy_pct,phone_win,phone_win_max," +
        "rtt_ms,rtt_min_ms,data_ticks,window_full_ticks,queue_full_ticks,frame_B,short_queue";

    void SampleLoop()
    {
        Status? last = null;
        long rx = 0, tx = 0, busy = 0, data = 0, wf = 0, qf = 0;
        long t0 = Stopwatch.GetTimestamp();
        int idleRows = 0;
        while (!_stop.IsCancellationRequested)
        {
            Thread.Sleep(1000);
            var st = _status;
            if (st == null) continue;
            var d = st.Diag;
            long now = Stopwatch.GetTimestamp();
            if (st != last)                                   // a new session: start the differences from here
            {
                last = st; rx = st.Rx; tx = st.Tx; busy = d.WriteBusyTicks; data = d.TicksData; wf = d.TicksWindowFull; qf = d.TicksQueueFull; t0 = now;
                continue;
            }
            double secs = (now - t0) / (double)Stopwatch.Frequency;
            if (secs <= 0) continue;
            bool idle = st.Tx == tx && st.Rx == rx && d.TicksData == data;
            if (idle) idleRows++; else idleRows = 0;
            if (st.State is LinkState.Up or LinkState.Negotiating && (!idle || idleRows % 10 == 1))   // idle: every 10 s
            {
                var row = string.Join(",",
                    DateTime.Now.ToString("HH:mm:ss"), st.State,
                    ((st.Tx - tx) / secs).ToString("0"), ((st.Rx - rx) / secs).ToString("0"),
                    st.Connections, d.QueuedBytes,
                    (100.0 * (d.WriteBusyTicks - busy) / Stopwatch.Frequency / secs).ToString("0"),
                    d.PhoneWindow, d.PhoneWindowMax, d.RttMs, d.RttMinMs,
                    d.TicksData - data, d.TicksWindowFull - wf, d.TicksQueueFull - qf,
                    d.RfcommMtu, st.ShortQueue ? 1 : 0);
                _q.TryAdd(("speed", row));
            }
            rx = st.Rx; tx = st.Tx; busy = d.WriteBusyTicks; data = d.TicksData; wf = d.TicksWindowFull; qf = d.TicksQueueFull; t0 = now;
        }
    }

    void WriteLoop()
    {
        var writers = new Dictionary<string, (string date, StreamWriter w)>();
        try
        {
            while (!_q.IsCompleted)
            {
                if (!_q.TryTake(out var item, 1000)) { foreach (var x in writers.Values) x.w.Flush(); continue; }
                try
                {
                    string date = DateTime.Now.ToString("yyyy-MM-dd");
                    if (!writers.TryGetValue(item.kind, out var cur) || cur.date != date)
                    {
                        cur.w?.Dispose();
                        string name = item.kind switch
                        {
                            "log" => $"gnubox-share-{date}.log",
                            "speed" => $"speed-{date}.csv",
                            _ => $"requests-{date}.jsonl",
                        };
                        string path = Path.Combine(Folder, name);
                        bool isNew = !File.Exists(path);
                        var w = new StreamWriter(new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false));
                        if (isNew && item.kind == "speed") w.WriteLine(SpeedHeader);
                        cur = (date, w);
                        writers[item.kind] = cur;
                    }
                    cur.w.WriteLine(item.line);
                    if (_q.Count == 0) cur.w.Flush();
                }
                catch { }
            }
        }
        finally { foreach (var x in writers.Values) try { x.w.Dispose(); } catch { } }
    }

    public void Dispose()
    {
        _stop.Cancel();
        _q.CompleteAdding();
        _writer.Join(2000);
    }
}
