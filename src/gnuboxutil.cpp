// gnuboxutil.cpp - logging helpers, error names, settings file
// GnuBox for Series 80 - GPL v2 or later, see COPYING.txt

#include <s32file.h>
#include <btdefcommport.h>
#include "gnubox.h"

// --------------------------------------------------------------------------
// utilities

void LogF(MGnuboxLog& aLog, TRefByValue<const TDesC> aFmt, ...)
	{
	VA_LIST list;
	VA_START(list, aFmt);
	TBuf<256> buf;
	TGnuboxOverflow overflow;
	buf.AppendFormatList(aFmt, list, &overflow);
	VA_END(list);
	aLog.Log(buf);
	}

void LogD(MGnuboxLog& aLog, TRefByValue<const TDesC> aFmt, ...)
	{
	VA_LIST list;
	VA_START(list, aFmt);
	TBuf<256> buf;
	TGnuboxOverflow overflow;
	buf.AppendFormatList(aFmt, list, &overflow);
	VA_END(list);
	aLog.Detail(buf);
	}

static const TText* ErrName(TInt aErr)
	{
	switch (aErr)
		{
	case KErrNone: return _S("OK");
	case KErrNotFound: return _S("not found");
	case KErrGeneral: return _S("general error");
	case KErrCancel: return _S("cancelled");
	case KErrNoMemory: return _S("no memory");
	case KErrNotSupported: return _S("not supported");
	case KErrArgument: return _S("bad argument");
	case KErrBadHandle: return _S("bad handle");
	case KErrOverflow: return _S("overflow");
	case KErrAlreadyExists: return _S("already exists");
	case KErrPathNotFound: return _S("path not found");
	case KErrInUse: return _S("in use");
	case KErrServerTerminated: return _S("server terminated");
	case KErrServerBusy: return _S("server busy");
	case KErrUnknown: return _S("unknown/null");
	case KErrNotReady: return _S("not ready");
	case KErrCorrupt: return _S("corrupt");
	case KErrAccessDenied: return _S("access denied");
	case KErrLocked: return _S("locked");
	case KErrDisconnected: return _S("disconnected");
	case KErrTimedOut: return _S("timed out");
	case KErrCouldNotConnect: return _S("could not connect");
	case KErrCouldNotDisconnect: return _S("could not disconnect");
	case KErrCommsLineFail: return _S("comms line failed");
	case KErrCommsFrame: return _S("comms frame error");
	case KErrCommsOverrun: return _S("comms overrun");
	case KErrCommsParity: return _S("comms parity");
	case KErrAbort: return _S("aborted");
	case KErrHardwareNotAvailable: return _S("hardware not available");
	case KErrEof: return _S("end of stream");
	case KErrPermissionDenied: return _S("permission denied");
	case -3050: return _S("PPP authentication failed");
	case -3606: return _S("no route / not ready");
	case -5120: return _S("DNS: name not found");
	// Bluetooth chip (HCI) errors: -6000 minus the HCI code (hcierrors.h)
	case -6002: return _S("BT: no link");
	case -6003: return _S("BT hardware failure - turn Bluetooth off and on");
	case -6004: return _S("BT: PC not reachable - in range, Bluetooth on?");
	case -6005: return _S("BT: authentication failed - pair again");
	case -6006: return _S("BT: pairing key missing - pair again");
	case -6008: return _S("BT: connection timed out - out of range?");
	case -6009: case -6011: return _S("BT: too many connections");
	case -6012: return _S("BT: not allowed now - turn Bluetooth off and on");
	case -6013: case -6014: case -6015: return _S("BT: the PC refused the connection");
	case -6016: return _S("BT: the PC stopped answering");
	case -6019: return _S("BT: the PC ended the connection");
	case -6020: return _S("BT: the PC is low on resources");
	case -6021: return _S("BT: the PC is turning Bluetooth off");
	case -6022: return _S("BT: ended by the phone");
	case -6023: return _S("BT: too many attempts - wait a minute");
	case -6024: return _S("BT: pairing not allowed");
	case -6031: return _S("BT: unspecified error - turn Bluetooth off and on");
	case -6034: return _S("BT: link manager timeout");
	// L2CAP and RFCOMM (bt_sock.h)
	case -6304: return _S("BT: connect failed");
	case -6305: return _S("BT: link lost");
	case -6306: return _S("BT: not connected");
	case -6310: return _S("BT: the PC disconnected");
	case -6311: return _S("BT: access denied - pair again");
	case -6312: return _S("BT: request timed out");
	case -6354: return _S("BT: no answer on the serial channel");
	case -6355: return _S("BT: the PC closed the serial channel");
	default: return _S("");
		}
	}

const TDesC& ErrText(TInt aErr)
	{
	// apps are DLLs on EKA1: no writable static data, keep the buffer in TLS
	TBuf<96>* buf = (TBuf<96>*)Dll::Tls();
	if (!buf)
		{
		buf = new TBuf<96>;
		if (!buf)
			return KNullDesC;
		Dll::SetTls(buf);
		}
	TPtrC name(ErrName(aErr));
	buf->Format(_L("%d"), aErr);
	if (name.Length())
		{
		buf->Append(_L(" ("));
		buf->Append(name);
		buf->Append(')');
		}
	return *buf;
	}

// --------------------------------------------------------------------------
// settings

TGnuboxSettings::TGnuboxSettings()
	: iHaveAddr(EFalse), iSecurity(KBTDefaultServiceSecurityNone), iMode(EGnuboxModePc), iPrevPrefIap(0),
	  iAutoReconnect(ETrue), iUseForAll(EFalse), iPrevPrefDialog(-1), iCompress(EFalse)
	{
	}

void TGnuboxSettings::Load(RFs& aFs)
	{
	RFileReadStream in;
	if (in.Open(aFs, KGnuboxSettingsFile, EFileRead) != KErrNone)
		return;
	TRAPD(err,
		{
		TInt version = in.ReadInt32L();
		if (version >= 1 && version <= 5)
			{
			for (TInt i = 0; i < 6; i++)
				iAddr[i] = in.ReadUint8L();
			iHaveAddr = in.ReadInt8L();
			iSecurity = in.ReadInt8L();
			in >> iName;
			if (version >= 2)
				iMode = in.ReadInt8L();
			if (version >= 3)
				iPrevPrefIap = in.ReadUint32L();
			if (version >= 4)
				{
				iAutoReconnect = in.ReadInt8L();
				iUseForAll = in.ReadInt8L();
				iPrevPrefDialog = in.ReadInt8L();
				}
			if (version >= 5)
				iCompress = in.ReadInt8L();
			}
		});
	in.Close();
	if (err != KErrNone)
		*this = TGnuboxSettings();
	}

TInt TGnuboxSettings::Save(RFs& aFs) const
	{
	TParsePtrC parse(KGnuboxSettingsFile);
	aFs.MkDirAll(parse.DriveAndPath());
	RFileWriteStream out;
	TInt err = out.Replace(aFs, KGnuboxSettingsFile, EFileWrite);
	if (err != KErrNone)
		return err;
	TRAP(err,
		{
		out.WriteInt32L(5);
		for (TInt i = 0; i < 6; i++)
			out.WriteUint8L(iAddr[i]);
		out.WriteInt8L(iHaveAddr);
		out.WriteInt8L(iSecurity);
		out << iName;
		out.WriteInt8L(iMode);
		out.WriteUint32L(iPrevPrefIap);
		out.WriteInt8L(iAutoReconnect);
		out.WriteInt8L(iUseForAll);
		out.WriteInt8L(iPrevPrefDialog);
		out.WriteInt8L(iCompress);
		out.CommitL();
		});
	out.Close();
	return err;
	}

