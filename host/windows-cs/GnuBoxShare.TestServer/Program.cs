// Test server: the C# core on a TCP port instead of Bluetooth, for host/windows/test/phone_sim.py.
//   dotnet run --project GnuBoxShare.TestServer -- 127.0.0.1:7777 [ip=realip ...] [-v] [-r]
//   -r prints every finished request (the window's requests list)
using GnuBoxShare;

var addr = args.FirstOrDefault(a => a.Contains(':')) ?? "127.0.0.1:7777";
var rewrite = args.Where(a => a.Contains('=')).Select(a => a.Split('=', 2)).ToDictionary(p => p[0], p => p[1]);
Log.Verbose = args.Contains("-v");
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };
var status = new Status();
if (args.Contains("-r"))
    status.Traffic.Completed += r => Console.WriteLine(
        $"REQ #{r.Id} c{r.Conn} {r.Method} {r.Url} | {r.Status} | {r.ContentType} | down {r.Down} up {r.Up} | {r.Ms:0} ms | {r.Remote} | {r.Note}");
await Runner.RunAsync(ct => TcpTestTransport.AcceptAsync(addr, ct), status, rewrite, cts.Token);
