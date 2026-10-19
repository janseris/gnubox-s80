// gnuboxcommdb.cpp - CommDb records for "GnuBox BT" access point
//
// Symbian OS 7.0s layout (Series 80 2.0):
//   IAP "GnuBox BT"  -> IAPService     = DialOutISP "GnuBox BT"
//                    -> IAPBearer      = ModemBearer "GnuBox BT"
//                    -> IAPNetwork     = default network
//                    -> Location       = location for data and fax
//   DialOutISP: empty DefaultTelNum  => NetDial does no dialling at all
//               (direct connection, PPP starts right away)
//   ModemBearer: PortName BTCOMM::0, CSYName BTCOMM; BTCOMM port 0 connects
//               to the default Bluetooth serial device (see gnuboxbt.cpp)
//
// The phone's own CSD/GPRS records are never modified (the original gnubox
// rewrote the shared modem record; on 7.0s every IAP has its own bearer).
//
// GPL v2 or later, see COPYING.txt

#include <commdb.h>
#include <cdbpreftable.h>
#include <cdbcols.h>
#include <d32comm.h>
#include <d32dbms.h>
#include <s32file.h>
#include "gnubox.h"
#include "gnubox_cols.h"

_LIT(KModemCsy, "BTCOMM");
_LIT(KModemPort, "BTCOMM::0");
_LIT(KModemTsy, "PHONETSY");
_LIT(KCsdAgent, "CSD.agt");
_LIT(KPppNif, "PPP");
_LIT(KDataport, "DATAPORT");
_LIT(KIspDescription, "GnuBox: direct PPP over Bluetooth");
_LIT(KPppUser, "gnubox");
_LIT(KPppPass, "gnubox");

// Login script for dial-up modem emulators (BlueDUN on Android and the like).
// NetDial runs it on the open BTCOMM port before PPP; syntax as in Symbian's
// own CSD agent tests: WAIT secs { "text" label }, "label:" lines, EXIT var.
_LIT(KDialupScript,
	"SEND \"AT\"+<0x0d>\n"
	"WAIT 3\n{\n\"OK\" atok\n}\n"
	"SEND \"AT\"+<0x0d>\n"
	"WAIT 5\n{\n\"OK\" atok\n}\n"
	"EXIT KErrNoModem$\n"
	"atok:\n"
	"SEND \"ATD*99#\"+<0x0d>\n"
	"WAIT 45\n{\n"
	"\"CONNECT\" connected\n"
	"\"NO CARRIER\" nocarrier\n"
	"\"BUSY\" busy\n"
	"\"ERROR\" moderror\n"
	"}\n"
	"EXIT KErrNoAnswer$\n"
	"nocarrier:\nEXIT KErrNoCarrier$\n"
	"busy:\nEXIT KErrBusy$\n"
	"moderror:\nEXIT KErrModemError$\n"
	"connected:\nEXIT\n");

static void WriteModeL(CCommsDbTableView& aView, TInt aMode, MGnuboxLog& aLog)
	{
	if (aMode == EGnuboxModeDialup)
		{
		TBool scriptOk = ETrue;
		TRAPD(err, aView.WriteLongTextL(TPtrC(ISP_LOGIN_SCRIPT), KDialupScript));
		if (err != KErrNone)
			{
			LogD(aLog, _L("  (login script: %S)"), &ErrText(err));
			scriptOk = EFalse;
			}
		TRAP(err, aView.WriteBoolL(TPtrC(ISP_USE_LOGIN_SCRIPT), scriptOk));
		aLog.Detail(_L("  login script: AT / ATD*99# / wait CONNECT"));
		}
	else
		{
		TRAPD(err, aView.WriteBoolL(TPtrC(ISP_USE_LOGIN_SCRIPT), EFalse));
		aLog.Detail(_L("  no login script (PPP starts at once)"));
		}
	}

// --------------------------------------------------------------------------
// helpers

static void RollbackOnLeave(TAny* aDb)
	{
	CCommsDatabase* db = (CCommsDatabase*)aDb;
	if (db->InTransaction())
		db->RollbackTransaction();
	}

static TBool ReadNameMatches(CCommsDbTableView& aView, const TDesC& aName)
	{
	TBuf<KCommsDbSvrMaxFieldLength> name;
	TRAPD(err, aView.ReadTextL(TPtrC(COMMDB_NAME), name));
	return err == KErrNone && name.CompareF(aName) == 0;
	}

// position the view on the record called aName; returns its id or 0
static TUint32 FindByNameL(CCommsDbTableView& aView, const TDesC& aName)
	{
	TInt err = aView.GotoFirstRecord();
	while (err == KErrNone)
		{
		if (ReadNameMatches(aView, aName))
			{
			TUint32 id = 0;
			aView.ReadUintL(TPtrC(COMMDB_ID), id);
			return id;
			}
		err = aView.GotoNextRecord();
		}
	return 0;
	}

static TUint32 FirstIdL(CCommsDatabase& aDb, const TDesC& aTable)
	{
	CCommsDbTableView* view = aDb.OpenTableLC(aTable);
	TUint32 id = 0;
	if (view->GotoFirstRecord() == KErrNone)
		view->ReadUintL(TPtrC(COMMDB_ID), id);
	CleanupStack::PopAndDestroy(view);
	return id;
	}

// write helpers that log instead of aborting for optional columns
static void TryText(CCommsDbTableView& aView, const TText* aCol, const TDesC& aValue, MGnuboxLog& aLog)
	{
	TPtrC col(aCol);
	TRAPD(err, aView.WriteTextL(col, aValue));
	if (err != KErrNone)
		LogD(aLog, _L("  (column %S: %S)"), &col, &ErrText(err));
	}

static void TryUint(CCommsDbTableView& aView, const TText* aCol, TUint32 aValue, MGnuboxLog& aLog)
	{
	TPtrC col(aCol);
	TRAPD(err, aView.WriteUintL(col, aValue));
	if (err != KErrNone)
		LogD(aLog, _L("  (column %S: %S)"), &col, &ErrText(err));
	}

static void TryBool(CCommsDbTableView& aView, const TText* aCol, TBool aValue, MGnuboxLog& aLog)
	{
	TPtrC col(aCol);
	TRAPD(err, aView.WriteBoolL(col, aValue));
	if (err != KErrNone)
		LogD(aLog, _L("  (column %S: %S)"), &col, &ErrText(err));
	}

// --------------------------------------------------------------------------
// ModemBearer "GnuBox BT"

static TUint32 CreateModemL(CCommsDatabase& aDb, MGnuboxLog& aLog)
	{
	CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(MODEM_BEARER));
	TUint32 id = FindByNameL(*view, KGnuboxModemName);
	TBool fresh = EFalse;
	if (id)
		{
		User::LeaveIfError(view->UpdateRecord());
		LogD(aLog, _L("ModemBearer %d exists, updating"), id);
		}
	else
		{
		// copy the phone's own CSD modem record (DATAPORT + CSD.agt) so that
		// every column Nokia expects has a sensible value
		TBool found = EFalse;
		TInt err = view->GotoFirstRecord();
		while (err == KErrNone && !found)
			{
			TBuf<KCommsDbSvrMaxFieldLength> csy, agent;
			TRAPD(e1, view->ReadTextL(TPtrC(MODEM_CSY_NAME), csy));
			TRAPD(e2, view->ReadTextL(TPtrC(MODEM_AGENT), agent));
			if (e1 == KErrNone && e2 == KErrNone &&
				csy.CompareF(KDataport) == 0 && agent.CompareF(KCsdAgent) == 0)
				found = ETrue;
			else
				err = view->GotoNextRecord();
			}
		if (found)
			{
			TUint32 src = 0;
			view->ReadUintL(TPtrC(COMMDB_ID), src);
			User::LeaveIfError(view->InsertCopyRecord(id));
			LogD(aLog, _L("ModemBearer %d created (copy of CSD modem %d)"), id, src);
			}
		else
			{
			User::LeaveIfError(view->InsertRecord(id));
			fresh = ETrue;
			LogD(aLog, _L("ModemBearer %d created (no CSD modem to copy)"), id);
			}
		}

	view->WriteTextL(TPtrC(COMMDB_NAME), KGnuboxModemName);
	TryText(*view, MODEM_AGENT, KCsdAgent, aLog);
	TryText(*view, MODEM_NIF_NAME, KPppNif, aLog);
	view->WriteTextL(TPtrC(MODEM_PORT_NAME), KModemPort);
	view->WriteTextL(TPtrC(MODEM_CSY_NAME), KModemCsy);
	view->WriteTextL(TPtrC(MODEM_TSY_NAME), KModemTsy);
	TryUint(*view, MODEM_DATA_BITS, EData8, aLog);
	TryUint(*view, MODEM_STOP_BITS, EStop1, aLog);
	TryUint(*view, MODEM_PARITY, EParityNone, aLog);
	TryUint(*view, MODEM_RATE, EBps115200, aLog);
	TryUint(*view, MODEM_HANDSHAKING, 0, aLog);
	TryUint(*view, MODEM_SPECIAL_RATE, 0, aLog);
	TryUint(*view, MODEM_COMM_ROLE, 0, aLog);		// DTE
	if (fresh)
		{
		TryUint(*view, MODEM_XON_CHAR, 17, aLog);
		TryUint(*view, MODEM_XOFF_CHAR, 19, aLog);
		TryText(*view, MODEM_MODEM_INIT_STRING, _L("AT"), aLog);
		TryText(*view, MODEM_DATA_INIT_STRING, _L("AT"), aLog);
		}
	// never close the link for inactivity (the copied CSD values were 300 s,
	// which caused "PPP link closed: -33 timed out"); only Disconnect ends it
	const TUint32 KNever = 0xFFFFFFFF;	// -1
	TryUint(*view, LAST_SOCKET_ACTIVITY_TIMEOUT, KNever, aLog);
	TryUint(*view, LAST_SESSION_CLOSED_TIMEOUT, KNever, aLog);
	TryUint(*view, LAST_SOCKET_CLOSED_TIMEOUT, KNever, aLog);
	User::LeaveIfError(view->PutRecordChanges());
	CleanupStack::PopAndDestroy(view);
	return id;
	}

// --------------------------------------------------------------------------
// DialOutISP "GnuBox BT"

static TUint32 FindByIdL(CCommsDbTableView& aView, TUint32 aId)
	{
	TInt err = aView.GotoFirstRecord();
	while (err == KErrNone)
		{
		TUint32 id = 0;
		aView.ReadUintL(TPtrC(COMMDB_ID), id);
		if (id == aId)
			return id;
		err = aView.GotoNextRecord();
		}
	return 0;
	}

static TUint32 CreateIspL(CCommsDatabase& aDb, TInt aMode, TBool aCompress, TUint32 aAdoptId, MGnuboxLog& aLog)
	{
	CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(DIAL_OUT_ISP));
	TUint32 id = aAdoptId ? FindByIdL(*view, aAdoptId) : 0;
	TBool adopted = id != 0;
	if (!id)
		id = FindByNameL(*view, KGnuboxIspName);
	if (adopted)
		{
		User::LeaveIfError(view->UpdateRecord());
		LogD(aLog, _L("DialOutISP %d (of the existing GnuBox BT access point) updated"), id);
		}
	else if (id)
		{
		User::LeaveIfError(view->UpdateRecord());
		LogD(aLog, _L("DialOutISP %d exists, updating"), id);
		}
	else if (view->GotoFirstRecord() == KErrNone)
		{
		TUint32 src = 0;
		view->ReadUintL(TPtrC(COMMDB_ID), src);
		User::LeaveIfError(view->InsertCopyRecord(id));
		LogD(aLog, _L("DialOutISP %d created (copy of %d)"), id, src);
		}
	else
		{
		User::LeaveIfError(view->InsertRecord(id));
		LogD(aLog, _L("DialOutISP %d created"), id);
		}

	if (!adopted)
		view->WriteTextL(TPtrC(COMMDB_NAME), KGnuboxIspName);
	TryText(*view, ISP_DESCRIPTION, KIspDescription, aLog);
	TryUint(*view, ISP_TYPE, EIspTypeInternetOnly, aLog);
	// the important one: no number => no dialling, PPP starts at once
	view->WriteTextL(TPtrC(ISP_DEFAULT_TEL_NUM), KNullDesC);
	TryBool(*view, ISP_DIAL_RESOLUTION, EFalse, aLog);
	TryBool(*view, ISP_PROMPT_FOR_LOGIN, EFalse, aLog);
	TryBool(*view, ISP_DISPLAY_PCT, EFalse, aLog);
	TryText(*view, ISP_IF_NETWORKS, _L("ip"), aLog);
	TryBool(*view, ISP_IF_PROMPT_FOR_AUTH, EFalse, aLog);
	TryText(*view, ISP_IF_AUTH_NAME, KPppUser, aLog);
	TryText(*view, ISP_IF_AUTH_PASS, KPppPass, aLog);
	TryUint(*view, ISP_IF_AUTH_RETRIES, 1, aLog);
	TryBool(*view, ISP_IF_CALLBACK_ENABLED, EFalse, aLog);
	TryBool(*view, ISP_IP_ADDR_FROM_SERVER, ETrue, aLog);
	TryBool(*view, ISP_IP_DNS_ADDR_FROM_SERVER, ETrue, aLog);
	TryBool(*view, ISP_IP6_DNS_ADDR_FROM_SERVER, EFalse, aLog);
	// compression: Van Jacobson TCP/IP header compression (40 -> ~4 bytes per
	// packet) and PPP data compression; both only used if the peer agrees
	TryBool(*view, ISP_ENABLE_IP_HEADER_COMP, aCompress, aLog);
	TryBool(*view, ISP_ENABLE_LCP_EXTENSIONS, EFalse, aLog);
	TryBool(*view, ISP_DISABLE_PLAIN_TEXT_AUTH, EFalse, aLog);
	TryBool(*view, ISP_ENABLE_SW_COMP, aCompress, aLog);
	TryUint(*view, ISP_BEARER_TYPE, EBearerTypeCSD, aLog);
	WriteModeL(*view, aMode, aLog);
	User::LeaveIfError(view->PutRecordChanges());
	CleanupStack::PopAndDestroy(view);
	return id;
	}

// --------------------------------------------------------------------------
// IAP "GnuBox BT"

static TUint32 CreateIapL(CCommsDatabase& aDb, TUint32 aIsp, TUint32 aModem, TUint32 aWeight, MGnuboxLog& aLog)
	{
	TUint32 network = 0;
	TRAPD(err, aDb.GetGlobalSettingL(TPtrC(DEFAULT_NETWORK), network));
	if (err != KErrNone || network == 0)
		network = FirstIdL(aDb, TPtrC(NETWORK));
	TUint32 location = 0;
	TRAP(err, aDb.GetGlobalSettingL(TPtrC(LOCATION_DATA_FAX), location));
	if (err != KErrNone || location == 0)
		location = FirstIdL(aDb, TPtrC(LOCATION));
	LogD(aLog, _L("Network %d, Location %d"), network, location);

	CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(IAP));
	TUint32 id = FindByNameL(*view, KGnuboxIapName);
	TBool existed = id != 0;
	if (id)
		{
		User::LeaveIfError(view->UpdateRecord());
		LogD(aLog, _L("IAP %d exists, updating"), id);
		}
	else
		{
		User::LeaveIfError(view->InsertRecord(id));
		LogD(aLog, _L("IAP %d created"), id);
		}
	view->WriteTextL(TPtrC(COMMDB_NAME), KGnuboxIapName);
	view->WriteUintL(TPtrC(IAP_SERVICE), aIsp);
	view->WriteTextL(TPtrC(IAP_SERVICE_TYPE), TPtrC(DIAL_OUT_ISP));
	TryUint(*view, IAP_BEARER, aModem, aLog);
	TryText(*view, IAP_BEARER_TYPE, TPtrC(MODEM_BEARER), aLog);
	if (network)
		TryUint(*view, IAP_NETWORK, network, aLog);
	if (!existed)
		{
		// the Series 80 access point UI orders by weighting; give ours a real one
		TryUint(*view, IAP_NETWORK_WEIGHTING, aWeight, aLog);
		}
	if (location)
		TryUint(*view, IAP_LOCATION, location, aLog);
	User::LeaveIfError(view->PutRecordChanges());
	CleanupStack::PopAndDestroy(view);
	return id;
	}

// --------------------------------------------------------------------------

static CCommsDatabase* OpenDbLC()
	{
	CCommsDatabase* db = CCommsDatabase::NewL(EDatabaseTypeIAP);
	CleanupStack::PushL(db);
	db->ShowHiddenRecords();
	return db;
	}

static void BeginLC(CCommsDatabase& aDb)
	{
	TInt err = KErrLocked;
	for (TInt i = 0; i < 10 && err == KErrLocked; i++)
		{
		err = aDb.BeginTransaction();
		if (err == KErrLocked)
			User::After(300000);
		}
	User::LeaveIfError(err);
	CleanupStack::PushL(TCleanupItem(RollbackOnLeave, &aDb));
	}

void GnuboxCommDb::CreateRecordsL(MGnuboxLog& aLog, TInt aMode, TBool aCompress)
	{
	aLog.Detail(_L("Creating access point records..."));
	CCommsDatabase* db = OpenDbLC();
	BeginLC(*db);
	TUint32 modem = CreateModemL(*db, aLog);
	// an access point called "GnuBox BT" made in Control panel is adopted:
	// its own DialOutISP record is reused, only the bearer is switched
	TUint32 adoptIsp = 0, weight = 1;
		{
		CCommsDbTableView* iaps = db->OpenTableLC(TPtrC(IAP));
		TInt err = iaps->GotoFirstRecord();
		while (err == KErrNone)
			{
			TUint32 w = 0;
			TRAPD(e, iaps->ReadUintL(TPtrC(IAP_NETWORK_WEIGHTING), w));
			if (e == KErrNone && w >= weight)
				weight = w + 1;
			err = iaps->GotoNextRecord();
			}
		if (FindByNameL(*iaps, KGnuboxIapName))
			{
			TBuf<KCommsDbSvrMaxFieldLength> type;
			TRAPD(e, iaps->ReadTextL(TPtrC(IAP_SERVICE_TYPE), type));
			if (e == KErrNone && type.CompareF(TPtrC(DIAL_OUT_ISP)) == 0)
				iaps->ReadUintL(TPtrC(IAP_SERVICE), adoptIsp);
			}
		CleanupStack::PopAndDestroy(iaps);
		}
	TUint32 isp = CreateIspL(*db, aMode, aCompress, adoptIsp, aLog);
	TUint32 iap = CreateIapL(*db, isp, modem, weight, aLog);
	User::LeaveIfError(db->CommitTransaction());
	CleanupStack::Pop();	// rollback item
	CleanupStack::PopAndDestroy(db);
	LogD(aLog, _L("Done: access point \"%S\" (IAP %d)."), &KGnuboxIapName, iap);
	}

static void DeleteNamedL(CCommsDatabase& aDb, const TDesC& aTable, const TDesC& aName, MGnuboxLog& aLog)
	{
	CCommsDbTableView* view = aDb.OpenTableLC(aTable);
	TUint32 id;
	while ((id = FindByNameL(*view, aName)) != 0)
		{
		User::LeaveIfError(view->DeleteRecord());
		LogD(aLog, _L("Deleted %S %d"), &aTable, id);
		}
	CleanupStack::PopAndDestroy(view);
	}

TBool GnuboxCommDb::ApplyModeL(MGnuboxLog& aLog, TInt aMode)
	{
	CCommsDatabase* db = OpenDbLC();
	BeginLC(*db);
	CCommsDbTableView* view = db->OpenTableLC(TPtrC(DIAL_OUT_ISP));
	TBool found = FindByNameL(*view, KGnuboxIspName) != 0;
	if (found)
		{
		User::LeaveIfError(view->UpdateRecord());
		WriteModeL(*view, aMode, aLog);
		User::LeaveIfError(view->PutRecordChanges());
		}
	CleanupStack::PopAndDestroy(view);
	User::LeaveIfError(db->CommitTransaction());
	CleanupStack::Pop();
	CleanupStack::PopAndDestroy(db);
	return found;
	}

void GnuboxCommDb::RemoveRecordsL(MGnuboxLog& aLog)
	{
	CCommsDatabase* db = OpenDbLC();
	BeginLC(*db);
	DeleteNamedL(*db, TPtrC(IAP), KGnuboxIapName, aLog);
	DeleteNamedL(*db, TPtrC(DIAL_OUT_ISP), KGnuboxIspName, aLog);
	DeleteNamedL(*db, TPtrC(MODEM_BEARER), KGnuboxModemName, aLog);
	User::LeaveIfError(db->CommitTransaction());
	CleanupStack::Pop();
	CleanupStack::PopAndDestroy(db);
	aLog.Detail(_L("GnuBox records removed."));
	}

TUint32 GnuboxCommDb::FindIapL()
	{
	CCommsDatabase* db = OpenDbLC();
	CCommsDbTableView* view = db->OpenTableLC(TPtrC(IAP));
	TUint32 id = FindByNameL(*view, KGnuboxIapName);
	CleanupStack::PopAndDestroy(2);
	return id;
	}

// --------------------------------------------------------------------------
// reading records

static void FormatColumnL(CCommsDbTableView& aView, const TDesC& aCol, TDes& aOut, TBool& aPresent)
	{
	aPresent = EFalse;
	TDbColType type;
	TUint32 attr;
	TRAPD(err, aView.ReadTypeAttribL(aCol, type, attr));
	if (err != KErrNone)
		return;
	aOut.Zero();
	switch (type)
		{
	case EDbColBit:
		{
		TBool b = EFalse;
		TRAP(err, aView.ReadBoolL(aCol, b));
		if (err == KErrNone)
			aOut.Append(b ? _L("TRUE") : _L("FALSE"));
		break;
		}
	case EDbColInt8: case EDbColUint8: case EDbColInt16: case EDbColUint16:
	case EDbColInt32: case EDbColUint32: case EDbColInt64:
		{
		TUint32 u = 0;
		TRAP(err, aView.ReadUintL(aCol, u));
		if (err == KErrNone)
			aOut.AppendNum((TUint)u, EDecimal);
		break;
		}
	case EDbColText8:
		{
		TBuf8<KCommsDbSvrMaxFieldLength> t8;
		TRAP(err, aView.ReadTextL(aCol, t8));
		if (err == KErrNone)
			{
			// BT addresses etc.: show non-printable as hex
			for (TInt i = 0; i < t8.Length() && aOut.Length() < aOut.MaxLength() - 3; i++)
				{
				TUint8 c = t8[i];
				if (c >= 32 && c < 127)
					aOut.Append(c);
				else
					aOut.AppendFormat(_L("\\x%02x"), c);
				}
			}
		break;
		}
	case EDbColText16:
		{
		TBuf<KCommsDbSvrMaxFieldLength> t;
		TRAP(err, aView.ReadTextL(aCol, t));
		if (err == KErrNone)
			aOut.Append(t.Left(aOut.MaxLength()));
		break;
		}
	case EDbColLongText8: case EDbColLongText16:
		{
		HBufC* h = NULL;
		TRAP(err, { h = aView.ReadLongTextLC(aCol); CleanupStack::Pop(h); });
		if (err == KErrNone && h)
			{
			TPtrC p(h->Des());
			for (TInt i = 0; i < p.Length() && aOut.Length() < aOut.MaxLength() - 2; i++)
				{
				if (p[i] == '\n') aOut.Append(_L("\\n"));
				else if (p[i] == '\r') aOut.Append(_L("\\r"));
				else aOut.Append(p[i]);
				}
			delete h;
			}
		break;
		}
	default:
		aOut.Append(_L("?"));
		break;
		}
	if (err == KErrNone)
		aPresent = ETrue;
	else if (err == KErrUnknown || err == KErrNotFound)
		{
		aOut.Copy(_L("NULL"));	// column exists but is null
		aPresent = ETrue;
		}
	}

static void ShowRecordLineL(CCommsDbTableView& aView, MGnuboxLog& aLog, const TText* const* aCols)
	{
	TBuf<200> line;
	for (TInt i = 0; aCols[i]; i++)
		{
		TBuf<80> val;
		TBool present;
		TPtrC col(aCols[i]);
		FormatColumnL(aView, col, val, present);
		if (!present)
			continue;
		if (line.Length() + col.Length() + val.Length() + 3 > line.MaxLength())
			break;
		if (line.Length())
			line.Append(_L(", "));
		line.Append(col);
		line.Append('=');
		line.Append(val);
		}
	aLog.Log(line);
	}

static const TText* const KIapCols[] = { COMMDB_ID, COMMDB_NAME, IAP_SERVICE, IAP_SERVICE_TYPE,
	IAP_BEARER, IAP_BEARER_TYPE, IAP_NETWORK, IAP_LOCATION, 0 };
static const TText* const KModemCols[] = { COMMDB_ID, COMMDB_NAME, MODEM_AGENT, MODEM_NIF_NAME,
	MODEM_PORT_NAME, MODEM_CSY_NAME, MODEM_TSY_NAME, MODEM_RATE, MODEM_HANDSHAKING, 0 };
static const TText* const KIspCols[] = { COMMDB_ID, COMMDB_NAME, ISP_DEFAULT_TEL_NUM,
	ISP_USE_LOGIN_SCRIPT, ISP_LOGIN_SCRIPT, ISP_IF_PROMPT_FOR_AUTH, ISP_IF_AUTH_NAME, ISP_IP_ADDR_FROM_SERVER,
	ISP_BEARER_TYPE, 0 };

static void ShowNamedL(CCommsDatabase& aDb, const TDesC& aTable, const TDesC& aName,
	const TText* const* aCols, MGnuboxLog& aLog)
	{
	CCommsDbTableView* view = aDb.OpenTableLC(aTable);
	if (FindByNameL(*view, aName))
		{
		TBuf<40> head(aTable);
		head.Append(':');
		aLog.Log(head);
		ShowRecordLineL(*view, aLog, aCols);
		}
	else
		LogF(aLog, _L("%S: no \"%S\" record"), &aTable, &aName);
	CleanupStack::PopAndDestroy(view);
	}

static const TText* const KIapListCols[] = { COMMDB_ID, COMMDB_NAME, IAP_SERVICE_TYPE,
	IAP_BEARER_TYPE, IAP_BEARER, IAP_NETWORK_WEIGHTING, 0 };

// for one IAP row: the service and bearer records it points to
static const TText* const KLinkedCols[] = { COMMDB_ID, COMMDB_NAME, MODEM_AGENT, MODEM_NIF_NAME,
	MODEM_CSY_NAME, MODEM_PORT_NAME, MODEM_TSY_NAME, MODEM_COMM_ROLE, ISP_DEFAULT_TEL_NUM,
	ISP_TYPE, ISP_BEARER_TYPE, ISP_USE_LOGIN_SCRIPT, LAN_BEARER_LDD_NAME, LAN_BEARER_PDD_NAME,
	_S("IfNetworks"), 0 };

static void ShowOneLinkedL(CCommsDatabase& aDb, const TDesC& aTable, TUint32 aId, MGnuboxLog& aLog)
	{
	if (aTable.Length() == 0 || aId == 0)
		return;
	CCommsDbTableView* v = aDb.OpenViewMatchingUintLC(aTable, TPtrC(COMMDB_ID), aId);
	if (v->GotoFirstRecord() == KErrNone)
		{
		TBuf<40> head;
		head.Format(_L("   %S:"), &aTable);
		aLog.Log(head);
		ShowRecordLineL(*v, aLog, KLinkedCols);
		}
	CleanupStack::PopAndDestroy(v);
	}

static void ShowLinkedL(CCommsDatabase& aDb, CCommsDbTableView& aIap, MGnuboxLog& aLog)
	{
	TBuf<KCommsDbSvrMaxFieldLength> type;
	TUint32 id = 0;
	TRAPD(e1, aIap.ReadTextL(TPtrC(IAP_SERVICE_TYPE), type));
	TRAPD(e2, aIap.ReadUintL(TPtrC(IAP_SERVICE), id));
	if (e1 == KErrNone && e2 == KErrNone)
		ShowOneLinkedL(aDb, type, id, aLog);
	type.Zero();
	id = 0;
	TRAP(e1, aIap.ReadTextL(TPtrC(IAP_BEARER_TYPE), type));
	TRAP(e2, aIap.ReadUintL(TPtrC(IAP_BEARER), id));
	if (e1 == KErrNone && e2 == KErrNone)
		ShowOneLinkedL(aDb, type, id, aLog);
	}

static void ShowPrefsL(CCommsDatabase& aDb, MGnuboxLog& aLog)
	{
	CCommsDbConnectionPrefTableView* prefs =
		aDb.OpenConnectionPrefTableInRankOrderLC(ECommDbConnectionDirectionOutgoing);
	TInt err = prefs->GotoFirstRecord();
	if (err != KErrNone)
		aLog.Log(_L("Connection preferences: none"));
	while (err == KErrNone)
		{
		CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
		TRAPD(e, prefs->ReadConnectionPreferenceL(pref));
		if (e == KErrNone)
			{
			_LIT(KAsk, "asks which access point to use");
			_LIT(KNoAsk, "connects without asking");
			_LIT(KWarn, "warns, then connects");
			const TDesC* how = pref.iDialogPref == ECommDbDialogPrefDoNotPrompt ? &KNoAsk() :
				pref.iDialogPref == ECommDbDialogPrefWarn ? &KWarn() : &KAsk();
			TBuf<KCommsDbSvrMaxFieldLength> name;
			TRAPD(e2,
				{
				CCommsDbTableView* v = aDb.OpenViewMatchingUintLC(TPtrC(IAP), TPtrC(COMMDB_ID), pref.iBearer.iIapId);
				if (v->GotoFirstRecord() == KErrNone)
					v->ReadTextL(TPtrC(COMMDB_NAME), name);
				CleanupStack::PopAndDestroy(v);
				});
			LogF(aLog, _L("Default connection %d: %S (IAP %d) - %S [dialog %d]"),
				pref.iRanking, &name, pref.iBearer.iIapId, how, pref.iDialogPref);
			}
		err = prefs->GotoNextRecord();
		}
	CleanupStack::PopAndDestroy(prefs);
	}

void GnuboxCommDb::ShowPreferencesL(MGnuboxLog& aLog)
	{
	CCommsDatabase* db = OpenDbLC();
	ShowPrefsL(*db, aLog);
	CleanupStack::PopAndDestroy(db);
	}

void GnuboxCommDb::ShowStatusL(MGnuboxLog& aLog)
	{
	CCommsDatabase* db = OpenDbLC();
	ShowNamedL(*db, TPtrC(IAP), KGnuboxIapName, KIapCols, aLog);
	ShowNamedL(*db, TPtrC(DIAL_OUT_ISP), KGnuboxIspName, KIspCols, aLog);
	ShowNamedL(*db, TPtrC(MODEM_BEARER), KGnuboxModemName, KModemCols, aLog);
	CleanupStack::PopAndDestroy(db);

	// what other apps see: no hidden records, and CommDb's own CSD bearer filter
	aLog.Log(_L("All access points (as other apps see them):"));
	CCommsDatabase* plain = CCommsDatabase::NewL(EDatabaseTypeIAP);
	CleanupStack::PushL(plain);
	CCommsDbTableView* view = plain->OpenTableLC(TPtrC(IAP));
	TBool visible = EFalse;
	TInt err = view->GotoFirstRecord();
	while (err == KErrNone)
		{
		ShowRecordLineL(*view, aLog, KIapListCols);
		TRAPD(e, ShowLinkedL(*plain, *view, aLog));
		if (ReadNameMatches(*view, KGnuboxIapName))
			visible = ETrue;
		err = view->GotoNextRecord();
		}
	CleanupStack::PopAndDestroy(view);
	_LIT(KYes, "yes");
	_LIT(KNo, "NO");
	LogF(aLog, _L("GnuBox BT visible (not hidden): %S"), visible ? &KYes() : &KNo());
	TBool csd = EFalse;
	TRAPD(e,
		{
		CCommsDbTableView* v = plain->OpenIAPTableViewMatchingBearerSetLC(
			KCommDbBearerCSD, ECommDbConnectionDirectionOutgoing);
		csd = FindByNameL(*v, KGnuboxIapName) != 0;
		CleanupStack::PopAndDestroy(v);
		});
	if (e == KErrNone)
		LogF(aLog, _L("GnuBox BT in CommDb's data-call list: %S"), csd ? &KYes() : &KNo());
	TRAP(e, ShowPrefsL(*plain, aLog));
	CleanupStack::PopAndDestroy(plain);
	}

// rank 1 outgoing preference = the phone's default connection.
// aIap == 0 keeps the current IAP (used for "ask every time").
void GnuboxCommDb::SetDefaultL(TUint32 aIap, TBool aAsk, MGnuboxLog& aLog, TUint32* aOldIap, TInt* aOldDialog)
	{
	if (aOldIap)
		*aOldIap = 0;
	if (aOldDialog)
		*aOldDialog = -1;
	CCommsDatabase* db = OpenDbLC();
	BeginLC(*db);
	CCommsDbConnectionPrefTableView* prefs =
		db->OpenConnectionPrefTableViewOnRankLC(ECommDbConnectionDirectionOutgoing, 1);
	const TUint32 KAllBearers = KCommDbBearerCSD | KCommDbBearerWcdma | KCommDbBearerLAN | KCommDbBearerVirtual;
	TCommDbDialogPref dlg = aAsk ? ECommDbDialogPrefPrompt : ECommDbDialogPrefDoNotPrompt;
	if (prefs->GotoFirstRecord() == KErrNone)
		{
		CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
		prefs->ReadConnectionPreferenceL(pref);
		if (aOldIap)
			*aOldIap = pref.iBearer.iIapId;
		if (aOldDialog)
			*aOldDialog = pref.iDialogPref;
		CCommsDbConnectionPrefTableView::TCommDbIapBearer bearer = pref.iBearer;
		if (aIap)
			bearer.iIapId = aIap;
		bearer.iBearerSet |= KAllBearers;	// GPRS, data call and LAN APs all allowed
		prefs->UpdateBearerL(bearer);
		prefs->UpdateDialogPrefL(dlg);
		LogD(aLog, _L("Default connection: IAP %d -> %d, dialog %d"), pref.iBearer.iIapId, bearer.iIapId, dlg);
		}
	else
		{
		if (!aIap)
			User::Leave(KErrNotFound);
		CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
		pref.iRanking = 1;
		pref.iDirection = ECommDbConnectionDirectionOutgoing;
		pref.iDialogPref = dlg;
		pref.iBearer.iBearerSet = KAllBearers;
		pref.iBearer.iIapId = aIap;
		prefs->InsertConnectionPreferenceL(pref);
		LogD(aLog, _L("Default connection created: IAP %d, dialog %d"), aIap, dlg);
		}
	CleanupStack::PopAndDestroy(prefs);
	User::LeaveIfError(db->CommitTransaction());
	CleanupStack::Pop();
	CleanupStack::PopAndDestroy(db);
	}

TBool GnuboxCommDb::ReadDefaultL(TUint32& aIap, TBool& aAsk)
	{
	aIap = 0;
	aAsk = ETrue;
	CCommsDatabase* db = OpenDbLC();
	CCommsDbConnectionPrefTableView* prefs =
		db->OpenConnectionPrefTableViewOnRankLC(ECommDbConnectionDirectionOutgoing, 1);
	TBool found = EFalse;
	if (prefs->GotoFirstRecord() == KErrNone)
		{
		CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
		prefs->ReadConnectionPreferenceL(pref);
		aIap = pref.iBearer.iIapId;
		aAsk = pref.iDialogPref != ECommDbDialogPrefDoNotPrompt;
		found = ETrue;
		}
	CleanupStack::PopAndDestroy(2);	// prefs, db
	return found;
	}

// every access point other apps can see, with a plain-words type
void GnuboxCommDb::ListIapsL(CGnuboxIapArray& aList)
	{
	aList.Reset();
	CCommsDatabase* db = CCommsDatabase::NewL(EDatabaseTypeIAP);	// no hidden records
	CleanupStack::PushL(db);
	CCommsDbTableView* view = db->OpenTableLC(TPtrC(IAP));
	TInt err = view->GotoFirstRecord();
	while (err == KErrNone)
		{
		TGnuboxIap iap;
		view->ReadUintL(TPtrC(COMMDB_ID), iap.iId);
		TRAPD(e, view->ReadTextL(TPtrC(COMMDB_NAME), iap.iName));
		TBuf<KCommsDbSvrMaxFieldLength> service, bearer;
		TRAP(e, view->ReadTextL(TPtrC(IAP_SERVICE_TYPE), service));
		TRAP(e, view->ReadTextL(TPtrC(IAP_BEARER_TYPE), bearer));
		if (iap.iName.CompareF(KGnuboxIapName) == 0)
			iap.iType.Copy(_L("Bluetooth"));
		else if (service.CompareF(TPtrC(OUTGOING_GPRS)) == 0)
			iap.iType.Copy(_L("GPRS"));
		else if (service.CompareF(TPtrC(LAN_SERVICE)) == 0)
			iap.iType.Copy(bearer.CompareF(TPtrC(LAN_BEARER)) == 0 && iap.iName.FindF(_L("pass")) >= 0 ?
				_L("USB") : _L("LAN/WLAN"));
		else if (service.CompareF(TPtrC(DIAL_OUT_ISP)) == 0)
			iap.iType.Copy(_L("data call"));
		else
			iap.iType.Copy(service.Left(iap.iType.MaxLength()));
		aList.AppendL(iap);
		err = view->GotoNextRecord();
		}
	CleanupStack::PopAndDestroy(2);	// view, db
	}

void GnuboxCommDb::MakePreferredL(MGnuboxLog& aLog, TUint32& aOldIap, TInt& aOldDialog)
	{
	TUint32 iap = FindIapL();
	if (!iap)
		User::Leave(KErrNotFound);
	SetDefaultL(iap, EFalse, aLog, &aOldIap, &aOldDialog);
	LogF(aLog, _L("Default connection: GnuBox BT (IAP %d), connects without asking."), iap);
	}

void GnuboxCommDb::RestorePreferredL(TUint32 aIap, TInt aDialog, MGnuboxLog& aLog)
	{
	CCommsDatabase* db = OpenDbLC();
	BeginLC(*db);
	CCommsDbConnectionPrefTableView* prefs =
		db->OpenConnectionPrefTableViewOnRankLC(ECommDbConnectionDirectionOutgoing, 1);
	if (prefs->GotoFirstRecord() == KErrNone)
		{
		CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
		prefs->ReadConnectionPreferenceL(pref);
		if (aIap)
			{
			CCommsDbConnectionPrefTableView::TCommDbIapBearer bearer = pref.iBearer;
			bearer.iIapId = aIap;
			prefs->UpdateBearerL(bearer);
			prefs->UpdateDialogPrefL(aDialog >= 0 ? (TCommDbDialogPref)aDialog : ECommDbDialogPrefPrompt);
			LogD(aLog, _L("Preference rank 1 restored to IAP %d, dialog %d"), aIap, aDialog);
			}
		else
			{
			prefs->DeleteConnectionPreferenceL();
			LogD(aLog, _L("Preference rank 1 removed (there was none before)"));
			}
		}
	CleanupStack::PopAndDestroy(prefs);
	User::LeaveIfError(db->CommitTransaction());
	CleanupStack::Pop();
	CleanupStack::PopAndDestroy(db);
	}

// --------------------------------------------------------------------------
// paired Bluetooth devices (BTDeviceTable has no Name column)

static TBool HexNibble(TUint c, TUint& v)
	{
	if (c >= '0' && c <= '9') { v = c - '0'; return ETrue; }
	if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return ETrue; }
	if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return ETrue; }
	return EFalse;
	}

void GnuboxCommDb::ListPairedL(CGnuboxBtDeviceArray& aList, MGnuboxLog& aLog)
	{
	aList.Reset();
	CCommsDatabase* db = OpenDbLC();
	CCommsDbTableView* view = db->OpenTableLC(TPtrC(BT_DEVICE_TABLE));
	TInt err = view->GotoFirstRecord();
	while (err == KErrNone)
		{
		TBuf8<32> raw;
		TRAPD(e, view->ReadTextL(TPtrC(BT_DEV_ADD), raw));
		TGnuboxBtDevice dev;
		TBool ok = EFalse;
		if (e == KErrNone && raw.Length() == 6)
			{
			dev.iAddr = TBTDevAddr(raw);
			ok = ETrue;
			}
		else if (e == KErrNone && raw.Length() >= 12)
			{
			// stored as hex text on some builds
			TBuf8<6> bin;
			ok = ETrue;
			for (TInt i = 0, j = 0; i < 6 && ok; i++)
				{
				while (j < raw.Length() && raw[j] == ':')
					j++;
				TUint hi, lo;
				if (j + 1 < raw.Length() && HexNibble(raw[j], hi) && HexNibble(raw[j + 1], lo))
					{
					bin.Append((TUint8)(hi * 16 + lo));
					j += 2;
					}
				else
					ok = EFalse;
				}
			if (ok)
				dev.iAddr = TBTDevAddr(bin);
			}
		if (ok)
			{
			TBuf<KCommsDbSvrMaxFieldLength + 2> name;
			TBool present = EFalse;
			FormatColumnL(*view, TPtrC(BT_DEV_FRIENDLY_NAME), name, present);
			if (!present || name.Length() == 0 || name.Compare(_L("NULL")) == 0)
				FormatColumnL(*view, TPtrC(BT_DEV_DFLT_NAME), name, present);
			if (!present || name.Compare(_L("NULL")) == 0)
				name.Zero();
			dev.iName = name.Left(dev.iName.MaxLength());
			aList.AppendL(dev);
			}
		else
			LogD(aLog, _L("  (skipped a device record: address length %d, %S)"), raw.Length(), &ErrText(e));
		err = view->GotoNextRecord();
		}
	CleanupStack::PopAndDestroy(2);	// view, db
	}

// --------------------------------------------------------------------------
// full dump to C:\Documents\GnuBox\commdb.txt (for debugging other phones)

static const TText* const KDumpTables[] = {
	IAP, NETWORK, DIAL_OUT_ISP, DIAL_IN_ISP, OUTGOING_GPRS, MODEM_BEARER, LAN_BEARER,
	LAN_SERVICE, LOCATION, CHARGECARD, PROXIES, AGENT_LOOKUP, WAP_ACCESS_POINT,
	WAP_IP_BEARER, BT_DEFAULT_TABLE, _S("ConnectionPreferences"), 0 };
	// BTDeviceTable/BTSecurityTable are left out on purpose: they hold link keys

static void WriteLineL(RFileWriteStream& aOut, const TDesC& aText)
	{
	TBuf8<512> b;
	b.Copy(aText.Left(510));
	b.Append(_L8("\r\n"));
	aOut.WriteL(b);
	}

void GnuboxCommDb::DumpL(MGnuboxLog& aLog, RFs& aFs)
	{
	aFs.MkDirAll(KGnuboxDir);
	RFileWriteStream out;
	User::LeaveIfError(out.Replace(aFs, KGnuboxDumpFile, EFileWrite));
	CleanupClosePushL(out);
	CCommsDatabase* db = OpenDbLC();
	TInt rows = 0;
	for (TInt t = 0; KDumpTables[t]; t++)
		{
		TPtrC table(KDumpTables[t]);
		TBuf<64> head;
		head.Format(_L("[%S]"), &table);
		WriteLineL(out, head);
		CCommsDbTableView* view = NULL;
		TRAPD(err, { view = db->OpenTableLC(table); CleanupStack::Pop(view); });
		if (err != KErrNone)
			{
			head.Format(_L("# cannot open: %d"), err);
			WriteLineL(out, head);
			continue;
			}
		CleanupStack::PushL(view);
		err = view->GotoFirstRecord();
		while (err == KErrNone)
			{
			WriteLineL(out, _L("ADD_SECTION"));
			for (TInt c = 0; KGnuboxColumns[c]; c++)
				{
				TPtrC col(KGnuboxColumns[c]);
				TBuf<KCommsDbSvrMaxFieldLength + 2> val;
				TBool present;
				FormatColumnL(*view, col, val, present);
				if (!present)
					continue;
				// never write passwords or keys into a file people may share
				if (col.FindF(_L("Pass")) >= 0 || col.FindF(_L("LinkKey")) >= 0)
					{
					if (val.Length() && val.CompareF(_L("NULL")) != 0)
						val.Copy(_L("***"));
					}
				TBuf<KCommsDbSvrMaxFieldLength + 64> line;
				line.Append('\t');
				line.Append(col);
				line.Append('=');
				line.Append(val.Left(line.MaxLength() - line.Length()));
				WriteLineL(out, line);
				}
			WriteLineL(out, _L("END_ADD"));
			rows++;
			err = view->GotoNextRecord();
			}
		CleanupStack::PopAndDestroy(view);
		}
	// global settings
	WriteLineL(out, _L("[GlobalSettings]"));
	for (TInt c = 0; KGnuboxColumns[c]; c++)
		{
		TPtrC col(KGnuboxColumns[c]);
		TUint32 u = 0;
		TRAPD(err, db->GetGlobalSettingL(col, u));
		TBuf<80> line;
		if (err == KErrNone)
			{
			line.Format(_L("\t%S=%u"), &col, u);
			WriteLineL(out, line);
			continue;
			}
		TBuf<KCommsDbSvrMaxFieldLength> t;
		TRAP(err, db->GetGlobalSettingL(col, t));
		if (err == KErrNone)
			{
			TBuf<KCommsDbSvrMaxFieldLength + 64> l2;
			l2.Format(_L("\t%S=%S"), &col, &t);
			WriteLineL(out, l2);
			}
		}
	out.CommitL();
	CleanupStack::PopAndDestroy(2);	// db, out
	LogF(aLog, _L("Wrote %d records to %S"), rows, &KGnuboxDumpFile);
	}
