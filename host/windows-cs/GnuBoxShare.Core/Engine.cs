// Engine.cs - the link to the phone: reads and writes the transport, runs PPP and the NAT
// on one event-loop thread (everything else posts actions to it). GPL v2 or later.
using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;

namespace GnuBoxShare;

/// <summary>The byte stream to the phone: Bluetooth RFCOMM on Windows, TCP for tests.</summary>
public interface ITransport : IDisposable
{
    string Name { get; }
    /// <summary>Returns 0 at the end of the stream.</summary>
    ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken ct);
    ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct);
}

public sealed class Engine : INatHost, IPppLink
{
    readonly Channel<Action> _events = Channel.CreateUnbounded<Action>(new UnboundedChannelOptions { SingleReader = true });
    readonly Channel<byte[]> _out = Channel.CreateUnbounded<byte[]>(new UnboundedChannelOptions { SingleReader = true });
    int _outQueued;                                   // frames waiting for the transport
    readonly Status _status;
    readonly Ppp _ppp;
    readonly Nat _nat;
    readonly HdlcDecoder _dec = new();
    bool _seenBytes;

    public Engine(Status status, Dictionary<string, string> rewrite)
    {
        _status = status;
        _ppp = new Ppp(this);
        _nat = new Nat(this, rewrite, status.Traffic);
    }

    public void Post(Action action) => _events.Writer.TryWrite(action);
    public bool LinkHasRoom => Volatile.Read(ref _outQueued) <= 12;

    public void SendIp(byte[] packet) => _ppp.SendIp(packet);

    void IPppLink.SendFrame(byte[] frame, bool[] escape)
    {
        Interlocked.Increment(ref _outQueued);
        _out.Writer.TryWrite(Hdlc.Encode(frame, escape));
    }

    void IPppLink.OnIp(byte[] packet) => _nat.OnIp(packet);
    void IPppLink.OnReset() => _nat.Reset();

    /// <summary>Serves one connection until the phone hangs up (EndOfStreamException) or ct is cancelled.</summary>
    public async Task RunAsync(ITransport t, CancellationToken ct)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        var token = linked.Token;
        Exception? ended = null;
        void End(Exception e) { ended ??= e; linked.Cancel(); }

        var reader = Task.Run(async () =>
        {
            var buf = new byte[65536];
            try
            {
                while (!token.IsCancellationRequested)
                {
                    int n = await t.ReadAsync(buf, token);
                    if (n <= 0) { End(new EndOfStreamException()); return; }
                    _status.AddRx(n);
                    var data = buf.AsSpan(0, n).ToArray();
                    Post(() => OnBytes(data));
                }
            }
            catch (OperationCanceledException) { }
            catch (Exception e) { End(e); }
        });

        var writer = Task.Run(async () =>
        {
            try
            {
                while (await _out.Reader.WaitToReadAsync(token))
                {
                    // send what is queued as one write (up to 8 KB)
                    var ms = new MemoryStream();
                    while (ms.Length < 8192 && _out.Reader.TryRead(out var f))
                    {
                        ms.Write(f);
                        Interlocked.Decrement(ref _outQueued);
                    }
                    var data = ms.ToArray();
                    await t.WriteAsync(data, token);
                    _status.AddTx(data.Length);
                }
            }
            catch (OperationCanceledException) { }
            catch (Exception e) { End(e); }
        });

        var ticker = Task.Run(async () =>
        {
            try
            {
                while (!token.IsCancellationRequested)
                {
                    await Task.Delay(50, token);
                    Post(Tick);
                }
            }
            catch (OperationCanceledException) { }
        });

        _status.State = LinkState.Waiting;
        try
        {
            await foreach (var action in _events.Reader.ReadAllAsync(token))
            {
                try { action(); }
                catch (Exception e) { Log.Info("internal error: " + e.Message); }
            }
        }
        catch (OperationCanceledException) { }
        finally
        {
            linked.Cancel();
            _nat.Reset();
            _status.Connections = 0;
            _status.Since = null;
            await Task.WhenAll(reader, writer, ticker).ContinueWith(_ => { });
        }
        if (ended != null) throw ended;
    }

    void OnBytes(byte[] data)
    {
        if (!_seenBytes)
        {
            _seenBytes = true;
            Log.Info("the phone is connected, starting PPP");
            _status.State = LinkState.Negotiating;
        }
        _dec.Feed(data, _ppp.OnFrame);
    }

    DateTime _lastStats = DateTime.UtcNow;

    void Tick()
    {
        if (_seenBytes) _ppp.Tick();
        _nat.Tick();
        _nat.Pump();
        _status.Connections = _nat.Connections;
        if (_ppp.IpUp && _status.State != LinkState.Up) { _status.State = LinkState.Up; _status.Since = DateTime.Now; }
        else if (!_ppp.IpUp && _status.State == LinkState.Up) { _status.State = LinkState.Waiting; _status.Since = null; }
        if (_ppp.IpUp && (DateTime.UtcNow - _lastStats).TotalSeconds > 60)
        {
            _lastStats = DateTime.UtcNow;
            Log.Info($"{_nat.Connections} connections, {_status.Tx / 1048576.0:0.0} MB to the phone, {_status.Rx / 1048576.0:0.0} MB from the phone");
        }
    }
}

public static class Runner
{
    /// <summary>The phone reaches this PC itself (e.g. localserver on port 8000) as the gateway or
    /// as the PC's USB-link (Windows ICS) address, so apps set up for USB also work over Bluetooth.</summary>
    public static readonly Dictionary<string, string> PcAliases = new()
    {
        ["10.64.64.1"] = "127.0.0.1",
        ["192.168.137.1"] = "127.0.0.1",
    };

    /// <summary>Waits for the phone, serves it, and waits again after a hang-up, until ct is cancelled.</summary>
    public static async Task RunAsync(Func<CancellationToken, Task<ITransport>> open, Status status,
        Dictionary<string, string>? rewrite, CancellationToken ct)
    {
        var map = new Dictionary<string, string>(PcAliases);
        if (rewrite != null) foreach (var kv in rewrite) map[kv.Key] = kv.Value;
        while (!ct.IsCancellationRequested)
        {
            ITransport? t = null;
            try
            {
                status.State = LinkState.Waiting;
                t = await open(ct);
                status.Where = t.Name;
                await new Engine(status, map).RunAsync(t, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (EndOfStreamException) { Log.Info("the phone disconnected; waiting for it again"); }
            catch (Exception e)
            {
                if (ct.IsCancellationRequested) break;
                status.State = LinkState.Error;
                Log.Info("link error: " + e.Message + " - trying again in 3 s");
                try { await Task.Delay(3000, ct); } catch (OperationCanceledException) { break; }
            }
            finally
            {
                t?.Dispose();
                status.Phone = "";
            }
        }
        status.State = LinkState.Stopped;
        Log.Info("sharing stopped");
    }
}

/// <summary>For tests: one TCP client acts as the phone's serial line.</summary>
public sealed class TcpTestTransport : ITransport
{
    readonly TcpClient _client;
    readonly NetworkStream _stream;
    public string Name { get; }

    TcpTestTransport(TcpClient c, string name) { _client = c; _stream = c.GetStream(); Name = name; }

    public static async Task<ITransport> AcceptAsync(string hostPort, CancellationToken ct)
    {
        var parts = hostPort.Split(':');
        var listener = new TcpListener(IPAddress.Parse(parts[0]), int.Parse(parts[1]));
        listener.Server.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        listener.Start(1);
        Log.Info("waiting for a test client on " + hostPort);
        try
        {
            var c = await listener.AcceptTcpClientAsync(ct);
            return new TcpTestTransport(c, "tcp " + hostPort);
        }
        finally { listener.Stop(); }
    }

    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken ct)
    {
        try { return await _stream.ReadAsync(buffer, ct); }
        catch (IOException) { return 0; }
    }

    public ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct) => _stream.WriteAsync(data, ct);
    public void Dispose() => _client.Dispose();
}
