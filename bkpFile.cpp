
#include "xmove.h"
#include "bkpFile.h"

CBackupFileWrite::CBackupFileWrite()
{
	m_hFile = INVALID_HANDLE_VALUE;
	m_dwlWritten = m_dwlCompressed = 0;
	memset(&m_stream, 0, sizeof(m_stream));
	m_dwError = 0;
	m_fCompressing = FALSE;
	m_fFlushed = TRUE;
}

CBackupFileWrite::~CBackupFileWrite()
{
	Close(this);
}

CBackupFileWrite* CBackupFileWrite::Open(const WCHAR *szFile)
{
	CBackupFileWrite *cbfw = new CBackupFileWrite();
	cbfw->m_hFile = CreateFile(szFile, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
	if(!cbfw->m_hFile || cbfw->m_hFile == INVALID_HANDLE_VALUE)
	{
		cbfw->m_dwError = GetLastError();
	}

	return cbfw;
}

void CBackupFileWrite::Close(CBackupFileWrite *cbfw)
{
	if(!cbfw || !cbfw->m_hFile || cbfw->m_hFile == INVALID_HANDLE_VALUE)
		return;
	cbfw->Flush();
	BZ2_bzCompress(&cbfw->m_stream, BZ_FINISH);
	CloseHandle(cbfw->m_hFile);
	cbfw->m_hFile = INVALID_HANDLE_VALUE;
}


void CBackupFileWrite::BeginBlock(bool fCompressed)
{
	if(m_fCompressing && !fCompressed)
	{
		Flush(true);
	}
	else if(!m_fCompressing && fCompressed)
	{
		m_stream.next_in = 0;
		m_stream.next_out = (char *) m_bWriteBuffer;
		m_stream.avail_in = 0;
		m_stream.avail_out = sizeof(m_bWriteBuffer);
		m_stream.bzalloc = 0;
		m_stream.bzfree = 0;
		m_stream.opaque = 0;
		m_fFlushed = TRUE;
		m_dwError = BZ2_bzCompressInit(&m_stream, 9, 0, 30 + ((compressionfactor - 1) * 110));
		if(m_dwError != BZ_OK)
			return;
	}

	m_fCompressing = fCompressed;
}

BOOL CBackupFileWrite::Write(const BYTE *pbData, DWORD dwLen)
{
	if(!dwLen)
		return TRUE;

	BOOL res = FALSE;
	DWORD dwWritten;
	if(!m_fCompressing)
	{
		if(!WriteFile(m_hFile, pbData, dwLen, &dwWritten, 0) || dwWritten != dwLen)
		{
			m_dwError = GetLastError();
			if(!m_dwError)
				m_dwError = 1;
			goto Exit;
		}
		res = TRUE;
		m_dwlWritten += dwWritten;
		goto Exit;
	}

	// I am compressing here.
	m_fFlushed = FALSE;
	m_stream.next_in = (char *) pbData;
	m_stream.avail_in = dwLen;
	m_stream.avail_out = sizeof(m_bWriteBuffer);
	m_stream.next_out = (char *) m_bWriteBuffer;

	m_dwError = BZ2_bzCompress(&m_stream, 0);
	if(m_dwError != BZ_RUN_OK)
		goto Exit;
	while(m_stream.avail_out < sizeof(m_bWriteBuffer) || m_stream.avail_in > 0)
	{
		if(m_stream.avail_out < sizeof(m_bWriteBuffer))
		{
			DWORD dwToWrite = sizeof(m_bWriteBuffer) - m_stream.avail_out;
			if(!WriteFile(m_hFile, m_bWriteBuffer, dwToWrite, &dwWritten, 0) || dwWritten != dwToWrite)
			{
				m_dwError = GetLastError();
				if(!m_dwError)
					m_dwError = 1;
				goto Exit;
			}
			m_stream.avail_out = sizeof(m_bWriteBuffer);
			m_stream.next_out = (char *) m_bWriteBuffer;
			m_dwlCompressed += dwWritten;
		}
		if(m_stream.avail_in > 0)
		{
			m_dwError = BZ2_bzCompress(&m_stream, 0);
			if(m_dwError != BZ_RUN_OK)
				goto Exit;
		}
	}

	res = TRUE;
	m_dwlWritten += dwLen;
	
Exit:
	return res;
}

BOOL CBackupFileWrite::Flush(bool fFinish)
{
	BOOL res = FALSE;
	DWORD dwBZRes = 0;
	DWORD dwWritten;
	if(!m_fCompressing || m_fFlushed)
	{
		return TRUE;
	}

	DWORD dwGoal = (fFinish) ? BZ_FINISH : BZ_FLUSH;
	DWORD dwContinue = (fFinish) ? BZ_FINISH_OK : BZ_FLUSH_OK;
	DWORD dwDone = (fFinish) ? BZ_STREAM_END : BZ_RUN_OK;
	m_stream.avail_in = 0;
	m_stream.next_in = 0;
	m_stream.avail_out = sizeof(m_bWriteBuffer);
	m_stream.next_out = (char *) m_bWriteBuffer;
	dwBZRes = BZ2_bzCompress(&m_stream, dwGoal);
	if(dwBZRes != dwContinue && dwBZRes != dwDone)
	{
		m_dwError = 1;
		goto Exit;
	}

	while(m_stream.avail_out < sizeof(m_bWriteBuffer))
	{
		DWORD dwToWrite = sizeof(m_bWriteBuffer) - m_stream.avail_out;
		if(!WriteFile(m_hFile, m_bWriteBuffer, dwToWrite, &dwWritten, 0) || dwWritten != dwToWrite)
		{
			m_dwError = GetLastError();
			if(!m_dwError)
				m_dwError = 1;
			goto Exit;
		}

		m_stream.avail_out = sizeof(m_bWriteBuffer);
		m_stream.next_out = (char *) m_bWriteBuffer;
		m_dwlCompressed += dwWritten;

		if(dwBZRes != dwContinue)
			break;

		dwBZRes = BZ2_bzCompress(&m_stream, dwGoal);
	}

	if(dwBZRes != dwDone)
		goto Exit;

	m_dwError = 0;
	if(fFinish)
	{
		BZ2_bzCompressEnd(&m_stream);
		m_fCompressing = false;
	}
	m_fFlushed = TRUE;
	res = TRUE;

Exit:
	return res;
}

DWORDLONG CBackupFileWrite::Seek(DWORDLONG dwlPosition, DWORD dwFrom)
{
	DWORDLONG dwlNewPosition;
	Flush();
	if(!SetFilePointerEx(m_hFile, *((LARGE_INTEGER *) &dwlPosition), (LARGE_INTEGER *) &dwlNewPosition, dwFrom))
	{
		m_dwError = GetLastError();
		return (DWORDLONG) -1;
	}
	return dwlNewPosition;
}

void CBackupFileWrite::ClearStatistics()
{
	m_dwlWritten = m_dwlCompressed = 0;
}

DWORDLONG CBackupFileWrite::BytesWritten()
{
	return m_dwlWritten;
}

DWORDLONG CBackupFileWrite::BytesCompressed()
{
	return m_dwlCompressed;
}

DWORD CBackupFileWrite::LastError()
{
	return m_dwError;
}




CBackupFileRead::CBackupFileRead()
{
	m_hFile = INVALID_HANDLE_VALUE;
	memset(&m_stream, 0, sizeof(m_stream));
	m_dwError = 0;
	m_fCompression = FALSE;
}

CBackupFileRead::~CBackupFileRead()
{
	Close(this);
}


CBackupFileRead* CBackupFileRead::Open(const WCHAR *szFile)
{
	CBackupFileRead *cbfr = new CBackupFileRead();
	cbfr->m_hFile = CreateFile(szFile, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
	if(!cbfr->m_hFile || cbfr->m_hFile == INVALID_HANDLE_VALUE)
	{
		cbfr->m_dwError = GetLastError();
	}

	return cbfr;
}

void CBackupFileRead::Close(CBackupFileRead *cbfr)
{
	if(!cbfr || !cbfr->m_hFile || cbfr->m_hFile == INVALID_HANDLE_VALUE)
		return;
	if(cbfr->m_fCompression)
		BZ2_bzDecompress(&cbfr->m_stream);
	CloseHandle(cbfr->m_hFile);
	cbfr->m_hFile = INVALID_HANDLE_VALUE;
}


void CBackupFileRead::BeginBlock(bool fCompressed)
{
	if(m_fCompression && !fCompressed)
	{
		BZ2_bzDecompressEnd(&m_stream);
	}
	else if(!m_fCompression && fCompressed)
	{
		m_stream.next_in = (char *) m_bReadBuffer;
		m_stream.next_out = 0;
		m_stream.avail_in = 0;
		m_stream.avail_out = 0;
		m_stream.bzalloc = 0;
		m_stream.bzfree = 0;
		m_stream.opaque = 0;
		m_dwError = BZ2_bzDecompressInit(&m_stream, 0, 0);
		if(m_dwError != BZ_OK)
			return;
	}

	m_fCompression = fCompressed;
}

BOOL CBackupFileRead::Read(BYTE *pbData, DWORD dwLen)
{
	if(!dwLen)
		return TRUE;

	DWORD dwBZRes = 0;
	BOOL res = FALSE;
	DWORD dwRead = 0;
	if(!m_fCompression)
	{
		if(!ReadFile(m_hFile, pbData, dwLen, &dwRead, 0) || dwRead != dwLen)
		{
			m_dwError = GetLastError();
			if(!m_dwError)
				m_dwError = 1;
			goto Exit;
		}
		res = TRUE;
		goto Exit;
	}

	// I am decompressing here.
	/* The algorithm is:
		While there is still data to be read (into the passed in buffer):
			If the in buffer is empty, fill it (as much as possible--may hit eof)
			Decompress as much data as possible/needed (which may be less than requested)
	*/
	m_stream.avail_out = dwLen;
	m_stream.next_out = (char *) pbData;
	while(m_stream.avail_out)
	{
		dwBZRes = BZ2_bzDecompress(&m_stream);
		if(dwBZRes != BZ_OK && dwBZRes != BZ_STREAM_END)
		{
			fprintf(stderr, "Error: Failure decompressing the backup file: %d\n", dwBZRes);
			m_dwError = 1;
			goto Exit;
		}

		if(m_stream.avail_in == 0 && m_stream.avail_out == 0 && dwBZRes == BZ_STREAM_END)
		{
			m_fCompression = false;
			BZ2_bzDecompressEnd(&m_stream);
		}

		if(m_stream.avail_in == 0 && m_stream.avail_out > 0)
		{
			m_stream.next_in = (char *) m_bReadBuffer;
			if(!ReadFile(m_hFile, m_bReadBuffer, sizeof(m_bReadBuffer), &dwRead, 0) && dwRead == 0)
			{
				m_dwError = GetLastError();
				if(!m_dwError)
					m_dwError = 1;
				goto Exit;
			}
			m_stream.avail_in = dwRead;
		}
	}

	res = TRUE;

Exit:
	return res;
}


DWORDLONG CBackupFileRead::Seek(DWORDLONG dwlPosition, DWORD dwFrom)
{
	DWORDLONG dwlNewPosition;
	if(!SetFilePointerEx(m_hFile, *((LARGE_INTEGER *) &dwlPosition), (LARGE_INTEGER *) &dwlNewPosition, dwFrom))
	{
		m_dwError = GetLastError();
		return (DWORDLONG) -1;
	}
	return dwlNewPosition;
}


DWORD CBackupFileRead::LastError()
{
	return m_dwError;
}


