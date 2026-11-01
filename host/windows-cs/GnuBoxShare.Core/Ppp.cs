// Ppp.cs - HDLC framing and the PPP server side (LCP, PAP, IPCP). GPL v2 or later.
using System.Buffers.Binary;

namespace GnuBoxShare;

public static class Hdlc
{
    public static readonly bool[] EscapeAll = EscapeTable(0xFFFFFFFF);

    /// <summary>Which bytes to escape: flag, escape, and control chars the peer's ACCM asks for.</summary>
    public static bool[] EscapeTable(uint accm)
    {
        var t = new bool[256];
        for (int b = 0; b < 256; b++)
            t[b] = b == 0x7E || b == 0x7D || (b < 0x20 && ((accm >> b) & 1) != 0);
        return t;
    }

    public static byte[] Encode(ReadOnlySpan<byte> payload, bool[] esc)
    {
        ushort fcs = (ushort)(Checksum.Fcs16(payload) ^ 0xFFFF);
        var outb = new byte[(payload.Length + 2) * 2 + 2];
        int o = 0;
        outb[o++] = 0x7E;
        void Put(byte b, byte[] buf, ref int i)
        {
            if (esc[b]) { buf[i++] = 0x7D; buf[i++] = (byte)(b ^ 0x20); }
            else buf[i++] = b;
        }
        foreach (var b in payload) Put(b, outb, ref o);
        Put((byte)(fcs & 0xFF), outb, ref o);
        Put((byte)(fcs >> 8), outb, ref o);
        outb[o++] = 0x7E;
        Array.Resize(ref outb, o);
        return outb;
    }
}

public sealed class HdlcDecoder
{
    readonly byte[] _buf = new byte[4096];
    int _len;
    bool _esc;

    public void Feed(ReadOnlySpan<byte> data, Action<byte[]> onFrame)
    {
        foreach (var raw in data)
        {
            byte b = raw;
            if (b == 0x7E)
            {
                if (_len >= 4)
                {
                    if (Checksum.Fcs16(_buf.AsSpan(0, _len)) == 0xF0B8)
                        onFrame(_buf.AsSpan(0, _len - 2).ToArray());
                    else
                        Log.Detail("PPP: bad FCS, frame dropped");
                }
                _len = 0;
                _esc = false;
            }
            else if (b == 0x7D)
                _esc = true;
            else
            {
                if (_esc) { b ^= 0x20; _esc = false; }
                if (_len < _buf.Length) _buf[_len++] = b;
                else _len = 0;
            }
        }
    }
}

public interface IPppLink
{
    void SendFrame(byte[] frame, bool[] escape);
    void OnIp(byte[] packet);
    void OnReset();
}

/// <summary>The server side of PPP: accepts the phone's options, gives it 10.64.64.2 and DNS 10.64.64.1.</summary>
public sealed class Ppp
{
    public const ushort Lcp = 0xC021, Pap = 0xC023, Ipcp = 0x8021, Ipv4 = 0x0021;

    readonly IPppLink _link;
    byte _ident = 1;
    bool _lcpOpen, _lcpAckedUs, _lcpAckedPeer, _ipcpAckedUs, _ipcpAckedPeer;
    readonly uint _magic = (uint)Random.Shared.Next(1, int.MaxValue);
    DateTime _lastReq = DateTime.MinValue;
    bool[] _esc = Hdlc.EscapeAll;
    bool _passive;

    public bool IpUp { get; private set; }

    public Ppp(IPppLink link) { _link = link; }

    // ---- sending
    void Send(ushort proto, ReadOnlySpan<byte> payload)
    {
        var frame = new byte[4 + payload.Length];
        frame[0] = 0xFF; frame[1] = 0x03;
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(2), proto);
        payload.CopyTo(frame.AsSpan(4));
        // LCP always uses the default escaping; everything else the negotiated ACCM
        _link.SendFrame(frame, proto == Lcp ? Hdlc.EscapeAll : _esc);
    }

    void SendCp(ushort proto, byte code, byte ident, ReadOnlySpan<byte> data)
    {
        var p = new byte[4 + data.Length];
        p[0] = code; p[1] = ident;
        BinaryPrimitives.WriteUInt16BigEndian(p.AsSpan(2), (ushort)p.Length);
        data.CopyTo(p.AsSpan(4));
        Send(proto, p);
    }

    static byte[] Opts(params (byte type, byte[] value)[] opts)
    {
        var ms = new MemoryStream();
        foreach (var (t, v) in opts) { ms.WriteByte(t); ms.WriteByte((byte)(v.Length + 2)); ms.Write(v); }
        return ms.ToArray();
    }

    static List<(byte type, byte[] value)> ParseOpts(ReadOnlySpan<byte> data)
    {
        var list = new List<(byte, byte[])>();
        int i = 0;
        while (i + 2 <= data.Length)
        {
            int l = data[i + 1];
            if (l < 2 || i + l > data.Length) break;
            list.Add((data[i], data.Slice(i + 2, l - 2).ToArray()));
            i += l;
        }
        return list;
    }

    void LcpRequest()
    {
        _ident++;
        var magic = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(magic, _magic);
        SendCp(Lcp, 1, _ident, Opts((2, new byte[4]), (5, magic)));     // ACCM 0, magic number
    }

    void IpcpRequest()
    {
        _ident++;
        SendCp(Ipcp, 1, _ident, Opts((3, Ip.Gateway)));
    }

    public void Tick()
    {
        if (_passive) return;
        var now = DateTime.UtcNow;
        if ((now - _lastReq).TotalSeconds < 3) return;
        if (!(_lcpAckedUs && _lcpAckedPeer)) { _lastReq = now; LcpRequest(); }
        else if (!(_ipcpAckedUs && _ipcpAckedPeer)) { _lastReq = now; IpcpRequest(); }
    }

    public void SendIp(byte[] packet)
    {
        if (IpUp) Send(Ipv4, packet);
    }

    // ---- receiving
    public void OnFrame(byte[] f)
    {
        ReadOnlySpan<byte> s = f;
        if (s.Length >= 2 && s[0] == 0xFF && s[1] == 0x03) s = s[2..];      // address/control may be compressed
        if (s.IsEmpty) return;
        ushort proto;
        if ((s[0] & 1) != 0) { proto = s[0]; s = s[1..]; }                  // protocol field compressed
        else
        {
            if (s.Length < 2) return;
            proto = BinaryPrimitives.ReadUInt16BigEndian(s);
            s = s[2..];
        }
        switch (proto)
        {
            case Ipv4: if (IpUp) _link.OnIp(s.ToArray()); break;
            case Lcp: OnLcp(s); break;
            case Ipcp: OnIpcp(s); break;
            case Pap: OnPap(s); break;
            default:
                if (_lcpOpen)                                               // CCP, IPv6CP...: Protocol-Reject
                {
                    Log.Detail($"PPP: rejecting protocol 0x{proto:x4}");
                    var data = new byte[2 + Math.Min(s.Length, 1000)];
                    BinaryPrimitives.WriteUInt16BigEndian(data, proto);
                    s[..(data.Length - 2)].CopyTo(data.AsSpan(2));
                    _ident++;
                    SendCp(Lcp, 8, _ident, data);
                }
                break;
        }
    }

    static bool Header(ReadOnlySpan<byte> d, out byte code, out byte ident, out byte[] body)
    {
        code = ident = 0; body = Array.Empty<byte>();
        if (d.Length < 4) return false;
        code = d[0]; ident = d[1];
        int len = Math.Min(BinaryPrimitives.ReadUInt16BigEndian(d[2..]), d.Length);
        if (len < 4) return false;
        body = d[4..len].ToArray();
        return true;
    }

    void OnLcp(ReadOnlySpan<byte> d)
    {
        if (!Header(d, out var code, out var ident, out var body)) return;
        switch (code)
        {
            case 1:                                                         // Configure-Request
                _passive = false;
                var rej = new List<(byte, byte[])>();
                foreach (var (t, v) in ParseOpts(body))
                    if (t is not (1 or 2 or 5 or 7 or 8))                   // MRU, ACCM, magic, PFC, ACFC are fine
                        rej.Add((t, v));                                    // authentication (3) and the rest: no
                if (rej.Count > 0) { SendCp(Lcp, 4, ident, Opts(rej.ToArray())); return; }
                SendCp(Lcp, 2, ident, body);                                // Configure-Ack
                uint accm = 0xFFFFFFFF;
                foreach (var (t, v) in ParseOpts(body))
                    if (t == 2 && v.Length == 4) accm = BinaryPrimitives.ReadUInt32BigEndian(v);
                _esc = Hdlc.EscapeTable(accm);
                if (_lcpOpen && _lcpAckedPeer)
                {
                    Log.Info("PPP: the phone restarted the link");
                    IpUp = false;
                    _ipcpAckedUs = _ipcpAckedPeer = false;
                    _lcpAckedUs = false;
                }
                _lcpAckedPeer = true;
                if (!_lcpAckedUs) { LcpRequest(); _lastReq = DateTime.UtcNow; }
                CheckLcp();
                break;
            case 2: _lcpAckedUs = true; CheckLcp(); break;                  // Configure-Ack
            case 3: case 4: LcpRequest(); break;                            // Nak / Reject
            case 5:                                                         // Terminate-Request
                SendCp(Lcp, 6, ident, ReadOnlySpan<byte>.Empty);
                Log.Info("PPP: the phone closed the link");
                Reset();
                break;
            case 9:                                                         // Echo-Request
                var rep = new byte[Math.Max(4, body.Length)];
                BinaryPrimitives.WriteUInt32BigEndian(rep, _magic);
                if (body.Length > 4) body.AsSpan(4).CopyTo(rep.AsSpan(4));
                SendCp(Lcp, 10, ident, rep);
                break;
        }
    }

    void CheckLcp()
    {
        if (_lcpAckedUs && _lcpAckedPeer && !_lcpOpen)
        {
            _lcpOpen = true;
            Log.Info("PPP: link up (LCP)");
            IpcpRequest();
            _lastReq = DateTime.UtcNow;
        }
    }

    void OnPap(ReadOnlySpan<byte> d)
    {
        if (Header(d, out var code, out var ident, out _) && code == 1)    // Authenticate-Request: accept
            SendCp(Pap, 2, ident, new byte[] { 2, (byte)'O', (byte)'K' });
    }

    void OnIpcp(ReadOnlySpan<byte> d)
    {
        if (!Header(d, out var code, out var ident, out var body)) return;
        switch (code)
        {
            case 1:
                var rej = new List<(byte, byte[])>();
                var nak = new List<(byte, byte[])>();
                foreach (var (t, v) in ParseOpts(body))
                {
                    if (t == 3) { if (!v.AsSpan().SequenceEqual(Ip.Phone)) nak.Add((t, Ip.Phone)); }
                    else if (t is 129 or 131) { if (!v.AsSpan().SequenceEqual(Ip.Gateway)) nak.Add((t, Ip.Gateway)); }
                    else rej.Add((t, v));                                   // VJ compression etc.
                }
                if (rej.Count > 0) SendCp(Ipcp, 4, ident, Opts(rej.ToArray()));
                else if (nak.Count > 0) SendCp(Ipcp, 3, ident, Opts(nak.ToArray()));
                else { SendCp(Ipcp, 2, ident, body); _ipcpAckedPeer = true; CheckIpcp(); }
                break;
            case 2: _ipcpAckedUs = true; CheckIpcp(); break;
            case 3: case 4: IpcpRequest(); break;
            case 5: SendCp(Ipcp, 6, ident, ReadOnlySpan<byte>.Empty); IpUp = false; break;
        }
    }

    void CheckIpcp()
    {
        if (_ipcpAckedUs && _ipcpAckedPeer && !IpUp)
        {
            IpUp = true;
            Log.Info($"PPP: IP up - phone {Ip.Str(Ip.Phone)}, gateway and DNS {Ip.Str(Ip.Gateway)}");
        }
    }

    void Reset()
    {
        _lcpOpen = _lcpAckedUs = _lcpAckedPeer = false;
        _ipcpAckedUs = _ipcpAckedPeer = false;
        IpUp = false;
        _esc = Hdlc.EscapeAll;
        _passive = true;
        _link.OnReset();
    }
}
