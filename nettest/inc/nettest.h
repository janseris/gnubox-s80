// nettest.h - NetTest for Series 80 (Nokia 9300 / 9300i / 9500), Symbian OS 7.0s
//
// Downloads the test files /nettest/f000.bin ... from the PC's localserver (ota_server.js,
// http://192.168.137.1:8000) over whatever connection is up (GnuBox Bluetooth or the USB
// cable), once for every combination of parallel connections and TCP receive window in the
// test list. Each request carries "X-NetTest-Run: <run> P<n> W<bytes>", so the PC side
// (GnuBox Share's requests log) can match its own per-second measurements to the runs.
// The results table is shown, written to C:\Data\NetTest\results.txt and POSTed to
// http://192.168.137.1:8000/results?name=nettest.
//
// GPL v2 or later.

#ifndef NETTEST_H
#define NETTEST_H

#include <eikapp.h>
#include <eikdoc.h>
#include <eikappui.h>
#include <coecntrl.h>
#include <es_sock.h>
#include <in_sock.h>
#include <badesca.h>

class CEikLabel;
class CEikTextListBox;

const TUid KUidNetTest = { 0x0A5D10A4 };
const TInt KNtMaxParallel = 3;
const TInt KNtMaxFiles = 118;

class MNtObserver
	{
public:
	virtual void NtLog(const TDesC& aText) = 0;
	virtual void NtStatus(const TDesC& aLine1, const TDesC& aLine2) = 0;
	virtual void NtFinished() = 0;
	};

// one run of the test: its settings and what was measured
class TNtRun
	{
public:
	TInt iParallel;
	TInt iWindow;
	TInt iFilesDone;
	TInt iBytes;
	TInt iErrors;
	TInt iMs;			// first connect started -> last file done
	TInt iFileMsMedian;	// request sent -> last byte, per file
	TInt iFirstMsMedian;	// request sent -> first byte, per file
	TInt iConnectMs;	// the slowest connection setup
	TBool iDone;
	};

class CNtEngine;

// one TCP connection: requests files one after another (HTTP/1.1 keep-alive)
class CNtConn : public CActive
	{
public:
	CNtConn(CNtEngine& aEngine, TInt aId);
	~CNtConn();
	void Start(TInt aWindow);
	void Stop();
	void Timeout();
	TBool Busy() const { return iState != EIdle; }
	TTime iLastActivity;
private:
	enum TState { EIdle, EConnecting, ESending, EReading };
	void RunL();
	void DoCancel();
	TInt RunError(TInt aError);
	void NextFile();
	void Read();
	void HandleData(const TDesC8& aData);
	void Fail(TInt aError);
private:
	CNtEngine& iEngine;
	TInt iId;
	TState iState;
	RSocket iSock;
	TBool iOpen;
	TInetAddr iAddr;
	TInt iWindow;
	TTime iConnectStart;
	TBuf8<16384> iBuf;
	TSockXfrLength iLen;
	TBuf8<300> iReq;
	TBuf8<1024> iHead;
	TInt iFile;			// -1: none
	TBool iInBody;
	TInt iBodyLeft;
	TInt iFileBytes;
	TTime iSent;
	TTime iFirst;
	TBool iGotFirst;
	};

// posts the results text to the PC
class CNtPoster : public CActive
	{
public:
	CNtPoster(RSocketServ& aSs, MNtObserver& aObs);
	~CNtPoster();
	void PostL(const TDesC8& aBody);
private:
	enum TState { EIdle, EConnecting, ESending, EReading };
	void RunL();
	void DoCancel();
	TInt RunError(TInt aError);
	void Done(TInt aError);
private:
	RSocketServ& iSs;
	MNtObserver& iObs;
	TState iState;
	RSocket iSock;
	TBool iOpen;
	TInetAddr iAddr;
	HBufC8* iData;
	TBuf8<256> iAnswer;
	TSockXfrLength iLen;
	};

class CNtEngine : public CBase
	{
public:
	static CNtEngine* NewL(MNtObserver& aObs);
	~CNtEngine();
	void StartL(TInt aFiles);
	void Stop();
	TBool Running() const { return iRunning; }
	void SendResultsL();
	// from the connections
	TInt NextFileIndex();
	void FileDone(TInt aIndex, TInt aBytes, TInt aMs, TInt aFirstMs);
	void FileFailed(TInt aIndex, TInt aConnId, TInt aError);
	void Connected(TInt aMs);
	void ConnIdle();
	TBool ShouldReconnect() const;
	TInt CurrentRun() const { return iRun; }
	const TNtRun& Run() const { return iRuns[iRun]; }
	RSocketServ& SocketServ() { return iSs; }
	TInt FilesTotal() const { return iFiles; }
	void Log(const TDesC& aText) { iObs.NtLog(aText); }
private:
	CNtEngine(MNtObserver& aObs);
	void ConstructL();
	void StartRun();
	void FinishRun();
	void CheckRunDone();
	static TInt TickCb(TAny* aSelf);
	void Tick();
	static TInt NextRunCb(TAny* aSelf);
	void BuildResults(TDes8& aText, TBool aForPc);
	void WriteResultsFile(const TDesC8& aText);
	static TInt Median(RArray<TInt>& aValues);
private:
	MNtObserver& iObs;
	RSocketServ iSs;
	CNtConn* iConns[KNtMaxParallel];
	CNtPoster* iPoster;
	CPeriodic* iTicker;
	CPeriodic* iPause;
	TNtRun iRuns[8];
	TInt iRunCount;
	TInt iRun;
	TInt iFiles;
	TInt iNext;
	RArray<TInt> iRetry;
	RArray<TInt> iFileMs;
	RArray<TInt> iFirstMs;
	TTime iRunStart;
	TTime iTestStart;
	TBool iRunning;
	TInt iTickBytes;
	TTime iTickTime;
	TInt iKBs10;		// speed of the last second, tenths of kB/s
	};

class CNtView : public CCoeControl
	{
public:
	static CNtView* NewL(const TRect& aRect);
	~CNtView();
	void Log(const TDesC& aText);
	void ClearL();
	void SetStatusL(const TDesC& aLine1, const TDesC& aLine2);
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
	TInt CountComponentControls() const;
	CCoeControl* ComponentControl(TInt aIndex) const;
private:
	void ConstructL(const TRect& aRect);
	void SizeChanged();
	void Draw(const TRect& aRect) const;
private:
	CEikLabel* iStatus1;
	CEikLabel* iStatus2;
	CEikTextListBox* iListBox;
	CDesCArrayFlat* iLines;
	TInt iHeaderHeight;
	};

class CNtAppUi : public CEikAppUi, public MNtObserver
	{
public:
	void ConstructL();
	~CNtAppUi();
	void HandleCommandL(TInt aCommand);
	// MNtObserver
	void NtLog(const TDesC& aText);
	void NtStatus(const TDesC& aLine1, const TDesC& aLine2);
	void NtFinished();
private:
	CNtView* iView;
	CNtEngine* iEngine;
	};

class CNtDocument : public CEikDocument
	{
public:
	CNtDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
private:
	CEikAppUi* CreateAppUiL();
	};

class CNtApplication : public CEikApplication
	{
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
	};

#endif
