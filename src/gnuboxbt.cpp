// gnuboxbt.cpp - choose the PC and make it BTCOMM::0's default device
//
// On Symbian OS 7.0s the Bluetooth CSY (BTCOMM) looks up its remote end in
// the Bluetooth "default service" table: for port n it takes the device
// address and the service UUID, does an SDP query for that UUID on the
// device and opens RFCOMM to the channel it finds. The original gnubox had
// this call commented out (CBTRegistry::SetDefaultCommPort); here it is used.
//
// GPL v2 or later, see COPYING.txt

#include <btextnotifiers.h>
#include <btregistry.h>
#include <btdefcommport.h>
#include <btsdp.h>
#include "gnubox.h"

const TUint32 KSerialPortUuid = 0x1101;	// Serial Port Profile
const TUint32 KDialupUuid = 0x1103;		// Dial-up Networking Profile
const TUint32 KBtcommPort = 0;			// BTCOMM::0

TUint32 GnuboxServiceUuid(TInt aMode)
	{
	return aMode == EGnuboxModeDialup ? KDialupUuid : KSerialPortUuid;
	}

const TDesC& GnuboxModeName(TInt aMode)
	{
	_LIT(KPc, "PC (Serial Port + pppd)");
	_LIT(KDialup, "Dial-up server (DUN, e.g. Android BlueDUN)");
	if (aMode == EGnuboxModeDialup)
		return KDialup;
	return KPc;
	}

void GnuboxBt::AddrToText(const TBTDevAddr& aAddr, TDes& aText)
	{
	aText.Zero();
	for (TInt i = 0; i < 6; i++)
		{
		if (i)
			aText.Append(':');
		aText.AppendNumFixedWidth(aAddr[i], EHex, 2);
		}
	aText.UpperCase();
	}

TBool GnuboxBt::SelectPcL(TGnuboxSettings& aSettings, MGnuboxLog& aLog)
	{
	RNotifier notifier;
	User::LeaveIfError(notifier.Connect());
	CleanupClosePushL(notifier);

	// no UUID/class filter: Android phones and PCs must both show up
	TBTDeviceSelectionParamsPckg selection;
	TBTDeviceResponseParamsPckg response;

	TRequestStatus status;
	notifier.StartNotifierAndGetResponse(status, KDeviceSelectionNotifierUid, selection, response);
	User::WaitForRequest(status);
	notifier.CancelNotifier(KDeviceSelectionNotifierUid);
	CleanupStack::PopAndDestroy(&notifier);

	if (status.Int() != KErrNone)
		{
		LogF(aLog, _L("No device selected (%S)"), &ErrText(status.Int()));
		return EFalse;
		}
	if (!response().IsValidBDAddr())
		{
		aLog.Log(_L("Selection returned no address"));
		return EFalse;
		}
	aSettings.iAddr = response().BDAddr();
	aSettings.iHaveAddr = ETrue;
	if (response().IsValidDeviceName())
		aSettings.iName = response().DeviceName().Left(aSettings.iName.MaxLength());
	else
		aSettings.iName.Zero();
	TBuf<20> addr;
	AddrToText(aSettings.iAddr, addr);
	LogF(aLog, _L("Selected %S (%S)"), &aSettings.iName, &addr);
	return ETrue;
	}

void GnuboxBt::SetDefaultPortL(const TGnuboxSettings& aSettings, MGnuboxLog& aLog)
	{
	TBTCommPortSettings port(KGnuboxBtServiceName, TUUID(GnuboxServiceUuid(aSettings.iMode)), KBtcommPort,
		(TUint8)aSettings.iSecurity, aSettings.iAddr);
	switch (aSettings.iSecurity)
		{
	case KBTDefaultServiceSecurityAuthenticateOnly:
		port.SetAuthentication();
		break;
	case KBTDefaultServiceSecurityAuthenticateAndEncrypt:
		port.SetEncryption();
		break;
	default:
		port.SetNoSecurity();
		break;
		}
	CBTRegistry* reg = CBTRegistry::NewLC();
	TInt err = reg->SetDefaultCommPort(port);
	CleanupStack::PopAndDestroy(reg);
	if (err != KErrNone)
		{
		LogF(aLog, _L("SetDefaultCommPort failed: %S"), &ErrText(err));
		User::Leave(err);
		}
	if (aSettings.iMode == EGnuboxModeDialup)
		aLog.Log(_L("BTCOMM::0 now points to this device (Dial-up Networking service)."));
	else
		aLog.Log(_L("BTCOMM::0 now points to this PC (Serial Port service)."));
	}

void GnuboxBt::ShowDefaultPortL(MGnuboxLog& aLog)
	{
	CBTRegistry* reg = CBTRegistry::NewLC();
	TBTCommPortSettings port;
	TInt err = reg->GetDefaultCommPort(KBtcommPort, port);
	CleanupStack::PopAndDestroy(reg);
	if (err != KErrNone)
		{
		LogF(aLog, _L("BTCOMM::0 default device: none (%S)"), &ErrText(err));
		return;
		}
	TBuf<20> addr;
	AddrToText(port.BDAddr(), addr);
	TUUID uuid = port.UUID();
	TPtrC8 u = uuid.Des();
	TBuf<40> uuidText;
	for (TInt i = 0; i < u.Length(); i++)
		uuidText.AppendNumFixedWidth(u[i], EHex, 2);
	const TDesC* sec = &KNullDesC;
	_LIT(KSecNone, "none");
	_LIT(KSecAuth, "authenticate");
	_LIT(KSecEnc, "authenticate+encrypt");
	if (port.IsEncryptionSet())
		sec = &KSecEnc;
	else if (port.IsAuthenticationOnlySet())
		sec = &KSecAuth;
	else
		sec = &KSecNone;
	TPtrC name(port.Name());
	LogF(aLog, _L("BTCOMM::0 -> %S, service \"%S\", security %S"), &addr, &name, sec);
	LogF(aLog, _L("  UUID %S"), &uuidText);
	}
