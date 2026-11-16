// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesianhome.cpp -- the Etesian Home application, rung E1 of docs/phorepoc/etesian-port.md: the Applications view of the Etesian
// shell, drawn with CWindowGc in one CONE window (plain CONE/Avkon C++, no Qt, no Phorepoc-only API):
//   * a black 24 dp status bar (name left, clock right);
//   * the apps of the application list in a 4-column grid, each a top-lit dark squircle (n = 2.8, Etesian guide chapters 05/06b)
//     carrying the app's own icon, with its short caption below;
//   * three page dots (Events | Applications | Open apps), the middle one lit: the other two views are later rungs;
//   * touch: tap launches, drag scrolls; keys: arrows move the selection, Enter / OK launches.
// It is the shell's idle application (etesianlaunch.exe starts it; the selector publishes its UID as the active idle application),
// so the Menu key brings it to the front. Sizes are written for a 320 dp wide reference screen (Etesian's 480 n9px x 2/3) and scaled.
#include <e32debug.h>
#include <e32math.h>
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
#include <apgicnfl.h>
#include <aknsutils.h>
#include <akniconutils.h>
#include <apacmdln.h>
#include <fbs.h>
#include <gdi.h>
#include <w32std.h>
#include "etesianhome.hrh"

const TUid KEtesianHomeUid = { 0xE7016B50 };

// Etesian dark-mode tokens (design/tokens, MIT)
const TUint32 KColBg = 0x000000;
const TUint32 KColFg = 0xFFFFFF;
const TUint32 KColFg2 = 0xC8C8C8;
const TUint32 KColAccent = 0x4591FF;
const TUint32 KColRim = 0x393839;

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
	HBufC* iCaption;
	CApaMaskedBitmap* iIcon;
	CFbsBitmap* iBmp = NULL;		// the skin-aware (MIF / SVG) app icon and its mask, sized for the tile
	CFbsBitmap* iMask = NULL;
	};

class CEtHomeContainer : public CCoeControl
	{
public:
	~CEtHomeContainer()
		{
		delete iClock;
		iApps.ResetAndDestroy();
		iLs.Close();
		}
	void ConstructL(const TRect& aRect)
		{
		CreateWindowL();
		SetRect(aRect);
		Window().SetPointerGrab(ETrue);
		EnableDragEvents();
		iW = aRect.Width();
		iUnit = aRect.Width() < aRect.Height() * 9 / 16 ? aRect.Width() : aRect.Height() * 9 / 16;
		iH = aRect.Height();
		iStatusH = Sc(24);
		iTile = Sc(60);
		iCellW = iW / 4;
		iCellH = Sc(60) + Sc(8) + Sc(14) + Sc(18);
		iBottomH = Sc(36);
		User::LeaveIfError(iLs.Connect());
		MakeTileL(iTileBmp, iMaskBmp, iTile);
		LoadAppsL();
		iClock = CPeriodic::NewL(CActive::EPriorityLow);
		iClock->Start(15000000, 15000000, TCallBack(ClockTick, this));
		ActivateL();
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
			// the icon the way the application menu gets it: through the skin (MIF / SVG icons are scaled, themed icons found)
			CFbsBitmap* bmp = NULL;
			CFbsBitmap* mask = NULL;
			TRAPD(ie, AknsUtils::CreateAppIconLC(AknsUtils::SkinInstance(), info.iUid, EAknsAppIconTypeList, bmp, mask); CleanupStack::Pop(2));
			if (ie == KErrNone && bmp)
				{
				const TSize g(iTile * 62 / 100, iTile * 62 / 100);
				AknIconUtils::SetSize(bmp, g, EAspectRatioPreserved);
				if (mask)
					AknIconUtils::SetSize(mask, g, EAspectRatioPreserved);
				a->iBmp = bmp;
				a->iMask = mask;
				}
			else
				{
				a->iIcon = CApaMaskedBitmap::NewLC();
				CleanupStack::Pop(a->iIcon);
				if (iLs.GetAppIcon(info.iUid, TSize(iTile, iTile), *a->iIcon) != KErrNone)
					{
					delete a->iIcon;
					a->iIcon = NULL;
					}
				}
			iApps.AppendL(a);
			CleanupStack::Pop(a);
			}
		iApps.Sort(TLinearOrder<CEtApp>(CompareApps));
		RDebug::Printf("PH: [etesianhome] %d apps (list r=%d)", iApps.Count(), r);
		}
	TInt Sc(TInt aDp) const { return (aDp * iUnit + 160) / 320; }

	static TInt CompareApps(const CEtApp& aA, const CEtApp& aB) { return aA.iCaption->CompareC(*aB.iCaption); }
	TInt Rows() const { return (iApps.Count() + 3) / 4; }
	TInt GridTop() const { return iStatusH + Sc(8); }
	TInt GridHeight() const { return iH - iBottomH - GridTop(); }
	TInt MaxScroll() const { TInt m = Rows() * iCellH - GridHeight(); return m > 0 ? m : 0; }
	TRect CellRect(TInt aIndex) const
		{
		const TInt c = aIndex % 4, r = aIndex / 4;
		return TRect(TPoint(c * iCellW, GridTop() + r * iCellH - iScroll), TSize(iCellW, iCellH));
		}
	TInt HitTest(const TPoint& aP) const
		{
		if (aP.iY < iStatusH || aP.iY >= iH - iBottomH)
			return -1;
		for (TInt i = 0; i < iApps.Count(); i++)
			if (CellRect(i).Contains(aP))
				return i;
		return -1;
		}
	void EnsureVisible()
		{
		if (iSel < 0)
			return;
		const TInt top = (iSel / 4) * iCellH, bottom = top + iCellH;
		if (top < iScroll)
			iScroll = top;
		else if (bottom > iScroll + GridHeight())
			iScroll = bottom - GridHeight();
		if (iScroll > MaxScroll())
			iScroll = MaxScroll();
		if (iScroll < 0)
			iScroll = 0;
		}

	void Launch(TInt aIndex)
		{
		if (aIndex < 0 || aIndex >= iApps.Count())
			return;
		const TUid uid = iApps[aIndex]->iUid;
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
	void Draw(const TRect&) const
		{
		CWindowGc& gc = SystemGc();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(KColBg));
		gc.DrawRect(Rect());
		// grid
		gc.SetClippingRect(TRect(0, iStatusH, iW, iH - iBottomH));
		const CFont* lab = iEikonEnv->AnnotationFont();
		gc.UseFont(lab);
		for (TInt i = 0; i < iApps.Count(); i++)
			{
			const TRect cell = CellRect(i);
			if (cell.iBr.iY <= iStatusH || cell.iTl.iY >= iH - iBottomH)
				continue;
			const TPoint tp(cell.iTl.iX + (iCellW - iTile) / 2, cell.iTl.iY + Sc(4));
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			gc.BitBltMasked(tp, &iTileBmp, TRect(TPoint(0, 0), TSize(iTile, iTile)), &iMaskBmp, ETrue);
			const CEtApp* a = iApps[i];
			if (a->iBmp)
				{
				const TSize sz = a->iBmp->SizeInPixels();
				const TPoint gp(tp.iX + (iTile - sz.iWidth) / 2, tp.iY + (iTile - sz.iHeight) / 2);
				if (a->iMask)
					gc.BitBltMasked(gp, a->iBmp, TRect(TPoint(0, 0), sz), a->iMask, ETrue);
				else
					gc.BitBlt(gp, a->iBmp, TRect(TPoint(0, 0), sz));
				}
			else if (a->iIcon && a->iIcon->Mask())
				{
				const TInt g = iTile * 62 / 100;		// the glyph sits inside the tile like Etesian's lit icons
				const TRect dst(TPoint(tp.iX + (iTile - g) / 2, tp.iY + (iTile - g) / 2), TSize(g, g));
				gc.DrawBitmapMasked(dst, a->iIcon, TRect(TPoint(0, 0), a->iIcon->SizeInPixels()), a->iIcon->Mask(), ETrue);
				}
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
			TBuf<24> cap(a->iCaption->Left(24));
			const TRect lr(cell.iTl.iX + 2, tp.iY + iTile + Sc(4), cell.iBr.iX - 2, tp.iY + iTile + Sc(4) + Sc(16));
			gc.DrawText(cap, lr, (lr.Height() + lab->AscentInPixels() - lab->DescentInPixels()) / 2, CGraphicsContext::ECenter);
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			}
		gc.DiscardFont();
		gc.CancelClippingRect();
		// status bar (always black, always visible)
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(KColBg));
		gc.DrawRect(TRect(0, 0, iW, iStatusH));
		const CFont* f = iEikonEnv->AnnotationFont();
		gc.UseFont(f);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(KColFg2));
		gc.DrawText(_L("Etesian"), TRect(Sc(11), 0, iW / 2, iStatusH), (iStatusH + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		TTime now;
		now.HomeTime();
		TBuf<16> ts;
		TRAP_IGNORE(now.FormatL(ts, _L("%J%:1%T%+B")));
		gc.SetPenColor(Rgb(KColFg));
		gc.DrawText(ts, TRect(iW / 2, 0, iW - Sc(11), iStatusH), (iStatusH + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ERight);
		gc.DiscardFont();
		// page dots: Events | Applications | Open apps (the first and last are later rungs)
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		const TInt dr = Sc(3), gap = Sc(14), cy = iH - iBottomH / 2;
		for (TInt d = 0; d < 3; d++)
			{
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(d == 1 ? Rgb(KColFg) : Rgb(0x505050));
			const TInt cx = iW / 2 + (d - 1) * gap;
			gc.DrawEllipse(TRect(cx - dr, cy - dr, cx + dr, cy + dr));
			}
		}

	// ---- input
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKey, TEventCode aType)
		{
		if (aType != EEventKey || iApps.Count() == 0)
			return EKeyWasNotConsumed;
		if (iSel < 0)
			iSel = 0;
		else
			switch (aKey.iCode)
				{
				case EKeyLeftArrow: if (iSel > 0) iSel--; break;
				case EKeyRightArrow: if (iSel < iApps.Count() - 1) iSel++; break;
				case EKeyUpArrow: if (iSel >= 4) iSel -= 4; break;
				case EKeyDownArrow: if (iSel + 4 < iApps.Count()) iSel += 4; else iSel = iApps.Count() - 1; break;
				case EKeyEnter:
				case EKeyDevice3:
					Launch(iSel);
					return EKeyWasConsumed;
				default:
					return EKeyWasNotConsumed;
				}
		EnsureVisible();
		DrawNow();
		return EKeyWasConsumed;
		}
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		switch (aE.iType)
			{
			case TPointerEvent::EButton1Down:
				iDownPos = aE.iPosition;
				iDownScroll = iScroll;
				iDragging = EFalse;
				iDownHit = HitTest(aE.iPosition);
				break;
			case TPointerEvent::EDrag:
				{
				const TInt dy = aE.iPosition.iY - iDownPos.iY;
				if (iDragging || dy > Sc(8) || dy < -Sc(8))
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
				if (!iDragging && iDownHit >= 0 && HitTest(aE.iPosition) == iDownHit)
					{
					iSel = iDownHit;
					Launch(iDownHit);
					}
				iDragging = EFalse;
				break;
			default:
				break;
			}
		}
	static TInt ClockTick(TAny* aThis)
		{
		static_cast<CEtHomeContainer*>(aThis)->DrawNow();
		return 0;
		}
	void SizeChangedL(const TRect& aRect) { (void)aRect; }

private:
	RApaLsSession iLs;
	RPointerArray<CEtApp> iApps;
	CFbsBitmap iTileBmp;
	CFbsBitmap iMaskBmp;
	CPeriodic* iClock = NULL;
	TInt iUnit = 0, iW = 0, iH = 0, iStatusH = 0, iTile = 0, iCellW = 0, iCellH = 0, iBottomH = 0;
	TInt iScroll = 0, iSel = -1, iDownScroll = 0, iDownHit = -1;
	TPoint iDownPos;
	TBool iDragging = EFalse;
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
		RDebug::Printf("PH: [etesianhome] up");
		}
	~CEtHomeAppUi()
		{
		if (iView)
			RemoveFromStack(iView);
		delete iView;
		}
	void HandleResourceChangeL(TInt aType)
		{
		CAknAppUi::HandleResourceChangeL(aType);
		}
private:
	CEtHomeContainer* iView = NULL;
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
