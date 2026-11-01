// gnuboxui.cpp - screen, menus and commands of GnuBox for Series 80
//
// One button does the job: Connect (records are created/repaired, Bluetooth
// is pointed at the chosen device, the connection is started and kept up)
// and the same button then reads Disconnect.
//
// GPL v2 or later, see COPYING.txt

#include <eikenv.h>
#include <eikon.hrh>
#include <uikon.hrh>
#include <eiklabel.h>
#include <eiktxlbx.h>
#include <eiktxlbm.h>
#include <eikmenup.h>
#include <eikmenub.h>
#include <eikaufty.h>
#include <eikbtgpc.h>
#include <btdefcommport.h>
#include <gnubox.rsg>
#include "gnubox.hrh"
#include "gnubox.h"

const TInt KMaxLogLines = 300;
const TInt KWrapAt = 78;

// CBA positions (Series 80: four buttons on the right)
const TInt KCbaConnect = 0;

// --------------------------------------------------------------------------
// view: two status lines on top, a scrolling log below

CGnuboxView* CGnuboxView::NewL(const TRect& aRect)
	{
	CGnuboxView* self = new (ELeave) CGnuboxView;
	CleanupStack::PushL(self);
	self->ConstructL(aRect);
	CleanupStack::Pop(self);
	return self;
	}

void CGnuboxView::ConstructL(const TRect& aRect)
	{
	CreateWindowL();
	iStatus1 = new (ELeave) CEikLabel;
	iStatus1->SetContainerWindowL(*this);
	iStatus1->SetTextL(_L("Not connected"));
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

CGnuboxView::~CGnuboxView()
	{
	delete iStatus1;
	delete iStatus2;
	delete iListBox;
	}

void CGnuboxView::SizeChanged()
	{
	TRect r = Rect();
	if (!iListBox)
		return;
	TInt h = iStatus1->MinimumSize().iHeight;
	iStatus1->SetExtent(TPoint(r.iTl.iX + 6, r.iTl.iY + 2), TSize(r.Width() - 12, h));
	iStatus2->SetExtent(TPoint(r.iTl.iX + 6, r.iTl.iY + 3 + h), TSize(r.Width() - 12, h));
	iListBox->SetRect(TRect(r.iTl.iX, r.iTl.iY + iHeaderHeight, r.iBr.iX, r.iBr.iY));
	}

void CGnuboxView::Draw(const TRect& /*aRect*/) const
	{
	CWindowGc& gc = SystemGc();
	TRect r = Rect();
	gc.SetPenStyle(CGraphicsContext::ENullPen);
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.SetBrushColor(TRgb(226, 234, 248));	// pale blue header
	gc.DrawRect(TRect(r.iTl, TSize(r.Width(), iHeaderHeight)));
	gc.SetBrushColor(KRgbWhite);
	gc.DrawRect(TRect(TPoint(r.iTl.iX, r.iTl.iY + iHeaderHeight), r.iBr));
	gc.SetPenStyle(CGraphicsContext::ESolidPen);
	gc.SetPenColor(TRgb(120, 140, 180));
	gc.DrawLine(TPoint(r.iTl.iX, r.iTl.iY + iHeaderHeight - 1),
		TPoint(r.iBr.iX, r.iTl.iY + iHeaderHeight - 1));
	}

TInt CGnuboxView::CountComponentControls() const
	{
	return 3;
	}

CCoeControl* CGnuboxView::ComponentControl(TInt aIndex) const
	{
	switch (aIndex)
		{
	case 0: return iStatus1;
	case 1: return iStatus2;
	default: return iListBox;
		}
	}

TKeyResponse CGnuboxView::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
	{
	return iListBox->OfferKeyEventL(aKeyEvent, aType);
	}

void CGnuboxView::SetStatusL(const TDesC& aLine1, const TDesC& aLine2)
	{
	iStatus1->SetTextL(aLine1);
	iStatus2->SetTextL(aLine2);
	DrawNow();
	}

void CGnuboxView::WriteLogFile(const TDesC& aText)
	{
	RFs& fs = iCoeEnv->FsSession();
	RFile file;
	TInt err = file.Open(fs, KGnuboxLogFile, EFileWrite | EFileShareAny);
	if (err == KErrNotFound || err == KErrPathNotFound)
		{
		fs.MkDirAll(KGnuboxDir);
		err = file.Create(fs, KGnuboxLogFile, EFileWrite | EFileShareAny);
		}
	if (err != KErrNone)
		return;
	TInt pos = 0;
	file.Seek(ESeekEnd, pos);
	if (pos > 200000)	// keep the log small
		{
		file.SetSize(0);
		pos = 0;
		file.Seek(ESeekStart, pos);
		}
	TTime now;
	now.HomeTime();
	TBuf<32> stamp;
	TRAPD(ignore, now.FormatL(stamp, _L("%H%:1%T%:2%S ")));
	TBuf8<300> line;
	line.Copy(stamp);
	TBuf8<300> text;
	text.Copy(aText.Left(line.MaxLength() - line.Length() - 2));
	line.Append(text);
	line.Append(_L8("\r\n"));
	file.Write(line);
	file.Close();
	}

void CGnuboxView::Detail(const TDesC& aText)
	{
	WriteLogFile(aText);
	}

void CGnuboxView::Log(const TDesC& aText)
	{
	WriteLogFile(aText);
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

void CGnuboxView::ClearL()
	{
	iLines->Reset();
	iListBox->HandleItemRemovalL();
	iListBox->Reset();
	DrawNow();
	}

// --------------------------------------------------------------------------
// app ui

void CGnuboxAppUi::ConstructL()
	{
	BaseConstructL();
	iView = CGnuboxView::NewL(ClientRect());
	AddToStackL(iView);
	iPaired = new (ELeave) CGnuboxBtDeviceArray(4);
	iIaps = new (ELeave) CGnuboxIapArray(8);
	iSettings.Load(iEikonEnv->FsSession());
	iConnection = CGnuboxConnection::NewL(*iView, *this);
	iStateText.Copy(_L("Not connected"));
	Welcome();
	// a connection left running by an earlier session?
	TUint32 iap = 0;
	TRAPD(err, iap = GnuboxCommDb::FindIapL());
	if (err == KErrNone && iap)
		{
		TRAP(err, iConnection->AttachExistingL(iap));
		}
	TRAP(err, UpdateStatusL());
	TRAP(err, UpdateButtonsL());
	}

CGnuboxAppUi::~CGnuboxAppUi()
	{
	delete iConnection;	// leaves the link up: only Disconnect ends it
	delete iPaired;
	delete iIaps;
	if (iView)
		{
		RemoveFromStack(iView);
		delete iView;
		}
	delete (TBuf<64>*)Dll::Tls();
	Dll::SetTls(NULL);
	}

void CGnuboxAppUi::Welcome()
	{
	MGnuboxLog& log = *iView;
	log.Log(_L("GnuBox - Internet over Bluetooth from an Android phone or a PC."));
	if (!iSettings.iHaveAddr)
		{
		log.Log(_L("1. Start BlueDUN on the phone (or gnubox-host.py on the PC)."));
		log.Log(_L("2. Press Device and choose the phone or PC (pair it first)."));
		log.Log(_L("3. Press Connect. The connection stays up until Disconnect."));
		}
	else
		log.Log(_L("Press Connect. The connection stays up until you press Disconnect."));
	log.Log(_L("Default menu: pick the connection apps use without asking"));
	log.Log(_L("(GnuBox BT, USB IP passthrough, GPRS...) or Ask every time."));
	}

void CGnuboxAppUi::UpdateStatusL()
	{
	TBuf<100> line1;
	switch (iConnection->State())
		{
	case EConnUp:
		{
		TBuf<20> since;
		TRAPD(ignore, iConnection->UpSince().FormatL(since, _L("%H%:1%T")));
		line1.Format(_L("CONNECTED since %S"), &since);
		break;
		}
	case EConnConnecting:
		line1.Copy(_L("Connecting..."));
		break;
	case EConnWaitRetry:
		line1.Copy(iStateText.Left(line1.MaxLength()));
		break;
	default:
		line1.Copy(_L("Not connected"));
		break;
		}
	TBuf<120> line2;
	if (iSettings.iHaveAddr)
		{
		TBuf<20> addr;
		GnuboxBt::AddrToText(iSettings.iAddr, addr);
		line2.Format(_L("Device: %S (%S)"), &iSettings.iName, &addr);
		}
	else
		line2.Copy(_L("Device: none - press Device"));
	line2.Append(iSettings.iMode == EGnuboxModeDialup ? _L("  |  Android dial-up") : _L("  |  PC (pppd)"));
	if (iSettings.iAutoReconnect)
		line2.Append(_L("  |  auto-reconnect"));
	iView->SetStatusL(line1, line2);
	}

void CGnuboxAppUi::UpdateButtonsL()
	{
	CEikButtonGroupContainer* cba = CEikButtonGroupContainer::Current();
	if (!cba)
		return;
	if (iConnection->State() == EConnIdle)
		cba->SetCommandL(KCbaConnect, EGnuboxCmdConnect, _L("Connect"));
	else
		cba->SetCommandL(KCbaConnect, EGnuboxCmdDisconnect, _L("Disconnect"));
	cba->DrawNow();
	}

void CGnuboxAppUi::ConnStateChanged(TGnuboxConnState /*aState*/, const TDesC& aInfo)
	{
	iStateText.Copy(aInfo.Left(iStateText.MaxLength()));
	TRAPD(err, UpdateStatusL());
	TRAP(err, UpdateButtonsL());
	}

// --------------------------------------------------------------------------
// device selection

void CGnuboxAppUi::UseDeviceL(const TBTDevAddr& aAddr, const TDesC& aName)
	{
	iSettings.iAddr = aAddr;
	iSettings.iHaveAddr = ETrue;
	iSettings.iName = aName.Left(iSettings.iName.MaxLength());
	GnuboxBt::SetDefaultPortL(iSettings, *iView);
	TInt err = iSettings.Save(iEikonEnv->FsSession());
	if (err != KErrNone)
		LogF(*iView, _L("Saving settings failed: %S"), &ErrText(err));
	LogF(*iView, _L("Device set: %S. It is remembered."), &iSettings.iName);
	if (iConnection->State() != EConnIdle)
		iView->Log(_L("(used from the next Connect)"));
	UpdateStatusL();
	}

void CGnuboxAppUi::OpenDeviceMenuL()
	{
	// "Device" is the first menu title, so opening the menu bar shows it
	CEikMenuBar* bar = iEikonEnv->AppUiFactory()->MenuBar();
	if (!bar)
		User::Leave(KErrNotSupported);
	bar->TryDisplayMenuBarL();
	TRAPD(ignore, bar->MoveHighlightToL(0, 0));
	}

void CGnuboxAppUi::CmdSearchDeviceL()
	{
	if (GnuboxBt::SelectPcL(iSettings, *iView))
		UseDeviceL(iSettings.iAddr, iSettings.iName);
	}

void CGnuboxAppUi::PickedL(TInt aIndex)
	{
	if (aIndex < 0 || aIndex >= iPaired->Count())
		return;
	TGnuboxBtDevice dev = (*iPaired)[aIndex];
	UseDeviceL(dev.iAddr, dev.iName);
	}

// --------------------------------------------------------------------------
// connect / disconnect

void CGnuboxAppUi::EnsureRecordsL()
	{
	// always (re)write: repairs the access point if it was edited in Control
	// panel (which insists on a phone number) and applies the current mode
	GnuboxCommDb::CreateRecordsL(*iView, iSettings.iMode, iSettings.iCompress);
	if (iSettings.iUseForAll)
		{
		TUint32 oldIap;
		TInt oldDialog;
		GnuboxCommDb::MakePreferredL(*iView, oldIap, oldDialog);
		TUint32 iap = GnuboxCommDb::FindIapL();
		if (oldIap != iap)	// remember what was there before us
			{
			iSettings.iPrevPrefIap = oldIap;
			iSettings.iPrevPrefDialog = oldDialog;
			iSettings.Save(iEikonEnv->FsSession());
			}
		}
	}

void CGnuboxAppUi::CmdConnectL()
	{
	if (!iSettings.iHaveAddr)
		{
		iView->Log(_L("Choose the device first (pair it in Control panel > Bluetooth)."));
		OpenDeviceMenuL();
		return;
		}
	if (iConnection->State() != EConnIdle)
		{
		iView->Log(_L("Already connected or connecting."));
		return;
		}
	LogF(*iView, _L("Connecting to %S ..."), &iSettings.iName);
	if (iSettings.iMode == EGnuboxModeDialup)
		iView->Log(_L("(BlueDUN must be running on the phone)"));
	EnsureRecordsL();
	GnuboxBt::SetDefaultPortL(iSettings, *iView);
	TUint32 iap = GnuboxCommDb::FindIapL();
	if (!iap)
		User::Leave(KErrNotFound);
	iConnection->ConnectL(iap, iSettings.iAutoReconnect);
	}

void CGnuboxAppUi::CmdDisconnect()
	{
	iConnection->Disconnect();
	}

// --------------------------------------------------------------------------
// settings

void CGnuboxAppUi::CmdSetModeL(TInt aMode)
	{
	if (iSettings.iMode == aMode)
		return;
	iSettings.iMode = aMode;
	iSettings.Save(iEikonEnv->FsSession());
	LogF(*iView, _L("Other device: %S"), &GnuboxModeName(aMode));
	if (iSettings.iHaveAddr)
		GnuboxBt::SetDefaultPortL(iSettings, *iView);
	GnuboxCommDb::ApplyModeL(*iView, aMode);
	UpdateStatusL();
	}

void CGnuboxAppUi::CmdSetSecurityL(TInt aLevel)
	{
	_LIT(KNone, "Bluetooth security: none (recommended)");
	_LIT(KAuth, "Bluetooth security: pairing required");
	_LIT(KEnc, "Bluetooth security: pairing + encryption");
	const TDesC* txt = aLevel == KBTDefaultServiceSecurityAuthenticateOnly ? &KAuth() :
		aLevel == KBTDefaultServiceSecurityAuthenticateAndEncrypt ? &KEnc() : &KNone();
	iSettings.iSecurity = aLevel;
	iSettings.Save(iEikonEnv->FsSession());
	iView->Log(*txt);
	if (iSettings.iHaveAddr)
		GnuboxBt::SetDefaultPortL(iSettings, *iView);
	if (iConnection->State() != EConnIdle)
		iView->Log(_L("(used from the next Connect)"));
	}

void CGnuboxAppUi::CmdToggleAutoReconnect()
	{
	iSettings.iAutoReconnect = !iSettings.iAutoReconnect;
	iConnection->SetAutoReconnect(iSettings.iAutoReconnect);
	iSettings.Save(iEikonEnv->FsSession());
	iView->Log(iSettings.iAutoReconnect ?
		_L("Auto-reconnect on: a lost connection is restored until you press Disconnect.") :
		_L("Auto-reconnect off."));
	TRAPD(err, UpdateStatusL());
	}

void CGnuboxAppUi::CmdToggleUseForAllL()
	{
	if (!iSettings.iUseForAll)
		{
		GnuboxCommDb::CreateRecordsL(*iView, iSettings.iMode, iSettings.iCompress);	// make sure it exists
		TUint32 oldIap;
		TInt oldDialog;
		TRAPD(err, GnuboxCommDb::MakePreferredL(*iView, oldIap, oldDialog));
		if (err != KErrNone)
			{
			LogF(*iView, _L("Could not change the default connection: %S"), &ErrText(err));
			TRAP(err, GnuboxCommDb::ShowPreferencesL(*iView));
			return;
			}
		if (oldIap != GnuboxCommDb::FindIapL())
			{
			iSettings.iPrevPrefIap = oldIap;
			iSettings.iPrevPrefDialog = oldDialog;
			}
		iSettings.iUseForAll = ETrue;
		iView->Log(_L("GnuBox BT is now the default access point, used without asking."));
		iView->Log(_L("Apps set to \"default\" / \"ask\" will use it."));
		TRAP(err, GnuboxCommDb::ShowPreferencesL(*iView));
		}
	else
		{
		GnuboxCommDb::RestorePreferredL(iSettings.iPrevPrefIap, iSettings.iPrevPrefDialog, *iView);
		iSettings.iUseForAll = EFalse;
		iView->Log(_L("Default access point restored to what it was before."));
		}
	iSettings.Save(iEikonEnv->FsSession());
	}

void CGnuboxAppUi::CmdToggleCompression()
	{
	iSettings.iCompress = !iSettings.iCompress;
	iSettings.Save(iEikonEnv->FsSession());
	iView->Log(iSettings.iCompress ?
		_L("Compression on (used if BlueDUN/pppd agrees). Takes effect at the next Connect.") :
		_L("Compression off. Takes effect at the next Connect."));
	if (iConnection->State() != EConnIdle)
		iView->Log(_L("Disconnect and Connect again to apply it."));
	}

void CGnuboxAppUi::CmdToggleTcpWindowL()
	{
	RFs& fs = iEikonEnv->FsSession();
	if (GnuboxTcp::IsEnabledL(fs))
		GnuboxTcp::DisableL(fs, *iView);
	else
		{
		iView->Log(_L("Bigger TCP window: the 8 KB default limits each download to"));
		iView->Log(_L("about 8 KB per round trip (~26 kB/s over Bluetooth)."));
		GnuboxTcp::EnableL(fs, *iView);
		}
	}

void CGnuboxAppUi::CmdSetDefaultL(TInt aIndex)
	{
	if (aIndex < 0)
		{
		GnuboxCommDb::SetDefaultL(0, ETrue, *iView);
		iSettings.iUseForAll = EFalse;
		iView->Log(_L("Default connection: ask every time (the phone shows its list)."));
		iView->Log(_L("Note: that list does not show GnuBox BT."));
		}
	else
		{
		if (aIndex >= iIaps->Count())
			return;
		TGnuboxIap iap = (*iIaps)[aIndex];
		TBool gnubox = iap.iName.CompareF(KGnuboxIapName) == 0;
		if (gnubox)
			GnuboxCommDb::CreateRecordsL(*iView, iSettings.iMode, iSettings.iCompress);
		TUint32 oldIap = 0;
		TInt oldDialog = -1;
		GnuboxCommDb::SetDefaultL(iap.iId, EFalse, *iView, &oldIap, &oldDialog);
		if (gnubox && !iSettings.iUseForAll && oldIap != iap.iId)
			{
			// remembered so that "Remove access point" can put it back
			iSettings.iPrevPrefIap = oldIap;
			iSettings.iPrevPrefDialog = oldDialog;
			}
		iSettings.iUseForAll = gnubox;
		LogF(*iView, _L("Default connection: %S (%S) - apps connect without asking."), &iap.iName, &iap.iType);
		if (gnubox && iConnection->State() == EConnIdle)
			iView->Log(_L("Apps will start the Bluetooth connection themselves, or press Connect."));
		}
	iSettings.Save(iEikonEnv->FsSession());
	TRAPD(err, GnuboxCommDb::ShowPreferencesL(*iView));
	}

void CGnuboxAppUi::CmdRepairL()
	{
	GnuboxCommDb::CreateRecordsL(*iView, iSettings.iMode, iSettings.iCompress);
	iView->Log(_L("Access point \"GnuBox BT\" written."));
	iView->Log(_L("Note: don't save it in Control panel - it asks for a phone number,"));
	iView->Log(_L("which would turn it into a real phone call. Connect repairs it."));
	}

void CGnuboxAppUi::CmdDetailsL()
	{
	LogF(*iView, _L("Other device: %S"), &GnuboxModeName(iSettings.iMode));
	GnuboxBt::ShowDefaultPortL(*iView);
	GnuboxCommDb::ShowStatusL(*iView);
	_LIT(KOn, "on");
	_LIT(KOff, "off");
	LogF(*iView, _L("Auto-reconnect %S, use for all apps %S"),
		iSettings.iAutoReconnect ? &KOn() : &KOff(),
		iSettings.iUseForAll ? &KOn() : &KOff());
	}

// --------------------------------------------------------------------------
// menus

static void AddItemL(CEikMenuPane& aPane, TInt aCmd, const TDesC& aText, TInt aFlags)
	{
	CEikMenuPaneItem::SData item;
	item.iCommandId = aCmd;
	item.iCascadeId = 0;
	item.iFlags = aFlags;
	item.iText.Copy(aText.Left(item.iText.MaxLength()));
	item.iExtraText.Zero();
	aPane.AddMenuItemL(item);
	}

void CGnuboxAppUi::DynInitMenuPaneL(TInt aResourceId, CEikMenuPane* aMenuPane)
	{
	if (!aMenuPane)
		return;
	if (aResourceId == R_GNUBOX_MENU_MAIN)
		{
		TBool idle = iConnection->State() == EConnIdle;
		aMenuPane->SetItemDimmed(EGnuboxCmdConnect, !idle);
		aMenuPane->SetItemDimmed(EGnuboxCmdDisconnect, idle);
		return;
		}
	if (aResourceId == R_GNUBOX_MENU_SETTINGS)
		{
		aMenuPane->SetItemButtonState(EGnuboxCmdModeDialup,
			iSettings.iMode == EGnuboxModeDialup ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdModePc,
			iSettings.iMode == EGnuboxModePc ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdAutoReconnect,
			iSettings.iAutoReconnect ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdCompression,
			iSettings.iCompress ? EEikMenuItemSymbolOn : 0);
		TBool tcp = EFalse;
		TRAPD(e, tcp = GnuboxTcp::IsEnabledL(iEikonEnv->FsSession()));
		aMenuPane->SetItemButtonState(EGnuboxCmdTcpWindow, tcp ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdSecNone,
			iSettings.iSecurity == KBTDefaultServiceSecurityNone ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdSecAuth,
			iSettings.iSecurity == KBTDefaultServiceSecurityAuthenticateOnly ? EEikMenuItemSymbolOn : 0);
		aMenuPane->SetItemButtonState(EGnuboxCmdSecEncrypt,
			iSettings.iSecurity == KBTDefaultServiceSecurityAuthenticateAndEncrypt ? EEikMenuItemSymbolOn : 0);
		return;
		}
	if (aResourceId == R_GNUBOX_MENU_DEFAULT)
		{
		TUint32 cur = 0;
		TBool ask = ETrue;
		TRAPD(err, GnuboxCommDb::ReadDefaultL(cur, ask));
		TRAP(err, GnuboxCommDb::ListIapsL(*iIaps));
		if (err != KErrNone)
			iIaps->Reset();
		aMenuPane->SetItemButtonState(EGnuboxCmdDefaultAsk, ask ? EEikMenuItemSymbolOn : 0);
		for (TInt k = 0; k < iIaps->Count() && k < EGnuboxMaxIaps; k++)
			{
			const TGnuboxIap& iap = (*iIaps)[k];
			TBuf<80> text;
			text.Copy(iap.iName.Left(50));
			text.Append(_L("  ("));
			text.Append(iap.iType);
			text.Append(')');
			TInt flags = EEikMenuItemCheckBox;
			if (!ask && iap.iId == cur)
				flags |= EEikMenuItemSymbolOn;
			AddItemL(*aMenuPane, EGnuboxCmdIapFirst + k, text, flags);
			}
		return;
		}
	if (aResourceId != R_GNUBOX_MENU_DEVICE)
		return;

	TRAPD(err, GnuboxCommDb::ListPairedL(*iPaired, *iView));
	if (err != KErrNone)
		{
		iPaired->Reset();
		LogD(*iView, _L("Cannot read paired devices: %d"), err);
		}
	TBool savedListed = EFalse;
	TInt i;
	for (i = 0; i < iPaired->Count(); i++)
		if (iSettings.iHaveAddr && (*iPaired)[i].iAddr == iSettings.iAddr)
			savedListed = ETrue;
	if (iSettings.iHaveAddr && !savedListed)
		{
		TGnuboxBtDevice dev;
		dev.iAddr = iSettings.iAddr;
		dev.iName = iSettings.iName.Left(dev.iName.MaxLength());
		iPaired->InsertL(0, dev);
		}
	if (iPaired->Count() == 0)
		{
		AddItemL(*aMenuPane, EGnuboxCmdDeviceFirst,
			_L("(no paired devices - pair in Control panel > Bluetooth)"), EEikMenuItemDimmed);
		return;
		}
	for (i = 0; i < iPaired->Count() && i < EGnuboxMaxDevices; i++)
		{
		const TGnuboxBtDevice& dev = (*iPaired)[i];
		TBuf<80> text;
		TBuf<20> addr;
		GnuboxBt::AddrToText(dev.iAddr, addr);
		text.Copy(dev.iName.Length() ? dev.iName.Left(50) : TPtrC(_L("(no name)")));
		text.Append(_L("  "));
		text.Append(addr);
		TInt flags = EEikMenuItemCheckBox;
		if (iSettings.iHaveAddr && dev.iAddr == iSettings.iAddr)
			flags |= EEikMenuItemSymbolOn;
		AddItemL(*aMenuPane, EGnuboxCmdDeviceFirst + i, text, flags);
		}
	}

void CGnuboxAppUi::HandleCommandL(TInt aCommand)
	{
	TInt err = KErrNone;
	switch (aCommand)
		{
	case EEikCmdExit:
		if (iConnection->State() != EConnIdle)
			iView->Log(_L("Exiting: the connection stays up (no auto-reconnect)."));
		Exit();
		return;
	case EGnuboxCmdConnect:
		TRAP(err, CmdConnectL());
		break;
	case EGnuboxCmdDisconnect:
		CmdDisconnect();
		break;
	case EGnuboxCmdDeviceMenu:
		TRAP(err, OpenDeviceMenuL());
		break;
	case EGnuboxCmdSearchDevice:
		TRAP(err, CmdSearchDeviceL());
		break;
	case EGnuboxCmdShowStatus:
		TRAP(err, CmdDetailsL());
		break;
	case EGnuboxCmdModeDialup:
		TRAP(err, CmdSetModeL(EGnuboxModeDialup));
		break;
	case EGnuboxCmdModePc:
		TRAP(err, CmdSetModeL(EGnuboxModePc));
		break;
	case EGnuboxCmdAutoReconnect:
		CmdToggleAutoReconnect();
		break;
	case EGnuboxCmdTcpWindow:
		TRAP(err, CmdToggleTcpWindowL());
		break;
	case EGnuboxCmdCompression:
		CmdToggleCompression();
		break;
	case EGnuboxCmdUseForAll:
		TRAP(err, CmdToggleUseForAllL());
		break;
	case EGnuboxCmdSecNone:
		TRAP(err, CmdSetSecurityL(KBTDefaultServiceSecurityNone));
		break;
	case EGnuboxCmdSecAuth:
		TRAP(err, CmdSetSecurityL(KBTDefaultServiceSecurityAuthenticateOnly));
		break;
	case EGnuboxCmdSecEncrypt:
		TRAP(err, CmdSetSecurityL(KBTDefaultServiceSecurityAuthenticateAndEncrypt));
		break;
	case EGnuboxCmdCreateRecords:
		TRAP(err, CmdRepairL());
		break;
	case EGnuboxCmdRemoveRecords:
		if (iSettings.iUseForAll)
			{
			TRAP(err, CmdToggleUseForAllL());
			}
		TRAP(err, GnuboxCommDb::RemoveRecordsL(*iView));
		if (err == KErrNone)
			iView->Log(_L("Access point \"GnuBox BT\" removed."));
		break;
	case EGnuboxCmdDumpCommDb:
		TRAP(err, GnuboxCommDb::DumpL(*iView, iEikonEnv->FsSession()));
		if (err == KErrNone)
			iView->Log(_L("Log: C:\\Documents\\GnuBox\\gnubox-log.txt"));
		break;
	case EGnuboxCmdClearLog:
		TRAP(err, iView->ClearL());
		break;
	case EGnuboxCmdAbout:
		iView->Log(_L("GnuBox for Series 80 2.7 - GPL. Based on gnubox by Mal Minhas"));
		iView->Log(_L("and Mika Raento."));
		break;
	case EGnuboxCmdDefaultAsk:
		TRAP(err, CmdSetDefaultL(-1));
		break;
	default:
		if (aCommand >= EGnuboxCmdIapFirst && aCommand < EGnuboxCmdIapFirst + EGnuboxMaxIaps)
			{
			TRAP(err, CmdSetDefaultL(aCommand - EGnuboxCmdIapFirst));
			}
		else if (aCommand >= EGnuboxCmdDeviceFirst && aCommand < EGnuboxCmdDeviceFirst + EGnuboxMaxDevices)
			{
			TRAP(err, PickedL(aCommand - EGnuboxCmdDeviceFirst));
			}
		break;
		}
	if (err != KErrNone)
		LogF(*iView, _L("Error: %S"), &ErrText(err));
	TRAP(err, UpdateButtonsL());
	}

// --------------------------------------------------------------------------
// document, application, entry points

CEikAppUi* CGnuboxDocument::CreateAppUiL()
	{
	return new (ELeave) CGnuboxAppUi;
	}

CApaDocument* CGnuboxApplication::CreateDocumentL()
	{
	return new (ELeave) CGnuboxDocument(*this);
	}

TUid CGnuboxApplication::AppDllUid() const
	{
	return KUidGnubox;
	}

EXPORT_C CApaApplication* NewApplication()
	{
	return new CGnuboxApplication;
	}

GLDEF_C TInt E32Dll(TDllReason)
	{
	return KErrNone;
	}
