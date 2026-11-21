// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesianshell.cpp -- "Etesian shell": chooses the UI shell used at the next start. Two rows, "Etesian" and "Symbian^3 (stock)";
// the current choice is marked. The choice is the shell id (text) in key 10 of the developer-menu repository 0xE7015A01, which core's
// shell selector (pheshellsel) reads at boot; key 11 (the unconfirmed-start counter) is reset and key 12 (the give-up note) cleared.
// A third control, "Restart now", restarts the phone through the starter (the same reset the Settings shell chooser offers).
// If the write is refused the screen says so (the package then has to carry the choice as a CenRep SP package, as orbit-ui does).
// Plain CONE/Avkon, one custom-drawn window; touch (tap a row) and keys (up/down, Enter/OK). Black, white type, accent ring: Etesian dark.
#include <e32debug.h>
#include <e32math.h>
#include <eikstart.h>
#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <coecntrl.h>
#include <eikenv.h>
#include <eikappui.h>
#include <avkon.hrh>
#include <aknnotewrappers.h>
#include <centralrepository.h>
#include <starterclient.h>
#include "../phedevmenu/pheshelltheme.h"
#include <fbs.h>
#include <bitdev.h>
#include <gdi.h>
#include <w32std.h>

const TUid KEtesianShellUid = { 0xE7016B52 };
const TUid KDevMenuRepo = { 0xE7015A01 };
const TUint32 KKeyShellId = 10, KKeyShellTries = 11, KKeyShellNote = 12, KKeyOrientation = 13;
// the theme hook (core's pheshelltheme.h, the same code pheshellsel's guard uses): Etesian's own theme is active while the Etesian shell is chosen;
// the theme that was active before is kept (developer-menu repository keys 15 / 16) and put back when S^3 is chosen again or the guard falls back
_LIT(KEtesianThemeName, "Etesian");

// the Etesian theme (UID E7016B31) is in the skins of the device: Phorepoc OS 0.12 and newer carry it in the image. Looked up by the UID
// of the skin package (and by its name, the same skin); an older image has neither and the current theme stays.
const TUint32 KEtesianSkinUid = 0xE7016B31;
static TBool EtesianThemeInstalledL()
	{
	RAknsSrvSession srv;
	CleanupClosePushL(srv);
	User::LeaveIfError(srv.Connect());
	CArrayPtr<CAknsSrvSkinInformationPkg>* skins = srv.EnumerateSkinPackagesL(EAknsSrvAll);
	TBool found = EFalse;
	for (TInt i = 0; i < skins->Count() && !found; i++)
		found = skins->At(i)->PID().iNumber == static_cast<TInt>(KEtesianSkinUid) || skins->At(i)->Name().CompareF(KEtesianThemeName) == 0;
	skins->ResetAndDestroy();
	delete skins;
	CleanupStack::PopAndDestroy(&srv);
	return found;
	}

static void SwitchThemeL(TBool aToEtesian)
	{
	if (aToEtesian)
		{
		TBool have = EFalse;
		TRAP_IGNORE(have = EtesianThemeInstalledL());
		if (!have)
			{
			// one note, and the theme in use stays: everything else about the shell works as usual
			RDebug::Printf("PH: [etesianshell] no Etesian theme on this device: current theme kept");
			_LIT(KNeedNewer, "The Etesian theme needs Phorepoc OS 0.12 or newer");
			CAknInformationNote* note = new (ELeave) CAknInformationNote(ETrue);
			note->ExecuteLD(KNeedNewer);
			return;
			}
		PheThemeEnterShellL(KEtesianThemeName);
		}
	else
		PheThemeLeaveShellL();
	}

_LIT(KFiraRegular, "Fira Sans");
_LIT(KFiraLight, "Fira Sans Light");
_LIT(KOrAuto, "Automatic");
_LIT(KOrPortrait, "Portrait");
_LIT(KOrLandscape, "Landscape");
_LIT(KIdEtesian, "etesian");
_LIT(KIdS3, "symbian3");

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


class CEtShellContainer : public CCoeControl
	{
public:
	void ConstructL(const TRect& aRect)
		{
		CreateWindowL();
		SetRect(aRect);
		Window().SetPointerGrab(ETrue);
		ReadCurrent();
		ActivateL();
		}
	void ReadCurrent()
		{
		iCurrent.Zero();
		CRepository* repo = NULL;
		TRAPD(e, repo = CRepository::NewL(KDevMenuRepo));
		if (e == KErrNone && repo)
			{
			TBuf<32> v;
			if (repo->Get(KKeyShellId, v) == KErrNone)
				iCurrent.Copy(v);
			delete repo;
			}
		if (iCurrent.Length() == 0)
			iCurrent.Copy(KIdS3);
		}
	void Choose(TInt aRow)
		{
		TPtrC id(aRow == 0 ? TPtrC(KIdEtesian) : TPtrC(KIdS3));
		CRepository* repo = NULL;
		TRAPD(e, repo = CRepository::NewL(KDevMenuRepo));
		TInt r = e;
		if (e == KErrNone && repo)
			{
			r = repo->Set(KKeyShellId, id);
			if (r == KErrNotFound)
				r = repo->Create(KKeyShellId, id);
			if (r == KErrNone)
				{
				repo->Set(KKeyShellTries, 0);
				TInt rn = repo->Set(KKeyShellNote, KNullDesC);
				if (rn == KErrNotFound)
					repo->Create(KKeyShellNote, KNullDesC);
				}
			delete repo;
			}
		if (r == KErrNone)
			{
			TRAPD(te, SwitchThemeL(aRow == 0));
			RDebug::Printf("PH: [etesianshell] theme switch: %d", te);
			}
		RDebug::Print(_L("PH: [etesianshell] chose %S: %d"), &id, r);
		iResult = r;
		iDone = ETrue;
		ReadCurrent();
		DrawNow();
		}
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKey, TEventCode aType)
		{
		if (aType != EEventKey)
			return EKeyWasNotConsumed;
		switch (aKey.iCode)
			{
			case EKeyUpArrow: if (iSel > 0) iSel--; break;
			case EKeyDownArrow: if (iSel < 3) iSel++; break;
			case EKeyEnter:
			case EKeyDevice3:
				if (iSel == 3)
					Restart();
				else if (iSel == 2)
					CycleOrientation();
				else
					Choose(iSel);
				return EKeyWasConsumed;
			default:
				return EKeyWasNotConsumed;
			}
		DrawNow();
		return EKeyWasConsumed;
		}
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		if (aE.iType == TPointerEvent::EButton1Up)
			{
			TInt row = RowAt(aE.iPosition);
			if (row == 3)
				{
				iSel = 3;
				Restart();
				}
			else if (row == 2)
				{
				iSel = 2;
				CycleOrientation();
				}
			else if (row >= 0)
				{
				iSel = row;
				Choose(row);
				}
			}
		}
	// the screen orientation the next start applies (core's pheshellsel): CenRep E7015A01 key 13, 0 Automatic | 1 Portrait | 2 Landscape
	TInt Orientation() const
		{
		TInt v = 0;
		CRepository* repo = NULL;
		TRAPD(e, repo = CRepository::NewL(KDevMenuRepo));
		if (e == KErrNone && repo)
			{
			if (repo->Get(KKeyOrientation, v) != KErrNone)
				v = 0;
			delete repo;
			}
		return v < 0 || v > 2 ? 0 : v;
		}
	void CycleOrientation()
		{
		const TInt next = (Orientation() + 1) % 3;
		CRepository* repo = NULL;
		TRAPD(e, repo = CRepository::NewL(KDevMenuRepo));
		TInt r = e;
		if (e == KErrNone && repo)
			{
			r = repo->Set(KKeyOrientation, next);
			if (r == KErrNotFound)
				r = repo->Create(KKeyOrientation, next);
			delete repo;
			}
		RDebug::Printf("PH: [etesianshell] orientation %d: %d", next, r);
		iResult = r;
		iDone = ETrue;
		DrawNow();
		}
	// the phone restarts through the starter, as the UI shell chooser in Settings does (ELanguageSwitchReset)
	void Restart()
		{
		RStarterSession starter;
		TInt r = starter.Connect();
		if (r == KErrNone)
			{
			RDebug::Printf("PH: [etesianshell] restart requested");
			starter.Reset(RStarterSession::ELanguageSwitchReset);
			starter.Close();
			}
		else
			RDebug::Printf("PH: [etesianshell] restart: starter connect %d", r);
		}
	// sizes in dp of a 320 dp wide reference (the shorter side), guide 04: margin 16 n9px = 11 dp, touch target >= 44 dp
	TInt Sc(TInt aDp) const { const TInt u = Rect().Width() < Rect().Height() ? Rect().Width() : Rect().Height(); return (aDp * u + 160) / 320; }
	TRect RowRect(TInt aRow) const
		{
		const TInt top = Sc(60);
		return TRect(Sc(11), top + aRow * (Sc(44) + Sc(8)), Rect().Width() - Sc(11), top + aRow * (Sc(44) + Sc(8)) + Sc(44));
		}
	TInt RowAt(const TPoint& aP) const
		{
		for (TInt i = 0; i < 4; i++)
			if (RowRect(i).Contains(aP))
				return i;
		return -1;
		}
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
	// the buttons: lit, text buttons with a radius of about 21 % of the height (guide 20 section 6)
	void MakeButtons()
		{
		const TRect r = RowRect(0);
		if (iBtn.Handle() && iBtn.SizeInPixels() == r.Size())
			return;
		TRAP_IGNORE(MakeLitL(iBtn, iBtnMask, r.Width(), r.Height(), r.Height() * 21 / 100, EFalse, 0x1C1C1C, 0x101010, 0x080808, 0x393839, 0x393839));
		TRAP_IGNORE(MakeLitL(iBtnAct, iBtnActMask, r.Width(), r.Height(), r.Height() * 21 / 100, EFalse, 0x2A5BA8, 0x1F4684, 0x143262, 0x6C9CE0, 0x143262));
		}
	void Draw(const TRect&) const
		{
		CWindowGc& gc = SystemGc();
		const TInt w = Rect().Width(), h = Rect().Height();
		const_cast<CEtShellContainer*>(this)->MakeButtons();
		CFont* title = const_cast<CEtShellContainer*>(this)->MakeFont(KFiraLight, Sc(21), EFalse);		// xlarge: page title, light
		CFont* body = const_cast<CEtShellContainer*>(this)->MakeFont(KFiraLight, Sc(16), EFalse);
		CFont* btn = const_cast<CEtShellContainer*>(this)->MakeFont(KFiraRegular, Sc(15), ETrue);		// button 15 bold
		CFont* small = const_cast<CEtShellContainer*>(this)->MakeFont(KFiraRegular, Sc(13), EFalse);
		if (!title) title = (CFont*)iEikonEnv->TitleFont();
		if (!body) body = (CFont*)iEikonEnv->NormalFont();
		if (!btn) btn = (CFont*)iEikonEnv->TitleFont();
		if (!small) small = (CFont*)iEikonEnv->AnnotationFont();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(0x000000));
		gc.DrawRect(Rect());
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(0xFFFFFF));
		const TRect tr(Sc(11), Sc(6), w, Sc(6) + title->HeightInPixels());
		gc.UseFont(title);
		gc.DrawText(_L("Shell for the next start"), tr, (tr.Height() + title->AscentInPixels() - title->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		gc.SetPenColor(Rgb(0xC8C8C8));
		TBuf<64> cur;
		cur.Format(_L("Now: %S"), &iCurrent);
		const TRect cr(Sc(11), tr.iBr.iY, w, tr.iBr.iY + small->HeightInPixels() + Sc(4));
		gc.UseFont(small);
		gc.DrawText(cur, cr, (cr.Height() + small->AscentInPixels() - small->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		for (TInt i = 0; i < 4; i++)
			{
			const TRect r = RowRect(i);
			gc.SetBrushStyle(CGraphicsContext::ENullBrush);
			const TBool primary = i == 3;
			gc.BitBltMasked(r.iTl, primary ? &iBtnAct : &iBtn, TRect(TPoint(0, 0), r.Size()), primary ? &iBtnActMask : &iBtnMask, ETrue);
			if (i == iSel)
				{
				gc.SetPenStyle(CGraphicsContext::ESolidPen);
				gc.SetPenColor(Rgb(0x4591FF));
				gc.SetPenSize(TSize(2, 2));
				gc.DrawRoundRect(r, TSize(r.Height() * 21 / 100, r.Height() * 21 / 100));
				}
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(0xFFFFFF));
			TBuf<48> orient;
			if (i == 2)
				{
				const TInt o = Orientation();
				orient.Format(_L("Screen: %S"), o == 1 ? &KOrPortrait : (o == 2 ? &KOrLandscape : &KOrAuto));
				}
			TPtrC nm(i == 0 ? TPtrC(_L("Etesian")) : (i == 1 ? TPtrC(_L("Symbian^3 (stock)")) : (i == 2 ? TPtrC(orient) : TPtrC(_L("Restart now")))));
			const TBool active = i < 2 && iCurrent.CompareF(i == 0 ? TPtrC(KIdEtesian) : TPtrC(KIdS3)) == 0;
			TBuf<48> line(nm);
			if (active)
				line.Append(_L("  (selected)"));
			gc.UseFont(btn);
			gc.DrawText(line, r, (r.Height() + btn->AscentInPixels() - btn->DescentInPixels()) / 2, CGraphicsContext::ECenter);
			}
		gc.SetBrushStyle(CGraphicsContext::ENullBrush);
		gc.SetPenColor(Rgb(0xC8C8C8));
		TBuf<96> msg;
		if (iResult == KErrNone && iDone)
			msg.Copy(_L("Saved. Restart the phone to switch."));
		else if (iResult != KErrNone && iDone)
			msg.Format(_L("Not saved (error %d)"), iResult);
		else
			msg.Copy(_L("Pick a row. Takes effect at the next start."));
		const TRect mr(Sc(11), RowRect(3).iBr.iY + Sc(6), w, RowRect(3).iBr.iY + Sc(6) + small->HeightInPixels() + Sc(4));
		gc.UseFont(small);
		gc.DrawText(msg, mr, (mr.Height() + small->AscentInPixels() - small->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		gc.DiscardFont();
		iCoeEnv->ScreenDevice()->ReleaseFont(title);
		iCoeEnv->ScreenDevice()->ReleaseFont(body);
		iCoeEnv->ScreenDevice()->ReleaseFont(btn);
		iCoeEnv->ScreenDevice()->ReleaseFont(small);
		}
	void SetDone() { iDone = ETrue; }
private:
	CFbsBitmap iBtn, iBtnMask, iBtnAct, iBtnActMask;
	TBuf<32> iCurrent;
	TInt iSel = 0;
	TInt iResult = 0;
	TBool iDone = EFalse;
	};

class CEtShellAppUi : public CAknAppUi
	{
public:
	void ConstructL()
		{
		BaseConstructL(EAknEnableSkin);
		iView = new (ELeave) CEtShellContainer;
		iView->ConstructL(ClientRect());
		AddToStackL(iView);
		}
	~CEtShellAppUi()
		{
		if (iView)
			RemoveFromStack(iView);
		delete iView;
		}
	void HandleCommandL(TInt aCommand)
		{
		if (aCommand == EAknSoftkeyExit || aCommand == EEikCmdExit)
			Exit();
		}
private:
	CEtShellContainer* iView = NULL;
	};

class CEtShellDocument : public CAknDocument
	{
public:
	CEtShellDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
	CEikAppUi* CreateAppUiL() { return new (ELeave) CEtShellAppUi; }
	};

class CEtShellApplication : public CAknApplication
	{
public:
	TUid AppDllUid() const { return KEtesianShellUid; }
	CApaDocument* CreateDocumentL() { return new (ELeave) CEtShellDocument(*this); }
	};

LOCAL_C CApaApplication* NewApplication()
	{
	return new CEtShellApplication;
	}

GLDEF_C TInt E32Main()
	{
	return EikStart::RunApplication(NewApplication);
	}
