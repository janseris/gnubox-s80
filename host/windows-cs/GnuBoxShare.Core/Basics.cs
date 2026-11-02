// Basics.cs - logging, status, byte queue, checksums. GPL v2 or later, like GnuBox.
using System.Buffers.Binary;

namespace GnuBoxShare;

public static class Log
{
    /// <summary>Where log lines go (the window replaces it). Lines carry the time.</summary>
    public static Action<string> Sink = Console.WriteLine;
    public static volatile bool Verbose;

    /// <summary>Also gets every line, details included, with milliseconds (the log file).</summary>
    public static Action<string>? FileSink;

    public static void Info(string text)
    {
        var now = DateTime.Now;
        Sink(now.ToString("HH:mm:ss") + " " + text);
        FileSink?.Invoke(now.ToString("HH:mm:ss.fff") + " " + text);
    }

    public static void Detail(string text)
    {
        if (Verbose) Info(text);
        else FileSink?.Invoke(DateTime.Now.ToString("HH:mm:ss.fff") + "   " + text);
    }
}

public enum LinkState { Stopped, Waiting, Negotiating, Up, Error }

/// <summary>What the window shows; written by the engine thread, read by the UI.</summary>
public sealed class Status
{
    public volatile LinkState State = LinkState.Stopped;
    public volatile string Where = "";
    public volatile string Phone = "";
    long _rx, _tx;
    public long Rx => Interlocked.Read(ref _rx);     // bytes from the phone (raw link)
    public long Tx => Interlocked.Read(ref _tx);     // bytes to the phone (raw link)
    public void AddRx(int n) => Interlocked.Add(ref _rx, n);
    public void AddTx(int n) => Interlocked.Add(ref _tx, n);
    public volatile int Connections;
    /// <summary>The requests list; the window keeps one across Start/Stop.</summary>
    public Traffic Traffic { get; set; } = new();
    /// <summary>Link diagnostics for the window (what limits the speed).</summary>
    public LinkDiag Diag { get; } = new();
    /// <summary>Test switch: keep fewer packets waiting for Bluetooth (4 instead of 12).</summary>
    public volatile bool ShortQueue;
    long _sinceTicks;
    public DateTime? Since
    {
        get { var t = Interlocked.Read(ref _sinceTicks); return t == 0 ? null : new DateTime(t); }
        set => Interlocked.Exchange(ref _sinceTicks, value?.Ticks ?? 0);
    }
}

/// <summary>What limits the speed: counters written by the engine, read by the window.</summary>
public sealed class LinkDiag
{
    long _writeBusyTicks, _ticksData, _ticksWindowFull, _ticksQueueFull;
    public long WriteBusyTicks => Interlocked.Read(ref _writeBusyTicks);      // Stopwatch ticks spent in Bluetooth send
    public void AddWriteBusy(long t) => Interlocked.Add(ref _writeBusyTicks, t);
    /// <summary>50 ms samples while the server side had data for the phone ...</summary>
    public long TicksData => Interlocked.Read(ref _ticksData);
    /// <summary>... and of those: the phone's TCP window was full / the Bluetooth queue was full.</summary>
    public long TicksWindowFull => Interlocked.Read(ref _ticksWindowFull);
    public long TicksQueueFull => Interlocked.Read(ref _ticksQueueFull);
    internal void Sample(bool data, bool windowFull, bool queueFull)
    {
        if (!data) return;
        Interlocked.Increment(ref _ticksData);
        if (windowFull) Interlocked.Increment(ref _ticksWindowFull);
        if (queueFull) Interlocked.Increment(ref _ticksQueueFull);
    }
    public volatile int QueuedBytes;          // HDLC bytes waiting for the Bluetooth send
    public volatile int RfcommMtu;            // serial channel frame size agreed with the phone (0 = unknown)
    public volatile int PhoneWindow;          // last TCP window the phone advertised on a busy connection
    public volatile int PhoneWindowMax;       // largest seen since Start
    public volatile int RttMs;                // smoothed round trip of data segments (0 = no sample yet)
    public volatile int RttMinMs;
    public volatile string Compression = "";  // what compression the phone asked for (we refuse it)
    internal void Rtt(double ms)
    {
        int m = (int)Math.Round(ms);
        RttMs = RttMs == 0 ? m : (RttMs * 7 + m) / 8;
        if (RttMinMs == 0 || m < RttMinMs) RttMinMs = m;
    }
}

/// <summary>Per TCP connection: the phone's window and the round trip, for the request details.</summary>
public sealed class TcpStats
{
    public volatile int PhoneWindow, PhoneWindowMax, RttMs, RttMinMs, RttSamples;
    public long TicksData, TicksWindowFull;
}

/// <summary>A simple growable FIFO of bytes.</summary>
public sealed class ByteQueue
{
    byte[] _buf = new byte[4096];
    int _start, _count;
    public int Count => _count;

    public void Append(ReadOnlySpan<byte> data)
    {
        if (_start + _count + data.Length > _buf.Length)
        {
            int need = _count + data.Length;
            if (need > _buf.Length)
            {
                var nb = new byte[Math.Max(need, _buf.Length * 2)];
                Buffer.BlockCopy(_buf, _start, nb, 0, _count);
                _buf = nb;
            }
            else
                Buffer.BlockCopy(_buf, _start, _buf, 0, _count);
            _start = 0;
        }
        data.CopyTo(_buf.AsSpan(_start + _count));
        _count += data.Length;
    }

    public ReadOnlySpan<byte> Peek(int n) => _buf.AsSpan(_start, Math.Min(n, _count));
    public void Discard(int n) { n = Math.Min(n, _count); _start += n; _count -= n; if (_count == 0) _start = 0; }
    public byte[] TakeAll() { var r = Peek(_count).ToArray(); Discard(_count); return r; }
    public void Clear() { _start = 0; _count = 0; }
}

public static class Checksum
{
    public static ushort Internet(ReadOnlySpan<byte> data, uint start = 0)
    {
        uint sum = start;
        int i = 0;
        for (; i + 1 < data.Length; i += 2)
            sum += (uint)(data[i] << 8 | data[i + 1]);
        if (i < data.Length)
            sum += (uint)(data[i] << 8);
        while (sum >> 16 != 0)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return (ushort)~sum;
    }

    /// <summary>TCP/UDP checksum with the IPv4 pseudo header.</summary>
    public static ushort Transport(ReadOnlySpan<byte> src, ReadOnlySpan<byte> dst, byte proto, ReadOnlySpan<byte> segment)
    {
        uint sum = 0;
        sum += (uint)(src[0] << 8 | src[1]) + (uint)(src[2] << 8 | src[3]);
        sum += (uint)(dst[0] << 8 | dst[1]) + (uint)(dst[2] << 8 | dst[3]);
        sum += proto;
        sum += (uint)segment.Length;
        return Internet(segment, sum);
    }

    static readonly ushort[] FcsTable = BuildFcs();

    static ushort[] BuildFcs()
    {
        var t = new ushort[256];
        for (int b = 0; b < 256; b++)
        {
            int v = b;
            for (int k = 0; k < 8; k++)
                v = (v & 1) != 0 ? (v >> 1) ^ 0x8408 : v >> 1;
            t[b] = (ushort)v;
        }
        return t;
    }

    /// <summary>PPP FCS-16 (RFC 1662).</summary>
    public static ushort Fcs16(ReadOnlySpan<byte> data, ushort fcs = 0xFFFF)
    {
        foreach (var b in data)
            fcs = (ushort)((fcs >> 8) ^ FcsTable[(fcs ^ b) & 0xFF]);
        return fcs;
    }
}

public static class Ip
{
    public static readonly byte[] Phone = { 10, 64, 64, 2 };
    public static readonly byte[] Gateway = { 10, 64, 64, 1 };
    public const int Mss = 1360;

    public static string Str(ReadOnlySpan<byte> a) => $"{a[0]}.{a[1]}.{a[2]}.{a[3]}";

    static int _ident;

    public static byte[] V4(ReadOnlySpan<byte> src, ReadOnlySpan<byte> dst, byte proto, ReadOnlySpan<byte> payload)
    {
        var p = new byte[20 + payload.Length];
        p[0] = 0x45;
        BinaryPrimitives.WriteUInt16BigEndian(p.AsSpan(2), (ushort)p.Length);
        BinaryPrimitives.WriteUInt16BigEndian(p.AsSpan(4), (ushort)Interlocked.Increment(ref _ident));
        p[6] = 0x40;                                   // don't fragment
        p[8] = 64;
        p[9] = proto;
        src.CopyTo(p.AsSpan(12));
        dst.CopyTo(p.AsSpan(16));
        BinaryPrimitives.WriteUInt16BigEndian(p.AsSpan(10), Checksum.Internet(p.AsSpan(0, 20)));
        payload.CopyTo(p.AsSpan(20));
        return p;
    }

    public static byte[] Udp(ReadOnlySpan<byte> src, int sport, ReadOnlySpan<byte> dst, int dport, ReadOnlySpan<byte> data)
    {
        var seg = new byte[8 + data.Length];
        BinaryPrimitives.WriteUInt16BigEndian(seg, (ushort)sport);
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(2), (ushort)dport);
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(4), (ushort)seg.Length);
        data.CopyTo(seg.AsSpan(8));
        ushort c = Checksum.Transport(src, dst, 17, seg);
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(6), c == 0 ? (ushort)0xFFFF : c);
        return V4(src, dst, 17, seg);
    }

    public const byte Fin = 0x01, Syn = 0x02, Rst = 0x04, Psh = 0x08, Ack = 0x10;

    public static byte[] Tcp(ReadOnlySpan<byte> src, int sport, ReadOnlySpan<byte> dst, int dport,
        uint seq, uint ack, byte flags, int window, ReadOnlySpan<byte> data = default, int mss = 0)
    {
        int optLen = mss > 0 ? 4 : 0;
        var seg = new byte[20 + optLen + data.Length];
        BinaryPrimitives.WriteUInt16BigEndian(seg, (ushort)sport);
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(2), (ushort)dport);
        BinaryPrimitives.WriteUInt32BigEndian(seg.AsSpan(4), seq);
        BinaryPrimitives.WriteUInt32BigEndian(seg.AsSpan(8), ack);
        seg[12] = (byte)(((20 + optLen) / 4) << 4);
        seg[13] = flags;
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(14), (ushort)Math.Clamp(window, 0, 65535));
        if (mss > 0)
        {
            seg[20] = 2; seg[21] = 4;
            BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(22), (ushort)mss);
        }
        data.CopyTo(seg.AsSpan(20 + optLen));
        BinaryPrimitives.WriteUInt16BigEndian(seg.AsSpan(16), Checksum.Transport(src, dst, 6, seg));
        return V4(src, dst, 6, seg);
    }
}
