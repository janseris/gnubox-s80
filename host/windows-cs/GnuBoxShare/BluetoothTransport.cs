// BluetoothTransport.cs - this PC as a Bluetooth Serial Port service, no COM port.
// Documented Win32 Winsock Bluetooth calls (ws2_32): an AF_BTH RFCOMM listening socket and an
// SDP record for the Serial Port service class (WSASetService). GnuBox's "Other device: PC"
// mode looks up exactly this service. Same calls as the Python version (gnubox_winshare.py),
// which works with the 9300. GPL v2 or later.
using System.Runtime.InteropServices;

namespace GnuBoxShare;

sealed unsafe class BluetoothTransport : ITransport
{
    const int AF_BTH = 32, SOCK_STREAM = 1, BTHPROTO_RFCOMM = 3, NS_BTH = 16, SD_BOTH = 2;
    const int RNRSERVICE_REGISTER = 0, RNRSERVICE_DELETE = 2;
    const uint BT_PORT_ANY = 0xFFFFFFFF;
    static readonly IntPtr InvalidSocket = new(-1);
    static readonly Guid SerialPortClass = new("00001101-0000-1000-8000-00805F9B34FB");

    [StructLayout(LayoutKind.Sequential, Pack = 1)]          // ws2bth.h: packed, 30 bytes
    struct SOCKADDR_BTH { public ushort addressFamily; public ulong btAddr; public Guid serviceClassId; public uint port; }

    [StructLayout(LayoutKind.Sequential)]
    struct SOCKET_ADDRESS { public IntPtr lpSockaddr; public int iSockaddrLength; }

    [StructLayout(LayoutKind.Sequential)]
    struct CSADDR_INFO { public SOCKET_ADDRESS LocalAddr, RemoteAddr; public int iSocketType, iProtocol; }

    [StructLayout(LayoutKind.Sequential)]
    struct WSAQUERYSETW
    {
        public uint dwSize; public IntPtr lpszServiceInstanceName, lpServiceClassId, lpVersion, lpszComment;
        public uint dwNameSpace; public IntPtr lpNSProviderId, lpszContext;
        public uint dwNumberOfProtocols; public IntPtr lpafpProtocols, lpszQueryString;
        public uint dwNumberOfCsAddrs; public IntPtr lpcsaBuffer;
        public uint dwOutputFlags; public IntPtr lpBlob;
    }

    [DllImport("ws2_32.dll")] static extern int WSAStartup(ushort version, byte[] data);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern IntPtr socket(int af, int type, int protocol);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int bind(IntPtr s, IntPtr name, int namelen);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int listen(IntPtr s, int backlog);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int getsockname(IntPtr s, IntPtr name, ref int namelen);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern IntPtr accept(IntPtr s, IntPtr addr, ref int addrlen);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int recv(IntPtr s, byte* buf, int len, int flags);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int send(IntPtr s, byte* buf, int len, int flags);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int shutdown(IntPtr s, int how);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int closesocket(IntPtr s);
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int WSASetServiceW(IntPtr qs, int op, uint flags);
    [DllImport("ws2_32.dll")] static extern int WSAGetLastError();
    [DllImport("ws2_32.dll", SetLastError = true)] static extern int getsockopt(IntPtr s, int level, int optname, byte* optval, ref int optlen);
    const int SOL_RFCOMM = 3;                                  // ws2bth.h: SOL_RFCOMM = BTHPROTO_RFCOMM
    const int SO_BTH_MTU = unchecked((int)0x80000007);         // negotiated RFCOMM frame size once connected

    IntPtr _listen = InvalidSocket, _client = InvalidSocket;
    readonly List<IntPtr> _mem = new();                  // unmanaged memory the SDP record points into
    IntPtr _querySet;
    bool _registered;
    int _disposed;

    public string Name { get; private set; } = "Bluetooth";
    public string PhoneAddress { get; private set; } = "";
    /// <summary>RFCOMM frame size agreed with the phone (0 if Windows doesn't say).</summary>
    public int FrameSize { get; private set; }

    IntPtr Alloc(int size) { var p = Marshal.AllocHGlobal(size); new Span<byte>((void*)p, size).Clear(); _mem.Add(p); return p; }
    IntPtr AllocString(string s) { var p = Marshal.StringToHGlobalUni(s); _mem.Add(p); return p; }

    static string BtAddr(ulong a) => string.Join(":", Enumerable.Range(0, 6).Reverse().Select(i => ((a >> (8 * i)) & 0xFF).ToString("X2")));

    /// <summary>Registers the service and waits for the phone to connect.</summary>
    public static Task<ITransport> OpenAsync(CancellationToken ct) => Task.Run<ITransport>(() =>
    {
        var t = new BluetoothTransport();
        try
        {
            t.Listen();
            using (ct.Register(() => { var l = t._listen; if (l != InvalidSocket) closesocket(l); t._listen = InvalidSocket; }))
                t.Accept(ct);
            return t;
        }
        catch
        {
            t.Dispose();
            throw;
        }
    }, ct);

    void Listen()
    {
        WSAStartup(0x0202, new byte[512]);
        _listen = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (_listen == InvalidSocket)
            throw new IOException($"Bluetooth socket: error {WSAGetLastError()} (no Bluetooth adapter, or it is off?)");
        int saSize = sizeof(SOCKADDR_BTH);
        var sa = Alloc(saSize);
        var addr = (SOCKADDR_BTH*)sa;
        addr->addressFamily = AF_BTH;
        addr->port = BT_PORT_ANY;
        if (bind(_listen, sa, saSize) != 0) throw new IOException($"Bluetooth bind: error {WSAGetLastError()}");
        if (listen(_listen, 1) != 0) throw new IOException($"Bluetooth listen: error {WSAGetLastError()}");
        int len = saSize;
        getsockname(_listen, sa, ref len);
        uint channel = addr->port;

        // SDP record: Serial Port service on this RFCOMM channel
        var guid = Alloc(16);
        *(Guid*)guid = SerialPortClass;
        var csa = Alloc(sizeof(CSADDR_INFO));
        var c = (CSADDR_INFO*)csa;
        c->LocalAddr.lpSockaddr = sa;
        c->LocalAddr.iSockaddrLength = saSize;
        c->iSocketType = SOCK_STREAM;
        c->iProtocol = BTHPROTO_RFCOMM;
        _querySet = Alloc(sizeof(WSAQUERYSETW));
        var q = (WSAQUERYSETW*)_querySet;
        q->dwSize = (uint)sizeof(WSAQUERYSETW);
        q->lpszServiceInstanceName = AllocString("GnuBox Share");
        q->lpszComment = AllocString("Internet for the Nokia 9300 (GnuBox)");
        q->lpServiceClassId = guid;
        q->dwNameSpace = NS_BTH;
        q->dwNumberOfCsAddrs = 1;
        q->lpcsaBuffer = csa;
        if (WSASetServiceW(_querySet, RNRSERVICE_REGISTER, 0) != 0)
            throw new IOException($"Bluetooth service registration: error {WSAGetLastError()}");
        _registered = true;
        Name = $"Bluetooth channel {channel}";
        Log.Info($"Bluetooth: Serial Port service 'GnuBox Share' on RFCOMM channel {channel} - press Connect in GnuBox");
    }

    void Accept(CancellationToken ct)
    {
        int size = sizeof(SOCKADDR_BTH);
        var peer = Alloc(size);
        int len = size;
        var h = accept(_listen, peer, ref len);
        if (h == InvalidSocket)
        {
            ct.ThrowIfCancellationRequested();
            throw new IOException($"Bluetooth accept: error {WSAGetLastError()}");
        }
        _client = h;
        PhoneAddress = BtAddr(((SOCKADDR_BTH*)peer)->btAddr);
        Name = "phone " + PhoneAddress;
        uint mtu = 0;
        int ml = 4;
        if (getsockopt(_client, SOL_RFCOMM, SO_BTH_MTU, (byte*)&mtu, ref ml) == 0) FrameSize = (int)mtu;
        Log.Info($"Bluetooth: phone {PhoneAddress} connected" + (FrameSize > 0 ? $", frame size (RFCOMM MTU) {FrameSize} bytes" : ""));
    }

    readonly byte[] _readBuf = new byte[16384];

    public ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken ct)
    {
        return new ValueTask<int>(Task.Run(() =>
        {
            using var reg = ct.Register(() => shutdown(_client, SD_BOTH));     // unblocks recv
            var arr = _readBuf;
            int n;
            fixed (byte* p = arr) n = recv(_client, p, Math.Min(arr.Length, buffer.Length), 0);
            if (n <= 0) return 0;                                             // closed, reset or stopped
            arr.AsSpan(0, n).CopyTo(buffer.Span);
            return n;
        }));
    }

    public ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        using var reg = ct.Register(() => shutdown(_client, SD_BOTH));
        var span = data.Span;
        fixed (byte* p = span)
        {
            int off = 0;
            while (off < span.Length)
            {
                int n = send(_client, p + off, span.Length - off, 0);
                if (n <= 0) throw new IOException($"Bluetooth send: error {WSAGetLastError()}");
                off += n;
            }
        }
        return ValueTask.CompletedTask;
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        if (_client != InvalidSocket) { shutdown(_client, SD_BOTH); closesocket(_client); _client = InvalidSocket; }
        if (_registered) { WSASetServiceW(_querySet, RNRSERVICE_DELETE, 0); _registered = false; }
        if (_listen != InvalidSocket) { closesocket(_listen); _listen = InvalidSocket; }
        foreach (var p in _mem) Marshal.FreeHGlobal(p);
        _mem.Clear();
    }
}
