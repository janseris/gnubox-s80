// gnubtport.h - what GnuBox's port of BTCOMM needs to build for Symbian OS 7.0s (EKA1, GCC 2.9):
// the Symbian^3 sources use the Bluetooth logger and a few EKA2 macros that 7.0s doesn't have.
// Logging is compiled out. EPL v1.0, like the rest of this folder.

#ifndef GNUBTPORT_H
#define GNUBTPORT_H

#ifndef NONSHARABLE_CLASS
#define NONSHARABLE_CLASS(x) class x
#endif

#define LOG_COMPONENT_BT_COMM "gnubtcom"
#define LOG_FUNC
#define LOG_STATIC_FUNC
#define LOG(a)
#define LOG1(a,b)
#define LOG2(a,b,c)
#define LOG3(a,b,c,d)
#define FTRACE(a)
#define CLOSE_LOGGER
#define CONNECT_LOGGER
#define OPEN_LOGGER

#endif
