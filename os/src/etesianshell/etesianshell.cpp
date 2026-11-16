// Copyright The Phorepoc Authors
// SPDX-License-Identifier: AGPL-3.0-only

// etesianshell.cpp -- "Etesian shell": chooses the UI shell used at the next start. Two rows, "Etesian" and "Symbian^3 (stock)";
// the current choice is marked. The choice is the shell id (text) in key 10 of the developer-menu repository 0xE7015A01, which core's
// shell selector (pheshellsel) reads at boot; key 11 (the unconfirmed-start counter) is reset and key 12 (the give-up note) cleared.
// A third control, "Restart now", restarts the phone through the starter (the same reset the Settings shell chooser offers).
// If the write is refused the screen says so (the package then has to carry the choice as a CenRep SP package, as orbit-ui does).
// Plain CONE/Avkon, one custom-drawn window; touch (tap a row) and keys (up/down, Enter/OK). Black, white type, accent ring: Etesian dark.
#include <e32debug.h>
#include <eikstart.h>
#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <coecntrl.h>
#include <eikenv.h>
#include <eikappui.h>
#include <avkon.hrh>
#include <centralrepository.h>
#include <starterclient.h>
#include <fbs.h>
#include <gdi.h>
#include <w32std.h>

const TUid KEtesianShellUid = { 0xE7016B52 };
const TUid KDevMenuRepo = { 0xE7015A01 };
const TUint32 KKeyShellId = 10, KKeyShellTries = 11, KKeyShellNote = 12;
_LIT(KIdEtesian, "etesian");
_LIT(KIdS3, "symbian3");

static TRgb Rgb(TUint32 aRgb) { return TRgb((aRgb >> 16) & 0xFF, (aRgb >> 8) & 0xFF, aRgb & 0xFF); }

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
			case EKeyDownArrow: if (iSel < 2) iSel++; break;
			case EKeyEnter:
			case EKeyDevice3:
				if (iSel == 2)
					Restart();
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
			if (row == 2)
				{
				iSel = 2;
				Restart();
				}
			else if (row >= 0)
				{
				iSel = row;
				Choose(row);
				}
			}
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
	TRect RowRect(TInt aRow) const
		{
		if (aRow == 2)		// the Restart button, at the bottom
			return TRect(Rect().Width() / 12, Rect().Height() * 5 / 6, Rect().Width() - Rect().Width() / 12, Rect().Height() * 5 / 6 + Rect().Height() / 8);
		const TInt w = Rect().Width(), h = Rect().Height();
		const TInt rh = h / 6;
		return TRect(w / 12, h / 3 + aRow * (rh + rh / 4), w - w / 12, h / 3 + aRow * (rh + rh / 4) + rh);
		}
	TInt RowAt(const TPoint& aP) const
		{
		for (TInt i = 0; i < 3; i++)
			if (RowRect(i).Contains(aP))
				return i;
		return -1;
		}
	void Draw(const TRect&) const
		{
		CWindowGc& gc = SystemGc();
		const TInt w = Rect().Width(), h = Rect().Height();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(Rgb(0x000000));
		gc.DrawRect(Rect());
		const CFont* f = iEikonEnv->NormalFont();
		gc.UseFont(f);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(Rgb(0xFFFFFF));
		gc.DrawText(_L("Shell for the next start"), TRect(w / 12, h / 12, w, h / 6), (h / 12 + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		gc.SetPenColor(Rgb(0xC8C8C8));
		TBuf<64> cur;
		cur.Format(_L("Now: %S"), &iCurrent);
		gc.DrawText(cur, TRect(w / 12, h / 6, w, h / 4), (h / 12 + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		for (TInt i = 0; i < 3; i++)
			{
			const TRect r = RowRect(i);
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(Rgb(i == 2 ? 0x1A3A6A : 0x1C1C1C));
			gc.SetPenStyle(i == iSel ? CGraphicsContext::ESolidPen : CGraphicsContext::ENullPen);
			gc.SetPenColor(Rgb(0x4591FF));
			gc.SetPenSize(TSize(2, 2));
			gc.DrawRoundRect(r, TSize(r.Height() / 4, r.Height() / 4));
			gc.SetPenStyle(CGraphicsContext::ESolidPen);
			gc.SetPenColor(Rgb(0xFFFFFF));
			TPtrC nm(i == 0 ? TPtrC(_L("Etesian")) : (i == 1 ? TPtrC(_L("Symbian^3 (stock)")) : TPtrC(_L("Restart now"))));
			const TBool active = i < 2 && iCurrent.CompareF(i == 0 ? TPtrC(KIdEtesian) : TPtrC(KIdS3)) == 0;
			TBuf<48> line(nm);
			if (active)
				line.Append(_L("  (selected)"));
			gc.DrawText(line, r, (r.Height() + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ECenter);
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
		gc.DrawText(msg, TRect(w / 12, h * 3 / 4 - h / 24, w, h * 5 / 6 - h / 24), (h / 12 + f->AscentInPixels() - f->DescentInPixels()) / 2, CGraphicsContext::ELeft);
		gc.DiscardFont();
		}
	void SetDone() { iDone = ETrue; }
private:
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
