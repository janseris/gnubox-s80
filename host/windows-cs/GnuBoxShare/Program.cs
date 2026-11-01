// Program.cs - GnuBox Share: this PC's internet for the Nokia 9300 over Bluetooth.
// One instance: starting it again brings the running window to the front. GPL v2 or later.
namespace GnuBoxShare;

static class Program
{
    const string MutexName = "GnuBoxShare.SingleInstance";
    const string ShowEventName = "GnuBoxShare.Show";

    [STAThread]
    static void Main(string[] args)
    {
        using var mutex = new Mutex(true, MutexName, out bool first);
        if (!first)
        {
            try { using var ev = EventWaitHandle.OpenExisting(ShowEventName); ev.Set(); } catch { }
            return;
        }
        using var show = new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName);

        ApplicationConfiguration.Initialize();
        Application.SetColorMode(SystemColorMode.System);         // follows Windows' light/dark setting
        var form = new MainForm(args.Contains("--autostart"));

        // a second start shows this window
        var waiter = new Thread(() =>
        {
            while (true)
            {
                show.WaitOne();
                try { if (form.IsHandleCreated) form.BeginInvoke((Action)form.ShowFromTray); } catch { }
            }
        }) { IsBackground = true };
        waiter.Start();

        Application.Run(form);
    }
}
