// gnuboxconn.cpp - keeps the "GnuBox BT" connection up until the user stops it
//
// Connect -> RConnection::Start on the GnuBox IAP (no prompt), then a DNS
// lookup as an Internet check. Every progress stage is logged in plain words.
// If the link drops (or a start fails) and the user has not pressed
// Disconnect, it tries again after 10 s (30 s after three failures).
// Disconnect stops the interface for every app, also one left running by an
// earlier GnuBox session (found with EnumerateConnections + Attach).
//
// GPL v2 or later, see COPYING.txt

#include <nifvar.h>
#include <es_enum.h>
#include "gnubox.h"

_LIT(KCheckHost, "example.com");
const TInt KRetryShort = 10;	// seconds
const TInt KRetryLong = 30;
const TInt KShortRetries = 3;

// --------------------------------------------------------------------------

CGnuboxConnection* CGnuboxConnection::NewL(MGnuboxLog& aLog, MGnuboxConnObserver& aObserver)
	{
	CGnuboxConnection* self = new (ELeave) CGnuboxConnection(aLog, aObserver);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

CGnuboxConnection::CGnuboxConnection(MGnuboxLog& aLog, MGnuboxConnObserver& aObserver)
	: CActive(EPriorityStandard), iLog(aLog), iObserver(aObserver),
	  iStep(EStepIdle), iPublicState(EConnIdle), iAuto(ETrue)
	{
	CActiveScheduler::Add(this);
	}

void CGnuboxConnection::ConstructL()
	{
	iDropCallBack = new (ELeave) CAsyncCallBack(TCallBack(DropCallBack, this), EPriorityStandard);
	iRetry = CGnuboxRetryTimer::NewL(*this);
	}

CGnuboxConnection::~CGnuboxConnection()
	{
	Cancel();
	delete iRetry;
	delete iDropCallBack;
	// leave the interface running: closing our handle does not stop it
	CloseHandles();
	}

CGnuboxRetryTimer* CGnuboxRetryTimer::NewL(CGnuboxConnection& aOwner)
	{
	CGnuboxRetryTimer* self = new (ELeave) CGnuboxRetryTimer(aOwner);
	CleanupStack::PushL(self);
	self->ConstructL();
	CActiveScheduler::Add(self);
	CleanupStack::Pop(self);
	return self;
	}

void CGnuboxConnection::SetState(TGnuboxConnState aState, const TDesC& aInfo)
	{
	iPublicState = aState;
	iObserver.ConnStateChanged(aState, aInfo);
	}

void CGnuboxConnection::CloseHandles()
	{
	delete iProgress;
	iProgress = NULL;
	if (iResolverOpen)
		iResolver.Close();
	iResolverOpen = EFalse;
	if (iConnOpen)
		iConn.Close();
	iConnOpen = EFalse;
	if (iServOpen)
		iServ.Close();
	iServOpen = EFalse;
	}

// --------------------------------------------------------------------------

void CGnuboxConnection::ConnectL(TUint32 aIapId, TBool aAutoReconnect)
	{
	iRetry->Cancel();
	Cancel();
	CloseHandles();
	iIap = aIapId;
	iAuto = aAutoReconnect;
	iUserStop = EFalse;
	iRetries = 0;
	StartAttemptL();
	}

void CGnuboxConnection::StartAttemptL()
	{
	CloseHandles();
	User::LeaveIfError(iServ.Connect());
	iServOpen = ETrue;
	User::LeaveIfError(iConn.Open(iServ));
	iConnOpen = ETrue;
	iProgress = new (ELeave) CGnuboxProgress(iConn, *this);
	iProgress->Start();

	iPref.SetIapId(iIap);
	iPref.SetDialogPreference(ECommDbDialogPrefDoNotPrompt);
	iPref.SetDirection(ECommDbConnectionDirectionOutgoing);
	LogD(iLog, _L("RConnection::Start IAP %d"), iIap);
	iStep = EStepStarting;
	iConn.Start(iPref, iStatus);
	SetActive();
	SetState(EConnConnecting, _L("Connecting..."));
	}

TBool CGnuboxConnection::AttachExistingL(TUint32 aIapId)
	{
	iIap = aIapId;
	CloseHandles();
	User::LeaveIfError(iServ.Connect());
	iServOpen = ETrue;
	User::LeaveIfError(iConn.Open(iServ));
	iConnOpen = ETrue;
	TUint count = 0;
	User::LeaveIfError(iConn.EnumerateConnections(count));
	for (TUint i = 1; i <= count; i++)
		{
		TConnectionInfoBuf info;
		if (iConn.GetConnectionInfo(i, info) != KErrNone)
			continue;
		if (info().iIapId != aIapId)
			continue;
		if (iConn.Attach(info, RConnection::EAttachTypeNormal) != KErrNone)
			break;
		iProgress = new (ELeave) CGnuboxProgress(iConn, *this);
		iProgress->Start();
		iUserStop = EFalse;
		iStep = EStepUp;
		iUpSince.HomeTime();
		iLog.Log(_L("GnuBox BT is already connected - attached to it."));
		SetState(EConnUp, _L("Connected"));
		return ETrue;
		}
	CloseHandles();
	return EFalse;
	}

// stop a GnuBox BT interface we have no handle to (e.g. after app restart)
TBool CGnuboxConnection::StopUnattachedL()
	{
	RSocketServ serv;
	User::LeaveIfError(serv.Connect());
	CleanupClosePushL(serv);
	RConnection conn;
	User::LeaveIfError(conn.Open(serv));
	CleanupClosePushL(conn);
	TUint count = 0;
	TBool stopped = EFalse;
	if (conn.EnumerateConnections(count) == KErrNone)
		{
		for (TUint i = 1; i <= count && !stopped; i++)
			{
			TConnectionInfoBuf info;
			if (conn.GetConnectionInfo(i, info) == KErrNone && info().iIapId == iIap &&
				conn.Attach(info, RConnection::EAttachTypeNormal) == KErrNone)
				{
				conn.Stop();
				stopped = ETrue;
				}
			}
		}
	CleanupStack::PopAndDestroy(2);	// conn, serv
	return stopped;
	}

void CGnuboxConnection::Disconnect()
	{
	iUserStop = ETrue;
	iRetry->Cancel();
	Cancel();
	TBool stopped = EFalse;
	if (iConnOpen)
		{
		iConn.Stop();
		stopped = ETrue;
		}
	CloseHandles();
	if (!stopped && iIap)
		{
		TRAPD(err, stopped = StopUnattachedL());
		}
	iStep = EStepIdle;
	iLog.Log(stopped ? _L("Disconnected.") : _L("Not connected."));
	SetState(EConnIdle, _L("Not connected"));
	}

// --------------------------------------------------------------------------

void CGnuboxConnection::RunL()
	{
	TInt err = iStatus.Int();
	switch (iStep)
		{
	case EStepStarting:
		if (err != KErrNone)
			{
			Failed(err);
			return;
			}
		iRetries = 0;
		iUpSince.HomeTime();
		iLog.Log(_L("Connected. Checking Internet access..."));
		if (iResolver.Open(iServ, KAfInet, KProtocolInetUdp, iConn) == KErrNone)
			{
			iResolverOpen = ETrue;
			iStep = EStepResolving;
			iResolver.GetByName(KCheckHost, iNameEntry, iStatus);
			SetActive();
			}
		else
			iStep = EStepUp;
		SetState(EConnUp, _L("Connected"));
		break;

	case EStepResolving:
		{
		if (err == KErrNone)
			{
			TInetAddr addr = TInetAddr::Cast(iNameEntry().iAddr);
			TBuf<40> a;
			addr.Output(a);
			LogF(iLog, _L("Internet works (example.com = %S)."), &a);
			iLog.Log(_L("Other apps can use the \"GnuBox BT\" access point now."));
			}
		else
			LogF(iLog, _L("Connected, but the name lookup failed: %S"), &ErrText(err));
		iResolver.Close();
		iResolverOpen = EFalse;
		iStep = EStepUp;
		break;
		}

	default:
		break;
		}
	}

TInt CGnuboxConnection::RunError(TInt aError)
	{
	Failed(aError);
	return KErrNone;
	}

void CGnuboxConnection::DoCancel()
	{
	switch (iStep)
		{
	case EStepStarting:
		if (iConnOpen)
			iConn.Stop();	// cancels Start
		break;
	case EStepResolving:
		iResolver.Cancel();
		break;
	default:
		break;
		}
	}

void CGnuboxConnection::Failed(TInt aErr)
	{
	TNifProgress last;
	TInt detail = KErrNone;
	if (iConnOpen && iConn.LastProgressError(last) == KErrNone && last.iError != KErrNone)
		{
		detail = last.iError;
		LogD(iLog, _L("last progress error: stage %d, %d"), last.iStage, last.iError);
		}
	LogF(iLog, _L("Could not connect: %S"), &ErrText(detail != KErrNone ? detail : aErr));
	CloseHandles();
	if (iAuto && !iUserStop)
		ScheduleRetry();
	else
		{
		iStep = EStepIdle;
		SetState(EConnIdle, _L("Not connected"));
		}
	}

void CGnuboxConnection::ScheduleRetry()
	{
	TInt secs = iRetries < KShortRetries ? KRetryShort : KRetryLong;
	iRetries++;
	iStep = EStepWait;
	TBuf<60> info;
	info.Format(_L("Reconnecting in %d s (attempt %d)"), secs, iRetries);
	iLog.Log(info);
	iRetry->Cancel();
	iRetry->After(secs * 1000000);
	SetState(EConnWaitRetry, info);
	}

void CGnuboxConnection::RetryNow()
	{
	if (iUserStop)
		return;
	TRAPD(err, StartAttemptL());
	if (err != KErrNone)
		{
		LogF(iLog, _L("Could not start: %S"), &ErrText(err));
		CloseHandles();
		ScheduleRetry();
		}
	}

// --------------------------------------------------------------------------
// progress: plain-language stage names; detect drops

static const TDesC& StageText(TInt aStage)
	{
	_LIT(KSelecting, "Choosing access point");
	_LIT(KSelected, "Access point chosen");
	_LIT(KFailure, "Connection failed");
	_LIT(KOpening, "Opening Bluetooth link");
	_LIT(KScript, "Dialling (login script)");
	_LIT(KScripted, "Login script done");
	_LIT(KSerialUp, "Bluetooth link open, starting PPP");
	_LIT(KLogin, "PPP login");
	_LIT(KAgentUp, "Link ready");
	_LIT(KPppUp, "PPP up - IP address received");
	_LIT(KClosing, "Hanging up");
	_LIT(KClosed, "Hung up");
	_LIT(KPppDown, "PPP link closed");
	_LIT(KIdle, "Not connected");
	_LIT(KBlocked, "Data temporarily blocked");
	_LIT(KOther, "");
	switch (aStage)
		{
	case KConnectionUninitialised: return KIdle;
	case KStartingSelection: return KSelecting;
	case KFinishedSelection: return KSelected;
	case KConnectionFailure: return KFailure;
	case KStartingConnection: return KOpening;
	case KCsdScanningScript: return KScript;
	case KCsdScannedScript: return KScripted;
	case KCsdFinishedConnect: return KSerialUp;
	case KCsdStartingLogIn: case KCsdFinishedLogIn: return KLogin;
	case KConnectionOpen: return KAgentUp;
	case KLinkLayerOpen: return KPppUp;
	case KDataTransferTemporarilyBlocked: return KBlocked;
	case KConnectionStartingClose: return KClosing;
	case KConnectionClosed: return KClosed;
	case KLinkLayerClosed: return KPppDown;
	default: return KOther;
		}
	}

void CGnuboxConnection::ProgressL(const TNifProgress& aProgress)
	{
	const TDesC& text = StageText(aProgress.iStage);
	if (aProgress.iError != KErrNone)
		LogF(iLog, _L("  %S: %S  [stage %d]"), &text, &ErrText(aProgress.iError), aProgress.iStage);
	else if (text.Length())
		LogF(iLog, _L("  %S  [stage %d]"), &text, aProgress.iStage);
	else
		LogD(iLog, _L("  stage %d"), aProgress.iStage);

	TBool down = aProgress.iStage == KLinkLayerClosed || aProgress.iStage == KConnectionClosed ||
		aProgress.iStage == KConnectionUninitialised;
	if (down && !iUserStop && (iStep == EStepUp || iStep == EStepResolving))
		{
		// handle it outside this callback: it deletes the progress watcher
		if (iDropError == KErrNone)
			iDropError = aProgress.iError;
		iDropCallBack->CallBack();
		}
	}

TInt CGnuboxConnection::DropCallBack(TAny* aSelf)
	{
	((CGnuboxConnection*)aSelf)->HandleDrop();
	return KErrNone;
	}

void CGnuboxConnection::HandleDrop()
	{
	if (iUserStop || (iStep != EStepUp && iStep != EStepResolving))
		return;
	Cancel();
	CloseHandles();
	if (iDropError != KErrNone)
		LogF(iLog, _L("Connection lost: %S"), &ErrText(iDropError));
	else
		iLog.Log(_L("Connection lost."));
	iDropError = KErrNone;
	if (iAuto)
		{
		iRetries = 0;
		ScheduleRetry();
		}
	else
		{
		iStep = EStepIdle;
		SetState(EConnIdle, _L("Not connected"));
		}
	}

// --------------------------------------------------------------------------

CGnuboxProgress::CGnuboxProgress(RConnection& aConn, CGnuboxConnection& aOwner)
	: CActive(EPriorityStandard + 1), iConn(aConn), iOwner(aOwner)
	{
	CActiveScheduler::Add(this);
	}

CGnuboxProgress::~CGnuboxProgress()
	{
	Cancel();
	}

void CGnuboxProgress::Start()
	{
	iConn.ProgressNotification(iBuf, iStatus);
	SetActive();
	}

void CGnuboxProgress::RunL()
	{
	if (iStatus.Int() != KErrNone)
		return;
	TNifProgress p = iBuf();
	Start();	// re-arm before reporting
	iOwner.ProgressL(p);
	}

void CGnuboxProgress::DoCancel()
	{
	iConn.CancelProgressNotification();
	}
