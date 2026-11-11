// Copyright (c) 1999-2009 Nokia Corporation and/or its subsidiary(-ies).
// All rights reserved.
// This component and the accompanying materials are made available
// under the terms of "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
//
// Initial Contributors:
// Nokia Corporation - initial contribution.
//
// Contributors:
//
// Description:
//

#include <e32std.h>
#include "gnubtport.h"
#include "btcommutil.h"

#ifndef GNUBTCOM_NOLOG
void GnuBtLogFunc(const char* aFunc)
	{
	RFileLogger::Write(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, TPtrC8((const TUint8*)aFunc));
	}
#endif

void BTCommUtil::Panic(TBTCommPanic aPanic)
	{
	LOG1(_L("PANIC GnuBtCom %d"), aPanic)
	User::Panic(KBTCommPanicName, aPanic);
	}

void BTCommUtil::Panic(TBTCommPanic aPanic, TInt aState)
	{
	LOG2(_L("PANIC GnuBtCom %d in state %d"), aPanic, aState)
	User::Panic(KBTCommPanicName, (aPanic*KStatePanicMultiplier) + KStatePanicDelimiter + aState);
	}
