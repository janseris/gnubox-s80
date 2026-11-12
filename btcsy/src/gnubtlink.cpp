// gnubtlink.cpp - see gnubtlink.h
#include "btcomm.h"
#include "gnubtlink.h"

// all ACL data packet types (hcibase.h values)
const TUint16 KGnuBtAllAcl = 0x0008 | 0x0010 | 0x0400 | 0x0800 | 0x4000 | 0x8000;

CGnuBtLinkTuner* CGnuBtLinkTuner::NewL(CBTPortProxy* aProxy)
	{
	CGnuBtLinkTuner* self = new (ELeave) CGnuBtLinkTuner(aProxy);
	CActiveScheduler::Add(self);
	return self;
	}

CGnuBtLinkTuner::CGnuBtLinkTuner(CBTPortProxy* aProxy)
	: CActive(EPriorityStandard), iProxy(aProxy)
	{
	}

CGnuBtLinkTuner::~CGnuBtLinkTuner()
	{
	Cancel();
	}

void CGnuBtLinkTuner::Start()
	{
	if (IsActive())
		return;
	iStep = 0;
	Issue();
	}

void CGnuBtLinkTuner::Issue()
	{
	iPackets() = KGnuBtAllAcl;
	if (iStep == 0)
		iProxy->iSocket.Ioctl(KLMSetPacketTypeIoctl, iStatus, &iPackets, KSolBtLM);
	else
		iProxy->iSocket.Ioctl(KHCIChangePacketTypeIoctl, iStatus, &iPackets, KSolBtHCI);
	SetActive();
	}

void CGnuBtLinkTuner::RunL()
	{
	LOG2(_L("packet types (all DM/DH 1-5): %S ioctl %d"),
		iStep == 0 ? &_L("link manager") : &_L("HCI"), iStatus.Int());
	if (iStatus.Int() != KErrNone && iStep == 0)
		{
		iStep = 1;
		Issue();
		return;
		}
	iStep = 2;
	}

void CGnuBtLinkTuner::DoCancel()
	{
	iProxy->iSocket.CancelIoctl();
	}
