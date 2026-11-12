// gnubtlink.h - GnuBox: tunes the Bluetooth link once the RFCOMM connection is up (EPL v1.0 like the
// rest of this folder, new code by the GnuBox project)
#ifndef GNUBTLINK_H
#define GNUBTLINK_H
#include "gnubtport.h"
#include <e32base.h>
#include <bt_sock.h>

class CBTPortProxy;

// Asks the link manager to allow every ACL packet type (DM1/DH1/DM3/DH3/DM5/DH5), so the radio
// can use 3- and 5-slot packets. Asynchronous (an Ioctl on the RFCOMM socket), so no ESOCK call
// blocks the comms thread; the result only goes to the log.
NONSHARABLE_CLASS(CGnuBtLinkTuner) : public CActive
	{
public:
	static CGnuBtLinkTuner* NewL(CBTPortProxy* aProxy);
	~CGnuBtLinkTuner();
	void Start();
	// GnuBox: start the locker again after aMicroSeconds (a delayed RFCOMM connect retry)
	void RetryConnectLater(TInt aMicroSeconds);
private:
	CGnuBtLinkTuner(CBTPortProxy* aProxy);
	void ConstructL();
	void RunL();
	void DoCancel();
	void Issue();
private:
	CBTPortProxy* iProxy;
	TLMPacketTypeBuf iPackets;
	TInt iStep;		// 0: link manager ioctl, 1: HCI ioctl, 2: done, 3: retry timer
	RTimer iTimer;
	};
#endif
