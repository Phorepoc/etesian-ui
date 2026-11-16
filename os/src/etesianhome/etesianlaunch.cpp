// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesianlaunch.cpp -- the Etesian shell's launcher exe. See etesianlaunch.mmp. Plain apparc + RProcess: nothing Phorepoc-specific.
#include <e32base.h>
#include <e32debug.h>
#include <apgcli.h>
#include <apacmdln.h>
#include <apgtask.h>
#include <f32file.h>
#include "../phedevmenu/pheshellapps.h"

const TUid KEtesianHomeUid = { 0xE7016B50 };

static TInt DoLaunch()
	{
	// shell-tagged apps: core's pheshellsel applies the tag files of the active shell at every start (pheshellapps.h); an image
	// from before that does not, so the launcher does it too (idempotent: the same lists are recomputed)
	RFs fs;
	if (fs.Connect() == KErrNone)
		{
		TRAP_IGNORE(PheApplyShellAppsL(fs, _L("etesian")));
		fs.Close();
		}
	RApaLsSession ls;
	TInt r = ls.Connect();
	if (r != KErrNone)
		return r;
	CleanupClosePushL(ls);
	TApaAppInfo info;
	// the application list is scanned asynchronously after boot (and a package install registers late): wait for Home to show up
	r = KErrNotFound;
	for (TInt i = 0; i < 80 && r != KErrNone; i++)
		{
		r = ls.GetAppInfo(info, KEtesianHomeUid);
		if (r != KErrNone)
			User::After(250000);
		}
	RDebug::Printf("PH: [etesianlaunch] Home registered: %d", r);
	TThreadId tid;
	if (r == KErrNone)
		{
		CApaCommandLine* cmd = CApaCommandLine::NewLC();
		cmd->SetExecutableNameL(info.iFullName);
		cmd->SetCommandL(EApaCommandRun);
		r = ls.StartApp(*cmd, tid);
		CleanupStack::PopAndDestroy(cmd);
		RDebug::Printf("PH: [etesianlaunch] StartApp: %d", r);
		}
	CleanupStack::PopAndDestroy(&ls);
	if (r != KErrNone)
		return r;
	RThread t;
	r = t.Open(tid);
	if (r != KErrNone)
		return r;
	TRequestStatus st;
	t.Logon(st);
	// up: the shell selector (pheshellsel) waits for this
	RProcess::Rendezvous(KErrNone);
	User::WaitForRequest(st);
	TInt reason = t.ExitReason();
	RDebug::Printf("PH: [etesianlaunch] Home exited (%d %d)", (TInt)t.ExitType(), reason);
	t.Close();
	return reason != KErrNone ? reason : KErrGeneral;
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* tc = CTrapCleanup::New();
	if (!tc)
		return KErrNoMemory;
	RDebug::Printf("PH: [etesianlaunch] start");
	TInt r = KErrNoMemory;
	TRAPD(e, r = DoLaunch());
	if (e != KErrNone)
		r = e;
	RProcess::Rendezvous(r);		// if Home never came up the selector hears the error instead of waiting
	delete tc;
	return r;
	}
