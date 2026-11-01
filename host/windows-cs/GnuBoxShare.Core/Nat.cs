// Nat.cs - user-space NAT: the phone's TCP connections end here and continue as ordinary
// Windows sockets; UDP is relayed, DNS answered with Windows' resolver, ping answered locally.
// All state is touched only on the engine thread; socket I/O completes elsewhere and posts back.
// GPL v2 or later.
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;

namespace GnuBoxShare;

public interface INatHost
{
    void Post(Action action);
    void SendIp(byte[] packet);
    /// <summary>False while too much is queued for the slow phone link.</summary>
    bool LinkHasRoom { get; }
}

public sealed class Nat
{
    internal readonly INatHost Host;
    internal readonly Dictionary<string, string> Rewrite;
    readonly Dictionary<(int sport, uint dst, int dport), TcpConn> _tcp = new();
    readonly Dictionary<(int sport, uint dst, int dport), UdpFlow> _udp = new();
    readonly Dictionary<string, (DateTime until, IPAddress[]? addrs)> _dnsCache = new();
    public int Connections => _tcp.Count;

    public Nat(INatHost host, Dictionary<string, string> rewrite) { Host = host; Rewrite = rewrite; }

    internal IPAddress RealAddress(byte[] dst)
    {
        var s = Ip.Str(dst);
        return IPAddress.Parse(Rewrite.TryGetValue(s, out var r) ? r : s);
    }

    internal void Remove(TcpConn c) => _tcp.Remove(c.Key);

    public void Reset()
    {
        foreach (var c in _tcp.Values.ToList()) c.Close("link closed");
        _tcp.Clear();
        foreach (var u in _udp.Values) u.Close();
        _udp.Clear();
    }

    public void OnIp(byte[] pkt)
    {
        if (pkt.Length < 20 || pkt[0] >> 4 != 4) return;
        int ihl = (pkt[0] & 15) * 4;
        int total = Math.Min(BinaryPrimitives.ReadUInt16BigEndian(pkt.AsSpan(2)), pkt.Length);
        if (total < ihl) return;
        byte proto = pkt[9];
        var src = pkt[12..16];
        var dst = pkt[16..20];
        var body = pkt.AsSpan(ihl, total - ihl);
        switch (proto)
        {
            case 6: OnTcp(dst, body); break;
            case 17: OnUdp(dst, body); break;
            case 1: OnIcmp(dst, body); break;
        }
    }

    void OnIcmp(byte[] dst, ReadOnlySpan<byte> body)
    {
        if (body.Length < 8 || body[0] != 8) return;                // echo request: answer it ourselves
        var rep = body.ToArray();
        rep[0] = 0; rep[2] = 0; rep[3] = 0;
        BinaryPrimitives.WriteUInt16BigEndian(rep.AsSpan(2), Checksum.Internet(rep));
        Host.SendIp(Ip.V4(dst, Ip.Phone, 1, rep));
    }

    // ---------------------------------------------------------------- UDP and DNS
    void OnUdp(byte[] dst, ReadOnlySpan<byte> body)
    {
        if (body.Length < 8) return;
        int sport = BinaryPrimitives.ReadUInt16BigEndian(body);
        int dport = BinaryPrimitives.ReadUInt16BigEndian(body[2..]);
        var data = body[8..].ToArray();
        if (dport == 53) { Dns(dst, sport, data); return; }
        var key = (sport, BinaryPrimitives.ReadUInt32BigEndian(dst), dport);
        if (!_udp.TryGetValue(key, out var flow))
        {
            flow = new UdpFlow(this, sport, dst, dport);
            _udp[key] = flow;
        }
        flow.Send(data);
    }

    void Dns(byte[] dst, int sport, byte[] query)
    {
        if (query.Length < 12 || BinaryPrimitives.ReadUInt16BigEndian(query.AsSpan(4)) != 1) return;
        int i = 12;
        var labels = new List<string>();
        while (i < query.Length && query[i] != 0)
        {
            int l = query[i];
            if (i + 1 + l > query.Length) return;
            labels.Add(System.Text.Encoding.ASCII.GetString(query, i + 1, l));
            i += 1 + l;
        }
        i++;
        if (i + 4 > query.Length) return;
        ushort qtype = BinaryPrimitives.ReadUInt16BigEndian(query.AsSpan(i));
        var question = query[12..(i + 4)];
        string name = string.Join(".", labels);

        void Answer(IPAddress[]? addrs)
        {
            // addrs null = the name doesn't exist; only A records are given (IPv4 link)
            var list = addrs == null || qtype is not (1 or 255) ? new List<IPAddress>() : addrs.Take(8).ToList();
            var ms = new MemoryStream();
            var hdr = new byte[12];
            hdr[0] = query[0]; hdr[1] = query[1];
            ushort flags = (ushort)(0x8180 | (query[2] & 0x01) << 8 | (addrs == null ? 3 : 0));
            BinaryPrimitives.WriteUInt16BigEndian(hdr.AsSpan(2), flags);
            BinaryPrimitives.WriteUInt16BigEndian(hdr.AsSpan(4), 1);
            BinaryPrimitives.WriteUInt16BigEndian(hdr.AsSpan(6), (ushort)list.Count);
            ms.Write(hdr);
            ms.Write(question);
            foreach (var a in list)
            {
                ms.Write(new byte[] { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4 });
                ms.Write(a.GetAddressBytes());
            }
            Host.SendIp(Ip.Udp(dst, 53, Ip.Phone, sport, ms.ToArray()));
        }

        if (_dnsCache.TryGetValue(name, out var c) && c.until > DateTime.UtcNow) { Answer(c.addrs); return; }
        _ = Task.Run(async () =>
        {
            IPAddress[]? addrs;
            try
            {
                var all = await System.Net.Dns.GetHostAddressesAsync(name, AddressFamily.InterNetwork);
                addrs = all.Where(a => a.AddressFamily == AddressFamily.InterNetwork).Distinct().ToArray();
            }
            catch { addrs = null; }
            Host.Post(() => { _dnsCache[name] = (DateTime.UtcNow.AddSeconds(60), addrs); Answer(addrs); });
        });
    }

    // ---------------------------------------------------------------- TCP
    void OnTcp(byte[] dst, ReadOnlySpan<byte> seg)
    {
        if (seg.Length < 20) return;
        int sport = BinaryPrimitives.ReadUInt16BigEndian(seg);
        int dport = BinaryPrimitives.ReadUInt16BigEndian(seg[2..]);
        uint seq = BinaryPrimitives.ReadUInt32BigEndian(seg[4..]);
        uint ack = BinaryPrimitives.ReadUInt32BigEndian(seg[8..]);
        int hl = (seg[12] >> 4) * 4;
        byte flags = seg[13];
        int win = BinaryPrimitives.ReadUInt16BigEndian(seg[14..]);
        if (hl < 20 || hl > seg.Length) return;
        var data = seg[hl..].ToArray();
        var key = (sport, BinaryPrimitives.ReadUInt32BigEndian(dst), dport);
        if (!_tcp.TryGetValue(key, out var c))
        {
            if ((flags & Ip.Syn) != 0 && (flags & Ip.Ack) == 0)
            {
                int mss = 536;
                var o = seg[20..hl];
                for (int i = 0; i < o.Length;)
                {
                    if (o[i] == 0) break;
                    if (o[i] == 1) { i++; continue; }
                    if (i + 1 >= o.Length || o[i + 1] < 2) break;
                    if (o[i] == 2 && o[i + 1] == 4 && i + 4 <= o.Length) mss = BinaryPrimitives.ReadUInt16BigEndian(o[(i + 2)..]);
                    i += o[i + 1];
                }
                _tcp[key] = new TcpConn(this, key, dst, dport, sport, seq, win, mss);
            }
            else if ((flags & Ip.Rst) == 0)                                   // unknown connection: reset it
                Host.SendIp(Ip.Tcp(dst, dport, Ip.Phone, sport, ack, (uint)(seq + data.Length), Ip.Rst | Ip.Ack, 0));
            return;
        }
        c.OnSegment(seq, ack, flags, win, data);
    }

    public void Tick()
    {
        var now = DateTime.UtcNow;
        foreach (var c in _tcp.Values.ToList()) c.Tick(now);
        foreach (var (k, u) in _udp.ToList())
            if ((now - u.Last).TotalSeconds > 60) { u.Close(); _udp.Remove(k); }
    }

    public void Pump()
    {
        foreach (var c in _tcp.Values.ToList()) c.Pump();
    }
}

sealed class UdpFlow
{
    readonly Nat _nat;
    readonly int _sport, _dport;
    readonly byte[] _dst;
    readonly Socket _sock = new(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
    readonly IPEndPoint _remote;
    public DateTime Last = DateTime.UtcNow;
    bool _closed;

    public UdpFlow(Nat nat, int sport, byte[] dst, int dport)
    {
        _nat = nat; _sport = sport; _dst = dst; _dport = dport;
        _remote = new IPEndPoint(nat.RealAddress(dst), dport);
        _sock.Bind(new IPEndPoint(IPAddress.Any, 0));
        _ = ReceiveLoop();
    }

    public void Send(byte[] data)
    {
        Last = DateTime.UtcNow;
        try { _sock.SendTo(data, _remote); } catch (SocketException) { }
    }

    async Task ReceiveLoop()
    {
        var buf = new byte[65536];
        while (!_closed)
        {
            SocketReceiveFromResult r;
            try { r = await _sock.ReceiveFromAsync(buf, SocketFlags.None, new IPEndPoint(IPAddress.Any, 0)); }
            catch { if (_closed) return; continue; }
            var data = buf.AsSpan(0, r.ReceivedBytes).ToArray();
            _nat.Host.Post(() => { Last = DateTime.UtcNow; _nat.Host.SendIp(Ip.Udp(_dst, _dport, Ip.Phone, _sport, data)); });
        }
    }

    public void Close() { _closed = true; _sock.Dispose(); }
}

/// <summary>One phone TCP connection, ended here and continued as a real socket.</summary>
sealed class TcpConn
{
    const int RecvWindow = 65535;
    const int SendWindowCap = 32768;

    readonly Nat _nat;
    public readonly (int sport, uint dst, int dport) Key;
    readonly byte[] _dst;
    readonly int _dport, _sport;
    uint _rcvNxt;
    readonly uint _iss;
    uint _sndUna, _sndNxt;
    int _peerWin;
    readonly int _mss;
    readonly ByteQueue _toServer = new();      // phone -> server, not yet handed to the socket
    readonly ByteQueue _unacked = new();       // server -> phone, sent, not yet acknowledged
    readonly ByteQueue _pending = new();       // server -> phone, read from the socket, not yet sent
    int _sendingBytes;
    bool _sending;
    string _state = "connecting";
    bool _peerFin, _serverEof, _finSent, _shutdownSent;
    double _rto = 3;
    DateTime? _rtxAt;
    DateTime _lastActivity = DateTime.UtcNow;
    long _bytesIn, _bytesOut;
    readonly Socket _sock = new(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
    volatile bool _readerMayRead;

    public TcpConn(Nat nat, (int, uint, int) key, byte[] dst, int dport, int sport, uint isnPeer, int peerWin, int peerMss)
    {
        _nat = nat; Key = key; _dst = dst; _dport = dport; _sport = sport;
        _rcvNxt = isnPeer + 1;
        _iss = (uint)Random.Shared.NextInt64(0, uint.MaxValue);
        _sndUna = _sndNxt = _iss;
        _peerWin = peerWin;
        _mss = Math.Min(Ip.Mss, peerMss > 0 ? peerMss : 536);
        _sock.NoDelay = true;
        var ep = new IPEndPoint(nat.RealAddress(dst), dport);
        Log.Detail($"TCP open {ep}");
        _ = ConnectAsync(ep);
    }

    async Task ConnectAsync(IPEndPoint ep)
    {
        try
        {
            await _sock.ConnectAsync(ep);
            _nat.Host.Post(Connected);
        }
        catch
        {
            _nat.Host.Post(Failed);
        }
    }

    void SendSeg(byte flags, ReadOnlySpan<byte> data = default, uint? seq = null, int mss = 0)
    {
        int win = Math.Max(0, RecvWindow - _toServer.Count - _sendingBytes);
        _nat.Host.SendIp(Ip.Tcp(_dst, _dport, Ip.Phone, _sport, seq ?? _sndNxt, _rcvNxt, flags, win, data, mss));
    }

    void Connected()
    {
        if (_state != "connecting") return;
        _state = "synrcvd";
        SendSeg(Ip.Syn | Ip.Ack, mss: Ip.Mss);
        _sndNxt = _iss + 1;
        _rtxAt = DateTime.UtcNow.AddSeconds(_rto);
    }

    void Failed()
    {
        if (_state != "connecting") return;
        SendSeg(Ip.Rst | Ip.Ack, seq: _iss);
        Close("connection refused or unreachable");
    }

    public void OnSegment(uint seq, uint ack, byte flags, int win, byte[] data)
    {
        _lastActivity = DateTime.UtcNow;
        _peerWin = win;
        if ((flags & Ip.Rst) != 0) { Close("reset by the phone"); return; }
        if ((flags & Ip.Syn) != 0)                                    // retransmitted SYN
        {
            if (_state == "synrcvd") SendSeg(Ip.Syn | Ip.Ack, seq: _iss, mss: Ip.Mss);
            return;
        }
        if ((flags & Ip.Ack) != 0) OnAck(ack);
        if (_state is "connecting" or "closed") return;
        if (data.Length > 0)
        {
            if (seq == _rcvNxt && _toServer.Count + _sendingBytes + data.Length <= RecvWindow)
            {
                _toServer.Append(data);
                _rcvNxt += (uint)data.Length;
                _bytesOut += data.Length;
            }
            SendSeg(Ip.Ack);                                          // in order, duplicate or out of order
        }
        if ((flags & Ip.Fin) != 0 && !_peerFin && seq + (uint)data.Length == _rcvNxt)
        {
            _peerFin = true;
            _rcvNxt++;
            SendSeg(Ip.Ack);
        }
        FlushToServer();
        Pump();
    }

    void OnAck(uint ack)
    {
        uint acked = ack - _sndUna;
        uint inflight = _sndNxt - _sndUna;
        if (acked == 0 || acked > inflight) return;
        if (_state == "synrcvd")
        {
            _state = "open";
            _sndUna++;
            acked--;
            _readerMayRead = true;
            _ = ReadLoop();
        }
        _unacked.Discard((int)Math.Min(acked, (uint)_unacked.Count));
        _sndUna += acked;
        _rto = 3;
        _rtxAt = _sndNxt != _sndUna ? DateTime.UtcNow.AddSeconds(_rto) : null;
        if (_finSent && _sndUna == _sndNxt && _peerFin) Close(null);
    }

    void FlushToServer()
    {
        if (_state != "open") return;
        if (!_sending && _toServer.Count > 0)
        {
            var chunk = _toServer.TakeAll();
            _sending = true;
            _sendingBytes = chunk.Length;
            _ = SendAsync(chunk);
        }
        if (_peerFin && !_sending && _toServer.Count == 0 && !_shutdownSent)
        {
            _shutdownSent = true;
            try { _sock.Shutdown(SocketShutdown.Send); } catch { }
        }
    }

    async Task SendAsync(byte[] chunk)
    {
        try
        {
            int off = 0;
            while (off < chunk.Length)
                off += await _sock.SendAsync(chunk.AsMemory(off), SocketFlags.None);
            _nat.Host.Post(() =>
            {
                _sending = false;
                _sendingBytes = 0;
                if (_state == "open") { SendSeg(Ip.Ack); FlushToServer(); }   // window update
            });
        }
        catch
        {
            _nat.Host.Post(() => { if (_state != "closed") { SendSeg(Ip.Rst | Ip.Ack); Close("server connection lost"); } });
        }
    }

    async Task ReadLoop()
    {
        var buf = new byte[32768];
        while (true)
        {
            if (!_readerMayRead) { await Task.Delay(15); if (_state == "closed") return; continue; }
            int n;
            try { n = await _sock.ReceiveAsync(buf, SocketFlags.None); }
            catch { n = 0; }
            var data = n > 0 ? buf.AsSpan(0, n).ToArray() : null;
            _readerMayRead = false;                               // the engine decides when to read again
            _nat.Host.Post(() => OnServerData(data));
            if (n == 0) return;
        }
    }

    void OnServerData(byte[]? data)
    {
        if (_state == "closed") return;
        if (data == null) _serverEof = true;
        else { _pending.Append(data); _bytesIn += data.Length; _lastActivity = DateTime.UtcNow; }
        Pump();
    }

    public void Pump()
    {
        if (_state != "open") return;
        while (_pending.Count > 0)
        {
            int inflight = (int)(_sndNxt - _sndUna);
            int room = Math.Min(_peerWin, SendWindowCap) - inflight;
            if (room <= 0) break;
            int n = Math.Min(Math.Min(_mss, room), _pending.Count);
            var chunk = _pending.Peek(n).ToArray();
            _pending.Discard(n);
            SendSeg(Ip.Ack | Ip.Psh, chunk);
            _unacked.Append(chunk);
            _sndNxt += (uint)n;
            _rtxAt ??= DateTime.UtcNow.AddSeconds(_rto);
        }
        if (_serverEof && _pending.Count == 0 && !_finSent)
        {
            SendSeg(Ip.Fin | Ip.Ack);
            _finSent = true;
            _sndNxt++;
            _rtxAt ??= DateTime.UtcNow.AddSeconds(_rto);
        }
        // read more from the server when little is waiting here and the phone link has room
        if (!_serverEof && _pending.Count < 16384 && _nat.Host.LinkHasRoom)
            _readerMayRead = true;
    }

    public void Tick(DateTime now)
    {
        if (_rtxAt is { } at && now >= at)
        {
            // resend the oldest unacknowledged segment (or the SYN-ACK / FIN)
            if (_state == "synrcvd") SendSeg(Ip.Syn | Ip.Ack, seq: _iss, mss: Ip.Mss);
            else if (_unacked.Count > 0) SendSeg(Ip.Ack | Ip.Psh, _unacked.Peek(_mss), seq: _sndUna);
            else if (_finSent) SendSeg(Ip.Fin | Ip.Ack, seq: _sndNxt - 1);
            _rto = Math.Min(_rto * 2, 30);
            _rtxAt = now.AddSeconds(_rto);
        }
        if ((now - _lastActivity).TotalSeconds > 600) { SendSeg(Ip.Rst | Ip.Ack); Close("idle 10 min"); }
        else if (_state == "connecting" && (now - _lastActivity).TotalSeconds > 30) Failed();
    }

    public void Close(string? why)
    {
        if (_state == "closed") return;
        _state = "closed";
        try { _sock.Dispose(); } catch { }
        _nat.Remove(this);
        Log.Detail($"TCP closed {Ip.Str(_dst)}:{_dport}  down {_bytesIn} B, up {_bytesOut} B" + (why != null ? $"  ({why})" : ""));
    }
}
