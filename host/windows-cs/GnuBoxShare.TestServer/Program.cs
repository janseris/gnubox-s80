// Test server: the C# core on a TCP port instead of Bluetooth, for host/windows/test/phone_sim.py.
//   dotnet run --project GnuBoxShare.TestServer -- 127.0.0.1:7777 [ip=realip ...] [-v] [-r]
//   -r prints every finished request (the window's requests list)
using GnuBoxShare;

var addr = args.FirstOrDefault(a => a.Contains(':')) ?? "127.0.0.1:7777";
var rewrite = args.Where(a => a.Contains('=') && !a.StartsWith("logs=")).Select(a => a.Split('=', 2)).ToDictionary(p => p[0], p => p[1]);
Log.Verbose = args.Contains("-v");
Nat.TraceSegments = args.Contains("-t");                   // every TCP segment into the log file
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };
var status = new Status();
var logDir = args.FirstOrDefault(a => a.StartsWith("logs="))?[5..];         // logs=<folder>: the window's log files
DiagFiles? files = null;
if (logDir != null)
{
    files = new DiagFiles(logDir);
    files.Watch(status);
    Log.FileSink = files.Line;
    status.Traffic.Completed += r => files.Request(r);
}
if (args.Contains("-r"))
    status.Traffic.Completed += r => Console.WriteLine(
        $"REQ #{r.Id} c{r.Conn} {r.Method} {r.Url} | {r.Status} | {r.ContentType} | down {r.Down} up {r.Up} | {r.Ms:0} ms | {r.Remote} | {r.Note}" +
        (r.Tcp is { } t ? $" | win {t.PhoneWindowMax} rtt {t.RttMs}/{t.RttMinMs} ms winfull {t.TicksWindowFull}/{t.TicksData}" : ""));
if (args.Contains("-d"))                                    // link diagnostics every 2 s
    _ = Task.Run(async () =>
    {
        long busy0 = 0, d0 = 0, w0 = 0, q0 = 0; var sw = System.Diagnostics.Stopwatch.StartNew(); long e0 = 0;
        while (true)
        {
            await Task.Delay(2000);
            var d = status.Diag; long e = sw.ElapsedTicks;
            long data = d.TicksData - d0;
            Console.WriteLine($"DIAG busy {100.0 * (d.WriteBusyTicks - busy0) / (e - e0):0}% queued {d.QueuedBytes} B " +
                $"win {d.PhoneWindow}/{d.PhoneWindowMax} rtt {d.RttMs}/{d.RttMinMs} ms " +
                $"windowfull {(data > 0 ? 100 * (d.TicksWindowFull - w0) / data : 0)}% queuefull {(data > 0 ? 100 * (d.TicksQueueFull - q0) / data : 0)}% " +
                $"mtu {d.RfcommMtu} comp [{d.Compression}]");
            busy0 = d.WriteBusyTicks; d0 = d.TicksData; w0 = d.TicksWindowFull; q0 = d.TicksQueueFull; e0 = e;
        }
    });
await Runner.RunAsync(ct => TcpTestTransport.AcceptAsync(addr, ct), status, rewrite, cts.Token);
