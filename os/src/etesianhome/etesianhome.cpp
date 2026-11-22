// Copyright The Phorepoc Authors
// SPDX-License-Identifier: AGPL-3.0-only

// etesianhome.cpp -- the Etesian Home application, rungs E1-E2 of docs/phorepoc/etesian-port.md: the three horizontal views of the Etesian
// phone Home (guide chapter 20), drawn with CWindowGc in one CONE window (plain CONE/Avkon C++, no Qt, no Phorepoc-only API):
//   Events        | Applications | Open apps
//   * a black 24 dp status bar (name left, clock right) and three page dots (tap, or swipe, or Left/Right at the edges);
//   * Applications: the apps of the application list in a 4-column grid (more when wider), each a top-lit dark squircle (n = 2.8) with the app's icon;
//   * Open apps: the running applications as cards with a preview of the app's last frame, tap = bring to the front; edit mode (long press
//     on a card, or Backspace) dims the previews, shows the red close squircle at each card's top-right and the lit Done / Close all buttons;
//     the list is the stock task switcher's (tsfswdatalist.cpp): no embedded groups, no hidden window groups, not the hidden-apps
//     property (KPSUidUikon/KUikAppHiddenList), not screensaver / idle, only apps apparc knows on the main screen;
//   * Events: day and date, then the notifier sources an application may read: missed calls (Logs' CenRep counter) and unread messages
//     (the message server's Inbox), tap opens Logs / Messages;
//   * type: Fira Sans (SIL OFL, shipped in the package) at the Etesian scale: 16 Light body, 16 Bold titles, 14 Regular summaries,
//     13 Regular labels, 12 meta, 15 Bold buttons; falls back to the Eikon faces when it is not installed;
//   * the Menu key (captured at a higher priority than the cap server): short = Home, Applications; long (0.6 s) = Open apps.
//     At its key down Home copies the screen (CWsScreenDevice::CopyScreenToBitmap): that is the foreground application's last frame,
//     its preview card.
// It is the shell's idle application (etesianlaunch.exe starts it; the selector publishes its UID as the active idle application) and,
// like the stock idle, publishes KPSIdlePhase1Ok when it is up. Sizes are in dp of a 320 dp wide reference (the shorter screen side).
#include <e32debug.h>
#include <e32math.h>
#include <e32property.h>
#include <eikstart.h>
#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <coecntrl.h>
#include <eikenv.h>
#include <eikappui.h>
#include <eikbtgpc.h>
#include <avkon.hrh>
#include <apgcli.h>
#include <apgtask.h>
#include <apgwgnam.h>
#include <apgicnfl.h>
#include <aknsutils.h>
#include <akniconutils.h>
#include <akntasklist.h>
#include <apacmdln.h>
#include <centralrepository.h>
#include <msvapi.h>
#include <msvids.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>
#include <gdi.h>
#include <w32std.h>
#include <etel3rdparty.h>
#include <aknsoftnotifier.h>
#include <aknkeylock.h>
#include "../phedevmenu/pheshellapps.h"
#include "etesianhome.hrh"

const TUid KEtesianHomeUid = { 0xE7016B50 };
const TUid KLogsAppUid = { 0x101F4CD5 };
const TUid KMessagesAppUid = { 0x100058C5 };
const TUid KCRUidLogs = { 0x101F874E };
const TUint32 KLogsNewMissedCalls = 6;
// the stock task switcher's exclusions (tsfswengine): the hidden-apps property, and apps that are never shown
const TUid KPSUidUikonCat = { 0x101F8773 };
const TUint32 KUikAppHiddenList = 0x10;
const TUint32 KAlwaysHiddenUids[] = { 0x100056CF /* screensaver */, 0x102750F0 /* idle app */ };

// Etesian dark-mode tokens (design/tokens, AGPL-3.0-only)
const TUint32 KColBg = 0x000000;
const TUint32 KColFg = 0xFFFFFF;
const TUint32 KColFg2 = 0xC8C8C8;
const TUint32 KColAccent = 0x4591FF;
const TUint32 KColRim = 0x393839;
const TUint32 KColCard = 0x1C1C1C;

_LIT(KFiraRegular, "Fira Sans");
_LIT(KFiraLight, "Fira Sans Light");

static TRgb Rgb(TUint32 aRgb) { return TRgb((aRgb >> 16) & 0xFF, (aRgb >> 8) & 0xFF, aRgb & 0xFF); }
static TInt Chan(TUint32 aRgb, TInt aShift) { return (aRgb >> aShift) & 0xFF; }

// is (x, y) inside a w x h shape inset by aInset px: n = 2.8 superellipse (aSquircle) or a rounded rectangle of radius aRadius
static TBool Inside(TReal aX, TReal aY, TReal aW, TReal aH, TReal aRadius, TBool aSquircle, TReal aInset)
	{
	const TReal cx = aX - aW / 2, cy = aY - aH / 2;
	const TReal ax = cx < 0 ? -cx : cx, ay = cy < 0 ? -cy : cy;
	if (aSquircle)
		{
		const TReal hw = aW / 2 - aInset, hh = aH / 2 - aInset;
		if (hw <= 0 || hh <= 0)
			return EFalse;
		TReal px = 0, py = 0;
		Math::Pow(px, ax / hw, 2.8);
		Math::Pow(py, ay / hh, 2.8);
		return px + py <= 1.0;
		}
	TReal dx = ax - (aW / 2 - aInset - aRadius), dy = ay - (aH / 2 - aInset - aRadius);
	if (dx < 0) dx = 0;
	if (dy < 0) dy = 0;
	if (ax > aW / 2 - aInset || ay > aH / 2 - aInset)
		return EFalse;
	return dx * dx + dy * dy <= aRadius * aRadius;
}

// a top-lit shape (guide 06b / 20 section 6): a vertical gradient aTop -> aBottom (and aMid in the middle when it is not KNoMid) with a
// 1.4 px rim (aRimTop on the top third, aRimBottom below) and its antialiased alpha mask (EGray256, drawn with BitBltMasked ... ETrue)
const TUint32 KNoMid = 0xFF000000;
static void MakeLitL(CFbsBitmap& aBmp, CFbsBitmap& aMask, TInt aW, TInt aH, TInt aRadius, TBool aSquircle,
					 TUint32 aTop, TUint32 aMid, TUint32 aBottom, TUint32 aRimTop, TUint32 aRimBottom)
	{
	aBmp.Reset();
	aMask.Reset();
	User::LeaveIfError(aBmp.Create(TSize(aW, aH), EColor16MU));
	User::LeaveIfError(aMask.Create(TSize(aW, aH), EGray256));
	const TInt strideT = CFbsBitmap::ScanLineLength(aW, EColor16MU);
	const TInt strideM = CFbsBitmap::ScanLineLength(aW, EGray256);
	aBmp.LockHeap();
	aMask.LockHeap();
	TUint8* t = (TUint8*)aBmp.DataAddress();
	TUint8* m = (TUint8*)aMask.DataAddress();
	for (TInt y = 0; y < aH; y++)
		{
		// colour of this row
		TUint32 col = 0;
		for (TInt sh = 0; sh <= 16; sh += 8)
			{
			TInt c;
			const TInt f = aH > 1 ? y * 1000 / (aH - 1) : 0;		// 0..1000
			if (aMid == KNoMid)
				c = Chan(aTop, sh) + (Chan(aBottom, sh) - Chan(aTop, sh)) * f / 1000;
			else if (f < 500)
				c = Chan(aTop, sh) + (Chan(aMid, sh) - Chan(aTop, sh)) * f / 500;
			else
				c = Chan(aMid, sh) + (Chan(aBottom, sh) - Chan(aMid, sh)) * (f - 500) / 500;
			col |= (TUint32)c << sh;
			}
		const TUint32 rim = y < aH / 3 ? aRimTop : aRimBottom;
		for (TInt x = 0; x < aW; x++)
			{
			TInt cov = 0;
			for (TInt sy = 0; sy < 3; sy++)
				for (TInt sx = 0; sx < 3; sx++)
					if (Inside(x + (sx + 0.5) / 3.0, y + (sy + 0.5) / 3.0, aW, aH, aRadius, aSquircle, 0))
						cov++;
			m[y * strideM + x] = (TUint8)(cov * 255 / 9);
			*(TUint32*)(t + y * strideT + x * 4) = Inside(x + 0.5, y + 0.5, aW, aH, aRadius, aSquircle, 1.4) ? col : rim;
			}
		}
	aMask.UnlockHeap();
	aBmp.UnlockHeap();
	}

class CEtApp : public CBase
	{
public:
	~CEtApp() { delete iCaption; delete iIcon; delete iBmp; delete iMask; }
	TUid iUid;
	HBufC* iCaption = NULL;
	CApaMaskedBitmap* iIcon = NULL;
	CFbsBitmap* iBmp = NULL;		// the skin-aware (MIF / SVG) app icon and its mask, sized for the tile
	CFbsBitmap* iMask = NULL;
	};

// the last frame of an application, copied from the screen when the Menu key went down while it was in front
class CEtThumb : public CBase
	{
public:
	~CEtThumb() { delete iBmp; delete iDim; }
	TInt iWgId = 0;
	CFbsBitmap* iBmp = NULL;
	CFbsBitmap* iDim = NULL;		// the same at 50 % (edit mode)
	};

// one running application: its window group, its list entry (icon, caption) and last frame
class CEtCard : public CBase
	{
public:
	~CEtCard() { delete iName; delete iOwnApp; }
	TInt iWgId = 0;
	TUid iUid;
	const CEtApp* iApp = NULL;		// the application list's entry, or the own one for an app the list does not show
	CEtApp* iOwnApp = NULL;
	HBufC* iName = NULL;
	};

// the message server wants an observer; the Events view only counts the Inbox, so it ignores every event
class CEtMsvObserver : public MMsvSessionObserver
	{
public:
	void HandleSessionEventL(TMsvSessionEvent, TAny*, TAny*, TAny*) {}
	};

_LIT(KMenuCharging, "  (charging)");
_LIT(KMenuNone, "");
enum TEtPage { EPageEvents = 0, EPageApps = 1, EPageOpen = 2 };
static const TBool KShowPageDots = EFalse;		// a setting in the guide; off by default
// the fonts of the Etesian type scale (guide 03)
enum TEtFont { EFLabel, EFBody, EFTitle, EFSum, EFXs, EFXsB, EFBtn, EFDay, EFDate, EFCount };

// the signal strength for the status bar: CTelephony, polled every 15 s; -1 = no data (the PC image has no modem: no bars are drawn)
class MEtSignalObserver
	{
public:
	virtual void SignalChanged(TInt aBars) = 0;
	};
class CEtSignal : public CActive
	{
public:
	static CEtSignal* NewL(MEtSignalObserver& aObs)
		{
		CEtSignal* s = new (ELeave) CEtSignal(aObs);
		CleanupStack::PushL(s);
		s->iTel = CTelephony::NewL();
		User::LeaveIfError(s->iTimer.CreateLocal());
		CActiveScheduler::Add(s);
		s->Ask();
		CleanupStack::Pop(s);
		return s;
		}
	~CEtSignal()
		{
		Cancel();
		iTimer.Close();
		delete iTel;
		}
private:
	CEtSignal(MEtSignalObserver& aObs) : CActive(EPriorityLow), iObs(aObs), iPkg(iSig) {}
	void Ask()
		{
		iWaiting = EFalse;
		iTel->GetSignalStrength(iStatus, iPkg);
		SetActive();
		}
	void RunL()
		{
		if (iWaiting)
			{
			Ask();
			return;
			}
		iObs.SignalChanged(iStatus.Int() == KErrNone ? (TInt)iSig.iBar : -1);
		iWaiting = ETrue;
		iTimer.After(iStatus, 15000000);
		SetActive();
		}
	void DoCancel()
		{
		if (iWaiting)
			iTimer.Cancel();
		else
			iTel->CancelAsync(CTelephony::EGetSignalStrengthCancel);
		}
	MEtSignalObserver& iObs;
	CTelephony* iTel = NULL;
	CTelephony::TSignalStrengthV1 iSig;
	CTelephony::TSignalStrengthV1Pckg iPkg;
	RTimer iTimer;
	TBool iWaiting = EFalse;
	};

class MEtStatusActions
	{
public:
	virtual void StatusLock() = 0;
	};

class CEtHomeContainer : public CCoeControl, public MEtSignalObserver
	{
public:
	CEtHomeContainer() { for (TInt i = 0; i < EFCount; i++) iF[i] = NULL; }
	~CEtHomeContainer()
		{
		delete iSignal;
		delete iClock;
		delete iWatch;
		delete iPreviewTimer;
		delete iMenuTimer;
		delete iCloseTimer;
		delete iHoldTimer;
		iCards.ResetAndDestroy();
		iThumbs.ResetAndDestroy();
		iApps.ResetAndDestroy();
		iLs.Close();
		ReleaseFonts();
		}
	void ConstructL(const TRect& aRect)
		{
		CreateWindowL();
		SetRect(aRect);
		Window().SetPointerGrab(ETrue);
		EnableDragEvents();
		Layout(aRect);
		User::LeaveIfError(iLs.Connect());
		LoadAppsL();
		iWatch = CPeriodic::NewL(CActive::EPriorityLow);
		iPreviewTimer = CPeriodic::NewL(CActive::EPriorityLow);
		TRAP_IGNORE(Window().EnableAdvancedPointers());
		iClock = CPeriodic::NewL(CActive::EPriorityLow);
		iClock->Start(15000000, 15000000, TCallBack(ClockTick, this));
		TRAP_IGNORE(iSignal = CEtSignal::NewL(*this));
		ReadBattery();
		iMenuTimer = CPeriodic::NewL(CActive::EPriorityHigh);
		iCloseTimer = CPeriodic::NewL(CActive::EPriorityLow);
		iHoldTimer = CPeriodic::NewL(CActive::EPriorityStandard);
		ActivateL();
		}
	// geometry from the rectangle; the 320 dp reference is the SHORTER side, so a rotation keeps the sizes and changes the columns
	void Layout(const TRect& aRect)
		{
		iW = aRect.Width();
		iH = aRect.Height();
		iUnit = iW < iH ? iW : iH;
		iStatusH = Sc(24);
		iTile = Sc(54);			// app icon 80 n9px = 54 dp
		iMargin = Sc(11);		// the default margin, 16 n9px
		iCols = iW / Sc(76);
		if (iCols < 4)
			iCols = 4;
		iCellW = iW / iCols;
		ReleaseFonts();
		MakeFonts();
		iLabelH = iF[EFLabel]->HeightInPixels() + 2;
		iCellH = Sc(4) + iTile + Sc(4) + iLabelH + Sc(8);
		iBottomH = Sc(36);
		TRAP_IGNORE(MakeLitL(iTileBmp, iMaskBmp, iTile, iTile, 0, ETrue, 0x2A2A2A, KNoMid, 0x080808, 0x505050, 0x393839));
		const TInt evt = Sc(44);		// notification thumbnails are 43-44 dp squircles
		TRAP_IGNORE(MakeLitL(iEvTile, iEvMask, evt, evt, 0, ETrue, 0x2A2A2A, KNoMid, 0x080808, 0x505050, 0x393839));
		iEvSize = evt;
		iXSize = 0;
		// Open apps: cards, 2 per row in portrait (the default), 4 in landscape; the preview keeps the screen's aspect
		iCardCols = iCardLevel > 0 ? iCardLevel : (iW > iH ? 4 : 2);
		iGap = iMargin;
		iCardW = (iW - (iCardCols + 1) * iGap) / iCardCols;
		iPrevW = iCardW - Sc(12);
		iPrevH = iPrevW * iH / iW;
		iCardH = Sc(6) + iPrevH + Sc(6) + iLabelH + Sc(8);
		iCardLeft = iGap;
		if (iCardCols == 1)
			{
			// the carousel: one card that fits the screen's height, centred
			const TInt availH = iH - iStatusH - iGap - iXSize / 3 - iBottomH - iGap - Sc(8);
			iPrevH = availH - Sc(6) - Sc(6) - iLabelH - Sc(8);
			iPrevW = iPrevH * iW / iH;
			iCardW = iPrevW + Sc(12);
			iCardLeft = (iW - iCardW) / 2;
			iCardH = Sc(6) + iPrevH + Sc(6) + iLabelH + Sc(8);
			}
		iScroll = 0;
		// the lit Done / Close all buttons and the red close squircle
		iBtnW = Sc(120);
		iBtnH = Sc(32);
		TRAP_IGNORE(MakeLitL(iBtnBmp, iBtnMask, iBtnW, iBtnH, iBtnH * 21 / 100, EFalse, 0x1C1C1C, 0x101010, 0x080808, 0x393839, 0x393839));
		iXSize = iCardW * 28 / 100;
		TRAP_IGNORE(MakeLitL(iXBmp, iXMask, iXSize, iXSize, 0, ETrue, 0xF78673, KNoMid, 0xEF2C10, 0xFFB5A8, 0xB01E0C));
		}
	// Fira Sans (shipped in the package: \resource\fonts); when the face is not installed the font store hands back its default
	CFont* MakeFont(const TDesC& aName, TInt aPx, TBool aBold)
		{
		TFontSpec spec(aName, aPx);
		if (aBold)
			spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
		CFont* f = NULL;
		if (iCoeEnv->ScreenDevice()->GetNearestFontInPixels(f, spec) != KErrNone)
			f = NULL;
		return f;
		}
	void MakeFonts()
		{
		iF[EFLabel] = MakeFont(KFiraRegular, Sc(13), EFalse);		// small: labels
		iF[EFBody] = MakeFont(KFiraLight, Sc(16), EFalse);		// default: body
		iF[EFTitle] = MakeFont(KFiraRegular, Sc(16), ETrue);		// events: titles 16 bold
		iF[EFSum] = MakeFont(KFiraRegular, Sc(14), EFalse);		// events: summary 14 regular
		iF[EFXs] = MakeFont(KFiraRegular, Sc(12), EFalse);		// meta, time
		iF[EFXsB] = MakeFont(KFiraRegular, Sc(12), ETrue);		// section header
		iF[EFBtn] = MakeFont(KFiraRegular, Sc(15), ETrue);		// button 15 bold
		iF[EFDay] = MakeFont(KFiraLight, Sc(32), EFalse);		// events header: the day, large light
		iF[EFDate] = MakeFont(KFiraLight, Sc(27), EFalse);		// the date, about 0.85 x the day
		iOwnFonts = ETrue;
		for (TInt i = 0; i < EFCount; i++)
			if (!iF[i])
				iOwnFonts = EFalse;
		if (!iOwnFonts)
			{
			// all-or-nothing: without the Fira faces everything falls back to Eikon's
			iOwnFonts = ETrue;
			ReleaseFonts();
			for (TInt i = 0; i < EFCount; i++)
				iF[i] = (CFont*)(i == EFBody || i == EFSum || i == EFDay || i == EFDate ? iEikonEnv->NormalFont() : (i == EFTitle || i == EFBtn ? iEikonEnv->TitleFont() : iEikonEnv->AnnotationFont()));
			}
		TFontSpec s1 = iF[EFLabel]->FontSpecInTwips();
		TFontSpec s2 = iF[EFBody]->FontSpecInTwips();
		RDebug::Print(_L("PH: [etesianhome] fonts: label '%S' body '%S' (own %d)"), &s1.iTypeface.iName, &s2.iTypeface.iName, iOwnFonts);
		}
	void ReleaseFonts()
		{
		if (iOwnFonts)
			for (TInt i = 0; i < EFCount; i++)
				if (iF[i])
					iCoeEnv->ScreenDevice()->ReleaseFont(iF[i]);
		iOwnFonts = EFalse;
		for (TInt i = 0; i < EFCount; i++)
			iF[i] = NULL;
		}

	// the icon the way the application menu gets it: through the skin (MIF / SVG icons are scaled, themed icons found)
	void LoadIconL(CEtApp& aApp)
		{
		CFbsBitmap* bmp = NULL;
		CFbsBitmap* mask = NULL;
		TRAPD(ie, AknsUtils::CreateAppIconLC(AknsUtils::SkinInstance(), aApp.iUid, EAknsAppIconTypeList, bmp, mask); CleanupStack::Pop(2));
		if (ie == KErrNone && bmp)
			{
			const TSize g(iTile * 62 / 100, iTile * 62 / 100);
			AknIconUtils::SetSize(bmp, g, EAspectRatioPreserved);
			if (mask)
				AknIconUtils::SetSize(mask, g, EAspectRatioPreserved);
			aApp.iBmp = bmp;
			aApp.iMask = mask;
			}
		else
			{
			aApp.iIcon = CApaMaskedBitmap::NewLC();
			CleanupStack::Pop(aApp.iIcon);
			if (iLs.GetAppIcon(aApp.iUid, TSize(iTile, iTile), *aApp.iIcon) != KErrNone)
				{
				delete aApp.iIcon;
				aApp.iIcon = NULL;
				}
			}
		}
	void LoadAppsL()
		{
		// the application list is scanned asynchronously after boot (and a package install registers late)
		TInt r = KErrNotReady;
		for (TInt i = 0; i < 40 && r != KErrNone; i++)
			{
			r = iLs.GetAllApps();
			if (r != KErrNone)
				User::After(250000);
			}
		iApps.ResetAndDestroy();
		// shell-tagged apps (pheshellapps.h): what belongs to another shell, and what this shell replaces, is not in the grid
		RArray<TUint32> managed, hide;
		CleanupClosePushL(managed);
		CleanupClosePushL(hide);
		{
		RFs fs;
		if (fs.Connect() == KErrNone)
			{
			TPheHiddenCollector apps(_L("etesian"), managed, hide);
			TRAP_IGNORE(PheForEachTagFileL(fs, _L(".apps"), apps));
			TPheHiddenCollector hides(_L("etesian"), managed, hide);
			hides.iHideMode = ETrue;
			TRAP_IGNORE(PheForEachTagFileL(fs, _L(".hides"), hides));
			fs.Close();
			}
		}
		TApaAppInfo info;
		while (r == KErrNone && iLs.GetNextApp(info) == KErrNone)
			{
			if (info.iUid == KEtesianHomeUid || hide.Find(info.iUid.iUid) != KErrNotFound)
				continue;
			TApaAppCapabilityBuf cap;
			// hidden registrations are skipped, except the apps this shell's own .apps file names (managed, and not hidden by another shell)
			const TBool own = managed.Find(info.iUid.iUid) != KErrNotFound && hide.Find(info.iUid.iUid) == KErrNotFound;
			if (!own && iLs.GetAppCapability(cap, info.iUid) == KErrNone && cap().iAppIsHidden)
				continue;
			CEtApp* a = new (ELeave) CEtApp;
			CleanupStack::PushL(a);
			a->iUid = info.iUid;
			const TDesC& name = info.iShortCaption.Length() ? info.iShortCaption : (info.iCaption.Length() ? info.iCaption : info.iFullName);
			a->iCaption = name.AllocL();
			LoadIconL(*a);
			RDebug::Print(_L("PH: [etesianhome] app %08x %S"), a->iUid.iUid, a->iCaption);
			iApps.AppendL(a);
			CleanupStack::Pop(a);
			}
		iApps.Sort(TLinearOrder<CEtApp>(CompareApps));
		ApplyOrder();
		RDebug::Printf("PH: [etesianhome] %d apps (list r=%d, %d tagged, %d hidden by the tags)", iApps.Count(), r, managed.Count(), hide.Count());
		CleanupStack::PopAndDestroy(2, &managed);
		}
	TInt Sc(TInt aDp) const { return (aDp * iUnit + 160) / 320; }

	static TInt CompareApps(const CEtApp& aA, const CEtApp& aB) { return aA.iCaption->CompareC(*aB.iCaption); }
	const CEtApp* FindApp(TUid aUid) const
		{
		for (TInt i = 0; i < iApps.Count(); i++)
			if (iApps[i]->iUid == aUid)
				return iApps[i];
		return NULL;
		}

	// ---- pages
	TInt Items() const { return iPage == EPageOpen ? iCards.Count() : iApps.Count(); }
	TInt Rows() const { return iPage == EPageOpen ? (iCards.Count() + iCardCols - 1) / iCardCols : (iApps.Count() + iCols - 1) / iCols; }
	TInt Cols() const { return iPage == EPageOpen ? iCardCols : iCols; }
	TInt RowH() const { return iPage == EPageOpen ? iCardH + iGap : iCellH; }
	TInt GridTop() const { return iStatusH + (iPage == EPageOpen ? iGap + iXSize / 3 : Sc(8)); }
	TInt GridHeight() const { return iH - iBottomH - GridTop(); }
	TInt MaxScroll() const { TInt m = Rows() * RowH() - GridHeight(); return m > 0 ? m : 0; }
	TRect CellRect(TInt aIndex) const
		{
		const TInt c = aIndex % Cols(), r = aIndex / Cols();
		if (iPage == EPageOpen)
			return TRect(TPoint(iCardLeft + c * (iCardW + iGap), GridTop() + r * RowH() - iScroll), TSize(iCardW, iCardH));
		return TRect(TPoint(c * iCellW, GridTop() + r * iCellH - iScroll), TSize(iCellW, iCellH));
		}
	// the red close squircle: at the card's top-right, slightly outside its corner (guide 20, Open apps edit mode)
	TRect CloseRect(TInt aIndex) const
		{
		const TRect c = CellRect(aIndex);
		return TRect(TPoint(c.iBr.iX - iXSize * 3 / 4, c.iTl.iY - iXSize / 4), TSize(iXSize, iXSize));
		}
	// ---- the Applications grid's own order (long press = edit mode, drag to move): one hex UID per line in the private directory
	void ApplyOrder()
		{
		RFs fs;
		if (fs.Connect() != KErrNone)
			return;
		fs.SetSessionToPrivate(EDriveC);
		RFile f;
		if (f.Open(fs, _L("order.txt"), EFileRead) == KErrNone)
			{
			TInt sz = 0;
			f.Size(sz);
			HBufC8* b = HBufC8::New(sz + 1);
			if (b)
				{
				TPtr8 bp(b->Des());
				f.Read(bp);
				TInt target = 0;
				TLex8 lex(bp);
				while (!lex.Eos())
					{
					lex.SkipSpace();
					TUint32 u = 0;
					if (lex.Val(u, EHex) != KErrNone)
						break;
					for (TInt i = target; i < iApps.Count(); i++)
						if ((TUint32)iApps[i]->iUid.iUid == u)
							{
							CEtApp* a = iApps[i];
							iApps.Remove(i);
							iApps.Insert(a, target++);
							break;
							}
					}
				delete b;
				}
			f.Close();
			}
		fs.Close();
		}
	void SaveOrder()
		{
		RFs fs;
		if (fs.Connect() != KErrNone)
			return;
		fs.CreatePrivatePath(EDriveC);
		fs.SetSessionToPrivate(EDriveC);
		RFile f;
		if (f.Replace(fs, _L("order.txt"), EFileWrite) == KErrNone)
			{
			for (TInt i = 0; i < iApps.Count(); i++)
				{
				TBuf8<16> l;
				l.Format(_L8("%08x\n"), iApps[i]->iUid.iUid);
				f.Write(l);
				}
			f.Close();
			}
		fs.Close();
		RDebug::Printf("PH: [etesianhome] apps order saved (%d)", iApps.Count());
		}
	TRect DoneRect() const { return TRect(TPoint(iW / 2 - iBtnW - Sc(6), iH - iBottomH + (iBottomH - iBtnH) / 2), TSize(iBtnW, iBtnH)); }
	TRect CloseAllRect() const { return TRect(TPoint(iW / 2 + Sc(6), iH - iBottomH + (iBottomH - iBtnH) / 2), TSize(iBtnW, iBtnH)); }
	TInt HitTest(const TPoint& aP) const
		{
		if (aP.iY < iStatusH || aP.iY >= iH - iBottomH)
			return -1;
		for (TInt i = 0; i < Items(); i++)
			if (CellRect(i).Contains(aP))
				return i;
		return -1;
		}
	void EnsureVisible()
		{
		if (iSel < 0)
			return;
		const TInt top = (iSel / Cols()) * RowH(), bottom = top + RowH();
		if (top < iScroll)
			iScroll = top;
		else if (bottom > iScroll + GridHeight())
			iScroll = bottom - GridHeight();
		if (iScroll > MaxScroll())
			iScroll = MaxScroll();
		if (iScroll < 0)
			iScroll = 0;
		}
	void SetPage(TInt aPage)
		{
		if (aPage < EPageEvents) aPage = EPageOpen;		// paging wraps (guide 20-phone, Home views)
		if (aPage > EPageOpen) aPage = EPageEvents;
		iPage = aPage;
		iEdit = EFalse;
		iEditApps = EFalse;
		iDragApp = -1;
		iScroll = 0;
		iSel = -1;
		if (iPage == EPageOpen)
			RefreshOpen();
		else if (iPage == EPageEvents)
			RefreshEvents();
		DrawNow();
		}
	// the Menu key: back to the first page of the grid (short press) or to Open apps (long press)
	void ShowApplications() { SetPage(EPageApps); }
	void ShowOpenApps() { SetPage(EPageOpen); }
	void MenuDown()
		{
		CaptureForeground();		// before anything changes on screen: the foreground application's last frame
		iMenuLong = EFalse;
		iMenuTimer->Cancel();
		iMenuTimer->Start(600000, 600000, TCallBack(MenuLongTick, this));
		}
	// a key up: true if it was a short press (the caller brings Home to the front)
	TBool MenuUp()
		{
		const TBool wasLong = iMenuLong;
		iMenuTimer->Cancel();
		if (!wasLong)
			ShowApplications();
		return !wasLong;
		}
	static TInt MenuLongTick(TAny* aThis)
		{
		CEtHomeContainer* self = static_cast<CEtHomeContainer*>(aThis);
		self->iMenuTimer->Cancel();
		self->iMenuLong = ETrue;
		self->ShowOpenApps();
		TApaTask me(CEikonEnv::Static()->WsSession());
		me.SetWgId(self->iCoeEnv->RootWin().Identifier());
		me.BringToForeground();
		RDebug::Printf("PH: [etesianhome] Menu key long: Open apps");
		return 0;
		}

	// ---- previews: the screen as the foreground application left it
	CFbsBitmap* ScaledL(const CFbsBitmap& aSrc, TInt aW, TInt aH, TBool aDim)
		{
		CFbsBitmap* dst = new (ELeave) CFbsBitmap;
		CleanupStack::PushL(dst);
		User::LeaveIfError(dst->Create(TSize(aW, aH), EColor16MU));
		CFbsBitmapDevice* dev = CFbsBitmapDevice::NewL(dst);
		CleanupStack::PushL(dev);
		CFbsBitGc* gc = NULL;
		User::LeaveIfError(dev->CreateContext(gc));
		CleanupStack::PushL(gc);
		gc->DrawBitmap(TRect(0, 0, aW, aH), &aSrc, TRect(TPoint(0, 0), aSrc.SizeInPixels()));
		if (aDim)
			{
			// 50 % black over the preview: halve every channel
			dst->LockHeap();
			TUint8* p = (TUint8*)dst->DataAddress();
			const TInt stride = CFbsBitmap::ScanLineLength(aW, EColor16MU);
			for (TInt y = 0; y < aH; y++)
				{
				TUint32* row = (TUint32*)(p + y * stride);
				for (TInt x = 0; x < aW; x++)
					row[x] = (row[x] >> 1) & 0x7F7F7F7F;
				}
			dst->UnlockHeap();
			}
		CleanupStack::PopAndDestroy(2, dev);		// gc, dev
		CleanupStack::Pop(dst);
		return dst;
		}
	// ---- the app row of the bottom swipe and hold (guide 23): one icon per grid column (4), the tile size + 36 dp high, along the bottom
	TRect RowStrip(const TSize& aScreen) const
		{
		const TInt h = iTile + Sc(36);
		return TRect(0, aScreen.iHeight - h, aScreen.iWidth, aScreen.iHeight);
		}
	TRect RowCell(const TRect& aStrip, TInt aI) const
		{
		const TInt cw = aStrip.Width() / 4;
		return TRect(aStrip.iTl.iX + aI * cw + (cw - iTile) / 2, aStrip.iTl.iY + Sc(18), aStrip.iTl.iX + aI * cw + (cw - iTile) / 2 + iTile, aStrip.iTl.iY + Sc(18) + iTile);
		}
	void DrawRow(CWindowGc& aGc, const TRect& aStrip) const
		{
		aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
		aGc.SetPenStyle(CGraphicsContext::ENullPen);
		for (TInt i = 0; i < 4 && i < iApps.Count(); i++)
			{
			const TRect c = RowCell(aStrip, i);
			aGc.BitBltMasked(c.iTl, &iTileBmp, TRect(TPoint(0, 0), TSize(iTile, iTile)), &iMaskBmp, ETrue);
			DrawIcon(aGc, *iApps[i], c, 62);
			}
		}
	TInt RowHit(const TRect& aStrip, const TPoint& aP) const
		{
		for (TInt i = 0; i < 4 && i < iApps.Count(); i++)
			if (RowCell(aStrip, i).Contains(aP))
				return i;
		return -1;
		}
	void LaunchRow(TInt aIndex) { Launch(aIndex); }
	void CaptureForeground()
		{
		RWsSession& ws = CEikonEnv::Static()->WsSession();
		const TInt fg = ws.GetFocusWindowGroup();
		if (fg <= 0 || fg == iCoeEnv->RootWin().Identifier())
			return;
		TRAPD(e, DoCaptureL(fg));
		if (e != KErrNone)
			RDebug::Printf("PH: [etesianhome] preview of %d: leave %d", fg, e);
		}
	void DoCaptureL(TInt aWgId)
		{
		CWsScreenDevice* sd = iCoeEnv->ScreenDevice();
		CFbsBitmap* full = new (ELeave) CFbsBitmap;
		CleanupStack::PushL(full);
		User::LeaveIfError(full->Create(sd->SizeInPixels(), sd->DisplayMode()));
		TInt r = sd->CopyScreenToBitmap(full);
		RDebug::Printf("PH: [etesianhome] preview of wg %d: copy %d (%dx%d)", aWgId, r, full->SizeInPixels().iWidth, full->SizeInPixels().iHeight);
		User::LeaveIfError(r);
		CEtThumb* t = new (ELeave) CEtThumb;
		CleanupStack::PushL(t);
		t->iWgId = aWgId;
		t->iBmp = ScaledL(*full, iPrevW, iPrevH, EFalse);
		t->iDim = ScaledL(*full, iPrevW, iPrevH, ETrue);
		for (TInt i = 0; i < iThumbs.Count(); i++)
			if (iThumbs[i]->iWgId == aWgId)
				{
				delete iThumbs[i];
				iThumbs.Remove(i);
				break;
				}
		iThumbs.AppendL(t);
		CleanupStack::Pop(t);
		CleanupStack::PopAndDestroy(full);
		}
	const CEtThumb* ThumbFor(TInt aWgId) const
		{
		for (TInt i = 0; i < iThumbs.Count(); i++)
			if (iThumbs[i]->iWgId == aWgId)
				return iThumbs[i];
		return NULL;
		}

	// ---- Open apps: the stock task switcher's list (tsfswdatalist.cpp, tsfshiddenapplist.cpp)
	TBool OnHiddenList(TUid aUid) const
		{
		for (TInt i = 0; i < (TInt)(sizeof(KAlwaysHiddenUids) / sizeof(TUint32)); i++)
			if (KAlwaysHiddenUids[i] == aUid.iUid)
				return ETrue;
		TBuf<50> hidden;		// KUikAppHiddenList: pairs of 16-bit halves of a UID, 0 ends the list
		if (RProperty::Get(KPSUidUikonCat, KUikAppHiddenList, hidden) == KErrNone)
			for (TInt i = 0; i + 1 < hidden.Length(); i += 2)
				{
				const TUint32 u = ((TUint32)hidden[i] << 16) | hidden[i + 1];
				if (u == 0)
					break;
				if (u == aUid.iUid)
					return ETrue;
				}
		return EFalse;
		}
	void RefreshOpen()
		{
		RWsSession& ws = iEikonEnv->WsSession();
		const TInt ownWg = iCoeEnv->RootWin().Identifier();
		TRAPD(e, DoRefreshOpenL(ws, ownWg));
		if (e != KErrNone)
			RDebug::Printf("PH: [etesianhome] open apps: leave %d", e);
		// previews of groups that are gone
		for (TInt i = iThumbs.Count() - 1; i >= 0; i--)
			{
			TBool live = EFalse;
			for (TInt j = 0; j < iCards.Count(); j++)
				if (iCards[j]->iWgId == iThumbs[i]->iWgId)
					live = ETrue;
			if (!live)
				{
				delete iThumbs[i];
				iThumbs.Remove(i);
				}
			}
		if (iScroll > MaxScroll())
			iScroll = MaxScroll();
		}
	void DoRefreshOpenL(RWsSession& aWs, TInt aOwnWg)
		{
		CAknTaskList* tl = CAknTaskList::NewLC(aWs);
		const RArray<RWsSession::TWindowGroupChainInfo>& chain = tl->WgArray();
		iCards.ResetAndDestroy();
		TInt skipped = 0;
		for (TInt i = 0; i < chain.Count(); i++)
			{
			const RWsSession::TWindowGroupChainInfo& info = chain[i];
			if (info.iParentId > 0 || info.iId == aOwnWg)
				continue;		// embedded groups belong to their root; Home itself is not a card
			CApaWindowGroupName* gn = NULL;
			TRAPD(ge, gn = CApaWindowGroupName::NewL(aWs, info.iId));
			if (ge != KErrNone || !gn)
				continue;
			CleanupStack::PushL(gn);
			const TUid uid = gn->AppUid();
			// the stock rules: a uid; the window group not hidden; not on the hidden / always-hidden lists; an application apparc knows
			// on the main screen (screen number 0, or -1 for a console app)
			TInt screenNo = 0;
			const TBool known = uid.iUid != 0 && uid != KEtesianHomeUid && iLs.GetDefaultScreenNumber(screenNo, uid) == KErrNone && (screenNo == 0 || screenNo == -1);
			if (known && !gn->Hidden() && !OnHiddenList(uid))
				{
				CEtCard* c = new (ELeave) CEtCard;
				CleanupStack::PushL(c);
				c->iWgId = info.iId;
				c->iUid = uid;
				c->iApp = FindApp(uid);
				if (!c->iApp)
					{
					// an app the application list leaves out (hidden registration): its own caption and icon
					c->iOwnApp = new (ELeave) CEtApp;
					c->iOwnApp->iUid = uid;
					c->iOwnApp->iCaption = gn->Caption().AllocL();
					TRAP_IGNORE(LoadIconL(*c->iOwnApp));
					c->iApp = c->iOwnApp;
					}
				c->iName = c->iApp->iCaption->AllocL();
				iCards.AppendL(c);
				CleanupStack::Pop(c);
				}
			else
				skipped++;
			CleanupStack::PopAndDestroy(gn);
			}
		CleanupStack::PopAndDestroy(tl);
		RDebug::Printf("PH: [etesianhome] open apps: %d (%d skipped by the stock rules)", iCards.Count(), skipped);
		}
	// ---- Open apps: pinch levels (guide 20-phone: carousel, 2, 3 and 4 per row). A pinch out zooms in (fewer, larger cards), a pinch in zooms out.
	void SetCardLevel(TInt aLevel)
		{
		if (aLevel < 1) aLevel = 1;
		if (aLevel > 4) aLevel = 4;
		if (aLevel == iCardCols && iCardLevel == aLevel)
			return;
		iCardLevel = aLevel;
		Layout(Rect());
		iScroll = 0;
		DrawNow();
		RDebug::Printf("PH: [etesianhome] Open apps level: %d per row", iCardCols);
		}
	void PinchStep(TInt aDirection)		// +1 = zoom in (fewer columns)
		{
		SetCardLevel(iCardCols - aDirection);
		}
	// the preview of the application in front is taken every 2.5 s while Home is in the background, so every card has one
	void WatchPreviews(TBool aOn)
		{
		iPreviewTimer->Cancel();
		if (aOn)
			iPreviewTimer->Start(2500000, 2500000, TCallBack(PreviewTick, this));
		}
	static TInt PreviewTick(TAny* aThis)
		{
		static_cast<CEtHomeContainer*>(aThis)->CaptureForeground();
		return 0;
		}
	// after a swipe closed the front application: its window group goes a moment later, so the Open apps list is read again until it has
	void WatchOpenApps()
		{
		iWatchLeft = 12;
		iWatch->Cancel();
		iWatch->Start(250000, 250000, TCallBack(WatchTick, this));
		}
	static TInt WatchTick(TAny* aThis)
		{
		CEtHomeContainer* self = static_cast<CEtHomeContainer*>(aThis);
		if (self->iPage == EPageOpen && !self->iDragging && !self->iEdit)
			{
			self->RefreshOpen();
			self->DrawNow();
			}
		if (--self->iWatchLeft <= 0)
			self->iWatch->Cancel();
		return 0;
		}
	void CloseCard(TInt aIndex)
		{
		if (aIndex < 0 || aIndex >= iCards.Count())
			return;
		TApaTask t(iEikonEnv->WsSession());
		t.SetWgId(iCards[aIndex]->iWgId);
		RDebug::Printf("PH: [etesianhome] close %08x", iCards[aIndex]->iUid.iUid);
		iClosingWg = iCards[aIndex]->iWgId;
		t.EndTask();
		// a polite close first; what is still there 1.5 s later is killed
		iCloseTimer->Cancel();
		iCloseTimer->Start(1500000, 1500000, TCallBack(CloseTick, this));
		}
	void CloseAll()
		{
		for (TInt i = 0; i < iCards.Count(); i++)
			{
			TApaTask t(iEikonEnv->WsSession());
			t.SetWgId(iCards[i]->iWgId);
			t.EndTask();
			}
		RDebug::Printf("PH: [etesianhome] close all (%d)", iCards.Count());
		iClosingWg = -1;
		iCloseTimer->Cancel();
		iCloseTimer->Start(1500000, 1500000, TCallBack(CloseTick, this));
		}
	static TInt CloseTick(TAny* aThis)
		{
		CEtHomeContainer* self = static_cast<CEtHomeContainer*>(aThis);
		self->iCloseTimer->Cancel();
		self->KillClosing();
		return 0;
		}
	void KillClosing()
		{
		const TInt wg = iClosingWg;
		iClosingWg = 0;
		const TInt before = iCards.Count();
		RefreshOpen();
		for (TInt i = 0; i < iCards.Count(); i++)
			if (wg == -1 || iCards[i]->iWgId == wg)
				{
				TApaTask t(iEikonEnv->WsSession());
				t.SetWgId(iCards[i]->iWgId);
				t.KillTask();
				RDebug::Printf("PH: [etesianhome] killed %08x", iCards[i]->iUid.iUid);
				}
		RefreshOpen();
		(void)before;
		DrawNow();
		}
	void ActivateCard(TInt aIndex)
		{
		if (aIndex < 0 || aIndex >= iCards.Count())
			return;
		TApaTask t(iEikonEnv->WsSession());
		t.SetWgId(iCards[aIndex]->iWgId);
		t.BringToForeground();
		RDebug::Printf("PH: [etesianhome] card foreground %08x", iCards[aIndex]->iUid.iUid);
		}

	// ---- Events: what the notifier sources publish that an application may read
	// the lock screen's cards: the same two sources as the Events page
	TInt LockCardCount() const { return EventCount(); }
	void LockCard(TInt aI, TDes& aTitle, TDes& aSub) const
		{
		TInt row = 0;
		for (TInt k = 0; k < 2; k++)
			{
			const TInt n = k == 0 ? iMissed : iUnread;
			if (n <= 0)
				continue;
			if (row++ == aI)
				{
				aTitle.Copy(k == 0 ? TPtrC(_L("Missed calls")) : TPtrC(_L("Messages")));
				aSub.Format(k == 0 ? TPtrC(_L("%d missed")) : TPtrC(_L("%d unread")), n);
				return;
				}
			}
		}
	void RefreshEvents()
		{
		iMissed = 0;
		iUnread = 0;
		CRepository* repo = NULL;
		TRAPD(e, repo = CRepository::NewL(KCRUidLogs));
		if (e == KErrNone && repo)
			{
			if (repo->Get(KLogsNewMissedCalls, iMissed) != KErrNone)
				iMissed = 0;
			delete repo;
			}
		TInt n = 0;
		TRAP(e, n = CountUnreadL());
		iUnread = e == KErrNone ? n : 0;
		RDebug::Printf("PH: [etesianhome] events: missed calls %d, unread messages %d (msv %d)", iMissed, iUnread, e);
		}
	// the unread messages of the Inbox: a synchronous message server session
	TInt CountUnreadL()
		{
		CEtMsvObserver obs;
		CMsvSession* s = CMsvSession::OpenSyncL(obs);
		CleanupStack::PushL(s);
		CMsvEntry* inbox = CMsvEntry::NewL(*s, KMsvGlobalInBoxIndexEntryId, TMsvSelectionOrdering());
		CleanupStack::PushL(inbox);
		CMsvEntrySelection* sel = inbox->ChildrenL();
		CleanupStack::PushL(sel);
		TInt n = 0;
		for (TInt i = 0; i < sel->Count(); i++)
			{
			const TMsvEntry& te = inbox->ChildDataL(sel->At(i));
			if (te.Unread() && te.iType.iUid == KUidMsvMessageEntryValue)
				n++;
			}
		CleanupStack::PopAndDestroy(3, s);		// sel, inbox, s
		return n;
		}
	TInt EventCount() const { return (iMissed > 0 ? 1 : 0) + (iUnread > 0 ? 1 : 0); }
	TInt EventsTop() const { return iStatusH + Sc(8) + iF[EFDay]->HeightInPixels() + iF[EFDate]->HeightInPixels() + Sc(8) + iF[EFXsB]->HeightInPixels() + Sc(6); }
	TRect EventRect(TInt aIndex) const
		{
		const TInt h = Sc(53);		// list item 80 n9px
		return TRect(TPoint(iMargin, EventsTop() + aIndex * (h + Sc(5))), TSize(iW - 2 * iMargin, h));
		}

	void Launch(TInt aIndex)
		{
		if (aIndex < 0 || aIndex >= iApps.Count())
			return;
		LaunchUid(iApps[aIndex]->iUid);
		}
	void LaunchUid(TUid uid)
		{
		// already running: bring its task to the front; else start it
		TApaTaskList tl(iEikonEnv->WsSession());
		TApaTask task = tl.FindApp(uid);
		if (task.Exists())
			{
			task.BringToForeground();
			RDebug::Printf("PH: [etesianhome] foreground %08x", uid.iUid);
			return;
			}
		TApaAppInfo info;
		TInt r = iLs.GetAppInfo(info, uid);
		if (r == KErrNone)
			{
			CApaCommandLine* cmd = CApaCommandLine::NewLC();
			cmd->SetExecutableNameL(info.iFullName);
			cmd->SetCommandL(EApaCommandRun);
			r = iLs.StartApp(*cmd);
			CleanupStack::PopAndDestroy(cmd);
			}
		RDebug::Printf("PH: [etesianhome] launch %08x r=%d", uid.iUid, r);
		}

	// ---- drawing
	TInt Baseline(const CFont* aFont, const TRect& aR) const { return (aR.Height() + aFont->AscentInPixels() - aFont->DescentInPixels()) / 2; }
	// a caption wider than its box is cut
	void Fit(TDes& aText, const CFont* aFont, TInt aWidth) const
		{
		while (aText.Length() > 1 && aFont->TextWidthInPixels(aText) > aWidth)
			{
			aText.SetLength(aText.Length() - 1);
			aText.Trim();
			}
		}
	// a squircle outline (n = 2.8): the selection highlight; guide 20: never a rounded rectangle for these
	void DrawSquircleRing(CWindowGc& gc, const TRect& aR) const
		{
		const TInt N = 48;
		TPoint pts[N + 1];
		const TReal hw = aR.Width() / 2.0, hh = aR.Height() / 2.0;
		for (TInt i = 0; i <= N; i++)
			{
			TReal a = 2 * KPi * i / N, c = 0, s = 0, pc = 0, ps = 0;
			Math::Cos(c, a);
			Math::Sin(s, a);
			const TReal ac = c < 0 ? -c : c, as = s < 0 ? -s : s;
			Math::Pow(pc, ac, 2.0 / 2.8);
			Math::Pow(ps, as, 2.0 / 2.8);
			pts[i] = TPoint(aR.iTl.iX + (TInt)(hw + hw * (c < 0 ? -pc : pc)), aR.iTl.iY + (TInt)(hh + hh * (s < 0 ? -ps : ps)));
			}
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColAccent));
		gc.SetPenSize(TSize(2, 2));
		for (TInt i = 0; i < N; i++)
			gc.DrawLine(pts[i], pts[i + 1]);
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		}
	// the battery (a rounded body, 7 levels) and the signal bars, right to left from aRight
	void DrawIndicators(CWindowGc& gc, TInt aRight) const
		{
		const TInt bh = Sc(9), bw = Sc(18), cy = iStatusH / 2;
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		if (iBatt >= 0)
			{
			const TRect body(aRight - bw, cy - bh / 2, aRight, cy + bh / 2);
			gc.SetBrushColor(Rgb(0x505050));
			gc.DrawRoundRect(body, TSize(Sc(2), Sc(2)));
			gc.SetBrushColor(Rgb(iBatt <= 1 && !iCharging ? 0xDA201E : (iCharging ? 0x62B700 : KColFg)));
			const TInt fill = (bw - Sc(4)) * (iBatt + 1) / 8;
			gc.DrawRect(TRect(body.iTl.iX + Sc(2), body.iTl.iY + Sc(2), body.iTl.iX + Sc(2) + fill, body.iBr.iY - Sc(2)));
			gc.SetBrushColor(Rgb(0x505050));
			gc.DrawRect(TRect(body.iBr.iX, cy - Sc(2), body.iBr.iX + Sc(2), cy + Sc(2)));
			aRight -= bw + Sc(10);
			}
		if (iBars >= 0)
			{
			const TInt lit = iBars > 7 ? 5 : (iBars * 5 + 6) / 7;
			const TInt step = Sc(4), w = Sc(3);
			for (TInt i = 0; i < 5; i++)
				{
				const TInt h = Sc(3) + i * Sc(2);
				const TInt x = aRight - (5 - i) * step;
				gc.SetBrushColor(i < lit ? Rgb(KColFg) : Rgb(0x505050));
				gc.DrawRect(TRect(x, cy + Sc(5) - h, x + w, cy + Sc(5)));
				}
			}
		}
	// the status menu (a tap on the status bar, guide 20-phone): the battery, the Control Panel and the Etesian shell settings
	void SetStatusActions(MEtStatusActions* aAct) { iStatusAct = aAct; }
	TInt MenuRows() const { return 4; }
	TRect MenuRow(TInt aI) const { return TRect(0, iStatusH + aI * Sc(44), iW, iStatusH + (aI + 1) * Sc(44)); }
	void DrawStatusMenu(CWindowGc& gc) const
		{
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(0x141414));
		gc.DrawRect(TRect(0, iStatusH, iW, iStatusH + MenuRows() * Sc(44)));
		gc.UseFont(iF[EFBody]);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg));
		for (TInt i = 0; i < MenuRows(); i++)
			{
			TBuf<40> t;
			if (i == 0)
				{
				if (iBatt >= 0)
					t.Format(_L("Battery %d%%%S"), (iBatt + 1) * 100 / 8, iCharging ? &KMenuCharging : &KMenuNone);
				else
					t.Copy(_L("Battery"));
				}
			else
				t.Copy(i == 1 ? TPtrC(_L("Settings")) : (i == 2 ? TPtrC(_L("Etesian shell")) : TPtrC(_L("Lock phone"))));
			const TRect r = MenuRow(i);
			gc.DrawText(t, TRect(r.iTl.iX + iMargin, r.iTl.iY, r.iBr.iX - iMargin, r.iBr.iY), Baseline(iF[EFBody], r), CGraphicsContext::ELeft);
			}
		gc.DiscardFont();
		}
	void LaunchUid(TUint32 aUid)
		{
		for (TInt i = 0; i < iApps.Count(); i++)
			if (iApps[i]->iUid.iUid == (TInt)aUid)
				{
				Launch(i);
				return;
				}
		}
	void DrawApps(CWindowGc& gc) const
		{
		gc.UseFont(iF[EFLabel]);
		for (TInt i = 0; i < iApps.Count(); i++)
			{
			const TRect cell = CellRect(i);
			if (cell.iBr.iY <= iStatusH || cell.iTl.iY >= iH - iBottomH)
				continue;
			const TPoint tp(cell.iTl.iX + (iCellW - iTile) / 2, cell.iTl.iY + Sc(4));
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.BitBltMasked(tp, &iTileBmp, TRect(TPoint(0, 0), TSize(iTile, iTile)), &iMaskBmp, ETrue);
			const CEtApp* a = iApps[i];
			if (iEditApps && i == iDragApp)
				continue;		// drawn under the finger below
			DrawIcon(gc, *a, TRect(tp, TSize(iTile, iTile)), 62);
			if (iEditApps)
				DrawSquircleRing(gc, TRect(tp.iX - 2, tp.iY - 2, tp.iX + iTile + 2, tp.iY + iTile + 2));
			if (i == iSel)
				DrawSquircleRing(gc, TRect(tp.iX - 3, tp.iY - 3, tp.iX + iTile + 3, tp.iY + iTile + 3));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			TBuf<32> cap(a->iCaption->Left(32));
			Fit(cap, iF[EFLabel], iCellW - 6);
			const TRect lr(cell.iTl.iX + 2, tp.iY + iTile + Sc(4), cell.iBr.iX - 2, tp.iY + iTile + Sc(4) + iLabelH);
			gc.DrawText(cap, lr, Baseline(iF[EFLabel], lr), CGraphicsContext::ECenter);
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			}
		gc.DiscardFont();
		if (iEditApps && iDragApp >= 0 && iDragApp < iApps.Count())
			{
			const TRect box(iDragPos.iX - iTile / 2, iDragPos.iY - iTile / 2, iDragPos.iX + iTile / 2, iDragPos.iY + iTile / 2);
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.BitBltMasked(box.iTl, &iTileBmp, TRect(TPoint(0, 0), TSize(iTile, iTile)), &iMaskBmp, ETrue);
			DrawIcon(gc, *iApps[iDragApp], box, 62);
			DrawSquircleRing(gc, box);
			}
		}
	// the app's icon centred in aBox, taking aPercent of its width
	void DrawIcon(CWindowGc& gc, const CEtApp& aApp, const TRect& aBox, TInt aPercent) const
		{
		const TInt g = aBox.Width() * aPercent / 100;
		const TRect dst(TPoint(aBox.iTl.iX + (aBox.Width() - g) / 2, aBox.iTl.iY + (aBox.Height() - g) / 2), TSize(g, g));
		if (aApp.iBmp)
			{
			const TSize sz = aApp.iBmp->SizeInPixels();
			if (aApp.iMask)
				gc.DrawBitmapMasked(dst, aApp.iBmp, TRect(TPoint(0, 0), sz), aApp.iMask, ETrue);
			else
				gc.DrawBitmap(dst, aApp.iBmp);
			}
		else if (aApp.iIcon && aApp.iIcon->Mask())
			gc.DrawBitmapMasked(dst, aApp.iIcon, TRect(TPoint(0, 0), aApp.iIcon->SizeInPixels()), aApp.iIcon->Mask(), ETrue);
		}
	void DrawLitButton(CWindowGc& gc, const TRect& aR, const TDesC& aText) const
		{
		gc.SetBrushStyle(CGraphicsContext::ENullBrush);
		gc.BitBltMasked(aR.iTl, &iBtnBmp, TRect(TPoint(0, 0), aR.Size()), &iBtnMask, ETrue);
		gc.UseFont(iF[EFBtn]);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg));
		gc.DrawText(aText, aR, Baseline(iF[EFBtn], aR), CGraphicsContext::ECenter);
		gc.DiscardFont();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		}
	void DrawOpen(CWindowGc& gc) const
		{
		if (iCards.Count() == 0)
			{
			gc.UseFont(iF[EFBody]);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg2));
			const TRect r(0, iStatusH, iW, iH - iBottomH);
			gc.DrawText(_L("No open applications"), r, Baseline(iF[EFBody], r), CGraphicsContext::ECenter);
			gc.DiscardFont();
			return;
			}
		for (TInt i = 0; i < iCards.Count(); i++)
			{
			const TRect c = CellRect(i);
			if (c.iBr.iY + iXSize <= iStatusH || c.iTl.iY - iXSize >= iH - iBottomH)
				continue;
			const CEtCard* card = iCards[i];
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(Rgb(KColCard));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColRim));
			gc.SetPenSize(TSize(1, 1));
			gc.DrawRoundRect(c, TSize(8, 8));		// cards: radius 8 (chapter 05)
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			// the preview: the app's last frame (dimmed to 50 % in edit mode), else its icon
			const TRect pv(TPoint(c.iTl.iX + Sc(6), c.iTl.iY + Sc(6)), TSize(iPrevW, iPrevH));
			const CEtThumb* th = ThumbFor(card->iWgId);
			if (th)
				gc.DrawBitmap(pv, iEdit ? th->iDim : th->iBmp);		// scaled: the zoom level can change after the preview was taken
			else if (card->iApp)
				DrawIcon(gc, *card->iApp, pv, 45);
			if (i == iSel)
				{
				gc.SetPenStyle(CGraphicsContext::ESolidPen);
				gc.SetPenColor(Rgb(KColAccent));
				gc.SetPenSize(TSize(2, 2));
				gc.SetBrushStyle(CGraphicsContext::ENullBrush);
				gc.DrawRoundRect(c, TSize(8, 8));
				gc.SetPenStyle(CGraphicsContext::ENullPen);
				}
			// the label: regular, white (guide 20)
			gc.UseFont(iF[EFLabel]);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			TBuf<32> nm(card->iName->Left(32));
			Fit(nm, iF[EFLabel], c.Width() - Sc(12));
			const TRect nr(c.iTl.iX + Sc(6), pv.iBr.iY + Sc(6), c.iBr.iX - Sc(6), pv.iBr.iY + Sc(6) + iLabelH);
			gc.DrawText(nm, nr, Baseline(iF[EFLabel], nr), CGraphicsContext::ECenter);
			gc.DiscardFont();
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			}
		if (iEdit)
			for (TInt i = 0; i < iCards.Count(); i++)
				{
				// the red lit close squircle with a thick white cross, top-right, slightly outside the corner
				const TRect x = CloseRect(i);
				if (x.iBr.iY <= iStatusH || x.iTl.iY >= iH - iBottomH)
					continue;
				gc.SetBrushStyle(CGraphicsContext::ENullBrush);
				gc.BitBltMasked(x.iTl, &iXBmp, TRect(TPoint(0, 0), x.Size()), &iXMask, ETrue);
				gc.SetPenStyle(CGraphicsContext::ESolidPen);
				gc.SetPenColor(Rgb(KColFg));
				gc.SetPenSize(TSize(Sc(3), Sc(3)));
				const TInt m = x.Width() * 3 / 10;
				gc.DrawLine(TPoint(x.iTl.iX + m, x.iTl.iY + m), TPoint(x.iBr.iX - m, x.iBr.iY - m));
				gc.DrawLine(TPoint(x.iBr.iX - m, x.iTl.iY + m), TPoint(x.iTl.iX + m, x.iBr.iY - m));
				gc.SetPenStyle(CGraphicsContext::ENullPen);
				}
		}
	// Events: header = the day (large, light) and the date (light, about 0.85 x), then a section header and the rows
	void DrawEvents(CWindowGc& gc) const
		{
		TTime now;
		now.HomeTime();
		TBuf<32> day, date;
		TRAP_IGNORE(now.FormatL(day, _L("%E")));
		TRAP_IGNORE(now.FormatL(date, _L("%D%M%Y%/0%1%/1%2%/2%3")));
		const TRect dr(iMargin, iStatusH + Sc(8), iW - iMargin, iStatusH + Sc(8) + iF[EFDay]->HeightInPixels());
		gc.UseFont(iF[EFDay]);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg));
		gc.DrawText(day, dr, Baseline(iF[EFDay], dr), CGraphicsContext::ELeft);
		const TRect dt(iMargin, dr.iBr.iY, iW - iMargin, dr.iBr.iY + iF[EFDate]->HeightInPixels());
		gc.UseFont(iF[EFDate]);
		gc.SetPenColor(Rgb(KColFg2));
		gc.DrawText(date, dt, Baseline(iF[EFDate], dt), CGraphicsContext::ELeft);
		if (EventCount() == 0)
			{
			gc.DiscardFont();
			return;			// no empty section and no filler text (guide 16-os-rules 13): the day and the date only
			}
		// section header: a small bold secondary label, right-aligned, with a hairline to its left
		const TRect sh(iMargin, dt.iBr.iY + Sc(8), iW - iMargin, dt.iBr.iY + Sc(8) + iF[EFXsB]->HeightInPixels());
		gc.UseFont(iF[EFXsB]);
		gc.DrawText(_L("Notifications"), sh, Baseline(iF[EFXsB], sh), CGraphicsContext::ERight);
		const TInt lw = iF[EFXsB]->TextWidthInPixels(_L("Notifications"));
		gc.SetPenColor(Rgb(KColRim));
		gc.SetPenSize(TSize(1, 1));
		gc.DrawLine(TPoint(sh.iTl.iX, sh.iTl.iY + sh.Height() / 2), TPoint(sh.iBr.iX - lw - Sc(8), sh.iTl.iY + sh.Height() / 2));
		gc.DiscardFont();
		TInt row = 0;
		for (TInt k = 0; k < 2; k++)
			{
			const TInt n = k == 0 ? iMissed : iUnread;
			if (n <= 0)
				continue;
			const TRect r = EventRect(row++);
			// rows: a lit 44 dp squircle with the app's icon, title 16 bold, summary 14 regular
			const TPoint tp(r.iTl.iX, r.iTl.iY + (r.Height() - iEvSize) / 2);
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.BitBltMasked(tp, &iEvTile, TRect(TPoint(0, 0), TSize(iEvSize, iEvSize)), &iEvMask, ETrue);
			const CEtApp* app = FindApp(k == 0 ? KLogsAppUid : KMessagesAppUid);
			if (app)
				DrawIcon(gc, *app, TRect(tp, TSize(iEvSize, iEvSize)), 62);
			TBuf<48> title, sub;
			if (k == 0)
				{
				title.Copy(_L("Missed calls"));
				sub.Format(_L("%d new"), n);
				}
			else
				{
				title.Copy(_L("Messages"));
				sub.Format(_L("%d unread"), n);
				}
			const TInt tx = tp.iX + iEvSize + iMargin;
			const TRect tr(tx, r.iTl.iY, r.iBr.iX, r.iTl.iY + r.Height() / 2 + Sc(2));
			const TRect sr(tx, r.iTl.iY + r.Height() / 2, r.iBr.iX, r.iBr.iY);
			gc.UseFont(iF[EFTitle]);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			gc.DrawText(title, tr, tr.Height() - Sc(3), CGraphicsContext::ELeft);
			gc.UseFont(iF[EFSum]);
			gc.SetPenColor(Rgb(KColFg2));
			gc.DrawText(sub, sr, iF[EFSum]->AscentInPixels() + Sc(1), CGraphicsContext::ELeft);
			gc.DiscardFont();
			}
		}
	void Draw(const TRect&) const
		{
		CWindowGc& gc = SystemGc();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(KColBg));
		gc.DrawRect(Rect());
		gc.SetClippingRect(TRect(0, iStatusH, iW, iH - iBottomH));
		if (iPage == EPageApps)
			DrawApps(gc);
		else if (iPage == EPageOpen)
			DrawOpen(gc);
		else
			DrawEvents(gc);
		gc.CancelClippingRect();
		// status bar (always black, always visible)
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(KColBg));
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.DrawRect(TRect(0, 0, iW, iStatusH));
		gc.DrawRect(TRect(0, iH - iBottomH, iW, iH));
		const CFont* f = iF[EFLabel];
		gc.UseFont(f);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg2));
		const TInt sb = (iStatusH + f->AscentInPixels() - f->DescentInPixels()) / 2;
		gc.DrawText(_L("Etesian"), TRect(iMargin, 0, iW / 2, iStatusH), sb, CGraphicsContext::ELeft);
		TTime now;
		now.HomeTime();
		TBuf<16> ts;
		TRAP_IGNORE(now.FormatL(ts, _L("%J%:1%T%+B")));
		gc.SetPenColor(Rgb(KColFg));
		gc.DrawText(ts, TRect(iW / 2, 0, iW - iMargin, iStatusH), sb, CGraphicsContext::ERight);
		const TInt tw = f->TextWidthInPixels(ts);
		gc.DiscardFont();
		DrawIndicators(gc, iW - iMargin - tw - Sc(10));
		if (iStatusMenu)
			DrawStatusMenu(gc);
		if (iEditApps)
			{
			DrawLitButton(gc, DoneRect(), _L("Done"));
			return;
			}
		if (iEdit && iPage == EPageOpen)
			{
			// edit mode: the lit Done and Close all buttons take the place of the dots
			DrawLitButton(gc, DoneRect(), _L("Done"));
			DrawLitButton(gc, CloseAllRect(), _L("Close all"));
			return;
			}
		// page dots: Events | Applications | Open apps, the current one lit. Hidden by default (guide 23: "Page dots hidden by default");
		// the swipe, Left/Right and the edge gestures page the views, and paging wraps
		if (!KShowPageDots)
			return;
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		const TInt dr = Sc(3), gap = Sc(14), cy = iH - iBottomH / 2;
		for (TInt d = 0; d < 3; d++)
			{
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(d == iPage ? Rgb(KColFg) : Rgb(0x505050));
			const TInt cx = iW / 2 + (d - 1) * gap;
			gc.DrawEllipse(TRect(cx - dr, cy - dr, cx + dr, cy + dr));
			}
		}

	// ---- input
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKey, TEventCode aType)
		{
		if (aType != EEventKey)
			return EKeyWasNotConsumed;
		if (iPage == EPageEvents)
			{
			if (aKey.iCode == EKeyRightArrow)
				SetPage(EPageApps);
			else if (aKey.iCode == EKeyEnter || aKey.iCode == EKeyDevice3)
				OpenEvent(0);
			else
				return EKeyWasNotConsumed;
			return EKeyWasConsumed;
			}
		if (iPage == EPageOpen && aKey.iCode >= '1' && aKey.iCode <= '4')
			{
			SetCardLevel(aKey.iCode - '0');
			return EKeyWasConsumed;
			}
		const TInt cols = Cols();
		if (Items() == 0)
			{
			if (aKey.iCode == EKeyLeftArrow) SetPage(iPage - 1);
			else if (aKey.iCode == EKeyRightArrow) SetPage(iPage + 1);
			else return EKeyWasNotConsumed;
			return EKeyWasConsumed;
			}
		if (iSel < 0)
			iSel = 0;
		else
			switch (aKey.iCode)
				{
				case EKeyLeftArrow:
					if (iSel % cols == 0) { SetPage(iPage - 1); return EKeyWasConsumed; }
					iSel--;
					break;
				case EKeyRightArrow:
					if (iSel % cols == cols - 1 || iSel == Items() - 1) { SetPage(iPage + 1); return EKeyWasConsumed; }
					iSel++;
					break;
				case EKeyUpArrow: if (iSel >= cols) iSel -= cols; break;
				case EKeyDownArrow: if (iSel + cols < Items()) iSel += cols; else iSel = Items() - 1; break;
				case EKeyBackspace:
				case EKeyDelete:
					// Open apps: the first press enters edit mode, a press in edit mode closes the selected card
					if (iPage != EPageOpen) return EKeyWasNotConsumed;
					if (!iEdit) iEdit = ETrue; else CloseCard(iSel);
					break;
				case EKeyEscape:
					if (iEdit) { iEdit = EFalse; break; }
					return EKeyWasNotConsumed;
				case EKeyEnter:
				case EKeyDevice3:
					if (iPage == EPageOpen) ActivateCard(iSel); else Launch(iSel);
					return EKeyWasConsumed;
				default:
					return EKeyWasNotConsumed;
				}
		EnsureVisible();
		DrawNow();
		return EKeyWasConsumed;
		}
	void OpenEvent(TInt aIndex)
		{
		TInt row = 0;
		for (TInt k = 0; k < 2; k++)
			if ((k == 0 ? iMissed : iUnread) > 0)
				{
				if (row++ == aIndex)
					LaunchUid(k == 0 ? KLogsAppUid : KMessagesAppUid);
				}
		}
	static TInt HoldTick(TAny* aThis)
		{
		CEtHomeContainer* self = static_cast<CEtHomeContainer*>(aThis);
		self->iHoldTimer->Cancel();
		if (self->iPage == EPageOpen && !self->iDragging && !self->iSwiping && self->iDownHit >= 0)
			{
			self->iEdit = ETrue;
			self->iHeld = ETrue;		// the finger lifting is not a tap
			self->DrawNow();
			}
		else if (self->iPage == EPageApps && !self->iEditApps && !self->iDragging && !self->iSwiping && self->iDownHit >= 0)
			{
			self->iEditApps = ETrue;		// the guide: a long press on the grid starts edit mode; the icon under the finger can be dragged
			self->iDragApp = self->iDownHit;
			self->iDragPos = self->iDownPos;
			self->iHeld = ETrue;
			RDebug::Printf("PH: [etesianhome] apps edit mode");
			self->DrawNow();
			}
		return 0;
		}
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		const TAdvancedPointerEvent* adv = aE.AdvancedPointerEvent();
		if (adv && adv->PointerNumber() > 0 && adv->PointerNumber() < 2 + 0 && iPage == EPageOpen)
			{
			const TInt n = adv->PointerNumber();
			iPinch[n] = aE.iPosition;
			iPinchDown[n] = aE.iType != TPointerEvent::EButton1Up;
			return;
			}
		if (adv && iPage == EPageOpen && adv->PointerNumber() == 0)
			{
			iPinch[0] = aE.iPosition;
			iPinchDown[0] = aE.iType != TPointerEvent::EButton1Up;
			if (iPinchDown[0] && iPinchDown[1])
				{
				const TInt dx = iPinch[0].iX - iPinch[1].iX, dy = iPinch[0].iY - iPinch[1].iY;
				const TInt dist = dx * dx + dy * dy;
				if (iPinchStart == 0)
					iPinchStart = dist;
				else if (!iPinchDone && dist * 10 > iPinchStart * 18)
					{ iPinchDone = ETrue; PinchStep(+1); }
				else if (!iPinchDone && dist * 10 < iPinchStart * 5)
					{ iPinchDone = ETrue; PinchStep(-1); }
				return;
				}
			if (!iPinchDown[1])
				{
				iPinchStart = 0;
				iPinchDone = EFalse;
				}
			}
		switch (aE.iType)
			{
			case TPointerEvent::EButton1Down:
				iDownPos = aE.iPosition;
				iDownScroll = iScroll;
				iDragging = EFalse;
				iSwiping = EFalse;
				iHeld = EFalse;
				iDownHit = iPage == EPageEvents ? -1 : HitTest(aE.iPosition);
				if ((iPage == EPageOpen && !iEdit) || (iPage == EPageApps && !iEditApps))
					{
					iHoldTimer->Cancel();
					iHoldTimer->Start(700000, 700000, TCallBack(HoldTick, this));		// a long press = edit mode
					}
				break;
			case TPointerEvent::EDrag:
				{
				if (iEditApps && iDragApp >= 0)
					{
					iDragPos = aE.iPosition;
					DrawNow();
					break;
					}
				const TInt dy = aE.iPosition.iY - iDownPos.iY;
				const TInt dx = aE.iPosition.iX - iDownPos.iX;
				if (!iDragging && !iSwiping && (dx > Sc(12) || dx < -Sc(12)) && (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy))
					{
					iSwiping = ETrue;		// a horizontal gesture changes the page when the finger lifts
					iHoldTimer->Cancel();
					}
				if (!iSwiping && (iDragging || dy > Sc(8) || dy < -Sc(8)))
					{
					iDragging = ETrue;
					iHoldTimer->Cancel();
					TInt s = iDownScroll - dy;
					if (s > MaxScroll()) s = MaxScroll();
					if (s < 0) s = 0;
					if (s != iScroll)
						{
						iScroll = s;
						DrawNow();
						}
					}
				break;
				}
			case TPointerEvent::EButton1Up:
				{
				iHoldTimer->Cancel();
				if (iEditApps && iDragApp >= 0)
					{
					const TInt tgt = HitTest(aE.iPosition);
					if (tgt >= 0 && tgt != iDragApp)
						{
						CEtApp* a = iApps[iDragApp];
						iApps.Remove(iDragApp);
						iApps.Insert(a, tgt);
						SaveOrder();
						}
					iDragApp = -1;
					iDragging = iSwiping = iHeld = EFalse;
					DrawNow();
					break;
					}
				const TInt dx = aE.iPosition.iX - iDownPos.iX;
				if (iSwiping && (dx > Sc(48) || dx < -Sc(48)))
					SetPage(dx < 0 ? iPage + 1 : iPage - 1);
				else if (!iDragging && !iSwiping && !iHeld)
					HandleTap(aE.iPosition);
				iDragging = iSwiping = iHeld = EFalse;
				break;
				}
			default:
				break;
			}
		}
	void HandleTap(const TPoint& aP)
		{
		if (aP.iY < iStatusH)
			{
			iStatusMenu = !iStatusMenu;
			DrawNow();
			return;
			}
		if (iEditApps)
			{
			if (DoneRect().Contains(aP))
				{
				iEditApps = EFalse;
				DrawNow();
				}
			return;		// in edit mode an icon does not launch
			}
		if (iStatusMenu)
			{
			iStatusMenu = EFalse;
			for (TInt i = 1; i < MenuRows(); i++)
				if (MenuRow(i).Contains(aP))
					{
					if (i == 3)
						{
						if (iStatusAct)
							iStatusAct->StatusLock();
						}
					else
						LaunchUid(i == 1 ? 0x100058ecu : 0xE7016B52u);
					}
			DrawNow();
			return;
			}
		if (iEdit && iPage == EPageOpen)
			{
			if (DoneRect().Contains(aP)) { iEdit = EFalse; DrawNow(); return; }
			if (CloseAllRect().Contains(aP)) { CloseAll(); return; }
			for (TInt i = 0; i < iCards.Count(); i++)
				if (CloseRect(i).Contains(aP)) { CloseCard(i); return; }
			return;		// in edit mode a card does not launch
			}
		if (KShowPageDots && aP.iY >= iH - iBottomH)
			{
			// a dot: x decides the page
			const TInt gap = Sc(14);
			SetPage(aP.iX < iW / 2 - gap / 2 ? EPageEvents : (aP.iX > iW / 2 + gap / 2 ? EPageOpen : EPageApps));
			return;
			}
		if (iPage == EPageEvents)
			{
			for (TInt i = 0; i < EventCount(); i++)
				if (EventRect(i).Contains(aP))
					OpenEvent(i);
			return;
			}
		const TInt hit = HitTest(aP);
		if (hit < 0 || hit != iDownHit)
			return;
		iSel = hit;
		if (iPage == EPageOpen)
			ActivateCard(hit);
		else
			Launch(hit);
		}
	static TInt ClockTick(TAny* aThis)
		{
		CEtHomeContainer* self = static_cast<CEtHomeContainer*>(aThis);
		self->ReadBattery();
		// the system state is logged when it changes (the shell selector confirms a good start by the Normal state)
		TInt st = -1;
		if (RProperty::Get(TUid::Uid(0x101F8766), 0x41, st) != KErrNone)
			st = -1;
		if (st != self->iLastState)
			{
			self->iLastState = st;
			RDebug::Printf("PH: [etesianhome] system state %d", st);
			}
		if (self->iPage == EPageOpen && !self->iDragging && !self->iEdit)
			self->RefreshOpen();
		else if (self->iPage == EPageEvents)
			self->RefreshEvents();
		self->DrawNow();
		return 0;
		}
	void SignalChanged(TInt aBars) { if (aBars != iBars) { iBars = aBars; DrawNow(); } }
	void ReadBattery()
		{
		const TUid KPowerState = { 0x10205041 };		// KPSUidHWRMPowerState: key 1 battery level 0..7, key 3 charging status
		TInt v = -1, c = 0;
		if (RProperty::Get(KPowerState, 1, v) != KErrNone)
			v = -1;
		RProperty::Get(KPowerState, 3, c);
		iBatt = v;
		iCharging = (c == 1);
		}
	void SizeChangedL(const TRect& aRect) { (void)aRect; }
	// the active theme changed (the Themes application, or the Etesian shell app): the icons are fetched again from the skin
	void SkinChanged()
		{
		TRAP_IGNORE(LoadAppsL());
		iCards.ResetAndDestroy();
		if (iPage == EPageOpen)
			RefreshOpen();
		DrawNow();
		RDebug::Printf("PH: [etesianhome] skin changed: icons reloaded");
		}
	// the screen turned: new rectangle, same tile size, other column counts (the old previews have the old aspect: dropped)
	void Relayout(const TRect& aRect)
		{
		SetRect(aRect);
		iThumbs.ResetAndDestroy();
		Layout(aRect);
		DrawNow();
		RDebug::Printf("PH: [etesianhome] layout %dx%d: %d columns, cards %d per row", iW, iH, iCols, iCardCols);
		}

private:
	RApaLsSession iLs;
	RPointerArray<CEtApp> iApps;
	RPointerArray<CEtCard> iCards;
	RPointerArray<CEtThumb> iThumbs;
	CFbsBitmap iTileBmp, iMaskBmp;		// the lit app squircle
	CFbsBitmap iEvTile, iEvMask;		// the 44 dp one of the Events rows
	CFbsBitmap iBtnBmp, iBtnMask;		// the lit Done / Close all button
	CFbsBitmap iXBmp, iXMask;			// the red close squircle
	CPeriodic* iClock = NULL;
	CPeriodic* iWatch = NULL;
	CPeriodic* iPreviewTimer = NULL;
	TPoint iPinch[2];
	TBool iPinchDown[2] = { EFalse, EFalse }, iPinchDone = EFalse;
	TInt iPinchStart = 0;
	TInt iWatchLeft = 0;
	CPeriodic* iMenuTimer = NULL;
	CPeriodic* iCloseTimer = NULL;
	CPeriodic* iHoldTimer = NULL;
	CFont* iF[EFCount];
	TBool iOwnFonts = EFalse;
	TInt iLastState = -2, iCols = 4, iLabelH = 0, iUnit = 0, iW = 0, iH = 0, iStatusH = 0, iTile = 0, iCellW = 0, iCellH = 0, iBottomH = 0, iMargin = 0;
	TInt iCardCols = 2, iCardW = 0, iCardH = 0, iGap = 0, iClosingWg = 0, iPrevW = 0, iPrevH = 0, iXSize = 0, iBtnW = 0, iBtnH = 0, iEvSize = 0;
	TInt iPage = EPageApps, iMissed = 0, iUnread = 0;
	TInt iScroll = 0, iSel = -1, iDownScroll = 0, iDownHit = -1;
	TPoint iDownPos;
	TBool iDragging = EFalse, iSwiping = EFalse, iMenuLong = EFalse, iEdit = EFalse, iHeld = EFalse;
	TInt iCardLeft = 0;
	TInt iCardLevel = 0;		// the Open apps zoom level set by a pinch or the keys 1..4 (0 = the default for the orientation)
	TInt iBatt = -1, iBars = -1;		// battery level 0..7 (HWRM power state), signal bars (CTelephony); -1 = unknown
	MEtStatusActions* iStatusAct = NULL;
	TBool iCharging = EFalse, iStatusMenu = EFalse, iEditApps = EFalse;
	TInt iDragApp = -1;
	TPoint iDragPos;
	CEtSignal* iSignal = NULL;
	};

// ---- edge gestures (guide 20-phone.md section 1 and 23-phone-behaviour.md "Edge swipes over an app") ----
// On by default: they are the N9's own behaviour, and "pure N9 first" only puts what the N9 did NOT have behind a setting.
// No WSERV plug-in (not installable) and nothing stock replaced: Home runs its own session with an always-in-front, hidden window group that holds four
// thin, fully transparent windows (left, right, bottom, top). They are in front only while Home is in the background. A drag that starts in one of them
// and moves INWARD is a gesture: left / right / bottom minimise (Home, Open apps), top closes the application in front, and bottom + 500 ms still shows the
// Open apps (the peek). Anything else that starts there (a tap, a drag along the edge) is handed back to the application: the strips step aside and the
// pointer sequence is replayed to what is under them, so taps and scrolling near the edges keep working.
// Sizes are millimetres (the guide's dp: 1 dp = 1/160 inch = 0.159 mm): edge zone 18 dp = 2.9 mm, slop 8 dp = 1.3 mm, status bar 24 dp + band 24 dp = 3.8 + 3.8 mm.
class MEtEdgeActions
	{
public:
	virtual void EdgeMinimise() = 0;		// left, right, bottom: Home to the front, Open apps
	virtual TRect EdgeRowStrip(const TSize& aScreen) = 0;			// bottom and hold: the first row of the grid comes up
	virtual void EdgeDrawRow(CWindowGc& aGc, const TRect& aStrip) = 0;
	virtual TInt EdgeRowHit(const TRect& aStrip, const TPoint& aP) = 0;
	virtual void EdgeRowLaunch(TInt aIndex) = 0;
	virtual void EdgeClosed() = 0;			// the front application was asked to close: Home to the front, Open apps
	};

class CEtEdges : public CActive
	{
public:
	enum { ELeft, ERight, EBottom, ETop, ETopBand, ECount };		// ETopBand: the 24 dp under the status bar, between the side zones, also "top"
	static CEtEdges* NewL(MEtEdgeActions& aAct, TInt aHomeWg)
		{
		CEtEdges* e = new (ELeave) CEtEdges(aAct, aHomeWg);
		CleanupStack::PushL(e);
		e->ConstructL();
		CleanupStack::Pop(e);
		return e;
		}
	~CEtEdges()
		{
		Cancel();
		delete iTimer;
		iPoints.Close();
		for (TInt i = 0; i < ECount; i++)
			iWin[i].Close();
		iShade.Close();
		iGroup.Close();
		delete iDev;
		iWs.Close();
		}
	// the strips are in front of the applications only while Home is behind them
	void SetEnabled(TBool aOn)
		{
		iEnabled = aOn;
		if (!aOn)
			HideShade();
		Apply();
		}
	void Relayout()
		{
		Geometry();
		Apply();
		}
private:
	CEtEdges(MEtEdgeActions& aAct, TInt aHomeWg) : CActive(EPriorityStandard), iAct(aAct), iHomeWg(aHomeWg) {}
	void ConstructL()
		{
		CActiveScheduler::Add(this);
		User::LeaveIfError(iWs.Connect());
		iDev = new (ELeave) CWsScreenDevice(iWs);
		User::LeaveIfError(iDev->Construct());
		iGroup = RWindowGroup(iWs);
		User::LeaveIfError(iGroup.Construct(0x45444745, EFalse));		// not focusable: key events stay with the application
		iGroup.EnableReceiptOfFocus(EFalse);
		iGroup.SetOrdinalPosition(0, 1500);							// above the applications (always-at-front is 1000)
		CApaWindowGroupName* gn = CApaWindowGroupName::NewLC(iWs, iGroup.Identifier());
		gn->SetHidden(ETrue);
		gn->SetSystem(ETrue);
		gn->SetWindowGroupName(iGroup);
		CleanupStack::PopAndDestroy(gn);
		// the shade: black over what is behind (a closing app darkens, the app row's backdrop); hidden unless needed, below the strips
		iShade = RWindow(iWs);
		User::LeaveIfError(iShade.Construct(iGroup, ECount + 1));
		iShade.SetRequiredDisplayMode(EColor16MA);
		iShade.SetTransparencyAlphaChannel();
		iShade.SetBackgroundColor(TRgb(0, 0, 0, 0));
		iShade.SetPointerGrab(ETrue);
		iShade.PointerFilter(EPointerFilterDrag | EPointerFilterMove | EPointerFilterEnterExit, 0);
		iShade.Activate();
		iShade.SetVisible(EFalse);
		for (TInt i = 0; i < ECount; i++)
			{
			iWin[i] = RWindow(iWs);
			User::LeaveIfError(iWin[i].Construct(iGroup, i + 1));
			iWin[i].SetRequiredDisplayMode(EColor16MA);
			iWin[i].SetTransparencyAlphaChannel();						// an error where the image has no alpha windows: the strips are then blank
			iWin[i].SetBackgroundColor(TRgb(0, 0, 0, 0));
			iWin[i].SetPointerGrab(ETrue);
			iWin[i].PointerFilter(EPointerFilterDrag | EPointerFilterMove | EPointerFilterEnterExit, 0);
			}
		iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
		Geometry();
		for (TInt i = 0; i < ECount; i++)
			iWin[i].Activate();
		Apply();
		Listen();
		}
	void Listen()
		{
		iWs.EventReady(&iStatus);
		SetActive();
		}
	void DoCancel()
		{
		iWs.EventReadyCancel();
		}
	// tenths of a millimetre to pixels, through the screen's own twips (1 mm = 56.69 twips); a sane fallback when the device reports nonsense
	TInt Mm(TInt aTenthMm) const
		{
		const TInt twips = aTenthMm * 5669 / 1000;
		TInt px = iDev->HorizontalTwipsToPixels(twips);
		const TSize s = iDev->SizeInPixels();
		const TInt shortSide = s.iWidth < s.iHeight ? s.iWidth : s.iHeight;
		if (px < 3 || px > shortSide / 4)
			px = shortSide * aTenthMm / 1000;						// the 360 px design: about 1/100 of the short side per mm
		return px < 6 ? 6 : px;
		}
	void Geometry()
		{
		const TSize s = iDev->SizeInPixels();
		iScreen = s;
		iZone = Mm(29);
		iSlop = Mm(13);
		const TInt top = Mm(38), band = Mm(38);		// the status bar 24 dp = 3.8 mm; the band under it 24 dp
		iWin[ELeft].SetExtent(TPoint(0, top), TSize(iZone, s.iHeight - top));
		iWin[ERight].SetExtent(TPoint(s.iWidth - iZone, top), TSize(iZone, s.iHeight - top));
		iWin[EBottom].SetExtent(TPoint(iZone, s.iHeight - iZone), TSize(s.iWidth - 2 * iZone, iZone));
		iWin[ETop].SetExtent(TPoint(0, 0), TSize(s.iWidth, top));
		iWin[ETopBand].SetExtent(TPoint(iZone, top), TSize(s.iWidth - 2 * iZone, band));
		RDebug::Printf("PH: [etesianedges] screen %dx%d zone %d slop %d top %d px", s.iWidth, s.iHeight, iZone, iSlop, top);
		}
	// the settings of the Etesian shell app (CenRep E7015A01): key 20 edge gestures (absent = on), key 21 Back by swiping in (0 off, 1 left, 2 right)
	void ReadConfig()
		{
		iCfgOn = ETrue;
		iCfgBack = 0;
		CRepository* r = NULL;
		TRAPD(e, r = CRepository::NewL(TUid::Uid(0xE7015A01)));
		if (e == KErrNone && r)
			{
			TInt v = 1;
			if (r->Get(20, v) == KErrNone)
				iCfgOn = v != 0;
			if (r->Get(21, v) == KErrNone && v >= 0 && v <= 2)
				iCfgBack = v;
			delete r;
			}
		}
	void Apply()
		{
		ReadConfig();
		for (TInt i = 0; i < ECount; i++)
			iWin[i].SetVisible(iEnabled && iCfgOn && !iPassing && !iRowShown);
		iWs.Flush();
		}
	void RunL()
		{
		TWsEvent e;
		iWs.GetEvent(e);
		if (e.Type() == EEventPointer)
			Pointer(e.Handle() - 1, *e.Pointer());
		Listen();
		}
	TInt Inward(const TPoint& aP) const
		{
		switch (iEdge)
			{
			case ELeft: return aP.iX - iStart.iX;
			case ERight: return iStart.iX - aP.iX;
			case EBottom: return iStart.iY - aP.iY;
			default: return aP.iY - iStart.iY;
			}
		}
	TInt Across(const TPoint& aP) const
		{
		const TInt d = (iEdge == ELeft || iEdge == ERight) ? aP.iY - iStart.iY : aP.iX - iStart.iX;
		return d < 0 ? -d : d;
		}
	void Pointer(TInt aEdge, const TPointerEvent& aPe)
		{
		const TPoint p = aPe.iParentPosition;
		if (aEdge == ECount)
			{
			ShadePointer(aPe);
			return;
			}
		switch (aPe.iType)
			{
			case TPointerEvent::EButton1Down:
				if (aEdge < 0 || aEdge >= ECount)
					return;
				iEdge = (aEdge == ETopBand) ? (TInt)ETop : aEdge;
				iStart = p;
				iAnchor = p;
				iGesture = EFalse;
				iDone = EFalse;
				iDownTime.UniversalTime();
				iPoints.Reset();
				iPoints.Append(p);
				iLastIn = 0;
				iLastMs = 0;
				iV = 0;
				iTracking = ETrue;
				break;
			case TPointerEvent::EDrag:
				if (!iTracking)
					return;
				if (iPoints.Count() < 200)
					iPoints.Append(p);
				{
				const TInt inNow = Inward(p), ms = ElapsedMs();
				if (ms - iLastMs >= 30)
					{
					iV = (inNow - iLastIn) * 1000 / (ms - iLastMs);		// px/s, inward
					iLastIn = inNow;
					iLastMs = ms;
					}
				if (iGesture && iEdge == ETop && !iDone)
					Darken(inNow);
				}
				if (!iGesture && !iDone)
					{
					const TInt in = Inward(p), ac = Across(p);
					if (in > iSlop && in > ac)
						iGesture = ETrue;
					else if (ac > 2 * iSlop)
						Pass(ETrue);						// along the edge: a scroll or a drag for the application
					}
				if (iGesture && iEdge == EBottom && !iDone)
					{
					// hold still for 500 ms (within the slop) anywhere on the swipe up: the peek; moving starts the wait again
					const TInt dx = p.iX - iAnchor.iX, dy = p.iY - iAnchor.iY;
					if (dx * dx + dy * dy > iSlop * iSlop)
						{
						iAnchor = p;
						StartHold();
						}
					else if (!iTimer->IsActive())
						StartHold();
					}
				break;
			case TPointerEvent::EButton1Up:
				if (!iTracking)
					return;
				iTimer->Cancel();
				if (iRowShown)
					{
					// let go on an icon: it opens; anywhere else the app rests on the row until a tap
					const TInt h = iAct.EdgeRowHit(iAct.EdgeRowStrip(iScreen), p);
					if (h >= 0)
						{
						HideShade();
						iAct.EdgeRowLaunch(h);
						}
					iTracking = EFalse;
					iGesture = EFalse;
					return;
					}
				if (iGesture && !iDone)
					Finish(p);
				else if (!iDone)
					Pass(EFalse);
				iTracking = EFalse;
				iGesture = EFalse;
				break;
			default:
				break;
			}
		}
	void StartHold()
		{
		iTimer->Cancel();
		iTimer->Start(500000, 500000, TCallBack(HoldTick, this));
		}
	static TInt HoldTick(TAny* aThis)
		{
		CEtEdges* s = static_cast<CEtEdges*>(aThis);
		s->iTimer->Cancel();
		if (s->iTracking && s->iGesture && !s->iDone)
			{
			s->iDone = ETrue;
			RDebug::Printf("PH: [etesianedges] bottom swipe + hold: the app row");
			s->ShowRow();
			}
		return 0;
		}
	// letting go: commit past half the screen (N9), or on a quick flick (900 dp/s = 143 mm/s) inward once past 8%; moving back towards the edge
	// faster than 300 dp/s (47.7 mm/s) always cancels. A side set as Back (setting) fires Back past 40 dp (6.4 mm) instead of minimising.
	void Finish(const TPoint& aP)
		{
		iDone = ETrue;
		const TInt in = Inward(aP);
		const TInt span = (iEdge == EBottom || iEdge == ETop) ? iScreen.iHeight : iScreen.iWidth;
		const TInt ms = ElapsedMs() < 1 ? 1 : ElapsedMs();
		const TInt perSec = in * 1000 / ms;
		const TInt pxPerMm = Mm(10);
		HideShade();
		if (iV < -pxPerMm * 48)
			{
			RDebug::Printf("PH: [etesianedges] edge %d: moving back, cancelled", iEdge);
			return;
			}
		if ((iEdge == ELeft && iCfgBack == 1) || (iEdge == ERight && iCfgBack == 2))
			{
			if (in >= pxPerMm * 64 / 10)
				{
				RDebug::Printf("PH: [etesianedges] edge %d: Back", iEdge);
				SendBack();
				}
			return;
			}
		const TBool fast = perSec > pxPerMm * 143 || iV > pxPerMm * 143;
		const TBool commit = fast ? in * 100 > span * 8 : in * 100 > span * 50;
		RDebug::Printf("PH: [etesianedges] edge %d inward %d of %d: %s", iEdge, in, span, commit ? "commit" : "spring back");
		if (!commit)
			return;
		if (iEdge == ETop)
			CloseFront();
		else
			iAct.EdgeMinimise();
		}
	void CloseFront()
		{
		const TInt fg = iWs.GetFocusWindowGroup();
		if (fg <= 0 || fg == iHomeWg || fg == iGroup.Identifier())
			return;
		CApaWindowGroupName* n = NULL;
		TRAPD(e, n = CApaWindowGroupName::NewL(iWs, fg));
		if (e != KErrNone)
			return;
		const TBool sys = n->IsSystem();
		delete n;
		if (sys)
			return;
		TApaTask t(iWs);
		t.SetWgId(fg);
		t.EndTask();
		RDebug::Printf("PH: [etesianedges] top swipe: closing window group %d", fg);
		iAct.EdgeClosed();
		}
	TInt ElapsedMs() const
		{
		TTime now;
		now.UniversalTime();
		return (TInt)(now.MicroSecondsFrom(iDownTime).Int64() / 1000);
		}
	void SendBack()
		{
		TRawEvent ev;
		ev.Set(TRawEvent::EKeyDown, EStdKeyDevice1);			// the right soft key = Back
		iWs.SimulateRawEvent(ev);
		ev.Set(TRawEvent::EKeyUp, EStdKeyDevice1);
		iWs.SimulateRawEvent(ev);
		iWs.Flush();
		}
	// the shade: black at aAlpha (0..255) over everything; aRow also draws the first row of the grid on top (the hold)
	void DrawShade(TInt aAlpha, TBool aRow)
		{
		CWindowGc* gc = NULL;
		if (iDev->CreateContext(gc) != KErrNone || !gc)
			return;
		gc->Activate(iShade);
		iShade.SetExtent(TPoint(0, 0), iScreen);
		iShade.Invalidate();
		iShade.BeginRedraw();
		gc->SetDrawMode(CGraphicsContext::EDrawModeWriteAlpha);
		gc->SetPenStyle(CGraphicsContext::ENullPen);
		gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc->SetBrushColor(TRgb(0, 0, 0, aAlpha));
		gc->DrawRect(TRect(TPoint(0, 0), iScreen));
		gc->SetDrawMode(CGraphicsContext::EDrawModePEN);
		if (aRow)
			iAct.EdgeDrawRow(*gc, iAct.EdgeRowStrip(iScreen));
		iShade.EndRedraw();
		gc->Deactivate();
		delete gc;
		iWs.Flush();
		}
	// a closing app darkens the further it is pulled down: 0.7 black at the bottom of the screen
	void Darken(TInt aInward)
		{
		const TInt a = aInward < 0 ? 0 : (aInward > iScreen.iHeight ? iScreen.iHeight : aInward) * 179 / iScreen.iHeight;
		if (a / 8 == iShadeAlpha / 8 && iShadeOn)
			return;
		iShadeAlpha = a;
		iShadeOn = ETrue;
		iShade.SetVisible(ETrue);
		DrawShade(a, EFalse);
		}
	void ShowRow()
		{
		iRowShown = ETrue;
		iShadeOn = ETrue;
		iShadeAlpha = 140;
		iShade.SetVisible(ETrue);
		DrawShade(140, ETrue);			// the app falls into shadow: 55 % black
		Apply();
		}
	void HideShade()
		{
		if (iShadeOn || iRowShown)
			{
			iShade.SetVisible(EFalse);
			iWs.Flush();
			}
		iShadeOn = EFalse;
		iRowShown = EFalse;
		iShadeAlpha = -100;
		}
	// the shade is itself pointer-reactive only while the row rests (the finger that held it is on the bottom strip)
	void ShadePointer(const TPointerEvent& aPe)
		{
		if (!iRowShown || aPe.iType != TPointerEvent::EButton1Up)
			return;
		const TInt h = iAct.EdgeRowHit(iAct.EdgeRowStrip(iScreen), aPe.iParentPosition);
		HideShade();
		Apply();
		if (h >= 0)
			iAct.EdgeRowLaunch(h);
		}
	// raw events are in the digitiser's (portrait) coordinates: a landscape screen mode is the portrait one turned (as the compositor's FilterPointer reads them)
	TPoint Digitiser(const TPoint& aP) const
		{
		if (iScreen.iWidth > iScreen.iHeight)
			return TPoint(iScreen.iHeight - 1 - aP.iY, aP.iX);
		return aP;
		}
	// not a gesture: the strips step aside and the pointer sequence goes to what is under them
	void Pass(TBool aMoved)
		{
		if (iDone)
			return;
		iDone = ETrue;
		iPassing = ETrue;
		Apply();
		TRawEvent ev;
		const TPoint s0 = iPoints.Count() ? iPoints[0] : iStart;
		ev.Set(TRawEvent::EPointerMove, Digitiser(s0).iX, Digitiser(s0).iY);
		iWs.SimulateRawEvent(ev);
		ev.Set(TRawEvent::EButton1Down, Digitiser(s0).iX, Digitiser(s0).iY);
		iWs.SimulateRawEvent(ev);
		if (aMoved)
			{
			// the finger is still down: its path so far is replayed, the rest of it (and the up) goes straight to the application
			for (TInt i = 1; i < iPoints.Count(); i++)
				{
				ev.Set(TRawEvent::EPointerMove, Digitiser(iPoints[i]).iX, Digitiser(iPoints[i]).iY);
				iWs.SimulateRawEvent(ev);
				}
			}
		else
			{
			// a tap: the up follows after a real finger's 100 ms
			iUpAt = s0;
			iUpPending = ETrue;
			}
		iWs.Flush();
		iTimer->Cancel();
		iTimer->Start(iUpPending ? 100000 : 400000, 400000, TCallBack(BackTick, this));
		}
	static TInt BackTick(TAny* aThis)
		{
		CEtEdges* s = static_cast<CEtEdges*>(aThis);
		s->iTimer->Cancel();
		if (s->iUpPending)
			{
			s->iUpPending = EFalse;
			TRawEvent ev;
			ev.Set(TRawEvent::EButton1Up, s->Digitiser(s->iUpAt).iX, s->Digitiser(s->iUpAt).iY);
			s->iWs.SimulateRawEvent(ev);
			s->iWs.Flush();
			s->iTimer->Start(300000, 300000, TCallBack(BackTick, s));
			return 0;
			}
		s->iPassing = EFalse;
		s->Apply();
		return 0;
		}
private:
	MEtEdgeActions& iAct;
	TInt iHomeWg;
	RWsSession iWs;
	CWsScreenDevice* iDev = NULL;
	RWindowGroup iGroup;
	RWindow iWin[ECount];
	CPeriodic* iTimer = NULL;
	RArray<TPoint> iPoints;
	TSize iScreen;
	TInt iZone = 20, iSlop = 8, iEdge = 0;
	TPoint iStart, iAnchor, iUpAt;
	TBool iUpPending = EFalse;
	RWindow iShade;
	TBool iShadeOn = EFalse, iRowShown = EFalse, iCfgOn = ETrue;
	TInt iShadeAlpha = -100, iCfgBack = 0, iLastIn = 0, iLastMs = 0, iV = 0;
	TTime iDownTime;
	TBool iEnabled = EFalse, iPassing = EFalse, iTracking = EFalse, iGesture = EFalse, iDone = EFalse;
	};

// ---- the lock screen layer (guide 20-phone, 23-phone-behaviour; audit item 5). The stock key guard (SysAp) stays the lock: the keys, the timing, the
// device-lock code. While it is enabled this layer, a hidden always-in-front window group ABOVE the key guard (priority 20000), shows the Etesian lock
// screen: the clock block, the newest notification cards (missed calls, unread messages: what Events reads) and "Swipe sideways to unlock". A swipe in
// any direction past a quarter of the screen asks the key guard to open (RAknKeylock2). If this process dies the stock lock is simply what is left.
class MEtLockSource
	{
public:
	virtual void LockRefresh() = 0;
	virtual TInt LockCardCount() = 0;
	virtual void LockCard(TInt aI, TDes& aTitle, TDes& aSub) = 0;
	};

class CEtLock : public CActive
	{
public:
	static CEtLock* NewL(MEtLockSource& aSrc)
		{
		CEtLock* l = new (ELeave) CEtLock(aSrc);
		CleanupStack::PushL(l);
		l->ConstructL();
		CleanupStack::Pop(l);
		return l;
		}
	~CEtLock()
		{
		Cancel();
		delete iPoll;
		iKl.Close();
		iWin.Close();
		iGroup.Close();
		if (iDev)
			{
			for (TInt i = 0; i < 4; i++)
				if (iF[i])
					iDev->ReleaseFont(iF[i]);
			}
		delete iDev;
		iWs.Close();
		}
	void Lock()
		{
		iKl.EnableKeyLock();
		RDebug::Printf("PH: [etesianlock] locked from the status menu");
		}
private:
	CEtLock(MEtLockSource& aSrc) : CActive(EPriorityStandard), iSrc(aSrc) {}
	void ConstructL()
		{
		CActiveScheduler::Add(this);
		User::LeaveIfError(iWs.Connect());
		iDev = new (ELeave) CWsScreenDevice(iWs);
		User::LeaveIfError(iDev->Construct());
		iGroup = RWindowGroup(iWs);
		User::LeaveIfError(iGroup.Construct(0x4C4F434B, EFalse));
		iGroup.EnableReceiptOfFocus(EFalse);
		iGroup.SetOrdinalPosition(0, 20000);						// above the key guard (always-at-front + 1) and the cap server backdrop (10000)
		CApaWindowGroupName* gn = CApaWindowGroupName::NewLC(iWs, iGroup.Identifier());
		gn->SetHidden(ETrue);
		gn->SetSystem(ETrue);
		gn->SetWindowGroupName(iGroup);
		CleanupStack::PopAndDestroy(gn);
		iWin = RWindow(iWs);
		User::LeaveIfError(iWin.Construct(iGroup, 1));
		iWin.SetBackgroundColor(TRgb(0, 0, 0));
		iWin.SetPointerGrab(ETrue);
		iWin.PointerFilter(EPointerFilterDrag | EPointerFilterMove | EPointerFilterEnterExit, 0);
		Geometry();
		iWin.Activate();
		iWin.SetVisible(EFalse);
		User::LeaveIfError(iKl.Connect());
		iPoll = CPeriodic::NewL(CActive::EPriorityLow);
		iPoll->Start(700000, 700000, TCallBack(PollTick, this));
		Listen();
		}
	void Listen()
		{
		iWs.EventReady(&iStatus);
		SetActive();
		}
	void DoCancel()
		{
		iWs.EventReadyCancel();
		}
	TInt Sc(TInt aDp) const { const TInt u = iScreen.iWidth < iScreen.iHeight ? iScreen.iWidth : iScreen.iHeight; return (aDp * u + 160) / 320; }
	void Geometry()
		{
		iScreen = iDev->SizeInPixels();
		iWin.SetExtent(TPoint(0, 0), iScreen);
		for (TInt i = 0; i < 4; i++)
			{
			if (iF[i])
				iDev->ReleaseFont(iF[i]);
			iF[i] = NULL;
			}
		// Fira Sans (the image's typeface): the clock 64 dp light, the date 21 light, the card title 16 regular, the card text and the hint 14 regular
		static const TInt px[4] = { 64, 21, 16, 14 };
		for (TInt i = 0; i < 4; i++)
			{
			TFontSpec spec(i < 2 ? _L("Fira Sans Light") : _L("Fira Sans"), Sc(px[i]));
			CFont* f = NULL;
			if (iDev->GetNearestFontInPixels(f, spec) == KErrNone)
				iF[i] = f;
			}
		}
	static TInt PollTick(TAny* aThis)
		{
		CEtLock* self = static_cast<CEtLock*>(aThis);
		const TBool locked = self->iKl.IsKeyLockEnabled();
		if (locked && !self->iShown)
			self->Show();
		else if (!locked && self->iShown)
			{
			// the system lifted the lock (the power key wakes it): the swipe is the only way out while the layer is up
			self->iKl.EnableKeyLock();
			self->Redraw();
			}
		else if (locked)
			self->Redraw();			// the clock minute
		return 0;
		}
	void Show()
		{
		iShown = ETrue;
		iSrc.LockRefresh();
		Geometry();			// the screen mode can differ from the one the layer was made in (the lock turns the phone to portrait)
		iLastMin = -1;
		iDrawn = EFalse;
		iGroup.SetOrdinalPosition(0, 20000);
		iWin.SetVisible(ETrue);
		Redraw();
		RDebug::Printf("PH: [etesianlock] shown (%d cards)", iSrc.LockCardCount());
		}
	void Hide()
		{
		iShown = EFalse;
		iWin.SetVisible(EFalse);
		iWs.Flush();
		RDebug::Printf("PH: [etesianlock] hidden");
		}
	void Redraw()
		{
		TTime now;
		now.HomeTime();
		TDateTime dt = now.DateTime();
		const TInt key = dt.Hour() * 60 + dt.Minute();
		if (key == iLastMin && iDrawn)
			return;
		iLastMin = key;
		iDrawn = ETrue;
		CWindowGc* gc = NULL;
		if (iDev->CreateContext(gc) != KErrNone || !gc)
			return;
		gc->Activate(iWin);
		iWin.Invalidate();
		iWin.BeginRedraw();
		gc->SetPenStyle(CGraphicsContext::ENullPen);
		gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc->SetBrushColor(TRgb(0, 0, 0));
		gc->DrawRect(TRect(TPoint(0, 0), iScreen));
		const TInt w = iScreen.iWidth, h = iScreen.iHeight;
		TInt y = Sc(28);
		TBuf<16> ts;
		TRAP_IGNORE(now.FormatL(ts, _L("%J%:1%T")));
		if (iF[0])
			{
			gc->UseFont(iF[0]);
			gc->SetPenStyle(CGraphicsContext::ESolidPen);
			gc->SetPenColor(TRgb(255, 255, 255));
			const TRect r(0, y, w, y + iF[0]->HeightInPixels());
			gc->DrawText(ts, r, iF[0]->AscentInPixels(), CGraphicsContext::ECenter);
			y = r.iBr.iY;
			gc->DiscardFont();
			}
		TBuf<40> ds;
		TRAP_IGNORE(now.FormatL(ds, _L("%E %*D %N")));
		if (iF[1])
			{
			gc->UseFont(iF[1]);
			gc->SetPenColor(TRgb(0xC8, 0xC8, 0xC8));
			const TRect r(0, y, w, y + iF[1]->HeightInPixels());
			gc->DrawText(ds, r, iF[1]->AscentInPixels(), CGraphicsContext::ECenter);
			y = r.iBr.iY + Sc(16);
			gc->DiscardFont();
			}
		// the newest three notification cards: 8 dp radius, a lit 44 dp squircle, title 16, text 14
		const TInt n = iSrc.LockCardCount() > 3 ? 3 : iSrc.LockCardCount();
		for (TInt i = 0; i < n; i++)
			{
			TBuf<48> title, sub;
			iSrc.LockCard(i, title, sub);
			const TRect card(Sc(11), y, w - Sc(11), y + Sc(60));
			gc->SetPenStyle(CGraphicsContext::ENullPen);
			gc->SetBrushColor(TRgb(0x1C, 0x20, 0x26));
			gc->DrawRoundRect(card, TSize(Sc(8), Sc(8)));
			const TRect ic(card.iTl.iX + Sc(8), card.iTl.iY + Sc(8), card.iTl.iX + Sc(8) + Sc(44), card.iTl.iY + Sc(8) + Sc(44));
			gc->SetBrushColor(TRgb(0x45, 0x91, 0xFF));
			gc->DrawRoundRect(ic, TSize(Sc(14), Sc(14)));
			gc->SetPenStyle(CGraphicsContext::ESolidPen);
			gc->SetPenColor(TRgb(255, 255, 255));
			if (iF[2])
				{
				gc->UseFont(iF[2]);
				gc->DrawText(title, TRect(ic.iBr.iX + Sc(10), card.iTl.iY + Sc(6), card.iBr.iX - Sc(8), card.iTl.iY + Sc(32)), iF[2]->AscentInPixels(), CGraphicsContext::ELeft);
				gc->DiscardFont();
				}
			if (iF[3])
				{
				gc->UseFont(iF[3]);
				gc->SetPenColor(TRgb(0xC8, 0xC8, 0xC8));
				gc->DrawText(sub, TRect(ic.iBr.iX + Sc(10), card.iTl.iY + Sc(32), card.iBr.iX - Sc(8), card.iBr.iY - Sc(4)), iF[3]->AscentInPixels(), CGraphicsContext::ELeft);
				gc->DiscardFont();
				}
			y = card.iBr.iY + Sc(8);
			}
		if (iF[3])
			{
			gc->UseFont(iF[3]);
			gc->SetPenStyle(CGraphicsContext::ESolidPen);
			gc->SetPenColor(TRgb(0xC8, 0xC8, 0xC8));
			gc->DrawText(_L("Swipe sideways to unlock"), TRect(0, h - Sc(40), w, h - Sc(8)), iF[3]->AscentInPixels(), CGraphicsContext::ECenter);
			gc->DiscardFont();
			}
		iWin.EndRedraw();
		gc->Deactivate();
		delete gc;
		iWs.Flush();
		}
	void RunL()
		{
		TWsEvent e;
		iWs.GetEvent(e);
		if (e.Type() == EEventPointer && iShown)
			{
			const TPointerEvent& pe = *e.Pointer();
			if (pe.iType == TPointerEvent::EButton1Down)
				iDown = pe.iParentPosition;
			else if (pe.iType == TPointerEvent::EButton1Up)
				{
				const TInt dx = pe.iParentPosition.iX - iDown.iX, dy = pe.iParentPosition.iY - iDown.iY;
				const TInt ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
				if (ax > iScreen.iWidth / 4 || ay > iScreen.iHeight / 4)
					Unlock();
				}
			}
		Listen();
		}
	void Unlock()
		{
		RDebug::Printf("PH: [etesianlock] swipe: unlock");
		iKl.DisableKeyLock();
		Hide();
		}
private:
	MEtLockSource& iSrc;
	RWsSession iWs;
	CWsScreenDevice* iDev = NULL;
	RWindowGroup iGroup;
	RWindow iWin;
	RAknKeylock2 iKl;
	CPeriodic* iPoll = NULL;
	CFont* iF[4] = { NULL, NULL, NULL, NULL };
	TSize iScreen;
	TPoint iDown;
	TBool iShown = EFalse, iDrawn = EFalse;
	TInt iLastMin = -1;
	};

class CEtHomeAppUi : public CAknAppUi, public MEtEdgeActions, public MEtLockSource, public MEtStatusActions
	{
public:
	void ConstructL()
		{
		BaseConstructL(EAknEnableSkin);
		// the shell draws its own bars: no Avkon status pane, no softkey bar
		if (StatusPane())
			StatusPane()->MakeVisible(EFalse);
		if (Cba())
			Cba()->MakeVisible(EFalse);
		iView = new (ELeave) CEtHomeContainer;
		iView->ConstructL(ApplicationRect());
		AddToStackL(iView);
		// The Menu key. Symbian^3's cap server captures it at priority 0 and toggles between the idle application and its
		// application menu (matrixmenu): with Home as the idle application, a press while Home is in front opens that menu. Home
		// captures the key itself at a higher priority (the same trick pheshellsel uses to see it at boot), so a short press, from
		// any application, brings Home to the front (Applications) and a long press (0.6 s) shows Open apps; the S^3 menu is never started.
		iMenuKey = iCoeEnv->RootWin().CaptureKeyUpAndDowns(EStdKeyApplication0, 0, 0, 5000);
		RDebug::Printf("PH: [etesianhome] up (menu key capture %d)", iMenuKey);
		// The idle application's contract with the start-up: when the idle screen is ready, publish KPSIdlePhase1Ok = EIdlePhase1Ok
		// (KPSUidStartup 0x43 = 101; the stock idle does it in aifw / hsidlestate). The Phone app's recovery system waits for it before
		// it signals the phone as started, and SSM stays at CriticalPhaseOK until then.
		const TUid KPSUidStartupCat = { 0x101F8766 };
		const TUint32 KPSIdlePhase1OkKey = 0x43;
		TInt ir = RProperty::Set(KPSUidStartupCat, KPSIdlePhase1OkKey, 101);
		RDebug::Printf("PH: [etesianhome] idle phase 1 ok published: %d", ir);
		// the edge gestures (on by default, the N9's own behaviour): they act while Home is in the background
		TRAPD(ee, iEdges = CEtEdges::NewL(*this, iCoeEnv->RootWin().Identifier()));
		RDebug::Printf("PH: [etesianhome] edge gestures: %d", ee);
		TRAPD(le, iLock = CEtLock::NewL(*this));
		RDebug::Printf("PH: [etesianhome] lock screen layer: %d", le);
		iView->SetStatusActions(this);
		}
	// ---- MEtEdgeActions: what the edge swipes do
	void ToHome(TBool aOpenApps)
		{
		if (!iView)
			return;
		if (aOpenApps)
			{
			iView->CaptureForeground();		// the front application's last frame, for its Open apps card
			iView->ShowOpenApps();
			}
		TApaTask self(iEikonEnv->WsSession());
		self.SetWgId(iCoeEnv->RootWin().Identifier());
		self.BringToForeground();
		}
	void EdgeMinimise()
		{
		RDebug::Printf("PH: [etesianhome] edge swipe: minimise to Home");
		ToHome(ETrue);
		}
	TRect EdgeRowStrip(const TSize& aScreen) { return iView ? iView->RowStrip(aScreen) : TRect(); }
	void EdgeDrawRow(CWindowGc& aGc, const TRect& aStrip) { if (iView) iView->DrawRow(aGc, aStrip); }
	TInt EdgeRowHit(const TRect& aStrip, const TPoint& aP) { return iView ? iView->RowHit(aStrip, aP) : -1; }
	void EdgeRowLaunch(TInt aIndex)
		{
		RDebug::Printf("PH: [etesianhome] app row: launch %d", aIndex);
		if (iView)
			iView->LaunchRow(aIndex);
		}
	void EdgeClosed()
		{
		RDebug::Printf("PH: [etesianhome] edge swipe: closed, Open apps");
		if (iView)
			{
			iView->ShowOpenApps();
			iView->WatchOpenApps();
			}
		ToHome(EFalse);
		}
	void NoSoftNotes()
		{
		CAknSoftNotifier* sn = NULL;
		TRAPD(e, sn = CAknSoftNotifier::NewL());
		if (e == KErrNone && sn)
			{
			TRAP_IGNORE(sn->SetIdleStateL(EFalse));		// the soft notes ("2 new messages") are shown by the idle application's state; Etesian shows notifications on its Events page
			delete sn;
			}
		}
	void StatusLock() { if (iLock) iLock->Lock(); }
	void LockRefresh() { if (iView) iView->RefreshEvents(); }
	TInt LockCardCount() { return iView ? iView->LockCardCount() : 0; }
	void LockCard(TInt aI, TDes& aTitle, TDes& aSub) { if (iView) iView->LockCard(aI, aTitle, aSub); }
	void HandleForegroundEventL(TBool aForeground)
		{
		CAknAppUi::HandleForegroundEventL(aForeground);
		if (iView)
			iView->WatchPreviews(!aForeground);
		if (aForeground)
			NoSoftNotes();
		if (iEdges)
			iEdges->SetEnabled(!aForeground);		// strips in front of the applications only while Home is behind them
		}
	void HandleWsEventL(const TWsEvent& aEvent, CCoeControl* aDestination)
		{
		const TInt type = aEvent.Type();
		if ((type == EEventKeyDown || type == EEventKeyUp) && aEvent.Key()->iScanCode == EStdKeyApplication0)
			{
			if (iView)
				{
				if (type == EEventKeyDown)
					iView->MenuDown();
				else if (iView->MenuUp())
					{
					TApaTask self(iEikonEnv->WsSession());
					self.SetWgId(iCoeEnv->RootWin().Identifier());
					self.BringToForeground();
					RDebug::Printf("PH: [etesianhome] Menu key: Home to the front");
					}
				}
			return;
			}
		CAknAppUi::HandleWsEventL(aEvent, aDestination);
		}
	~CEtHomeAppUi()
		{
		delete iLock;
		delete iEdges;
		if (iMenuKey >= 0)
			iCoeEnv->RootWin().CancelCaptureKeyUpAndDowns(iMenuKey);
		if (iView)
			RemoveFromStack(iView);
		delete iView;
		}
	void HandleResourceChangeL(TInt aType)
		{
		CAknAppUi::HandleResourceChangeL(aType);
		if (aType == KEikDynamicLayoutVariantSwitch && iEdges)
			iEdges->Relayout();
		if (aType == KEikDynamicLayoutVariantSwitch && iView)
			iView->Relayout(ApplicationRect());
		else if (aType == KAknsMessageSkinChange && iView)
			iView->SkinChanged();		// another theme: the app icons come from the active skin
		}
private:
	CEtHomeContainer* iView = NULL;
	TInt iMenuKey = -1;
	CEtEdges* iEdges = NULL;
	CEtLock* iLock = NULL;
	};

class CEtHomeDocument : public CAknDocument
	{
public:
	CEtHomeDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
	CEikAppUi* CreateAppUiL() { return new (ELeave) CEtHomeAppUi; }
	};

class CEtHomeApplication : public CAknApplication
	{
public:
	TUid AppDllUid() const { return KEtesianHomeUid; }
	CApaDocument* CreateDocumentL() { return new (ELeave) CEtHomeDocument(*this); }
	};

LOCAL_C CApaApplication* NewApplication()
	{
	return new CEtHomeApplication;
	}

GLDEF_C TInt E32Main()
	{
	RDebug::Printf("PH: [etesianhome] E32Main");
	return EikStart::RunApplication(NewApplication);
	}
