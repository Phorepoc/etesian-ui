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

enum TEtPage { EPageEvents = 0, EPageApps = 1, EPageOpen = 2 };
// the fonts of the Etesian type scale (guide 03)
enum TEtFont { EFLabel, EFBody, EFTitle, EFSum, EFXs, EFXsB, EFBtn, EFDay, EFDate, EFCount };

class CEtHomeContainer : public CCoeControl
	{
public:
	CEtHomeContainer() { for (TInt i = 0; i < EFCount; i++) iF[i] = NULL; }
	~CEtHomeContainer()
		{
		delete iClock;
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
		iClock = CPeriodic::NewL(CActive::EPriorityLow);
		iClock->Start(15000000, 15000000, TCallBack(ClockTick, this));
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
		iCardCols = iW > iH ? 4 : 2;
		iGap = iMargin;
		iCardW = (iW - (iCardCols + 1) * iGap) / iCardCols;
		iPrevW = iCardW - Sc(12);
		iPrevH = iPrevW * iH / iW;
		iCardH = Sc(6) + iPrevH + Sc(6) + iLabelH + Sc(8);
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
			if (iLs.GetAppCapability(cap, info.iUid) == KErrNone && cap().iAppIsHidden)
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
			return TRect(TPoint(iGap + c * (iCardW + iGap), GridTop() + r * RowH() - iScroll), TSize(iCardW, iCardH));
		return TRect(TPoint(c * iCellW, GridTop() + r * iCellH - iScroll), TSize(iCellW, iCellH));
		}
	// the red close squircle: at the card's top-right, slightly outside its corner (guide 20, Open apps edit mode)
	TRect CloseRect(TInt aIndex) const
		{
		const TRect c = CellRect(aIndex);
		return TRect(TPoint(c.iBr.iX - iXSize * 3 / 4, c.iTl.iY - iXSize / 4), TSize(iXSize, iXSize));
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
		if (aPage < EPageEvents) aPage = EPageEvents;
		if (aPage > EPageOpen) aPage = EPageOpen;
		iPage = aPage;
		iEdit = EFalse;
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
			DrawIcon(gc, *a, TRect(tp, TSize(iTile, iTile)), 62);
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
				gc.BitBlt(pv.iTl, iEdit ? th->iDim : th->iBmp);
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
		// section header: a small bold secondary label, right-aligned, with a hairline to its left
		const TRect sh(iMargin, dt.iBr.iY + Sc(8), iW - iMargin, dt.iBr.iY + Sc(8) + iF[EFXsB]->HeightInPixels());
		gc.UseFont(iF[EFXsB]);
		gc.DrawText(_L("Notifications"), sh, Baseline(iF[EFXsB], sh), CGraphicsContext::ERight);
		const TInt lw = iF[EFXsB]->TextWidthInPixels(_L("Notifications"));
		gc.SetPenColor(Rgb(KColRim));
		gc.SetPenSize(TSize(1, 1));
		gc.DrawLine(TPoint(sh.iTl.iX, sh.iTl.iY + sh.Height() / 2), TPoint(sh.iBr.iX - lw - Sc(8), sh.iTl.iY + sh.Height() / 2));
		gc.DiscardFont();
		if (EventCount() == 0)
			{
			gc.UseFont(iF[EFBody]);
			gc.SetPenColor(Rgb(KColFg2));
			const TRect r(0, EventsTop(), iW, iH - iBottomH);
			gc.DrawText(_L("No new events"), TRect(r.iTl, TSize(r.Width(), Sc(40))), Baseline(iF[EFBody], TRect(r.iTl, TSize(r.Width(), Sc(40)))), CGraphicsContext::ECenter);
			gc.DiscardFont();
			return;
			}
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
		gc.DiscardFont();
		if (iEdit && iPage == EPageOpen)
			{
			// edit mode: the lit Done and Close all buttons take the place of the dots
			DrawLitButton(gc, DoneRect(), _L("Done"));
			DrawLitButton(gc, CloseAllRect(), _L("Close all"));
			return;
			}
		// page dots: Events | Applications | Open apps, the current one lit
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
		return 0;
		}
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		switch (aE.iType)
			{
			case TPointerEvent::EButton1Down:
				iDownPos = aE.iPosition;
				iDownScroll = iScroll;
				iDragging = EFalse;
				iSwiping = EFalse;
				iHeld = EFalse;
				iDownHit = iPage == EPageEvents ? -1 : HitTest(aE.iPosition);
				if (iPage == EPageOpen && !iEdit)
					{
					iHoldTimer->Cancel();
					iHoldTimer->Start(700000, 700000, TCallBack(HoldTick, this));		// a long press = edit mode
					}
				break;
			case TPointerEvent::EDrag:
				{
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
		if (iEdit && iPage == EPageOpen)
			{
			if (DoneRect().Contains(aP)) { iEdit = EFalse; DrawNow(); return; }
			if (CloseAllRect().Contains(aP)) { CloseAll(); return; }
			for (TInt i = 0; i < iCards.Count(); i++)
				if (CloseRect(i).Contains(aP)) { CloseCard(i); return; }
			return;		// in edit mode a card does not launch
			}
		if (aP.iY >= iH - iBottomH)
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
	};

class CEtHomeAppUi : public CAknAppUi
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
		if (iMenuKey >= 0)
			iCoeEnv->RootWin().CancelCaptureKeyUpAndDowns(iMenuKey);
		if (iView)
			RemoveFromStack(iView);
		delete iView;
		}
	void HandleResourceChangeL(TInt aType)
		{
		CAknAppUi::HandleResourceChangeL(aType);
		if (aType == KEikDynamicLayoutVariantSwitch && iView)
			iView->Relayout(ApplicationRect());
		else if (aType == KAknsMessageSkinChange && iView)
			iView->SkinChanged();		// another theme: the app icons come from the active skin
		}
private:
	CEtHomeContainer* iView = NULL;
	TInt iMenuKey = -1;
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
