// gnubtport.h - what GnuBox's port of BTCOMM needs to build for Symbian OS 7.0s (EKA1, GCC 2.9):
// the Symbian^3 sources use the Bluetooth logger and a few EKA2 macros that 7.0s doesn't have.
// Logging is compiled out. EPL v1.0, like the rest of this folder.

#ifndef GNUBTPORT_H
#define GNUBTPORT_H

#ifndef NONSHARABLE_CLASS
#define NONSHARABLE_CLASS(x) class x
#endif

#define LOG_COMPONENT_BT_COMM "gnubtcom"
#define FTRACE(a)
#define CLOSE_LOGGER
#define CONNECT_LOGGER
#define OPEN_LOGGER

// Diagnostic log: C:\Logs\GnuBtCom\gnubtcom.txt, written only if that folder exists
// (Symbian's file logger skips the write otherwise). Every function entered and the
// original BTCOMM log lines. Off when GNUBTCOM_NOLOG is defined.
#ifndef GNUBTCOM_NOLOG
#include <flogger.h>
_LIT(KGnuBtLogDir, "GnuBtCom");
_LIT(KGnuBtLogFile, "gnubtcom.txt");
void GnuBtLogFunc(const char* aFunc);
#define LOG_FUNC GnuBtLogFunc(__PRETTY_FUNCTION__);
#define LOG_STATIC_FUNC GnuBtLogFunc(__PRETTY_FUNCTION__);
#define LOG(a) RFileLogger::Write(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a);
#define LOG1(a,b) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b);
#define LOG2(a,b,c) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b, c);
#define LOG3(a,b,c,d) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b, c, d);
#else
#define LOG_FUNC
#define LOG_STATIC_FUNC
#define LOG(a)
#define LOG1(a,b)
#define LOG2(a,b,c)
#define LOG3(a,b,c,d)
#endif

#endif
