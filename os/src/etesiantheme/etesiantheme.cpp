// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// etesiantheme.cpp -- a test utility, not part of the package: "etesiantheme <theme name>" makes an installed theme the active one (the way the
// Themes application does, core's pheshelltheme.h); "etesiantheme -" restores the theme saved when a shell brought its own. Run it from the
// Terminal application's shell. Logs to the serial console.
#include <e32base.h>
#include <e32cons.h>
#include <e32debug.h>
#include "../phedevmenu/pheshelltheme.h"

static TInt Run()
	{
	TBuf<64> arg;
	User::CommandLine(arg);
	arg.Trim();
	if (arg.Length() == 0)
		{
		RDebug::Printf("PH: [etesiantheme] usage: etesiantheme <theme name> | -");
		return KErrArgument;
		}
	if (arg.Compare(_L("-")) == 0)
		{
		PheThemeLeaveShellL();
		return KErrNone;
		}
	TAknsPkgID pid;
	TInt loc = 0;
	TInt r = PheFindThemeL(arg, pid, loc);
	RDebug::Print(_L("PH: [etesiantheme] theme '%S' found: %d"), &arg, r);
	if (r == KErrNone)
		{
		r = PheActivateThemeL(pid, loc);
		RDebug::Print(_L("PH: [etesiantheme] theme '%S' activated: %d"), &arg, r);
		}
	return r;
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* tc = CTrapCleanup::New();
	if (!tc)
		return KErrNoMemory;
	TInt r = KErrNoMemory;
	TRAPD(e, r = Run());
	delete tc;
	return e != KErrNone ? e : r;
	}
