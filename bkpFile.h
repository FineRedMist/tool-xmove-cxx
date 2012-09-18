

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include <windows.h>
#include "bzlib.h"

class CBackupFileWrite
{
protected:
	CBackupFileWrite();

public:
	~CBackupFileWrite();

	static CBackupFileWrite* Open(const WCHAR *szFile);
	static void Close(CBackupFileWrite *cbfw);

	void BeginBlock(bool fCompressed);
	BOOL Write(const BYTE *pbData, DWORD dwLen);
	BOOL Flush(bool fFinish = false);

	DWORDLONG Seek(DWORDLONG dwlPosition = 0, DWORD dwFrom = FILE_CURRENT);

	void ClearStatistics();
	DWORDLONG BytesWritten();
	DWORDLONG BytesCompressed();

	DWORD LastError();

protected:
	BOOL m_fFlushed;
	BOOL m_fCompressing;
	bz_stream m_stream;
	BYTE m_bWriteBuffer[65536];
	HANDLE m_hFile;
	DWORDLONG m_dwlWritten, m_dwlCompressed;

	DWORD m_dwError;
};


class CBackupFileRead
{
protected:
	CBackupFileRead();

public:
	~CBackupFileRead();

	static CBackupFileRead* Open(const WCHAR *szFile);
	static void Close(CBackupFileRead *cbfw);

	void BeginBlock(bool fCompressed);
	BOOL Read(BYTE *pbData, DWORD dwLen);

	DWORDLONG Seek(DWORDLONG dwlPosition = 0, DWORD dwFrom = FILE_CURRENT);

	DWORD LastError();

protected:
	BOOL m_fCompression;
	bz_stream m_stream;
	BYTE m_bReadBuffer[65536];
	HANDLE m_hFile;

	DWORD m_dwError;
};