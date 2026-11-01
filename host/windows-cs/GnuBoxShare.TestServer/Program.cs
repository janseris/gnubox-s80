// Test server: the C# core on a TCP port instead of Bluetooth, for host/windows/test/phone_sim.py.
//   dotnet run --project GnuBoxShare.TestServer -- 127.0.0.1:7777 [ip=realip ...] [-v]
using GnuBoxShare;

var addr = args.FirstOrDefault(a => a.Contains(':')) ?? "127.0.0.1:7777";
var rewrite = args.Where(a => a.Contains('=')).Select(a => a.Split('=', 2)).ToDictionary(p => p[0], p => p[1]);
Log.Verbose = args.Contains("-v");
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };
await Runner.RunAsync(ct => TcpTestTransport.AcceptAsync(addr, ct), new Status(), rewrite, cts.Token);
