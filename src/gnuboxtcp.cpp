// gnuboxtcp.cpp - optional bigger TCP windows for the whole phone
//
// The 7.0s TCP/IP stack (TCPIP6.PRT) reads tcp_recv_buf / tcp_send_buf from
// tcpip.ini; the default receive window is 8192 bytes. Download speed per TCP
// connection is at most window / round-trip time, and over Bluetooth + PPP +
// the Android NAT the round trip is ~0.3 s, i.e. ~26 kB/s with 8 KB. A 32 KB
// window lifts that limit to what the Bluetooth link can carry.
//
// Enable: C:\System\Data\tcpip.ini = current file (C: copy if there is one,
// else the ROM one from Z:) with a [tcp] section holding the two keys; an
// existing C: file is backed up first. Disable: the backup is put back, or
// the file GnuBox created is removed. The stack reads the file when it
// starts, so the phone must be restarted (or every connection closed).
//
// GPL v2 or later, see COPYING.txt

#include <f32file.h>
#include "gnubox.h"

_LIT(KIniC, "C:\\System\\Data\\tcpip.ini");
_LIT(KIniZ, "Z:\\System\\Data\\tcpip.ini");
_LIT(KIniBackup, "C:\\System\\Data\\tcpip.ini.gnubox-backup");
_LIT8(KMarker, "; GnuBox: bigger TCP windows");
_LIT8(KTcpSection, "[tcp]");
_LIT8(KRecvKey, "tcp_recv_buf");
_LIT8(KSendKey, "tcp_send_buf");
_LIT8(KCrLf, "\r\n");

const TInt KRecvBuf = 32768;
const TInt KSendBuf = 16384;
const TInt KMaxIni = 16384;

static HBufC8* ReadFileLC(RFs& aFs, const TDesC& aName)
	{
	RFile f;
	User::LeaveIfError(f.Open(aFs, aName, EFileRead | EFileShareReadersOnly));
	CleanupClosePushL(f);
	TInt size = 0;
	User::LeaveIfError(f.Size(size));
	if (size > KMaxIni)
		User::Leave(KErrTooBig);
	HBufC8* buf = HBufC8::NewL(size + 256);
	TPtr8 p = buf->Des();
	User::LeaveIfError(f.Read(p));
	CleanupStack::PopAndDestroy(&f);
	CleanupStack::PushL(buf);
	return buf;
	}

static void WriteFileL(RFs& aFs, const TDesC& aName, const TDesC8& aData)
	{
	TParsePtrC parse(aName);
	aFs.MkDirAll(parse.DriveAndPath());
	RFile f;
	User::LeaveIfError(f.Replace(aFs, aName, EFileWrite));
	CleanupClosePushL(f);
	User::LeaveIfError(f.Write(aData));
	User::LeaveIfError(f.Flush());
	CleanupStack::PopAndDestroy(&f);
	}

static TBool Exists(RFs& aFs, const TDesC& aName)
	{
	TEntry e;
	return aFs.Entry(aName, e) == KErrNone;
	}

TBool GnuboxTcp::IsEnabledL(RFs& aFs)
	{
	if (!Exists(aFs, KIniC))
		return EFalse;
	HBufC8* buf = ReadFileLC(aFs, KIniC);
	TBool on = buf->Find(KMarker) >= 0;
	CleanupStack::PopAndDestroy(buf);
	return on;
	}

// copy aIn to aOut without our marker and without tcp_recv_buf/tcp_send_buf lines
static void StripOurLines(const TDesC8& aIn, TDes8& aOut)
	{
	TPtrC8 rest(aIn);
	while (rest.Length())
		{
		TInt nl = rest.Locate('\n');
		TPtrC8 line = nl >= 0 ? rest.Left(nl + 1) : rest;
		rest.Set(nl >= 0 ? rest.Mid(nl + 1) : TPtrC8());
		TPtrC8 t = line;
		while (t.Length() && (t[0] == ' ' || t[0] == '\t'))
			t.Set(t.Mid(1));
		if (t.Find(KMarker) == 0 || t.FindF(KRecvKey) == 0 || t.FindF(KSendKey) == 0)
			continue;
		aOut.Append(line);
		}
	}

void GnuboxTcp::EnableL(RFs& aFs, MGnuboxLog& aLog)
	{
	TBool haveC = Exists(aFs, KIniC);
	HBufC8* src = NULL;
	if (haveC)
		{
		src = ReadFileLC(aFs, KIniC);
		if (!Exists(aFs, KIniBackup) && src->Find(KMarker) < 0)
			{
			WriteFileL(aFs, KIniBackup, *src);
			aLog.Log(_L("Backed up C:\\System\\Data\\tcpip.ini"));
			}
		}
	else
		src = ReadFileLC(aFs, KIniZ);

	HBufC8* out = HBufC8::NewLC(src->Length() + 512);
	TPtr8 o = out->Des();
	StripOurLines(*src, o);
	if (o.Length() && o[o.Length() - 1] != '\n')
		o.Append(KCrLf);

	TBuf8<128> keys;
	keys.Append(KMarker);
	keys.Append(KCrLf);
	keys.Append(KRecvKey);
	keys.AppendFormat(_L8("= %d\r\n"), KRecvBuf);
	keys.Append(KSendKey);
	keys.AppendFormat(_L8("= %d\r\n"), KSendBuf);

	TInt sec = o.FindF(KTcpSection);
	if (sec >= 0)
		{
		// insert right after the "[tcp]" line
		TInt eol = o.Mid(sec).Locate('\n');
		TInt at = eol >= 0 ? sec + eol + 1 : o.Length();
		o.Insert(at, keys);
		}
	else
		{
		o.Append(KCrLf);
		o.Append(KTcpSection);
		o.Append(KCrLf);
		o.Append(keys);
		}
	WriteFileL(aFs, KIniC, o);
	CleanupStack::PopAndDestroy(2);	// out, src
	LogF(aLog, _L("TCP windows set: receive %d, send %d bytes."), KRecvBuf, KSendBuf);
	aLog.Log(_L("Restart the 9300 to apply (the TCP/IP stack reads this at start)."));
	}

void GnuboxTcp::DisableL(RFs& aFs, MGnuboxLog& aLog)
	{
	if (Exists(aFs, KIniBackup))
		{
		HBufC8* bak = ReadFileLC(aFs, KIniBackup);
		WriteFileL(aFs, KIniC, *bak);
		CleanupStack::PopAndDestroy(bak);
		aFs.Delete(KIniBackup);
		aLog.Log(_L("Original C:\\System\\Data\\tcpip.ini restored."));
		}
	else if (IsEnabledL(aFs))	// only ever delete a file GnuBox wrote
		{
		User::LeaveIfError(aFs.Delete(KIniC));
		aLog.Log(_L("GnuBox's tcpip.ini removed - the phone's built-in settings apply."));
		}
	aLog.Log(_L("Restart the 9300 to apply."));
	}
