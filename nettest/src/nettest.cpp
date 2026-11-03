// nettest.cpp - NetTest for Series 80: how fast files of the Maps 3D view's sizes arrive from the
// PC over the current connection, for each number of parallel connections and TCP receive window.
// See nettest.h. GPL v2 or later.

#include <eikenv.h>
#include <eikon.hrh>
#include <uikon.hrh>
#include <eiklabel.h>
#include <eiktxlbx.h>
#include <eiktxlbm.h>
#include <coemain.h>
#include <f32file.h>
#include <nettest.rsg>
#include "nettest.hrh"
#include "nettest.h"

const TUint32 KNtServer = INET_ADDR(192, 168, 137, 1);
const TInt KNtPort = 8000;
const TInt KNtMaxErrors = 20;			// per run, then the run is given up
const TInt KNtTimeoutSec = 30;			// no data for this long: reconnect
const TInt KMaxLogLines = 400;
const TInt KWrapAt = 78;

struct TNtSetting { TInt iParallel; TInt iWindow; TInt iNewConn; };
// the big-file test (the sizes of a Maps 3D download): window first with 2 connections
// (Maps' default), then 3 and 1 connections; connections kept open
static const TNtSetting KNtBig[8] =
	{
	{ 2, 8192, 0 }, { 2, 16384, 0 }, { 2, 32768, 0 }, { 2, 65535, 0 },
	{ 3, 16384, 0 }, { 3, 32768, 0 }, { 1, 16384, 0 }, { 1, 65535, 0 }
	};
// the small-file test (2-50 KB): connections kept open or a new one for every file
static const TNtSetting KNtSmall[5] =
	{
	{ 1, 16384, 0 }, { 1, 16384, 1 }, { 2, 16384, 0 }, { 2, 16384, 1 }, { 3, 16384, 0 }
	};
static const TInt KNtSizeKb[5] = { 2, 5, 10, 20, 50 };

_LIT8(KNtNew8, " new");
_LIT(KNtNewLong, ", a new connection for every file");
_LIT(KNtNewShort, " (new per file)");
_LIT(KNtResultsDir, "C:\\Data\\NetTest\\");
_LIT(KNtResultsFile, "C:\\Data\\NetTest\\results.txt");

static TInt MsBetween(const TTime& aFrom, const TTime& aTo)
	{
	TTimeIntervalMicroSeconds d = aTo.MicroSecondsFrom(aFrom);
	TInt64 v = d.Int64();
	v /= TInt64(1000);
	return v.GetTInt();
	}

// tenths of kB/s
static TInt Speed10(TInt aBytes, TInt aMs)
	{
	if (aMs <= 0)
		return 0;
	TInt64 v = TInt64(aBytes);
	v *= TInt64(10000);
	v /= TInt64(1024);
	v /= TInt64(aMs);
	return v.GetTInt();
	}

// ==========================================================================
// one connection

CNtConn::CNtConn(CNtEngine& aEngine, TInt aId)
	: CActive(EPriorityStandard), iEngine(aEngine), iId(aId), iState(EIdle), iFile(-1)
	{
	CActiveScheduler::Add(this);
	}

CNtConn::~CNtConn()
	{
	Stop();
	}

void CNtConn::Start(TInt aWindow, TBool aNewConn)
	{
	iWindow = aWindow;
	iNewConn = aNewConn;
	iFile = -1;
	TInt err = iSock.Open(iEngine.SocketServ(), KAfInet, KSockStream, KProtocolInetTcp);
	if (err != KErrNone)
		{
		Fail(err);
		return;
		}
	iOpen = ETrue;
	// the receive window must be set before Connect
	iSock.SetOpt(KSoTcpRecvWinSize, KSolInetTcp, aWindow);
	iSock.SetOpt(KSoTcpSendWinSize, KSolInetTcp, 16384);
	iAddr.SetAddress(KNtServer);
	iAddr.SetPort(KNtPort);
	iConnectStart.HomeTime();
	iLastActivity = iConnectStart;
	iState = EConnecting;
	iSock.Connect(iAddr, iStatus);
	SetActive();
	}

void CNtConn::Stop()
	{
	Cancel();
	if (iOpen)
		{
		iSock.Close();
		iOpen = EFalse;
		}
	iState = EIdle;
	iFile = -1;
	}

void CNtConn::Timeout()
	{
	Cancel();
	Fail(KErrTimedOut);
	}

void CNtConn::DoCancel()
	{
	switch (iState)
		{
	case EConnecting: iSock.CancelConnect(); break;
	case ESending: iSock.CancelWrite(); break;
	case EReading: iSock.CancelRecv(); break;
	default: break;
		}
	}

TInt CNtConn::RunError(TInt aError)
	{
	Fail(aError);
	return KErrNone;
	}

void CNtConn::Fail(TInt aError)
	{
	TInt file = iFile;
	if (iOpen)
		{
		iSock.Close();
		iOpen = EFalse;
		}
	iState = EIdle;
	iFile = -1;
	iEngine.FileFailed(file, iId, aError);
	if (iEngine.ShouldReconnect())
		Start(iWindow, iNewConn);
	else
		iEngine.ConnIdle();
	}

void CNtConn::NextFile()
	{
	iFile = iEngine.NextFileIndex();
	if (iFile < 0)
		{
		// nothing left for this connection
		if (iOpen)
			{
			iSock.Close();
			iOpen = EFalse;
			}
		iState = EIdle;
		iEngine.ConnIdle();
		return;
		}
	const TNtRun& run = iEngine.Run();
	iReq.Format(_L8("GET /nettest/%c%03d.bin HTTP/1.1\r\nHost: 192.168.137.1:8000\r\n"
		"X-NetTest-Run: %d P%d W%d%S\r\nConnection: keep-alive\r\n\r\n"),
		iEngine.Small() ? 's' : 'f', iFile, iEngine.CurrentRun() + 1, run.iParallel, run.iWindow,
		iNewConn ? &(const TDesC8&)KNtNew8 : &(const TDesC8&)KNullDesC8);
	iHead.Zero();
	iInBody = EFalse;
	iBodyLeft = 0;
	iFileBytes = 0;
	iGotFirst = EFalse;
	iSent.HomeTime();
	iLastActivity = iSent;
	iState = ESending;
	iSock.Write(iReq, iStatus);
	SetActive();
	}

void CNtConn::Read()
	{
	iBuf.Zero();
	iState = EReading;
	iSock.RecvOneOrMore(iBuf, 0, iStatus, iLen);
	SetActive();
	}

void CNtConn::RunL()
	{
	TInt err = iStatus.Int();
	iLastActivity.HomeTime();
	switch (iState)
		{
	case EConnecting:
		if (err != KErrNone)
			{
			Fail(err);
			return;
			}
		iEngine.Connected(MsBetween(iConnectStart, iLastActivity));
		NextFile();
		break;
	case ESending:
		if (err != KErrNone)
			{
			Fail(err);
			return;
			}
		Read();
		break;
	case EReading:
		if (err != KErrNone)
			{
			Fail(err == KErrNone ? KErrEof : err);
			return;
			}
		HandleData(iBuf);
		break;
	default:
		break;
		}
	}

void CNtConn::HandleData(const TDesC8& aData)
	{
	if (!iGotFirst)
		{
		iFirst = iLastActivity;
		iGotFirst = ETrue;
		}
	iFileBytes += aData.Length();
	if (!iInBody)
		{
		TInt before = iHead.Length();
		TInt room = iHead.MaxLength() - before;
		iHead.Append(aData.Left(room < aData.Length() ? room : aData.Length()));
		TInt end = iHead.Find(_L8("\r\n\r\n"));
		if (end < 0)
			{
			if (iHead.Length() >= iHead.MaxLength())
				Fail(KErrCorrupt);
			else
				Read();
			return;
			}
		if (iHead.Length() < 12 || iHead.Mid(9, 3) != _L8("200"))
			{
			Fail(KErrNotFound);
			return;
			}
		TBuf8<1024> low;
		low.Copy(iHead.Left(end));
		low.LowerCase();
		TInt cl = low.Find(_L8("content-length:"));
		if (cl < 0)
			{
			Fail(KErrNotSupported);
			return;
			}
		TLex8 lex(low.Mid(cl + 15));
		lex.SkipSpace();
		TInt length = 0;
		if (lex.Val(length) != KErrNone)
			{
			Fail(KErrCorrupt);
			return;
			}
		TInt bodyHere = before + aData.Length() - (end + 4);
		iBodyLeft = length - bodyHere;
		iInBody = ETrue;
		}
	else
		iBodyLeft -= aData.Length();

	if (iBodyLeft > 0)
		{
		Read();
		return;
		}
	TTime now;
	now.HomeTime();
	// a file's time: from the request, or with a new connection per file from its connect
	iEngine.FileDone(iFile, iFileBytes, MsBetween(iNewConn ? iConnectStart : iSent, now), MsBetween(iSent, iFirst));
	iFile = -1;
	if (iNewConn)
		{
		iSock.Close();
		iOpen = EFalse;
		iState = EIdle;
		if (iEngine.HasMoreFiles())
			Start(iWindow, ETrue);
		else
			iEngine.ConnIdle();
		return;
		}
	NextFile();
	}

// ==========================================================================
// posting the results to the PC

CNtPoster::CNtPoster(RSocketServ& aSs, MNtObserver& aObs)
	: CActive(EPriorityStandard), iSs(aSs), iObs(aObs), iState(EIdle)
	{
	CActiveScheduler::Add(this);
	}

CNtPoster::~CNtPoster()
	{
	Cancel();
	if (iOpen)
		iSock.Close();
	delete iData;
	}

void CNtPoster::PostL(const TDesC8& aBody)
	{
	if (IsActive())
		return;
	delete iData;
	iData = NULL;
	iData = HBufC8::NewL(aBody.Length() + 300);
	TPtr8 p = iData->Des();
	p.Format(_L8("POST /results?name=nettest HTTP/1.1\r\nHost: 192.168.137.1:8000\r\n"
		"Content-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"), aBody.Length());
	p.Append(aBody);
	User::LeaveIfError(iSock.Open(iSs, KAfInet, KSockStream, KProtocolInetTcp));
	iOpen = ETrue;
	iAddr.SetAddress(KNtServer);
	iAddr.SetPort(KNtPort);
	iState = EConnecting;
	iSock.Connect(iAddr, iStatus);
	SetActive();
	}

void CNtPoster::DoCancel()
	{
	switch (iState)
		{
	case EConnecting: iSock.CancelConnect(); break;
	case ESending: iSock.CancelWrite(); break;
	case EReading: iSock.CancelRecv(); break;
	default: break;
		}
	}

TInt CNtPoster::RunError(TInt aError)
	{
	Done(aError);
	return KErrNone;
	}

void CNtPoster::Done(TInt aError)
	{
	if (iOpen)
		{
		iSock.Close();
		iOpen = EFalse;
		}
	iState = EIdle;
	TBuf<80> t;
	if (aError == KErrNone && iAnswer.Find(_L8(" 200 ")) >= 0)
		t.Copy(_L("Results sent to the PC (localserver\\uploads\\..._nettest.txt)."));
	else
		t.Format(_L("Sending the results to the PC failed (%d)."), aError);
	iObs.NtLog(t);
	}

void CNtPoster::RunL()
	{
	TInt err = iStatus.Int();
	if (err != KErrNone && !(iState == EReading && err == KErrEof))
		{
		Done(err);
		return;
		}
	switch (iState)
		{
	case EConnecting:
		iState = ESending;
		iSock.Write(*iData, iStatus);
		SetActive();
		break;
	case ESending:
		iAnswer.Zero();
		iState = EReading;
		iSock.RecvOneOrMore(iAnswer, 0, iStatus, iLen);
		SetActive();
		break;
	default:
		Done(KErrNone);
		break;
		}
	}

// ==========================================================================
// the upload test: does a big POST stall on a kept-open connection? (Maps 9300 saw the second
// multi-KB POST on a reused connection send one 16 KB send window and then nothing, over GnuBox)

const TInt KNtUpSize = 40960;
const TInt KNtUpCount = 4;
const TInt KNtUpTimeoutSec = 30;
_LIT8(KNtKept8, "kept");
_LIT8(KNtFresh8, "fresh");
_LIT(KNtFreshW, "fresh");
_LIT(KNtKeptW, "kept-open");
_LIT(KNtAFresh, "a fresh");
_LIT(KNtTheKept, "the kept-open");
_LIT(KNtPlusConnect, " (+ connect)");

CNtUpload* CNtUpload::NewL(RSocketServ& aSs, MNtObserver& aObs)
	{
	CNtUpload* self = new (ELeave) CNtUpload(aSs, aObs);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

CNtUpload::CNtUpload(RSocketServ& aSs, MNtObserver& aObs)
	: CActive(EPriorityStandard), iSs(aSs), iObs(aObs), iState(EIdle)
	{
	CActiveScheduler::Add(this);
	}

void CNtUpload::ConstructL()
	{
	iTicker = CPeriodic::NewL(CActive::EPriorityLow);
	}

CNtUpload::~CNtUpload()
	{
	Cancel();
	CloseSock();
	if (iTicker)
		iTicker->Cancel();
	delete iTicker;
	delete iPost;
	}

void CNtUpload::CloseSock()
	{
	if (iOpen)
		{
		iSock.Close();
		iOpen = EFalse;
		}
	}

void CNtUpload::StartL()
	{
	if (iRunning)
		return;
	if (!iPost)
		iPost = HBufC8::NewL(KNtUpSize + 400);
	iResults.Copy(_L8("NetTest 1.3 upload test: POSTs of 40 KB, 4 on one kept-open connection, then 4 each on a fresh connection\r\n"
		"connection; POST; result; ms; kB/s; connect ms\r\n"));
	iRunning = ETrue;
	iMode = 0;
	iIndex = 0;
	iObs.NtLog(_L("Upload test: 4 POSTs of 40 KB on one kept-open connection, then 4 each on a fresh connection."));
	iTicker->Cancel();
	iTicker->Start(1000000, 1000000, TCallBack(TickCb, this));
	Connect();
	}

void CNtUpload::Stop()
	{
	if (!iRunning)
		return;
	Cancel();
	CloseSock();
	iTicker->Cancel();
	iRunning = EFalse;
	iState = EIdle;
	iObs.NtLog(_L("Upload test stopped."));
	}

void CNtUpload::Connect()
	{
	CloseSock();
	iConnectMs = 0;
	TInt err = iSock.Open(iSs, KAfInet, KSockStream, KProtocolInetTcp);
	if (err != KErrNone)
		{
		PostDone(err);
		return;
		}
	iOpen = ETrue;
	iSock.SetOpt(KSoTcpRecvWinSize, KSolInetTcp, 16384);
	iSock.SetOpt(KSoTcpSendWinSize, KSolInetTcp, 16384);		// as Maps 9300
	iAddr.SetAddress(KNtServer);
	iAddr.SetPort(KNtPort);
	iPostStart.HomeTime();
	iLastActivity = iPostStart;
	iState = EConnecting;
	iSock.Connect(iAddr, iStatus);
	SetActive();
	}

void CNtUpload::SendPost()
	{
	TPtr8 p = iPost->Des();
	p.Format(_L8("POST /results?name=nettest_upload&ext=bin HTTP/1.1\r\nHost: 192.168.137.1:8000\r\n"
		"X-NetTest-Run: upload %S %d\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n"),
		iMode ? &(const TDesC8&)KNtFresh8 : &(const TDesC8&)KNtKept8, iIndex + 1, KNtUpSize);
	TInt start = p.Length();
	p.SetLength(start + KNtUpSize);
	TUint8* d = (TUint8*)p.Ptr() + start;
	for (TInt i = 0; i < KNtUpSize; i++)
		d[i] = (TUint8)(i * 7 + iIndex);
	iAnswer.Zero();
	iPostStart.HomeTime();
	iLastActivity = iPostStart;
	TBuf<100> t;
	t.Format(_L("Upload test: %S connection, POST %d of %d"), iMode ? &(const TDesC&)KNtAFresh : &(const TDesC&)KNtTheKept, iIndex + 1, KNtUpCount);
	iObs.NtStatus(t, KNullDesC);
	iState = ESending;
	iSock.Write(*iPost, iStatus);
	SetActive();
	}

void CNtUpload::DoCancel()
	{
	switch (iState)
		{
	case EConnecting: iSock.CancelConnect(); break;
	case ESending: iSock.CancelWrite(); break;
	case EReading: iSock.CancelRecv(); break;
	default: break;
		}
	}

TInt CNtUpload::RunError(TInt aError)
	{
	PostDone(aError);
	return KErrNone;
	}

void CNtUpload::RunL()
	{
	TInt err = iStatus.Int();
	iLastActivity.HomeTime();
	switch (iState)
		{
	case EConnecting:
		if (err != KErrNone)
			{
			PostDone(err);
			return;
			}
		iConnectMs = MsBetween(iPostStart, iLastActivity);
		SendPost();
		break;
	case ESending:
		if (err != KErrNone)
			{
			PostDone(err);
			return;
			}
		iChunk.Zero();
		iState = EReading;
		iSock.RecvOneOrMore(iChunk, 0, iStatus, iLen);
		SetActive();
		break;
	case EReading:
		{
		if (err != KErrNone)
			{
			PostDone(err);
			return;
			}
		TInt room = iAnswer.MaxLength() - iAnswer.Length();
		iAnswer.Append(iChunk.Left(room < iChunk.Length() ? room : iChunk.Length()));
		TInt end = iAnswer.Find(_L8("\r\n\r\n"));
		if (end >= 0 && iAnswer.Length() >= 12)
			{
			if (iAnswer.Mid(9, 3) != _L8("200"))
				{
				PostDone(KErrGeneral);
				return;
				}
			if (iAnswer.Find(_L8("saved")) >= 0)
				{
				PostDone(KErrNone);
				return;
				}
			}
		iChunk.Zero();
		iSock.RecvOneOrMore(iChunk, 0, iStatus, iLen);
		SetActive();
		break;
		}
	default:
		break;
		}
	}

void CNtUpload::PostDone(TInt aError)
	{
	TTime now;
	now.HomeTime();
	TInt ms = MsBetween(iPostStart, now);
	TInt s = aError == KErrNone ? Speed10(KNtUpSize, ms) : 0;
	TBuf<120> t;
	if (aError == KErrNone)
		t.Format(_L("  POST %d (%S connection): 40 KB in %d ms = %d.%d kB/s%S"), iIndex + 1,
			iMode ? &(const TDesC&)KNtFreshW : &(const TDesC&)KNtKeptW, ms, s / 10, s % 10, iConnectMs ? &(const TDesC&)KNtPlusConnect : &(const TDesC&)KNullDesC);
	else if (aError == KErrTimedOut)
		t.Format(_L("  POST %d (%S connection): STALLED - nothing for %d s"), iIndex + 1,
			iMode ? &(const TDesC&)KNtFreshW : &(const TDesC&)KNtKeptW, KNtUpTimeoutSec);
	else
		t.Format(_L("  POST %d (%S connection): error %d after %d ms"), iIndex + 1,
			iMode ? &(const TDesC&)KNtFreshW : &(const TDesC&)KNtKeptW, aError, ms);
	iObs.NtLog(t);
	TBuf8<100> line;
	line.Format(_L8("%S; %d; %d; %d; %d.%d; %d\r\n"), iMode ? &(const TDesC8&)KNtFresh8 : &(const TDesC8&)KNtKept8,
		iIndex + 1, aError, ms, s / 10, s % 10, iConnectMs);
	if (iResults.Length() + line.Length() <= iResults.MaxLength())
		iResults.Append(line);
	iConnectMs = 0;
	if (aError != KErrNone)
		CloseSock();
	iState = EIdle;
	iIndex++;
	if (iIndex >= KNtUpCount)
		{
		if (iMode == 0)
			{
			iMode = 1;
			iIndex = 0;
			CloseSock();
			iObs.NtLog(_L("Now 4 POSTs of 40 KB, each on a fresh connection."));
			Connect();
			return;
			}
		Finish();
		return;
		}
	if (iMode == 1 || !iOpen)
		Connect();
	else
		SendPost();
	}

void CNtUpload::Finish()
	{
	iTicker->Cancel();
	CloseSock();
	iRunning = EFalse;
	iState = EIdle;
	iObs.NtLog(_L("Upload test finished."));
	iObs.NtStatus(_L("Upload test finished"), _L("Results: on screen, C:\\Data\\NetTest\\results.txt and on the PC"));
	if (iEngine)
		{
		TRAPD(err, iEngine->PostTextL(iResults));
		}
	iObs.NtFinished();
	}

TInt CNtUpload::TickCb(TAny* aSelf)
	{
	CNtUpload* self = static_cast<CNtUpload*>(aSelf);
	if (!self->iRunning || self->iState == EIdle)
		return 1;
	TTime now;
	now.HomeTime();
	if (MsBetween(self->iLastActivity, now) > KNtUpTimeoutSec * 1000)
		{
		self->Cancel();
		self->PostDone(KErrTimedOut);
		}
	return 1;
	}

// ==========================================================================
// the engine: runs the test list

CNtEngine* CNtEngine::NewL(MNtObserver& aObs)
	{
	CNtEngine* self = new (ELeave) CNtEngine(aObs);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

CNtEngine::CNtEngine(MNtObserver& aObs) : iObs(aObs)
	{
	}

void CNtEngine::ConstructL()
	{
	User::LeaveIfError(iSs.Connect());
	for (TInt i = 0; i < KNtMaxParallel; i++)
		iConns[i] = new (ELeave) CNtConn(*this, i);
	iPoster = new (ELeave) CNtPoster(iSs, iObs);
	iTicker = CPeriodic::NewL(CActive::EPriorityLow);
	iPause = CPeriodic::NewL(CActive::EPriorityLow);
	iRunCount = 8;
	}

CNtEngine::~CNtEngine()
	{
	if (iTicker)
		iTicker->Cancel();
	delete iTicker;
	if (iPause)
		iPause->Cancel();
	delete iPause;
	for (TInt i = 0; i < KNtMaxParallel; i++)
		delete iConns[i];
	delete iPoster;
	iRetry.Close();
	iFileMs.Close();
	iFirstMs.Close();
	for (TInt k = 0; k < 5; k++)
		iSizeMs[k].Close();
	iSs.Close();
	}

void CNtEngine::StartL(TInt aFiles, TBool aSmall)
	{
	if (iRunning)
		return;
	iFiles = aFiles;
	iSmall = aSmall;
	iRunCount = aSmall ? 5 : 8;
	for (TInt i = 0; i < iRunCount; i++)
		{
		TNtRun& r = iRuns[i];
		const TNtSetting& set = aSmall ? KNtSmall[i] : KNtBig[i];
		r.iParallel = set.iParallel;
		r.iWindow = set.iWindow;
		r.iNewConn = set.iNewConn;
		for (TInt k = 0; k < 5; k++)
			r.iSizeMs[k] = 0;
		r.iFilesDone = r.iBytes = r.iErrors = r.iMs = 0;
		r.iFileMsMedian = r.iFirstMsMedian = r.iConnectMs = 0;
		r.iDone = EFalse;
		}
	iRun = 0;
	iRunning = ETrue;
	iTestStart.HomeTime();
	TBuf<120> t;
	if (aSmall)
		t.Format(_L("Small-file test started: %d runs, %d files each (2, 5, 10, 20, 50 KB; /nettest/s000.bin...)."), iRunCount, iFiles);
	else
		t.Format(_L("Test started: %d runs, %d files each (/nettest/f000.bin...)."), iRunCount, iFiles);
	Log(t);
	iTicker->Cancel();
	iTicker->Start(1000000, 1000000, TCallBack(TickCb, this));
	StartRun();
	}

void CNtEngine::StartRun()
	{
	TNtRun& r = iRuns[iRun];
	iNext = 0;
	iRetry.Reset();
	iFileMs.Reset();
	iFirstMs.Reset();
	for (TInt k = 0; k < 5; k++)
		iSizeMs[k].Reset();
	iRunStart.HomeTime();
	iTickBytes = 0;
	iTickTime = iRunStart;
	iKBs10 = 0;
	TBuf<100> t;
	t.Format(_L("Run %d/%d: %d connection(s), TCP window %d B%S"), iRun + 1, iRunCount, r.iParallel, r.iWindow,
		r.iNewConn ? &(const TDesC&)KNtNewLong : &(const TDesC&)KNullDesC);
	Log(t);
	for (TInt i = 0; i < r.iParallel; i++)
		iConns[i]->Start(r.iWindow, r.iNewConn);
	}

TInt CNtEngine::NextFileIndex()
	{
	if (!iRunning)
		return -1;
	if (iRetry.Count())
		{
		TInt f = iRetry[0];
		iRetry.Remove(0);
		return f;
		}
	if (iNext < iFiles)
		return iNext++;
	return -1;
	}

void CNtEngine::Connected(TInt aMs)
	{
	if (aMs > iRuns[iRun].iConnectMs)
		iRuns[iRun].iConnectMs = aMs;
	}

void CNtEngine::FileDone(TInt aIndex, TInt aBytes, TInt aMs, TInt aFirstMs)
	{
	TNtRun& r = iRuns[iRun];
	r.iFilesDone++;
	r.iBytes += aBytes;
	iFileMs.Append(aMs);
	iFirstMs.Append(aFirstMs);
	if (iSmall && aIndex >= 0)
		iSizeMs[aIndex % 5].Append(aMs);
	}

void CNtEngine::FileFailed(TInt aIndex, TInt aConnId, TInt aError)
	{
	if (!iRunning)
		return;
	TNtRun& r = iRuns[iRun];
	r.iErrors++;
	if (aIndex >= 0)
		iRetry.Append(aIndex);
	TBuf<100> t;
	t.Format(_L("  connection %d: error %d (file %d), retrying"), aConnId + 1, aError, aIndex);
	Log(t);
	}

TBool CNtEngine::ShouldReconnect() const
	{
	return iRunning && iRuns[iRun].iErrors < KNtMaxErrors;
	}

void CNtEngine::ConnIdle()
	{
	CheckRunDone();
	}

void CNtEngine::CheckRunDone()
	{
	if (!iRunning)
		return;
	for (TInt i = 0; i < KNtMaxParallel; i++)
		if (iConns[i]->Busy())
			return;
	FinishRun();
	}

TInt CNtEngine::Median(RArray<TInt>& aValues)
	{
	TInt n = aValues.Count();
	if (!n)
		return 0;
	for (TInt i = 1; i < n; i++)			// insertion sort: at most 118 values
		{
		TInt v = aValues[i];
		TInt j = i - 1;
		while (j >= 0 && aValues[j] > v)
			{
			aValues[j + 1] = aValues[j];
			j--;
			}
		aValues[j + 1] = v;
		}
	return aValues[n / 2];
	}

void CNtEngine::FinishRun()
	{
	TNtRun& r = iRuns[iRun];
	TTime now;
	now.HomeTime();
	r.iMs = MsBetween(iRunStart, now);
	r.iFileMsMedian = Median(iFileMs);
	r.iFirstMsMedian = Median(iFirstMs);
	r.iDone = ETrue;
	TInt s = Speed10(r.iBytes, r.iMs);
	TBuf<160> t;
	t.Format(_L("  %d files, %d KB in %d.%d s = %d.%d kB/s; per file %d ms (first byte %d ms); errors %d"),
		r.iFilesDone, r.iBytes / 1024, r.iMs / 1000, (r.iMs % 1000) / 100, s / 10, s % 10,
		r.iFileMsMedian, r.iFirstMsMedian, r.iErrors);
	Log(t);
	if (iSmall)
		{
		for (TInt k = 0; k < 5; k++)
			r.iSizeMs[k] = Median(iSizeMs[k]);
		t.Format(_L("  per file: 2 KB %d ms, 5 KB %d ms, 10 KB %d ms, 20 KB %d ms, 50 KB %d ms; connect %d ms"),
			r.iSizeMs[0], r.iSizeMs[1], r.iSizeMs[2], r.iSizeMs[3], r.iSizeMs[4], r.iConnectMs);
		Log(t);
		}
	iRun++;
	if (iRun < iRunCount)
		{
		iPause->Cancel();
		iPause->Start(3000000, 3000000, TCallBack(NextRunCb, this));	// let the link go quiet
		return;
		}
	// all done
	iRunning = EFalse;
	iTicker->Cancel();
	TInt total = MsBetween(iTestStart, now) / 1000;
	t.Format(_L("Test finished in %d min %d s."), total / 60, total % 60);
	Log(t);
	TRAPD(err, SendResultsL());
	iObs.NtStatus(_L("Test finished"), _L("Results: on screen, C:\\Data\\NetTest\\results.txt and on the PC"));
	iObs.NtFinished();
	}

TInt CNtEngine::NextRunCb(TAny* aSelf)
	{
	CNtEngine* self = static_cast<CNtEngine*>(aSelf);
	self->iPause->Cancel();
	if (self->iRunning)
		self->StartRun();
	return 0;
	}

void CNtEngine::Stop()
	{
	if (!iRunning)
		return;
	iRunning = EFalse;
	iTicker->Cancel();
	iPause->Cancel();
	for (TInt i = 0; i < KNtMaxParallel; i++)
		iConns[i]->Stop();
	Log(_L("Stopped."));
	iObs.NtStatus(_L("Stopped"), KNullDesC);
	iObs.NtFinished();
	}

TInt CNtEngine::TickCb(TAny* aSelf)
	{
	static_cast<CNtEngine*>(aSelf)->Tick();
	return 1;
	}

void CNtEngine::Tick()
	{
	if (!iRunning)
		return;
	TTime now;
	now.HomeTime();
	// a connection without data for too long: reconnect it
	for (TInt i = 0; i < KNtMaxParallel; i++)
		if (iConns[i]->Busy() && MsBetween(iConns[i]->iLastActivity, now) > KNtTimeoutSec * 1000)
			iConns[i]->Timeout();
	if (!iRunning || iPause->IsActive())
		return;
	const TNtRun& r = iRuns[iRun];
	TInt ms = MsBetween(iRunStart, now);
	TInt dms = MsBetween(iTickTime, now);
	if (dms >= 900)
		{
		iKBs10 = Speed10(r.iBytes - iTickBytes, dms);
		iTickBytes = r.iBytes;
		iTickTime = now;
		}
	TInt avg = Speed10(r.iBytes, ms);
	TBuf<120> l1, l2;
	l1.Format(_L("Run %d/%d: %d conn.%S, window %d KB - %d of %d files, %d KB"),
		iRun + 1, iRunCount, r.iParallel, r.iNewConn ? &(const TDesC&)KNtNewShort : &(const TDesC&)KNullDesC,
		(r.iWindow + 512) / 1024, r.iFilesDone, iFiles, r.iBytes / 1024);
	l2.Format(_L("%d.%d kB/s average, files finished in the last second: %d.%d kB/s, %d s, errors %d"),
		avg / 10, avg % 10, iKBs10 / 10, iKBs10 % 10, ms / 1000, r.iErrors);
	iObs.NtStatus(l1, l2);
	}

void CNtEngine::BuildResults(TDes8& aText, TBool /*aForPc*/)
	{
	TTime now;
	now.HomeTime();
	TBuf<40> stamp;
	TRAPD(ignore, iTestStart.FormatL(stamp, _L("%F%Y-%M-%D %H:%T:%S")));
	TBuf8<200> line;
	line.Copy(iSmall ? _L8("NetTest 1.3 small-file results, test started ") : _L8("NetTest 1.3 results, test started "));
	TBuf8<40> stamp8;
	stamp8.Copy(stamp);
	line.Append(stamp8);
	line.AppendFormat(_L8(", %d files per run from http://192.168.137.1:8000/nettest/\r\n"), iFiles);
	aText.Append(line);
	aText.Append(_L8("run; connections; TCP window B; files; KB; seconds; kB/s; file median ms; first byte median ms; slowest connect ms; errors"));
	aText.Append(iSmall ? _L8("; new connection per file; 2 KB ms; 5 KB ms; 10 KB ms; 20 KB ms; 50 KB ms\r\n") : _L8("\r\n"));
	for (TInt i = 0; i < iRunCount; i++)
		{
		const TNtRun& r = iRuns[i];
		if (!r.iDone)
			continue;
		TInt s = Speed10(r.iBytes, r.iMs);
		line.Format(_L8("%d; %d; %d; %d; %d; %d.%d; %d.%d; %d; %d; %d; %d\r\n"),
			i + 1, r.iParallel, r.iWindow, r.iFilesDone, r.iBytes / 1024, r.iMs / 1000, (r.iMs % 1000) / 100,
			s / 10, s % 10, r.iFileMsMedian, r.iFirstMsMedian, r.iConnectMs, r.iErrors);
		if (iSmall)
			{
			line.SetLength(line.Length() - 2);		// before the \r\n
			line.AppendFormat(_L8("; %d; %d; %d; %d; %d; %d\r\n"), r.iNewConn ? 1 : 0,
				r.iSizeMs[0], r.iSizeMs[1], r.iSizeMs[2], r.iSizeMs[3], r.iSizeMs[4]);
			}
		if (aText.Length() + line.Length() <= aText.MaxLength())
			aText.Append(line);
		}
	}

void CNtEngine::WriteResultsFile(const TDesC8& aText)
	{
	RFs& fs = CCoeEnv::Static()->FsSession();
	fs.MkDirAll(KNtResultsDir);
	RFile f;
	TInt err = f.Open(fs, KNtResultsFile, EFileWrite);
	if (err == KErrNotFound)
		err = f.Create(fs, KNtResultsFile, EFileWrite);
	if (err != KErrNone)
		return;
	TInt pos = 0;
	f.Seek(ESeekEnd, pos);
	f.Write(aText);
	f.Write(_L8("\r\n"));
	f.Close();
	}

void CNtEngine::PostTextL(const TDesC8& aText)
	{
	WriteResultsFile(aText);
	iPoster->PostL(aText);
	}

void CNtEngine::SendResultsL()
	{
	HBufC8* text = HBufC8::NewLC(4096);
	TPtr8 p = text->Des();
	BuildResults(p, ETrue);
	if (!iRunning)
		WriteResultsFile(p);
	iPoster->PostL(p);
	CleanupStack::PopAndDestroy(text);
	}

// ==========================================================================
// view: two status lines on top, a scrolling log below (as GnuBox)

CNtView* CNtView::NewL(const TRect& aRect)
	{
	CNtView* self = new (ELeave) CNtView;
	CleanupStack::PushL(self);
	self->ConstructL(aRect);
	CleanupStack::Pop(self);
	return self;
	}

void CNtView::ConstructL(const TRect& aRect)
	{
	CreateWindowL();
	iStatus1 = new (ELeave) CEikLabel;
	iStatus1->SetContainerWindowL(*this);
	iStatus1->SetTextL(_L("NetTest 1.3"));
	iStatus2 = new (ELeave) CEikLabel;
	iStatus2->SetContainerWindowL(*this);
	iStatus2->SetTextL(KNullDesC);
	iHeaderHeight = iStatus1->MinimumSize().iHeight * 2 + 8;
	iListBox = new (ELeave) CEikTextListBox;
	iListBox->SetContainerWindowL(*this);
	iListBox->ConstructL(this, 0);
	iListBox->CreateScrollBarFrameL(ETrue);
	iListBox->ScrollBarFrame()->SetScrollBarVisibilityL(
		CEikScrollBarFrame::EOff, CEikScrollBarFrame::EAuto);
	iLines = new (ELeave) CDesCArrayFlat(32);
	CTextListBoxModel* model = iListBox->Model();
	model->SetItemTextArray(iLines);
	model->SetOwnershipType(ELbmOwnsItemArray);
	SetRect(aRect);
	ActivateL();
	}

CNtView::~CNtView()
	{
	delete iStatus1;
	delete iStatus2;
	delete iListBox;
	}

void CNtView::SizeChanged()
	{
	TRect r = Rect();
	if (!iListBox)
		return;
	TInt h = iStatus1->MinimumSize().iHeight;
	iStatus1->SetExtent(TPoint(r.iTl.iX + 6, r.iTl.iY + 2), TSize(r.Width() - 12, h));
	iStatus2->SetExtent(TPoint(r.iTl.iX + 6, r.iTl.iY + 3 + h), TSize(r.Width() - 12, h));
	iListBox->SetRect(TRect(r.iTl.iX, r.iTl.iY + iHeaderHeight, r.iBr.iX, r.iBr.iY));
	}

void CNtView::Draw(const TRect& /*aRect*/) const
	{
	CWindowGc& gc = SystemGc();
	TRect r = Rect();
	gc.SetPenStyle(CGraphicsContext::ENullPen);
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.SetBrushColor(TRgb(232, 244, 232));
	gc.DrawRect(TRect(r.iTl, TSize(r.Width(), iHeaderHeight)));
	gc.SetBrushColor(KRgbWhite);
	gc.DrawRect(TRect(TPoint(r.iTl.iX, r.iTl.iY + iHeaderHeight), r.iBr));
	gc.SetPenStyle(CGraphicsContext::ESolidPen);
	gc.SetPenColor(TRgb(120, 160, 120));
	gc.DrawLine(TPoint(r.iTl.iX, r.iTl.iY + iHeaderHeight - 1),
		TPoint(r.iBr.iX, r.iTl.iY + iHeaderHeight - 1));
	}

TInt CNtView::CountComponentControls() const
	{
	return 3;
	}

CCoeControl* CNtView::ComponentControl(TInt aIndex) const
	{
	switch (aIndex)
		{
	case 0: return iStatus1;
	case 1: return iStatus2;
	default: return iListBox;
		}
	}

TKeyResponse CNtView::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
	{
	return iListBox->OfferKeyEventL(aKeyEvent, aType);
	}

void CNtView::SetStatusL(const TDesC& aLine1, const TDesC& aLine2)
	{
	iStatus1->SetTextL(aLine1);
	iStatus2->SetTextL(aLine2);
	// redraw only the header (the two status lines), not the log below: the test should
	// leave the CPU to the Bluetooth stack
	TRect r = Rect();
	Window().Invalidate(TRect(r.iTl, TSize(r.Width(), iHeaderHeight)));
	}

void CNtView::Log(const TDesC& aText)
	{
	TPtrC rest(aText);
	TBool first = ETrue;
	do
		{
		TInt take = rest.Length();
		if (take > KWrapAt)
			{
			take = KWrapAt;
			for (TInt i = KWrapAt - 1; i > KWrapAt * 2 / 3; i--)
				if (rest[i] == ' ')
					{
					take = i + 1;
					break;
					}
			}
		TBuf<KWrapAt + 4> line;
		if (!first)
			line.Append(_L("    "));
		line.Append(rest.Left(take));
		TRAPD(err,
			{
			if (iLines->Count() >= KMaxLogLines)
				{
				iLines->Delete(0);
				iListBox->HandleItemRemovalL();
				}
			iLines->AppendL(line);
			iListBox->HandleItemAdditionL();
			});
		rest.Set(rest.Mid(take));
		first = EFalse;
		}
	while (rest.Length());
	if (iLines->Count())
		{
		iListBox->SetCurrentItemIndex(iLines->Count() - 1);
		iListBox->DrawNow();
		}
	}

void CNtView::ClearL()
	{
	iLines->Reset();
	iListBox->HandleItemRemovalL();
	iListBox->Reset();
	DrawNow();
	}

// ==========================================================================
// app ui

void CNtAppUi::ConstructL()
	{
	BaseConstructL();
	iView = CNtView::NewL(ClientRect());
	AddToStackL(iView);
	iEngine = CNtEngine::NewL(*this);
	iUpload = CNtUpload::NewL(iEngine->SocketServ(), *this);
	iUpload->SetEngine(iEngine);
	iView->Log(_L("NetTest 1.3: downloads the PC's test files (the sizes of a Maps 3D download, 118 files, 2.5 MB) "
		"once for each of 8 settings: 2 connections with a 8 / 16 / 32 / 64 KB TCP window, 3 connections with "
		"16 / 32 KB, 1 connection with 16 / 64 KB."));
	iView->Log(_L("Before: connect GnuBox (or the USB cable), and run the PC's localserver (ota_server.js) "
		"with the files in localserver\\nettest. Then: Full test (~10-15 min over Bluetooth) or Quick test "
		"(40 files a run, ~4 min). Small files: 2-50 KB files, connections kept open or new for each file (~3 min). "
		"Upload test (menu): 8 POSTs of 40 KB, kept-open vs. fresh connections (~1 min)."));
	}

CNtAppUi::~CNtAppUi()
	{
	delete iUpload;
	delete iEngine;
	if (iView)
		{
		RemoveFromStack(iView);
		delete iView;
		}
	}

void CNtAppUi::NtLog(const TDesC& aText)
	{
	iView->Log(aText);
	}

void CNtAppUi::NtStatus(const TDesC& aLine1, const TDesC& aLine2)
	{
	TRAPD(err, iView->SetStatusL(aLine1, aLine2));
	}

void CNtAppUi::NtFinished()
	{
	}

void CNtAppUi::HandleCommandL(TInt aCommand)
	{
	switch (aCommand)
		{
	case ENtCmdFull:
		if (!iUpload->Running())
			iEngine->StartL(KNtMaxFiles, EFalse);
		break;
	case ENtCmdQuick:
		if (!iUpload->Running())
			iEngine->StartL(40, EFalse);
		break;
	case ENtCmdSmall:
		if (!iUpload->Running())
			iEngine->StartL(KNtSmallFiles, ETrue);
		break;
	case ENtCmdUpload:
		if (!iEngine->Running())
			iUpload->StartL();
		break;
	case ENtCmdStop:
		iEngine->Stop();
		iUpload->Stop();
		break;
	case ENtCmdSend:
		iEngine->SendResultsL();
		break;
	case ENtCmdClear:
		iView->ClearL();
		break;
	case EEikCmdExit:
		iUpload->Stop();
		iEngine->Stop();
		Exit();
		break;
	default:
		break;
		}
	}

// ==========================================================================
// document, application, entry points

CEikAppUi* CNtDocument::CreateAppUiL()
	{
	return new (ELeave) CNtAppUi;
	}

CApaDocument* CNtApplication::CreateDocumentL()
	{
	return new (ELeave) CNtDocument(*this);
	}

TUid CNtApplication::AppDllUid() const
	{
	return KUidNetTest;
	}

EXPORT_C CApaApplication* NewApplication()
	{
	return new CNtApplication;
	}

GLDEF_C TInt E32Dll(TDllReason)
	{
	return KErrNone;
	}
