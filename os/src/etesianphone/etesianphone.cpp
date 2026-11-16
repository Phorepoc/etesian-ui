// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesianphone.cpp -- the Etesian dialler, the pilot of the shell-tagged apps (docs/phorepoc/etesian-port.md 17). Plain CONE/Avkon, one
// custom-drawn window in the Etesian look (black, Fira Sans, lit squircle keys, page header 48 dp, bottom toolbar with the lit Back
// squircle on the left):
//   * Keypad: the number (light 32), a 3 x 4 pad of lit squircles, Call (green lit) and Backspace;
//   * Calls: the recent calls from the Logs engine (CLogViewRecent), tap = put the number in the pad;
//   * Call = CTelephony::DialNewCall (the stock call path: the Phone application takes the call over); without a SIM / modem the
//     result is the telephony error, shown under the number.
// It is tagged for the Etesian shell (\resource\phe\shells\etesian.apps / .hides / .assoc): Home shows it, the stock Phone app is hidden
// from the launchers, and x-epoc-url/tel goes to it; under S^3 the files name it as not belonging to the shell.
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
#include <etel3rdparty.h>
#include <logcli.h>
#include <logview.h>
#include <fbs.h>
#include <bitdev.h>
#include <gdi.h>
#include <w32std.h>
#include "etesianphone.hrh"

const TUid KEtesianPhoneUid = { 0xE7016B53 };
_LIT(KFiraRegular, "Fira Sans");
_LIT(KFiraLight, "Fira Sans Light");


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


class CEtPhoneContainer;

// dialling through the stock call path
class CEtDial : public CActive
	{
public:
	CEtDial(CEtPhoneContainer& aOwner) : CActive(EPriorityStandard), iOwner(aOwner), iParamsPckg(iParams) { CActiveScheduler::Add(this); }
	~CEtDial() { Cancel(); delete iTel; }
	void DialL(const TDesC& aNumber)
		{
		if (IsActive())
			return;
		if (!iTel)
			iTel = CTelephony::NewL();
		iParams.iIdRestrict = CTelephony::ESendMyId;
		CTelephony::TTelNumber n(aNumber);
		iTel->DialNewCall(iStatus, iParamsPckg, n, iCallId);
		SetActive();
		}
	void RunL();
	void DoCancel() { if (iTel) iTel->CancelAsync(CTelephony::EDialNewCallCancel); }
private:
	CEtPhoneContainer& iOwner;
	CTelephony* iTel = NULL;
	CTelephony::TCallId iCallId;
	CTelephony::TCallParamsV1 iParams;
	CTelephony::TCallParamsV1Pckg iParamsPckg;
	};

// the recent calls (all lists), read from the Logs engine
class CEtCallLog : public CActive
	{
public:
	struct TEntry { TBuf<32> iNumber; TBuf<32> iName; TInt iDir; };
	CEtCallLog(CEtPhoneContainer& aOwner) : CActive(EPriorityLow), iOwner(aOwner) { CActiveScheduler::Add(this); }
	~CEtCallLog() { Cancel(); delete iView; delete iClient; iFs.Close(); iEntries.Close(); }
	void StartL()
		{
		User::LeaveIfError(iFs.Connect());
		iClient = CLogClient::NewL(iFs);
		iView = CLogViewRecent::NewL(*iClient);
		iPhase = 0;
		if (iView->SetRecentListL((TLogRecentList)-1, iStatus))
			SetActive();
		else
			Finished();
		}
	void RunL();
	void DoCancel() { if (iView) iView->Cancel(); }
	void Finished();
	RArray<TEntry> iEntries;
private:
	CEtPhoneContainer& iOwner;
	RFs iFs;
	CLogClient* iClient = NULL;
	CLogViewRecent* iView = NULL;
	TInt iPhase;
	};

enum TEtTab { ETabKeypad = 0, ETabCalls = 1 };

class CEtPhoneContainer : public CCoeControl
	{
public:
	~CEtPhoneContainer()
		{
		delete iDial;
		delete iLog;
		for (TInt i = 0; i < 9; i++)
			if (iF[i])
				iCoeEnv->ScreenDevice()->ReleaseFont(iF[i]);
		}
	void ConstructL(const TRect& aRect)
		{
		CreateWindowL();
		SetRect(aRect);
		Window().SetPointerGrab(ETrue);
		for (TInt i = 0; i < 9; i++)
			iF[i] = NULL;
		Layout(aRect);
		iDial = new (ELeave) CEtDial(*this);
		iLog = new (ELeave) CEtCallLog(*this);
		TRAP_IGNORE(iLog->StartL());
		ActivateL();
		}
	TInt Sc(TInt aDp) const { return (aDp * iUnit + 160) / 320; }
	CFont* Font(const TDesC& aName, TInt aPx, TBool aBold)
		{
		TFontSpec spec(aName, aPx);
		if (aBold)
			spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
		CFont* f = NULL;
		iCoeEnv->ScreenDevice()->GetNearestFontInPixels(f, spec);
		return f;
		}
	void Layout(const TRect& aRect)
		{
		iW = aRect.Width();
		iH = aRect.Height();
		iUnit = iW < iH ? iW : iH;
		for (TInt i = 0; i < 9; i++)
			if (iF[i])
				{
				iCoeEnv->ScreenDevice()->ReleaseFont(iF[i]);
				iF[i] = NULL;
				}
		iF[0] = Font(KFiraLight, Sc(21), EFalse);		// page header: xlarge light
		iF[1] = Font(KFiraLight, Sc(32), EFalse);		// the number and the keys
		iF[2] = Font(KFiraRegular, Sc(13), EFalse);		// small
		iF[3] = Font(KFiraRegular, Sc(16), ETrue);		// call list names: 16 bold
		iF[4] = Font(KFiraRegular, Sc(14), EFalse);		// summaries: 14 regular
		iF[5] = Font(KFiraRegular, Sc(15), ETrue);		// tabs, buttons
		iHeaderH = Sc(48);
		iTabH = Sc(36);
		iToolH = iW > iH ? Sc(37) + Sc(8) : Sc(48);
		iNumH = Sc(60);
		// the pad: 3 x 4 squircles in what is left
		const TInt avail = iH - iHeaderH - iTabH - iNumH - iToolH - Sc(30);
		TInt key = avail / 4;
		if (key > iW / 3 - Sc(16))
			key = iW / 3 - Sc(16);
		if (key > Sc(72))
			key = Sc(72);
		iKey = key;
		TRAP_IGNORE(MakeLitL(iKeyBmp, iKeyMask, iKey, iKey, 0, ETrue, 0x2A2A2A, KNoMid, 0x080808, 0x505050, 0x393839));
		const TInt tb = iToolH - Sc(8);
		iTbSize = tb;
		TRAP_IGNORE(MakeLitL(iBackBmp, iBackMask, tb, tb, 0, ETrue, 0x2A2A2A, KNoMid, 0x080808, 0x505050, 0x393839));
		TRAP_IGNORE(MakeLitL(iCallBmp, iCallMask, tb * 3 / 2, tb, 0, ETrue, 0x84C638, KNoMid, 0x509600, 0xB8E57A, 0x3E7400));
		}
	TRect KeyRect(TInt aIdx) const		// 0..11: 1 2 3 4 5 6 7 8 9 * 0 #
		{
		const TInt c = aIdx % 3, r = aIdx / 3;
		const TInt gx = (iW - 3 * iKey) / 4, gy = Sc(6);
		const TInt top = iHeaderH + iTabH + iNumH;
		return TRect(TPoint(gx + c * (iKey + gx), top + r * (iKey + gy)), TSize(iKey, iKey));
		}
	TRect BackRect() const { return TRect(TPoint(Sc(11), iH - iToolH + Sc(4)), TSize(iTbSize, iTbSize)); }
	TRect CallRect() const { return TRect(TPoint(iW / 2 - iTbSize * 3 / 4, iH - iToolH + Sc(4)), TSize(iTbSize * 3 / 2, iTbSize)); }
	TRect DelRect() const { return TRect(TPoint(iW - Sc(11) - iTbSize, iH - iToolH + Sc(4)), TSize(iTbSize, iTbSize)); }
	TRect TabRect(TInt aTab) const { return TRect(TPoint(aTab * iW / 2, iHeaderH), TSize(iW / 2, iTabH)); }
	TInt RowH() const { return Sc(53); }
	TRect RowRect(TInt aIdx) const { return TRect(TPoint(0, iHeaderH + iTabH + aIdx * RowH() - iScroll), TSize(iW, RowH())); }
	TInt Baseline(const CFont* f, const TRect& r) const { return (r.Height() + f->AscentInPixels() - f->DescentInPixels()) / 2; }

	void DialResult(TInt aErr)
		{
		RDebug::Printf("PH: [etesianphone] dial result %d", aErr);
		iStatus = aErr == KErrNone ? 1 : aErr;
		DrawNow();
		}
	void LogChanged() { DrawNow(); }
	void Press(TChar aCh)
		{
		if (iNumber.Length() < 32)
			iNumber.Append(aCh);
		iStatus = 0;
		DrawNow();
		}
	void Backspace()
		{
		if (iNumber.Length())
			iNumber.SetLength(iNumber.Length() - 1);
		iStatus = 0;
		DrawNow();
		}
	void Call()
		{
		if (!iNumber.Length())
			return;
		RDebug::Print(_L("PH: [etesianphone] dial %S"), &iNumber);
		TRAPD(e, iDial->DialL(iNumber));
		if (e != KErrNone)
			DialResult(e);
		else
			{
			iStatus = 2;
			DrawNow();
			}
		}
	void SetTab(TInt aTab) { iTab = aTab; iScroll = 0; DrawNow(); }

	void Draw(const TRect&) const
		{
		CWindowGc& gc = SystemGc();
		gc.SetPenStyle(CGraphicsContext::ENullPen);
		gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		gc.SetBrushColor(TRgb(0, 0, 0));
		gc.DrawRect(Rect());
		// page header: the title, large light
		const TRect hr(Sc(11), 0, iW, iHeaderH);
		gc.UseFont(iF[0]);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(TRgb(255, 255, 255));
		gc.DrawText(_L("Phone"), hr, Baseline(iF[0], hr), CGraphicsContext::ELeft);
		// tabs: Keypad | Calls, the selected one with the accent rule
		gc.UseFont(iF[5]);
		for (TInt t = 0; t < 2; t++)
			{
			const TRect tr = TabRect(t);
			gc.SetPenColor(t == iTab ? TRgb(255, 255, 255) : TRgb(0x8C, 0x8C, 0x8C));
			gc.DrawText(t == 0 ? _L("Keypad") : _L("Calls"), tr, Baseline(iF[5], tr), CGraphicsContext::ECenter);
			if (t == iTab)
				{
				gc.SetPenColor(TRgb(0x45, 0x91, 0xFF));
				gc.SetPenSize(TSize(2, 2));
				gc.DrawLine(TPoint(tr.iTl.iX + Sc(24), tr.iBr.iY - 1), TPoint(tr.iBr.iX - Sc(24), tr.iBr.iY - 1));
				}
			}
		gc.SetBrushStyle(CGraphicsContext::ENullBrush);
		if (iTab == ETabKeypad)
			DrawKeypad(gc);
		else
			DrawCalls(gc);
		// the toolbar: Back (lit squircle) on the left, Call in the middle, Backspace right
		const TRect b = BackRect();
		gc.BitBltMasked(b.iTl, &iBackBmp, TRect(TPoint(0, 0), b.Size()), &iBackMask, ETrue);
		gc.SetPenStyle(CGraphicsContext::ESolidPen);
		gc.SetPenColor(TRgb(255, 255, 255));
		gc.SetPenSize(TSize(Sc(2), Sc(2)));
		gc.DrawLine(TPoint(b.iTl.iX + b.Width() * 6 / 10, b.iTl.iY + b.Height() / 4), TPoint(b.iTl.iX + b.Width() * 4 / 10, b.iTl.iY + b.Height() / 2));
		gc.DrawLine(TPoint(b.iTl.iX + b.Width() * 4 / 10, b.iTl.iY + b.Height() / 2), TPoint(b.iTl.iX + b.Width() * 6 / 10, b.iTl.iY + b.Height() * 3 / 4));
		if (iTab == ETabKeypad)
			{
			gc.BitBltMasked(CallRect().iTl, &iCallBmp, TRect(TPoint(0, 0), CallRect().Size()), &iCallMask, ETrue);
			gc.UseFont(iF[5]);
			gc.DrawText(_L("Call"), CallRect(), Baseline(iF[5], CallRect()), CGraphicsContext::ECenter);
			gc.BitBltMasked(DelRect().iTl, &iBackBmp, TRect(TPoint(0, 0), DelRect().Size()), &iBackMask, ETrue);
			gc.DrawText(_L("<"), DelRect(), Baseline(iF[5], DelRect()), CGraphicsContext::ECenter);
			}
		gc.DiscardFont();
		}
	void DrawKeypad(CWindowGc& gc) const
		{
		// the number, centred, light 32 (guide: large light type); the call state under it
		const TRect nr(Sc(11), iHeaderH + iTabH, iW - Sc(11), iHeaderH + iTabH + iNumH - Sc(14));
		gc.UseFont(iF[1]);
		gc.SetPenColor(TRgb(255, 255, 255));
		gc.DrawText(iNumber, nr, Baseline(iF[1], nr), CGraphicsContext::ECenter);
		gc.UseFont(iF[2]);
		gc.SetPenColor(iStatus > 1 || iStatus < 0 ? TRgb(0xDA, 0x20, 0x1E) : TRgb(0xC8, 0xC8, 0xC8));
		TBuf<64> msg;
		if (iStatus == 2)
			msg.Copy(_L("Calling ..."));
		else if (iStatus == 1)
			msg.Copy(_L("Connected"));
		else if (iStatus < 0)
			msg.Format(_L("Call failed (%d)"), iStatus);
		const TRect mr(Sc(11), nr.iBr.iY, iW - Sc(11), iHeaderH + iTabH + iNumH);
		gc.DrawText(msg, mr, Baseline(iF[2], mr), CGraphicsContext::ECenter);
		gc.UseFont(iF[1]);
		for (TInt k = 0; k < 12; k++)
			{
			const TRect r = KeyRect(k);
			gc.BitBltMasked(r.iTl, &iKeyBmp, TRect(TPoint(0, 0), r.Size()), &iKeyMask, ETrue);
			gc.SetPenColor(TRgb(255, 255, 255));
			TBuf<1> d;
			d.Append(KKeys[k]);
			gc.DrawText(d, r, Baseline(iF[1], r), CGraphicsContext::ECenter);
			}
		}
	void DrawCalls(CWindowGc& gc) const
		{
		const TInt n = iLog ? iLog->iEntries.Count() : 0;
		const TRect clip(0, iHeaderH + iTabH, iW, iH - iToolH);
		if (n == 0)
			{
			gc.UseFont(iF[4]);
			gc.SetPenColor(TRgb(0xC8, 0xC8, 0xC8));
			gc.DrawText(_L("No recent calls"), clip, Baseline(iF[4], clip), CGraphicsContext::ECenter);
			return;
			}
		gc.SetClippingRect(clip);
		for (TInt i = 0; i < n; i++)
			{
			const TRect r = RowRect(i);
			if (r.iBr.iY < clip.iTl.iY || r.iTl.iY > clip.iBr.iY)
				continue;
			const CEtCallLog::TEntry& e = iLog->iEntries[i];
			const TRect t1(r.iTl.iX + Sc(11), r.iTl.iY, r.iBr.iX - Sc(11), r.iTl.iY + r.Height() / 2 + Sc(2));
			const TRect t2(r.iTl.iX + Sc(11), r.iTl.iY + r.Height() / 2, r.iBr.iX - Sc(11), r.iBr.iY);
			gc.UseFont(iF[3]);
			gc.SetPenColor(TRgb(255, 255, 255));
			gc.DrawText(e.iName.Length() ? e.iName : e.iNumber, t1, t1.Height() - Sc(3), CGraphicsContext::ELeft);
			gc.UseFont(iF[4]);
			gc.SetPenColor(TRgb(0xC8, 0xC8, 0xC8));
			gc.DrawText(e.iDir == 0 ? _L("Received") : (e.iDir == 1 ? _L("Dialled") : _L("Missed")), t2, iF[4]->AscentInPixels() + Sc(1), CGraphicsContext::ELeft);
			}
		gc.CancelClippingRect();
		}
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKey, TEventCode aType)
		{
		if (aType != EEventKey)
			return EKeyWasNotConsumed;
		const TUint c = aKey.iCode;
		if ((c >= '0' && c <= '9') || c == '*' || c == '#' || c == '+')
			{
			if (iTab == ETabKeypad)
				Press(c);
			return EKeyWasConsumed;
			}
		if (c == EKeyBackspace)
			{
			Backspace();
			return EKeyWasConsumed;
			}
		if (c == EKeyEnter || c == EKeyDevice3)
			{
			if (iTab == ETabKeypad)
				Call();
			return EKeyWasConsumed;
			}
		if (c == EKeyLeftArrow)
			{
			SetTab(ETabKeypad);
			return EKeyWasConsumed;
			}
		if (c == EKeyRightArrow)
			{
			SetTab(ETabCalls);
			return EKeyWasConsumed;
			}
		if (c == EKeyEscape)
			{
			CEikonEnv::Static()->EikAppUi()->HandleCommandL(EEikCmdExit);
			return EKeyWasConsumed;
			}
		return EKeyWasNotConsumed;
		}
	void HandlePointerEventL(const TPointerEvent& aE)
		{
		if (aE.iType != TPointerEvent::EButton1Up)
			return;
		const TPoint p = aE.iPosition;
		if (BackRect().Contains(p))
			{
			CEikonEnv::Static()->EikAppUi()->HandleCommandL(EEikCmdExit);
			return;
			}
		for (TInt t = 0; t < 2; t++)
			if (TabRect(t).Contains(p))
				{
				SetTab(t);
				return;
				}
		if (iTab == ETabKeypad)
			{
			for (TInt k = 0; k < 12; k++)
				if (KeyRect(k).Contains(p))
					{
					Press(KKeys[k]);
					return;
					}
			if (CallRect().Contains(p))
				Call();
			else if (DelRect().Contains(p))
				Backspace();
			}
		else
			for (TInt i = 0; iLog && i < iLog->iEntries.Count(); i++)
				if (RowRect(i).Contains(p))
					{
					iNumber.Copy(iLog->iEntries[i].iNumber);
					SetTab(ETabKeypad);
					return;
					}
		}
	void SizeChangedL(const TRect&) {}
	void Relayout(const TRect& aRect) { SetRect(aRect); Layout(aRect); DrawNow(); }
	TBuf<32> iNumber;
private:
	static const TText KKeys[12];
	CEtDial* iDial = NULL;
	CEtCallLog* iLog = NULL;
	CFont* iF[9];
	CFbsBitmap iKeyBmp, iKeyMask, iBackBmp, iBackMask, iCallBmp, iCallMask;
	TInt iW = 0, iH = 0, iUnit = 0, iHeaderH = 0, iTabH = 0, iToolH = 0, iNumH = 0, iKey = 0, iTbSize = 0, iScroll = 0, iTab = 0, iStatus = 0;
	};

const TText CEtPhoneContainer::KKeys[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#' };

void CEtDial::RunL() { iOwner.DialResult(iStatus.Int()); }

void CEtCallLog::Finished() { iOwner.LogChanged(); }
void CEtCallLog::RunL()
	{
	if (iStatus.Int() != KErrNone)
		{
		RDebug::Printf("PH: [etesianphone] call log: %d", iStatus.Int());
		Finished();
		return;
		}
	if (iPhase == 0)
		{
		iPhase = 1;
		if (!iView->FirstL(iStatus))
			{
			Finished();
			return;
			}
		SetActive();
		return;
		}
	// one event
	TEntry e;
	e.iNumber.Copy(iView->Event().Number().Left(32));
	e.iName.Copy(iView->Event().RemoteParty().Left(32));
	e.iDir = iView->Event().Direction() == _L("Incoming") ? 0 : (iView->Event().Direction() == _L("Missed call") ? 2 : 1);
	if (iEntries.Count() < 30)
		iEntries.Append(e);
	if (iEntries.Count() < 30 && iView->NextL(iStatus))
		SetActive();
	else
		{
		RDebug::Printf("PH: [etesianphone] call log: %d entries", iEntries.Count());
		Finished();
		}
	}

class CEtPhoneAppUi : public CAknAppUi
	{
public:
	void ConstructL()
		{
		BaseConstructL(EAknEnableSkin);
		if (StatusPane())
			StatusPane()->MakeVisible(EFalse);
		iView = new (ELeave) CEtPhoneContainer;
		iView->ConstructL(ApplicationRect());
		AddToStackL(iView);
		RDebug::Printf("PH: [etesianphone] up");
		}
	~CEtPhoneAppUi()
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
	void HandleResourceChangeL(TInt aType)
		{
		CAknAppUi::HandleResourceChangeL(aType);
		if (aType == KEikDynamicLayoutVariantSwitch && iView)
			iView->Relayout(ApplicationRect());
		}
private:
	CEtPhoneContainer* iView = NULL;
	};

class CEtPhoneDocument : public CAknDocument
	{
public:
	CEtPhoneDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
	CEikAppUi* CreateAppUiL() { return new (ELeave) CEtPhoneAppUi; }
	};

class CEtPhoneApplication : public CAknApplication
	{
public:
	TUid AppDllUid() const { return KEtesianPhoneUid; }
	CApaDocument* CreateDocumentL() { return new (ELeave) CEtPhoneDocument(*this); }
	};

LOCAL_C CApaApplication* NewApplication() { return new CEtPhoneApplication; }
GLDEF_C TInt E32Main() { return EikStart::RunApplication(NewApplication); }
