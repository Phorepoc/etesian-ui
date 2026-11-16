// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesianhome.cpp -- the Etesian Home application, rungs E1-E2 of docs/phorepoc/etesian-port.md: the three horizontal views of the Etesian
// phone Home (guide chapter 20), drawn with CWindowGc in one CONE window (plain CONE/Avkon C++, no Qt, no Phorepoc-only API):
//   Events        | Applications | Open apps
//   * a black 24 dp status bar (name left, clock right) and three page dots (tap, or swipe, or Left/Right at the edges);
//   * Applications: the apps of the application list in a grid, each a top-lit dark squircle (n = 2.8, chapters 05/06b) with the app's icon;
//   * Open apps: the running applications as cards (the S^3 task-list API the stock fast-swap uses: CAknTaskList + CApaWindowGroupName),
//     tap = bring to the front, the X in the corner = close (TApaTask::EndTask, KillTask if it stays);
//   * Events: missed calls (Logs' CenRep counter) and unread messages (the message server's Inbox), tap opens Logs / Messages.
//   * type: Fira Sans (SIL OFL, shipped in the package; falls back to the system face when it is not installed);
//   * the Menu key (captured at a higher priority than the cap server): short = Home, Applications; long (0.6 s) = Open apps.
// It is the shell's idle application (etesianlaunch.exe starts it; the selector publishes its UID as the active idle application) and,
// like the stock idle, publishes KPSIdlePhase1Ok when it is up. Sizes are written for a 320 dp wide reference (the shorter screen side).
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
#include <gdi.h>
#include <w32std.h>
#include "etesianhome.hrh"

const TUid KEtesianHomeUid = { 0xE7016B50 };
const TUid KLogsAppUid = { 0x101F4CD5 };
const TUid KMessagesAppUid = { 0x100058C5 };
const TUid KCRUidLogs = { 0x101F874E };
const TUint32 KLogsNewMissedCalls = 6;

// Etesian dark-mode tokens (design/tokens, MIT)
const TUint32 KColBg = 0x000000;
const TUint32 KColFg = 0xFFFFFF;
const TUint32 KColFg2 = 0xC8C8C8;
const TUint32 KColAccent = 0x4591FF;
const TUint32 KColRim = 0x393839;
const TUint32 KColCard = 0x1C1C1C;

_LIT(KFiraRegular, "Fira Sans");
_LIT(KFiraLight, "Fira Sans Light");

static TRgb Rgb(TUint32 aRgb) { return TRgb((aRgb >> 16) & 0xFF, (aRgb >> 8) & 0xFF, aRgb & 0xFF); }

// n = 2.8 superellipse coverage at (x, y) in -1..1 (1 = inside)
static TBool InSquircle(TReal aX, TReal aY, TReal aScale)
	{
	TReal ax = aX < 0 ? -aX : aX, ay = aY < 0 ? -aY : aY;
	TReal px = 0, py = 0;
	Math::Pow(px, ax, 2.8);
	Math::Pow(py, ay, 2.8);
	return px + py <= aScale;
	}

// the lit squircle tile: a dark gradient (#2A2A2A at the top to #080808 at the bottom, Etesian's measured dark button) with a 1 px rim,
// and its n = 2.8 alpha mask (EGray256: white = opaque, drawn with BitBltMasked ... ETrue like application icon masks)
static void MakeTileL(CFbsBitmap& aTile, CFbsBitmap& aMask, TInt aSize)
	{
	User::LeaveIfError(aTile.Create(TSize(aSize, aSize), EColor16MU));
	User::LeaveIfError(aMask.Create(TSize(aSize, aSize), EGray256));
	const TInt strideT = CFbsBitmap::ScanLineLength(aSize, EColor16MU);
	const TInt strideM = CFbsBitmap::ScanLineLength(aSize, EGray256);
	aTile.LockHeap();
	aMask.LockHeap();
	TUint8* t = (TUint8*)aTile.DataAddress();
	TUint8* m = (TUint8*)aMask.DataAddress();
	const TReal half = aSize / 2.0;
	const TReal inset = 1.0 - 1.4 / half;		// the rim is ~1.4 px wide
	TReal insetScale = 0;
	Math::Pow(insetScale, inset, 2.8);
	for (TInt y = 0; y < aSize; y++)
		{
		// top light to bottom dark
		const TInt shade = 0x2A - (0x2A - 0x08) * y / (aSize - 1);
		for (TInt x = 0; x < aSize; x++)
			{
			TInt cov = 0;
			for (TInt sy = 0; sy < 3; sy++)
				for (TInt sx = 0; sx < 3; sx++)
					if (InSquircle((x + (sx + 0.5) / 3.0 - half) / half, (y + (sy + 0.5) / 3.0 - half) / half, 1.0))
						cov++;
			m[y * strideM + x] = (TUint8)(cov * 255 / 9);
			TUint32 px = (shade << 16) | (shade << 8) | shade;
			if (!InSquircle((x + 0.5 - half) / half, (y + 0.5 - half) / half, insetScale))
				{
				const TInt rim = y < aSize / 3 ? 0x50 : 0x39;		// a lit top edge
				px = (rim << 16) | (rim << 8) | rim;
				}
			*(TUint32*)(t + y * strideT + x * 4) = px;
			}
		}
	aMask.UnlockHeap();
	aTile.UnlockHeap();
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

// one running application: its window group, its list entry (icon, caption)
class CEtCard : public CBase
	{
public:
	~CEtCard() { delete iName; }
	TInt iWgId = 0;
	TUid iUid;
	const CEtApp* iApp = NULL;
	HBufC* iName = NULL;
	TBool iClosing = EFalse;
	};

// the message server wants an observer; the Events view only counts the Inbox, so it ignores every event
class CEtMsvObserver : public MMsvSessionObserver
	{
public:
	void HandleSessionEventL(TMsvSessionEvent, TAny*, TAny*, TAny*) {}
	};

enum TEtPage { EPageEvents = 0, EPageApps = 1, EPageOpen = 2 };

class CEtHomeContainer : public CCoeControl
	{
public:
	~CEtHomeContainer()
		{
		delete iClock;
		delete iMenuTimer;
		delete iCloseTimer;
		iCards.ResetAndDestroy();
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
		ActivateL();
		}
	// geometry from the rectangle; the 320 dp reference is the SHORTER side, so a rotation keeps the sizes and changes the columns
	void Layout(const TRect& aRect)
		{
		iW = aRect.Width();
		iH = aRect.Height();
		iUnit = iW < iH ? iW : iH;
		iStatusH = Sc(24);
		iTile = Sc(60);
		iCols = iW / Sc(76);
		if (iCols < 4)
			iCols = 4;
		iCellW = iW / iCols;
		ReleaseFonts();
		MakeFonts();
		iLabelH = iLabel->HeightInPixels() + 2;
		if (iLabelH < Sc(16))
			iLabelH = Sc(16);
		iCellH = Sc(4) + iTile + Sc(4) + iLabelH + Sc(8);
		iBottomH = Sc(36);
		iTileBmp.Reset();
		iMaskBmp.Reset();
		TRAP_IGNORE(MakeTileL(iTileBmp, iMaskBmp, iTile));
		// Open apps: cards, 2 per row in portrait, 4 in landscape
		iCardCols = iW > iH ? 4 : 2;
		iGap = Sc(12);
		iCardW = (iW - (iCardCols + 1) * iGap) / iCardCols;
		iCardH = iCardW * 5 / 4;
		iScroll = 0;
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
		iLabel = MakeFont(KFiraRegular, Sc(12), EFalse);
		iBody = MakeFont(KFiraLight, Sc(16), EFalse);
		iBold = MakeFont(KFiraRegular, Sc(15), ETrue);
		iSmall = MakeFont(KFiraLight, Sc(12), EFalse);
		iOwnFonts = (iLabel && iBody && iBold && iSmall);
		if (!iLabel) iLabel = (CFont*)iEikonEnv->AnnotationFont();
		if (!iBody) iBody = (CFont*)iEikonEnv->NormalFont();
		if (!iBold) iBold = (CFont*)iEikonEnv->TitleFont();
		if (!iSmall) iSmall = (CFont*)iEikonEnv->AnnotationFont();
		TFontSpec s1 = iLabel->FontSpecInTwips();
		TFontSpec s2 = iBody->FontSpecInTwips();
		RDebug::Print(_L("PH: [etesianhome] fonts: label '%S' body '%S' (own %d)"), &s1.iTypeface.iName, &s2.iTypeface.iName, iOwnFonts);
		}
	void ReleaseFonts()
		{
		// only fonts this class got from the screen device are released (an all-or-nothing flag: a mix falls back to Eikon's fonts)
		if (iOwnFonts)
			{
			CFont* all[4] = { iLabel, iBody, iBold, iSmall };
			for (TInt i = 0; i < 4; i++)
				{
				TBool dup = EFalse;
				for (TInt j = 0; j < i; j++)
					if (all[j] == all[i])
						dup = ETrue;
				if (!dup)
					iCoeEnv->ScreenDevice()->ReleaseFont(all[i]);
				}
			}
		iOwnFonts = EFalse;
		iLabel = iBody = iBold = iSmall = NULL;
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
		TApaAppInfo info;
		while (r == KErrNone && iLs.GetNextApp(info) == KErrNone)
			{
			if (info.iUid == KEtesianHomeUid)
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
			iApps.AppendL(a);
			CleanupStack::Pop(a);
			}
		iApps.Sort(TLinearOrder<CEtApp>(CompareApps));
		RDebug::Printf("PH: [etesianhome] %d apps (list r=%d)", iApps.Count(), r);
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
	TInt GridTop() const { return iStatusH + (iPage == EPageOpen ? iGap : Sc(8)); }
	TInt GridHeight() const { return iH - iBottomH - GridTop(); }
	TInt MaxScroll() const { TInt m = Rows() * RowH() - GridHeight(); return m > 0 ? m : 0; }
	TRect CellRect(TInt aIndex) const
		{
		const TInt c = aIndex % Cols(), r = aIndex / Cols();
		if (iPage == EPageOpen)
			return TRect(TPoint(iGap + c * (iCardW + iGap), GridTop() + r * RowH() - iScroll), TSize(iCardW, iCardH));
		return TRect(TPoint(c * iCellW, GridTop() + r * iCellH - iScroll), TSize(iCellW, iCellH));
		}
	TRect CloseRect(TInt aIndex) const
		{
		const TRect c = CellRect(aIndex);
		const TInt s = Sc(30);
		return TRect(TPoint(c.iBr.iX - s - Sc(4), c.iTl.iY + Sc(4)), TSize(s, s));
		}
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

	// ---- Open apps: the root window groups of the running applications
	void RefreshOpen()
		{
		RWsSession& ws = iEikonEnv->WsSession();
		const TInt ownWg = iCoeEnv->RootWin().Identifier();
		TRAPD(e, DoRefreshOpenL(ws, ownWg));
		if (e != KErrNone)
			RDebug::Printf("PH: [etesianhome] open apps: leave %d", e);
		if (iScroll > MaxScroll())
			iScroll = MaxScroll();
		}
	void DoRefreshOpenL(RWsSession& aWs, TInt aOwnWg)
		{
		CAknTaskList* tl = CAknTaskList::NewLC(aWs);
		const RArray<RWsSession::TWindowGroupChainInfo>& chain = tl->WgArray();
		iCards.ResetAndDestroy();
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
			const CEtApp* app = FindApp(uid);
			// a card is a visible, user-level application: not hidden, not a system server, one the application list shows
			if (!gn->Hidden() && !gn->IsSystem() && uid.iUid != 0 && uid != KEtesianHomeUid && app)
				{
				CEtCard* c = new (ELeave) CEtCard;
				CleanupStack::PushL(c);
				c->iWgId = info.iId;
				c->iUid = uid;
				c->iApp = app;
				c->iName = app->iCaption->AllocL();
				iCards.AppendL(c);
				CleanupStack::Pop(c);
				}
			CleanupStack::PopAndDestroy(gn);
			}
		CleanupStack::PopAndDestroy(tl);
		RDebug::Printf("PH: [etesianhome] open apps: %d", iCards.Count());
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
		RefreshOpen();
		for (TInt i = 0; i < iCards.Count(); i++)
			if (iCards[i]->iWgId == wg)
				{
				TApaTask t(iEikonEnv->WsSession());
				t.SetWgId(wg);
				t.KillTask();
				RDebug::Printf("PH: [etesianhome] killed %08x", iCards[i]->iUid.iUid);
				}
		RefreshOpen();
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
	TRect EventRect(TInt aIndex) const
		{
		const TInt h = Sc(64);
		return TRect(TPoint(Sc(12), iStatusH + Sc(12) + aIndex * (h + Sc(8))), TSize(iW - Sc(24), h));
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
	void DrawApps(CWindowGc& gc) const
		{
		gc.UseFont(iLabel);
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
				{
				// selection (keys): an accent ring around the tile
				gc.SetPenStyle(CGraphicsContext::ESolidPen);
				gc.SetPenColor(Rgb(KColAccent));
				gc.SetPenSize(TSize(2, 2));
				gc.SetBrushStyle(CGraphicsContext::ENullBrush);
				gc.DrawRoundRect(TRect(tp.iX - 2, tp.iY - 2, tp.iX + iTile + 2, tp.iY + iTile + 2), TSize(iTile / 4, iTile / 4));
				gc.SetPenStyle(CGraphicsContext::ENullPen);
				}
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			TBuf<32> cap(a->iCaption->Left(32));
			Fit(cap, iLabel, iCellW - 6);
			const TRect lr(cell.iTl.iX + 2, tp.iY + iTile + Sc(4), cell.iBr.iX - 2, tp.iY + iTile + Sc(4) + iLabelH);
			gc.DrawText(cap, lr, Baseline(iLabel, lr), CGraphicsContext::ECenter);
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
	void DrawOpen(CWindowGc& gc) const
		{
		if (iCards.Count() == 0)
			{
			gc.UseFont(iBody);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg2));
			const TRect r(0, iStatusH, iW, iH - iBottomH);
			gc.DrawText(_L("No open applications"), r, Baseline(iBody, r), CGraphicsContext::ECenter);
			gc.DiscardFont();
			return;
			}
		for (TInt i = 0; i < iCards.Count(); i++)
			{
			const TRect c = CellRect(i);
			if (c.iBr.iY <= iStatusH || c.iTl.iY >= iH - iBottomH)
				continue;
			const CEtCard* card = iCards[i];
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(Rgb(KColCard));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(i == iSel ? KColAccent : KColRim));
			gc.SetPenSize(i == iSel ? TSize(2, 2) : TSize(1, 1));
			gc.DrawRoundRect(c, TSize(8, 8));		// cards: radius 8 (chapter 05)
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			if (card->iApp)
				DrawIcon(gc, *card->iApp, TRect(TPoint(c.iTl.iX, c.iTl.iY + Sc(8)), TSize(c.Width(), c.Height() - Sc(8) - iLabelH - Sc(12))), 45);
			gc.UseFont(iBody);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			TBuf<32> nm(card->iName->Left(32));
			Fit(nm, iBody, c.Width() - Sc(16));
			const TRect nr(c.iTl.iX + Sc(8), c.iBr.iY - iLabelH - Sc(12), c.iBr.iX - Sc(8), c.iBr.iY - Sc(8));
			gc.DrawText(nm, nr, Baseline(iBody, nr), CGraphicsContext::ECenter);
			gc.DiscardFont();
			// the X: a dark rounded button with a cross
			const TRect x = CloseRect(i);
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(Rgb(KColRim));
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			gc.DrawRoundRect(x, TSize(x.Width() / 3, x.Width() / 3));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg));
			gc.SetPenSize(TSize(2, 2));
			const TInt m = x.Width() / 3;
			gc.DrawLine(TPoint(x.iTl.iX + m, x.iTl.iY + m), TPoint(x.iBr.iX - m, x.iBr.iY - m));
			gc.DrawLine(TPoint(x.iBr.iX - m, x.iTl.iY + m), TPoint(x.iTl.iX + m, x.iBr.iY - m));
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			}
		}
	void DrawEvents(CWindowGc& gc) const
		{
		if (EventCount() == 0)
			{
			gc.UseFont(iBody);
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColFg2));
			const TRect r(0, iStatusH, iW, iH - iBottomH);
			gc.DrawText(_L("No new events"), r, Baseline(iBody, r), CGraphicsContext::ECenter);
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
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(Rgb(KColCard));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(KColRim));
			gc.SetPenSize(TSize(1, 1));
			gc.DrawRoundRect(r, TSize(8, 8));
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
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
			const TRect tr(r.iTl.iX + Sc(14), r.iTl.iY + Sc(6), r.iBr.iX - Sc(14), r.iTl.iY + r.Height() / 2);
			const TRect sr(r.iTl.iX + Sc(14), r.iTl.iY + r.Height() / 2, r.iBr.iX - Sc(14), r.iBr.iY - Sc(4));
			gc.UseFont(iBold);
			gc.SetPenColor(Rgb(KColFg));
			gc.DrawText(title, tr, Baseline(iBold, tr), CGraphicsContext::ELeft);
			gc.UseFont(iSmall);
			gc.SetPenColor(Rgb(KColFg2));
			gc.DrawText(sub, sr, Baseline(iSmall, sr), CGraphicsContext::ELeft);
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
		const CFont* f = iSmall;
		gc.UseFont(f);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg2));
		const TInt sb = (iStatusH + f->AscentInPixels() - f->DescentInPixels()) / 2;
		gc.DrawText(_L("Etesian"), TRect(Sc(11), 0, iW / 2, iStatusH), sb, CGraphicsContext::ELeft);
		TTime now;
		now.HomeTime();
		TBuf<16> ts;
		TRAP_IGNORE(now.FormatL(ts, _L("%J%:1%T%+B")));
		gc.SetPenColor(Rgb(KColFg));
		gc.DrawText(ts, TRect(iW / 2, 0, iW - Sc(11), iStatusH), sb, CGraphicsContext::ERight);
		gc.DiscardFont();
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
					if (iPage == EPageOpen) { CloseCard(iSel); return EKeyWasConsumed; }
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
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		switch (aE.iType)
			{
			case TPointerEvent::EButton1Down:
				iDownPos = aE.iPosition;
				iDownScroll = iScroll;
				iDragging = EFalse;
				iSwiping = EFalse;
				iDownHit = iPage == EPageEvents ? -1 : HitTest(aE.iPosition);
				break;
			case TPointerEvent::EDrag:
				{
				const TInt dy = aE.iPosition.iY - iDownPos.iY;
				const TInt dx = aE.iPosition.iX - iDownPos.iX;
				if (!iDragging && !iSwiping && (dx > Sc(12) || dx < -Sc(12)) && (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy))
					iSwiping = ETrue;		// a horizontal gesture changes the page when the finger lifts
				if (!iSwiping && (iDragging || dy > Sc(8) || dy < -Sc(8)))
					{
					iDragging = ETrue;
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
				const TInt dx = aE.iPosition.iX - iDownPos.iX;
				if (iSwiping && (dx > Sc(48) || dx < -Sc(48)))
					SetPage(dx < 0 ? iPage + 1 : iPage - 1);
				else if (!iDragging && !iSwiping)
					HandleTap(aE.iPosition);
				iDragging = iSwiping = EFalse;
				break;
				}
			default:
				break;
			}
		}
	void HandleTap(const TPoint& aP)
		{
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
			{
			if (CloseRect(hit).Contains(aP))
				CloseCard(hit);
			else
				ActivateCard(hit);
			}
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
		if (self->iPage == EPageOpen && !self->iDragging)
			self->RefreshOpen();
		else if (self->iPage == EPageEvents)
			self->RefreshEvents();
		self->DrawNow();
		return 0;
		}
	void SizeChangedL(const TRect& aRect) { (void)aRect; }
	// the screen turned: new rectangle, same tile size, other column counts
	void Relayout(const TRect& aRect)
		{
		SetRect(aRect);
		Layout(aRect);
		DrawNow();
		RDebug::Printf("PH: [etesianhome] layout %dx%d: %d columns, cards %d per row", iW, iH, iCols, iCardCols);
		}

private:
	RApaLsSession iLs;
	RPointerArray<CEtApp> iApps;
	RPointerArray<CEtCard> iCards;
	CFbsBitmap iTileBmp;
	CFbsBitmap iMaskBmp;
	CPeriodic* iClock = NULL;
	CPeriodic* iMenuTimer = NULL;
	CPeriodic* iCloseTimer = NULL;
	CFont* iLabel = NULL;
	CFont* iBody = NULL;
	CFont* iBold = NULL;
	CFont* iSmall = NULL;
	TBool iOwnFonts = EFalse;
	TInt iLastState = -2, iCols = 4, iLabelH = 0, iUnit = 0, iW = 0, iH = 0, iStatusH = 0, iTile = 0, iCellW = 0, iCellH = 0, iBottomH = 0;
	TInt iCardCols = 2, iCardW = 0, iCardH = 0, iGap = 0, iClosingWg = 0;
	TInt iPage = EPageApps, iMissed = 0, iUnread = 0;
	TInt iScroll = 0, iSel = -1, iDownScroll = 0, iDownHit = -1;
	TPoint iDownPos;
	TBool iDragging = EFalse, iSwiping = EFalse, iMenuLong = EFalse;
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
