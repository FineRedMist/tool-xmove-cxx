

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include "bzlib.h"

#define XMB_VERSION	01000064
#define XMB_VERSION_STRING	"1.0 build 100"


#define BIT_TEST(var, val) ((var & val) == val)

#define ARRAYSIZE(arr)  ((sizeof(arr))/(sizeof(arr[0])))

extern int verbosity;
extern int compressionfactor;
extern bool testing;
extern BYTE copyBuffer[1048576];


int backup(const WCHAR *szSrcPath, const WCHAR *szBackupFile, const WCHAR *szHelpText);
int restore(const WCHAR *szDstPath, const WCHAR *szBackupFile, const WCHAR *szMapFile);
int test(const WCHAR *szBackupFile, const WCHAR *szMapFile, bool fUsersOnly = false);
int dir(const WCHAR *szBackupFile);
int users(const WCHAR *szBackupFile);
