// Traffic.cs - what the phone asks for, for the requests list: HTTP requests (method, URL,
// status, type, size, time, headers), HTTPS connections (only the server name is visible),
// DNS lookups, other TCP and UDP. Written on the engine thread, read by the window.
// GPL v2 or later.
using System.Globalization;
using System.Text;

namespace GnuBoxShare;

public enum RequestKind { Http, Https, Tcp, Udp, Dns }

public sealed class RequestInfo
{
    public long Id;
    public RequestKind Kind;
    public DateTime Start = DateTime.Now;
    public DateTime? FirstByte;              // response header (HTTP) or first data from the server
    public DateTime? End;                    // finished: response complete or connection closed
    public string Method = "";               // GET, POST, ... / TLS / TCP / UDP / DNS
    public string Url = "";
    public string Status = "";               // "200 OK", "1.2.3.4", "not found", "refused" ...
    public string ContentType = "";
    public long Down, Up;                    // bytes on the wire (headers + body for HTTP)
    public string Remote = "";               // address:port as the phone sees it
    public int Conn;                         // TCP connection number
    public string RequestHeaders = "", ResponseHeaders = "";
    public string Note = "";
    public TcpStats? Tcp;                    // the TCP connection's window / round trip (null for DNS, UDP)
    public volatile int Version;             // bumped on every change, for the window
    public bool Done => End != null;
    public double? Ms => End is { } e ? (e - Start).TotalMilliseconds : null;
    internal void Changed() => Version++;
}

public sealed class Traffic
{
    readonly object _lock = new();
    readonly List<RequestInfo> _items = new();
    long _nextId;
    public int Max { get; set; } = 3000;
    /// <summary>For tests: called on the engine thread when a request finishes.</summary>
    public event Action<RequestInfo>? Completed;

    internal RequestInfo Add(RequestInfo r)
    {
        lock (_lock)
        {
            r.Id = ++_nextId;
            _items.Add(r);
            if (_items.Count > Max) _items.RemoveRange(0, _items.Count - Max * 4 / 5);
        }
        return r;
    }

    internal void Complete(RequestInfo r, string? note = null)
    {
        if (r.End != null) return;
        if (note != null && r.Note.Length == 0) r.Note = note;
        r.End = DateTime.Now;
        r.Changed();
        try { Completed?.Invoke(r); } catch { }
    }

    public RequestInfo[] Snapshot() { lock (_lock) return _items.ToArray(); }
    public void Clear() { lock (_lock) _items.Clear(); }
    public long LastId { get { lock (_lock) return _nextId; } }
}

/// <summary>One HTTP/1.x message head.</summary>
sealed class HttpHead
{
    public string Text = "", First = "", Method = "", Target = "", Reason = "";
    public int Code, Size;
    public readonly Dictionary<string, string> Headers = new(StringComparer.OrdinalIgnoreCase);
    public string H(string name) => Headers.TryGetValue(name, out var v) ? v : "";

    public static HttpHead Parse(string text, int size, bool response)
    {
        var h = new HttpHead { Text = text, Size = size };
        var lines = text.Split("\r\n");
        h.First = lines[0];
        var parts = h.First.Split(' ', 3);
        if (response)
        {
            if (parts.Length > 1) int.TryParse(parts[1], out h.Code);
            h.Reason = parts.Length > 2 ? parts[2] : "";
        }
        else
        {
            h.Method = parts[0];
            h.Target = parts.Length > 1 ? parts[1] : "";
        }
        foreach (var l in lines.Skip(1))
        {
            int c = l.IndexOf(':');
            if (c > 0) h.Headers[l[..c].Trim()] = l[(c + 1)..].Trim();
        }
        return h;
    }
}

/// <summary>Follows one direction of an HTTP/1.x stream: message heads and the bodies between
/// them (Content-Length, chunked, or until the connection closes).</summary>
sealed class HttpParser
{
    enum S { Head, Body, ToClose, ChunkSize, ChunkData, ChunkCrlf, Trailer, Off }
    S _s = S.Head;
    readonly bool _response;
    readonly MemoryStream _buf = new();
    long _left;
    public Action<HttpHead>? OnHead;
    public Func<bool>? NoBodyExpected;       // response to HEAD
    public Action<int>? OnBody;              // wire bytes after the head
    public Action? OnEnd;
    public bool Off => _s == S.Off;
    public bool UntilClose => _s == S.ToClose;

    public HttpParser(bool response) { _response = response; }

    public void Stop() { _s = S.Off; _buf.SetLength(0); }

    bool LooksLikeStart(ReadOnlySpan<byte> b)
    {
        if (_response)
        {
            var p = "HTTP/"u8;
            int n = Math.Min(b.Length, p.Length);
            return b[..n].SequenceEqual(p[..n]);
        }
        for (int i = 0; i < Math.Min(b.Length, 8); i++)
        {
            if (b[i] == ' ') return i >= 3;
            if (b[i] < 'A' || b[i] > 'Z') return false;
        }
        return b.Length < 8;
    }

    public void Feed(ReadOnlySpan<byte> d)
    {
        while (d.Length > 0)
        {
            switch (_s)
            {
                case S.Off:
                    return;
                case S.Head:
                {
                    int before = (int)_buf.Length;
                    _buf.Write(d);
                    var b = _buf.GetBuffer();
                    int len = (int)_buf.Length;
                    if (!LooksLikeStart(b.AsSpan(0, Math.Min(len, 16)))) { Stop(); return; }
                    int from = Math.Max(0, before - 3);
                    int e = b.AsSpan(from, len - from).IndexOf("\r\n\r\n"u8);
                    if (e < 0) { if (len > 32768) Stop(); return; }
                    e += from;
                    int headLen = e + 4;
                    var head = HttpHead.Parse(Encoding.Latin1.GetString(b, 0, e), headLen, _response);
                    d = d[(headLen - before)..];
                    _buf.SetLength(0);
                    StartBody(head);
                    break;
                }
                case S.Body:
                {
                    int n = (int)Math.Min(_left, d.Length);
                    OnBody?.Invoke(n);
                    _left -= n;
                    d = d[n..];
                    if (_left == 0) End();
                    break;
                }
                case S.ToClose:
                    OnBody?.Invoke(d.Length);
                    return;
                case S.ChunkSize:
                case S.Trailer:
                {
                    int nl = d.IndexOf((byte)'\n');
                    if (nl < 0)
                    {
                        _buf.Write(d);
                        OnBody?.Invoke(d.Length);
                        if (_buf.Length > 4096) Stop();
                        return;
                    }
                    _buf.Write(d[..(nl + 1)]);
                    OnBody?.Invoke(nl + 1);
                    d = d[(nl + 1)..];
                    string line = Encoding.Latin1.GetString(_buf.GetBuffer(), 0, (int)_buf.Length).Trim();
                    _buf.SetLength(0);
                    if (_s == S.ChunkSize)
                    {
                        int semi = line.IndexOf(';');
                        if (semi >= 0) line = line[..semi].Trim();
                        if (!long.TryParse(line, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var size) || size < 0) { Stop(); return; }
                        if (size == 0) _s = S.Trailer;
                        else { _left = size; _s = S.ChunkData; }
                    }
                    else if (line.Length == 0) End();
                    break;
                }
                case S.ChunkData:
                case S.ChunkCrlf:
                {
                    int n = (int)Math.Min(_left, d.Length);
                    OnBody?.Invoke(n);
                    _left -= n;
                    d = d[n..];
                    if (_left == 0)
                    {
                        if (_s == S.ChunkData) { _left = 2; _s = S.ChunkCrlf; }
                        else _s = S.ChunkSize;
                    }
                    break;
                }
            }
        }
    }

    void StartBody(HttpHead h)
    {
        OnHead?.Invoke(h);
        if (_response)
        {
            if (h.Code / 100 == 1) { _s = S.Head; return; }               // 100 Continue: the real answer follows
            if (h.Code is 204 or 304 || (NoBodyExpected?.Invoke() ?? false)) { End(); return; }
        }
        if (h.H("Transfer-Encoding").Contains("chunked", StringComparison.OrdinalIgnoreCase)) { _s = S.ChunkSize; return; }
        if (long.TryParse(h.H("Content-Length"), out var cl) && cl >= 0)
        {
            if (cl == 0) End();
            else { _left = cl; _s = S.Body; }
            return;
        }
        if (_response) _s = S.ToClose;
        else End();
    }

    void End()
    {
        _s = S.Head;
        OnEnd?.Invoke();
    }
}

/// <summary>What one TCP connection carries, as entries in the requests list.</summary>
sealed class ConnTraffic
{
    enum Mode { Unknown, Http, Tls, Other, Off }
    readonly Traffic _t;
    readonly string _remote, _host;
    readonly int _port, _conn;
    Mode _mode;
    RequestInfo? _entry;                                   // TLS / other TCP: one entry per connection
    readonly Queue<RequestInfo> _waiting = new();          // HTTP requests without a response yet
    RequestInfo? _sending, _answering;
    HttpParser? _req, _resp;
    MemoryStream? _hello;
    bool _closed, _anyHttp;

    readonly TcpStats _stats;

    public ConnTraffic(Traffic t, string remote, string host, int port, int conn, TcpStats stats)
    {
        _t = t; _remote = remote; _host = host; _port = port; _conn = conn; _stats = stats;
    }

    RequestInfo Add(RequestInfo r) { r.Tcp = _stats; return _t.Add(r); }

    string HostPort => _host + (_port is 80 or 443 ? "" : ":" + _port);

    public void FromPhone(ReadOnlySpan<byte> d)
    {
        if (d.Length == 0 || _closed) return;
        try
        {
            if (_mode == Mode.Unknown)
            {
                if (d[0] == 0x16) StartTls();
                else if (d[0] >= 'A' && d[0] <= 'Z') StartHttp();
                else StartOther();
            }
            switch (_mode)
            {
                case Mode.Http:
                    _req!.Feed(d);
                    if (_req.Off && !_anyHttp)                     // not HTTP after all
                    {
                        StartOther();
                        _entry!.Up += d.Length;
                    }
                    break;
                case Mode.Tls:
                    _entry!.Up += d.Length;
                    if (_hello != null)
                    {
                        _hello.Write(d);
                        if (TryServerName(_hello.GetBuffer().AsSpan(0, (int)_hello.Length), out var name) || _hello.Length > 16384)
                        {
                            if (!string.IsNullOrEmpty(name))
                            {
                                _entry.Url = "https://" + name + (_port == 443 ? "" : ":" + _port) + "/";
                                _entry.Note = "TLS server name (SNI) " + name + "; the rest is encrypted";
                            }
                            _hello = null;
                        }
                    }
                    _entry.Changed();
                    break;
                case Mode.Other:
                    _entry!.Up += d.Length;
                    _entry.Changed();
                    break;
            }
        }
        catch { _mode = Mode.Off; }
    }

    public void FromServer(ReadOnlySpan<byte> d)
    {
        if (d.Length == 0 || _closed) return;
        try
        {
            if (_mode == Mode.Unknown) StartOther();       // the server speaks first (SMTP, FTP, ...)
            switch (_mode)
            {
                case Mode.Http:
                    _resp!.Feed(d);
                    break;
                case Mode.Tls:
                case Mode.Other:
                    _entry!.FirstByte ??= DateTime.Now;
                    _entry.Down += d.Length;
                    _entry.Changed();
                    break;
            }
        }
        catch { _mode = Mode.Off; }
    }

    void StartTls()
    {
        _mode = Mode.Tls;
        _hello = new MemoryStream();
        _entry = Add(new RequestInfo
        {
            Kind = RequestKind.Https, Method = "TLS", Url = "https://" + HostPort + "/", Remote = _remote, Conn = _conn,
            Note = "encrypted: only the server is visible",
        });
    }

    void StartOther()
    {
        _mode = Mode.Other;
        _entry = Add(new RequestInfo { Kind = RequestKind.Tcp, Method = "TCP", Url = _host + ":" + _port, Remote = _remote, Conn = _conn });
    }

    void StartHttp()
    {
        _mode = Mode.Http;
        _req = new HttpParser(false)
        {
            OnHead = h =>
            {
                string url = h.Target.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ? h.Target
                    : "http://" + (h.H("Host") is { Length: > 0 } host ? host : HostPort) + h.Target;
                var r = Add(new RequestInfo
                {
                    Kind = RequestKind.Http, Method = h.Method, Url = url, Remote = _remote, Conn = _conn,
                    RequestHeaders = h.Text, Up = h.Size,
                });
                _waiting.Enqueue(r);
                _sending = r;
                _anyHttp = true;
            },
            OnBody = n => { if (_sending != null) { _sending.Up += n; _sending.Changed(); } },
        };
        _resp = new HttpParser(true)
        {
            OnHead = h =>
            {
                if (h.Code / 100 == 1)
                {
                    if (_waiting.Count > 0) { _waiting.Peek().Down += h.Size; _waiting.Peek().Changed(); }
                    return;
                }
                _answering = _waiting.Count > 0 ? _waiting.Dequeue() : null;
                if (_answering == null) return;
                _answering.FirstByte = DateTime.Now;
                _answering.Status = (h.Code + " " + h.Reason).Trim();
                _answering.ContentType = h.H("Content-Type");
                _answering.ResponseHeaders = h.Text;
                _answering.Down += h.Size;
                _answering.Changed();
            },
            NoBodyExpected = () => _answering?.Method == "HEAD",
            OnBody = n => { if (_answering != null) { _answering.Down += n; _answering.Changed(); } },
            OnEnd = () => { if (_answering != null) _t.Complete(_answering); _answering = null; },
        };
    }

    /// <summary>The connection is gone; neverConnected: the server could not be reached.</summary>
    public void Closed(string? why, bool neverConnected)
    {
        if (_closed) return;
        _closed = true;
        try
        {
            if (neverConnected)
            {
                var r = Add(new RequestInfo
                {
                    Kind = RequestKind.Tcp, Method = "TCP", Url = _host + ":" + _port, Remote = _remote, Conn = _conn,
                    Status = "could not connect",
                });
                _t.Complete(r, why);
                return;
            }
            if (_answering != null)
            {
                if (_resp!.UntilClose) _t.Complete(_answering);
                else _t.Complete(_answering, "connection closed before the end" + (why != null ? $" ({why})" : ""));
            }
            foreach (var r in _waiting)
            {
                r.Status = "no response";
                _t.Complete(r, why ?? "connection closed");
            }
            if (_entry != null)
            {
                if (_entry.Status.Length == 0) _entry.Status = why == null ? "closed" : why;
                _t.Complete(_entry);
            }
        }
        catch { }
    }

    /// <summary>Server name from a TLS ClientHello. False while more bytes are needed.</summary>
    static bool TryServerName(ReadOnlySpan<byte> b, out string? name)
    {
        name = null;
        if (b.Length < 5) return false;
        if (b[0] != 0x16) return true;
        int recLen = b[3] << 8 | b[4];
        if (b.Length < 5 + recLen) return false;
        var h = b.Slice(5, recLen);
        if (h.Length < 4 || h[0] != 1) return true;
        int p = 4 + 2 + 32;                                       // type, length, version, random
        if (p >= h.Length) return true;
        p += 1 + h[p];                                            // session id
        if (p + 2 > h.Length) return true;
        p += 2 + (h[p] << 8 | h[p + 1]);                          // cipher suites
        if (p >= h.Length) return true;
        p += 1 + h[p];                                            // compression methods
        if (p + 2 > h.Length) return true;                        // no extensions (SSL 3 / old TLS)
        int extEnd = Math.Min(h.Length, p + 2 + (h[p] << 8 | h[p + 1]));
        p += 2;
        while (p + 4 <= extEnd)
        {
            int type = h[p] << 8 | h[p + 1], len = h[p + 2] << 8 | h[p + 3];
            p += 4;
            if (type == 0 && p + 5 <= extEnd)
            {
                int nlen = h[p + 3] << 8 | h[p + 4];
                if (h[p + 2] == 0 && p + 5 + nlen <= extEnd) name = Encoding.ASCII.GetString(h.Slice(p + 5, nlen));
                return true;
            }
            p += len;
        }
        return true;
    }
}
