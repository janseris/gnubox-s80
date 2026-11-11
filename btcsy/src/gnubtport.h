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

// Diagnostic log: C:\Logs\GnuBtCom\gnubtcom.txt. Every function entered and the original
// BTCOMM log lines. On only if that folder exists when the driver is loaded (checked once,
// the flag is the DLL's thread-local storage in the comms thread): each line costs a file
// logger call, which slows the driver to a few kB/s. Compiled out with GNUBTCOM_NOLOG.
#ifndef GNUBTCOM_NOLOG
#include <flogger.h>
_LIT(KGnuBtLogDir, "GnuBtCom");
_LIT(KGnuBtLogFile, "gnubtcom.txt");
void GnuBtLogFunc(const char* aFunc);
_LIT(KGnuBtLogFolder, "C:\\Logs\\GnuBtCom\\");
void GnuBtLogInit();	// sets the flag from the folder
#define GNUBT_LOGGING (Dll::Tls()!=NULL)
#define LOG_FUNC { if (GNUBT_LOGGING) GnuBtLogFunc(__PRETTY_FUNCTION__); }
#define LOG_STATIC_FUNC LOG_FUNC
#define LOG(a) { if (GNUBT_LOGGING) RFileLogger::Write(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a); }
#define LOG1(a,b) { if (GNUBT_LOGGING) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b); }
#define LOG2(a,b,c) { if (GNUBT_LOGGING) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b, c); }
#define LOG3(a,b,c,d) { if (GNUBT_LOGGING) RFileLogger::WriteFormat(KGnuBtLogDir, KGnuBtLogFile, EFileLoggingModeAppend, a, b, c, d); }
#else
inline void GnuBtLogInit() {}
#define LOG_FUNC
#define LOG_STATIC_FUNC
#define LOG(a)
#define LOG1(a,b)
#define LOG2(a,b,c)
#define LOG3(a,b,c,d)
#endif

#endif
