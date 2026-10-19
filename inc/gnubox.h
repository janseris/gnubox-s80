// gnubox.h - GnuBox for Series 80 (Nokia 9300/9300i/9500), Symbian OS 7.0s
//
// Based on the idea of gnubox by Mal Minhas (2002) and Mika Raento's
// 6600 changes: give the phone an Internet access point whose "modem" is
// the Bluetooth serial port (BTCOMM::0) and whose phone number is empty
// (= direct PPP, no dialling). The PC on the other end runs pppd.
//
// GPL v2 or later, see COPYING.txt

#ifndef GNUBOX_H
#define GNUBOX_H

#include <eikapp.h>
#include <eikdoc.h>
#include <eikappui.h>
#include <coecntrl.h>
#include <badesca.h>
#include <f32file.h>
#include <bttypes.h>
#include <es_sock.h>
#include <in_sock.h>
#include <commdbconnpref.h>

const TUid KUidGnubox = { 0x097764f0 };

// names of the records GnuBox owns in CommDb
_LIT(KGnuboxIapName, "GnuBox BT");
_LIT(KGnuboxIspName, "GnuBox BT");
_LIT(KGnuboxModemName, "GnuBox BT");
_LIT(KGnuboxBtServiceName, "GnuBox");

_LIT(KGnuboxDir, "C:\\Documents\\GnuBox\\");
_LIT(KGnuboxLogFile, "C:\\Documents\\GnuBox\\gnubox-log.txt");
_LIT(KGnuboxDumpFile, "C:\\Documents\\GnuBox\\commdb.txt");
_LIT(KGnuboxSettingsFile, "C:\\System\\Apps\\gnubox\\gnubox.ini");

// --------------------------------------------------------------------------
class MGnuboxLog
	{
public:
	// shown on screen and written to the log file
	virtual void Log(const TDesC& aText) = 0;
	// technical detail: log file only
	virtual void Detail(const TDesC& aText) = 0;
	};

// a small Format() helper that never panics on overflow
class TGnuboxOverflow : public TDes16Overflow
	{
public:
	void Overflow(TDes16&) {}
	};

void LogF(MGnuboxLog& aLog, TRefByValue<const TDesC> aFmt, ...);
void LogD(MGnuboxLog& aLog, TRefByValue<const TDesC> aFmt, ...);
const TDesC& ErrText(TInt aErr);

// --------------------------------------------------------------------------
// what is on the other end of the Bluetooth link
enum TGnuboxMode
	{
	// PC running pppd on a Serial Port (SPP, 0x1101) service: PPP starts at once
	EGnuboxModePc = 0,
	// dial-up modem emulation (DUN, 0x1103), e.g. BlueDUN on Android: the login
	// script sends AT, then ATD*99#, waits for CONNECT, then PPP starts
	EGnuboxModeDialup = 1
	};
TUint32 GnuboxServiceUuid(TInt aMode);
const TDesC& GnuboxModeName(TInt aMode);

// a paired device from CommDb's BTDeviceTable
class TGnuboxBtDevice
	{
public:
	TBTDevAddr iAddr;
	TBuf<60> iName;
	};
typedef CArrayFixFlat<TGnuboxBtDevice> CGnuboxBtDeviceArray;

// an access point, for the Default menu
class TGnuboxIap
	{
public:
	TUint32 iId;
	TBuf<50> iName;
	TBuf<16> iType;	// "Bluetooth", "GPRS", "USB", ...
	};
typedef CArrayFixFlat<TGnuboxIap> CGnuboxIapArray;

// persistent settings
class TGnuboxSettings
	{
public:
	TGnuboxSettings();
	void Load(RFs& aFs);
	TInt Save(RFs& aFs) const;
public:
	TBTDevAddr iAddr;
	TBuf<64> iName;
	TBool iHaveAddr;
	TInt iSecurity;		// KBTDefaultServiceSecurity*
	TInt iMode;			// TGnuboxMode
	TUint32 iPrevPrefIap;	// rank-1 IAP before "use for all apps" (0 = none)
	TBool iAutoReconnect;	// reconnect after a drop until the user disconnects
	TBool iUseForAll;		// rank-1 preference = GnuBox BT, no prompt
	TInt iPrevPrefDialog;	// dialog preference before (-1 = none)
	TBool iCompress;		// PPP header + data compression
	};

// --------------------------------------------------------------------------
// CommDb manipulation (gnuboxcommdb.cpp)
class GnuboxCommDb
	{
public:
	static void CreateRecordsL(MGnuboxLog& aLog, TInt aMode, TBool aCompress);
	static TBool ApplyModeL(MGnuboxLog& aLog, TInt aMode);	// EFalse if no records yet
	static void RemoveRecordsL(MGnuboxLog& aLog);
	static void ShowStatusL(MGnuboxLog& aLog);
	static void ShowPreferencesL(MGnuboxLog& aLog);
	static void DumpL(MGnuboxLog& aLog, RFs& aFs);
	static TUint32 FindIapL();	// 0 if not found
	static void ListPairedL(CGnuboxBtDeviceArray& aList, MGnuboxLog& aLog);
	// rank 1 outgoing connection preference -> GnuBox BT without a prompt;
	// returns the previous IAP (0 = none) and dialog preference (-1 = none)
	static void MakePreferredL(MGnuboxLog& aLog, TUint32& aOldIap, TInt& aOldDialog);
	static void RestorePreferredL(TUint32 aIap, TInt aDialog, MGnuboxLog& aLog);
	// the phone's default connection (ConnectionPreferences rank 1)
	static void SetDefaultL(TUint32 aIap, TBool aAsk, MGnuboxLog& aLog,
		TUint32* aOldIap = NULL, TInt* aOldDialog = NULL);
	static TBool ReadDefaultL(TUint32& aIap, TBool& aAsk);
	static void ListIapsL(CGnuboxIapArray& aList);
	};

// --------------------------------------------------------------------------
// optional bigger TCP windows in C:\System\Data\tcpip.ini (gnuboxtcp.cpp)
class GnuboxTcp
	{
public:
	static TBool IsEnabledL(RFs& aFs);
	static void EnableL(RFs& aFs, MGnuboxLog& aLog);
	static void DisableL(RFs& aFs, MGnuboxLog& aLog);
	};

// --------------------------------------------------------------------------
// Bluetooth (gnuboxbt.cpp)
class GnuboxBt
	{
public:
	static TBool SelectPcL(TGnuboxSettings& aSettings, MGnuboxLog& aLog);
	static void SetDefaultPortL(const TGnuboxSettings& aSettings, MGnuboxLog& aLog);
	static void ShowDefaultPortL(MGnuboxLog& aLog);
	static void AddrToText(const TBTDevAddr& aAddr, TDes& aText);
	};

// --------------------------------------------------------------------------
// the connection (gnuboxconn.cpp): start, watch, reconnect, stop

enum TGnuboxConnState
	{
	EConnIdle,			// not connected
	EConnConnecting,	// starting the access point
	EConnUp,			// connected
	EConnWaitRetry		// lost or failed; waiting to try again
	};

class MGnuboxConnObserver
	{
public:
	virtual void ConnStateChanged(TGnuboxConnState aState, const TDesC& aInfo) = 0;
	};

class CGnuboxProgress;
class CGnuboxRetryTimer;

class CGnuboxConnection : public CActive
	{
public:
	static CGnuboxConnection* NewL(MGnuboxLog& aLog, MGnuboxConnObserver& aObserver);
	~CGnuboxConnection();
	void ConnectL(TUint32 aIapId, TBool aAutoReconnect);
	void Disconnect();
	TBool AttachExistingL(TUint32 aIapId);	// pick up a connection left running
	void SetAutoReconnect(TBool aOn) { iAuto = aOn; }
	TGnuboxConnState State() const { return iPublicState; }
	const TTime& UpSince() const { return iUpSince; }
	// called by CGnuboxProgress / CGnuboxRetryTimer
	void ProgressL(const TNifProgress& aProgress);
	void RetryNow();
private:
	CGnuboxConnection(MGnuboxLog& aLog, MGnuboxConnObserver& aObserver);
	void ConstructL();
	void RunL();
	void DoCancel();
	TInt RunError(TInt aError);
	void StartAttemptL();
	void Failed(TInt aErr);
	void ScheduleRetry();
	void CloseHandles();
	void SetState(TGnuboxConnState aState, const TDesC& aInfo);
	static TInt DropCallBack(TAny* aSelf);
	void HandleDrop();
	TBool StopUnattachedL();
private:
	enum TStep { EStepIdle, EStepStarting, EStepResolving, EStepUp, EStepWait };
	MGnuboxLog& iLog;
	MGnuboxConnObserver& iObserver;
	TStep iStep;
	TGnuboxConnState iPublicState;
	RSocketServ iServ;
	TBool iServOpen;
	RConnection iConn;
	TBool iConnOpen;
	RHostResolver iResolver;
	TBool iResolverOpen;
	TNameEntry iNameEntry;
	TCommDbConnPref iPref;
	CGnuboxProgress* iProgress;
	CAsyncCallBack* iDropCallBack;
	CGnuboxRetryTimer* iRetry;
	TUint32 iIap;
	TBool iAuto;
	TBool iUserStop;
	TInt iRetries;
	TInt iDropError;
	TTime iUpSince;
	};

class CGnuboxProgress : public CActive
	{
public:
	CGnuboxProgress(RConnection& aConn, CGnuboxConnection& aOwner);
	~CGnuboxProgress();
	void Start();
private:
	void RunL();
	void DoCancel();
	TInt RunError(TInt) { return KErrNone; }
private:
	RConnection& iConn;
	CGnuboxConnection& iOwner;
	TNifProgressBuf iBuf;
	};

class CGnuboxRetryTimer : public CTimer
	{
public:
	static CGnuboxRetryTimer* NewL(CGnuboxConnection& aOwner);
private:
	CGnuboxRetryTimer(CGnuboxConnection& aOwner) : CTimer(EPriorityStandard), iOwner(aOwner) {}
	void RunL() { iOwner.RetryNow(); }
	CGnuboxConnection& iOwner;
	};

// --------------------------------------------------------------------------
// GUI (gnuboxui.cpp)
class CEikTextListBox;
class CEikLabel;

class CGnuboxView : public CCoeControl, public MGnuboxLog
	{
public:
	static CGnuboxView* NewL(const TRect& aRect);
	~CGnuboxView();
	void Log(const TDesC& aText);
	void Detail(const TDesC& aText);
	void ClearL();
	void SetStatusL(const TDesC& aLine1, const TDesC& aLine2);
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
	TInt CountComponentControls() const;
	CCoeControl* ComponentControl(TInt aIndex) const;
private:
	void ConstructL(const TRect& aRect);
	void SizeChanged();
	void Draw(const TRect& aRect) const;
	void WriteLogFile(const TDesC& aText);
private:
	CEikLabel* iStatus1;
	CEikLabel* iStatus2;
	CEikTextListBox* iListBox;
	CDesCArrayFlat* iLines;	// owned by the list box model
	TInt iHeaderHeight;
	};

class CGnuboxAppUi : public CEikAppUi, public MGnuboxConnObserver
	{
public:
	void ConstructL();
	~CGnuboxAppUi();
	void HandleCommandL(TInt aCommand);
	void DynInitMenuPaneL(TInt aResourceId, CEikMenuPane* aMenuPane);
	void ConnStateChanged(TGnuboxConnState aState, const TDesC& aInfo);
private:
	void CmdConnectL();
	void CmdDisconnect();
	void CmdDetailsL();
	void CmdRepairL();
	void CmdSetModeL(TInt aMode);
	void CmdSetSecurityL(TInt aLevel);
	void CmdToggleAutoReconnect();
	void CmdToggleUseForAllL();
	void CmdToggleCompression();
	void CmdToggleTcpWindowL();
	void CmdSetDefaultL(TInt aIndex);	// -1 = ask every time
	void OpenDeviceMenuL();
	void CmdSearchDeviceL();
	void PickedL(TInt aIndex);
	void UseDeviceL(const TBTDevAddr& aAddr, const TDesC& aName);
	void EnsureRecordsL();
	void UpdateStatusL();
	void UpdateButtonsL();
	void Welcome();
private:
	CGnuboxBtDeviceArray* iPaired;
	CGnuboxIapArray* iIaps;
	CGnuboxView* iView;
	CGnuboxConnection* iConnection;
	TGnuboxSettings iSettings;
	TBuf<80> iStateText;
	};

class CGnuboxDocument : public CEikDocument
	{
public:
	CGnuboxDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
	CEikAppUi* CreateAppUiL();
	};

class CGnuboxApplication : public CEikApplication
	{
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
	};

#endif
