

#include "bkpFile.h"
#include "stack.h"
#include "layout.h"
#include "xmove.h"
#include "sdhelpers.h"

#include <windows.h>
#include <stdio.h>

class CFileInfo
{
public:
	CFileInfo()
	{
		m_dwAttr = 0;
		m_szName = 0;
	};
	~CFileInfo()
	{
		if(m_szName)
			delete [] m_szName;
	}

	DWORD m_dwAttr;
	WCHAR *m_szName;

};

typedef stack<CFileInfo *> stackFiles;

static CBackupFileWrite *pBackupFile = 0;
static sHeaderRegion bkpHeader;
static stackFiles ssTodo;
static CIdentityList idList;
static stackString ssCurDir;

CHAR *GetDateTime();
CHAR *GetHostName();
DWORD dwStartNameAt = 0;

int backupData();
int backupUsers();

#define ERRORWITHGOTO(err, desc, dest)								\
	{																\
		err = (int) GetLastError();									\
		fprintf(stderr, "Error: %s Error %d\n", desc, err);			\
		goto dest;													\
	}

int WriteBackupData(const VOID *data, size_t dwSize, const char *szDesc)
{
	int res = 0;
	;
	if(!pBackupFile->Write((const BYTE *) data, (DWORD) dwSize))
	{
		res = pBackupFile->LastError();
		fprintf(stderr, "Error: Failed to write %s to the backup file.  Error %d\n", szDesc, res);
	}
	return res;
}

const char *szMeasures[] =
	{
		"bytes",
		"kilobytes",
		"megabytes",
		"gigabytes",
		"terabytes"
	};

int backup(const WCHAR *szSrcPath, const WCHAR *szBackupFile, const WCHAR *szHelpText)
{
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	int res = 0;

	InitializeHeader(bkpHeader);
	memset(&wfad, 0, sizeof(wfad));

	if(!GetFileAttributesEx(szSrcPath, GetFileExInfoStandard, &wfad))
	{
		res = (int) GetLastError();
		fprintf(stderr, "Error: Failed to examine the file \"%S\".  Error %d\n", szSrcPath, res);
		goto Exit;
	}

	DWORD len = wcslen(szSrcPath);
	for(DWORD i = 0; i < len; ++i)
	{
		if(i == 1 && szSrcPath[i] == L':')
			dwStartNameAt = i + 1;
		if(szSrcPath[i] == L'\\' || szSrcPath[i] == L'/')
			dwStartNameAt = i + 1;
	}
	if(dwStartNameAt >= len)
	{
		res = 1;
		fputs("Error: Do not end the file to backup with a '\\' or '/'\n", stderr);
		goto Exit;
	}
	pBackupFile = CBackupFileWrite::Open(szBackupFile);
	if(pBackupFile == 0 || pBackupFile->LastError())
		ERRORWITHGOTO(res, "Failed to open the backup file.", Exit);

	res = WriteBackupData(&bkpHeader, sizeof(bkpHeader), "the header block");
	if(res)
		goto Exit;

	pBackupFile->ClearStatistics();
	pBackupFile->BeginBlock(true);
	CFileInfo *cfi = new CFileInfo();
	cfi->m_szName = new WCHAR[(wcslen(szSrcPath) + 1) * sizeof(WCHAR)];
	cfi->m_dwAttr = wfad.dwFileAttributes;
	wcscpy(cfi->m_szName, szSrcPath);
	ssTodo.Push(cfi);
	res = backupData();
	if(res)
		goto Exit;

	pBackupFile->Flush(true);
	pBackupFile->BeginBlock(true);
	DWORDLONG dwlCurPos;
	dwlCurPos = pBackupFile->Seek();
	if(dwlCurPos == (DWORDLONG) -1)
		ERRORWITHGOTO(res, "Failed to seek in the backup file.", Exit);
	
	bkpHeader.dwlUserOffset = dwlCurPos;

	res = backupUsers();
	if(res)
		goto Exit;

	pBackupFile->Flush(true);
	DWORDLONG dwlNewPos;
	dwlNewPos = 0;
	dwlNewPos = pBackupFile->Seek(0, FILE_BEGIN);
	if(dwlNewPos == (DWORDLONG) -1)
		ERRORWITHGOTO(res, "Failed to seek in the backup file.", Exit);

	DWORD dwRawIndex = 0;
	DWORDLONG dwlRaw = pBackupFile->BytesWritten();
	while((dwRawIndex + 1 < ARRAYSIZE(szMeasures)) && ((dwlRaw >> 10) > 100))
	{
		dwlRaw = (dwlRaw >> 10) + ((dwlRaw % 1024) > 511 ? 1 : 0);
		++dwRawIndex;
	}

	DWORD dwCompIndex = 0;
	DWORDLONG dwlComp = pBackupFile->BytesCompressed();
	while((dwCompIndex + 1 < ARRAYSIZE(szMeasures)) && ((dwlComp >> 10) > 100))
	{
		dwlComp = (dwlComp >> 10) + ((dwlComp % 1024) > 511 ? 1 : 0);
		++dwCompIndex;
	}

	double ratio = 100 - ((double) pBackupFile->BytesCompressed()) * 100 / ((double) pBackupFile->BytesWritten());
	sprintf(bkpHeader.szDesc, "XMove version %s\n       Computer Name %s\n         %s\n%20I64d %s total\n%20I64d %s compressed\n%19.1f%% compression ratio\n%20d files\n%20d directories\n%20d users\n\n%S\n\x1A",
						XMB_VERSION_STRING, GetHostName(), GetDateTime(), dwlRaw, szMeasures[dwRawIndex], dwlComp, szMeasures[dwCompIndex], ratio,
						bkpHeader.dwFileCount, bkpHeader.dwDirectoryCount, bkpHeader.dwUserCount, szHelpText);

	res = WriteBackupData(&bkpHeader, sizeof(bkpHeader), "the header block (2nd pass)");
	if(res)
		goto Exit;

	dwlComp = strlen(bkpHeader.szDesc);
	bkpHeader.szDesc[dwlComp - 1] = 0;
	printf("\nDone!\n\n%s", bkpHeader.szDesc);

Exit:
	if(pBackupFile)
		delete pBackupFile;
	return res;
}

int backupFile(const CFileInfo *cfi);
int backupDirectory(const CFileInfo *cfi);

int backupData()
{
	CFileInfo *cfi = 0;
	int res = 0;
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	memset(&wfad, 0, sizeof(wfad));

	while(!ssTodo.IsEmpty())
	{
		cfi = ssTodo.Pop();

		if(!wcscmp(cfi->m_szName, L".."))
		{
			delete cfi;
			cfi = 0;
			BYTE bEndDirectory = BackupID_EndDirectory;
			if(ssCurDir.Count())
			{
				WCHAR *szSrcPath = ssCurDir.Pop();
				free(szSrcPath);
			}
			res = WriteBackupData(&bEndDirectory, 1, "end of directory marker");
			if(res)
				goto Exit;
			continue;
		}

		if(BIT_TEST(cfi->m_dwAttr, FILE_ATTRIBUTE_DIRECTORY))
		{
			res = backupDirectory(cfi);
		}
		else
		{
			res = backupFile(cfi);
		}
		if(res)
			goto Exit;

		if(!verbosity)
			printf("Working: %d directories, %d files\r", bkpHeader.dwDirectoryCount, bkpHeader.dwFileCount);

		delete cfi;
		cfi = 0;
	}

	// Clears the verbosity output.
	if(!verbosity)
		puts("");

	char endID = BackupID_End;
	res = WriteBackupData(&endID, 1, "the data end marker");
	if(res)
		goto Exit;

Exit:
	if(cfi)
		delete cfi;
	return res;
}

int backupSecurity(HANDLE hObject, const WCHAR *szHelpString);

int backupFile(const CFileInfo *cfi)
{
	HANDLE hSrcFile = INVALID_HANDLE_VALUE;
	char fileID = BackupID_File;
	LPVOID lpContext = 0;
	int res = 0;
	DWORD dwCurDir = (ssCurDir.Peek() ? wcslen(ssCurDir.Peek()) + 1: 0);
	DWORD dwSrcLen = wcslen(cfi->m_szName);
	DWORD dwFileLen = dwSrcLen + dwCurDir + 1;
	WCHAR *szFile = (WCHAR *) malloc(dwFileLen * sizeof(WCHAR));
	if(dwCurDir)
	{
		wcscpy(szFile, (ssCurDir.Peek() ? ssCurDir.Peek() : L""));
		szFile[dwCurDir - 1] = L'\\';
	}
	wcscpy(szFile + dwCurDir, cfi->m_szName);

	hSrcFile = CreateFile(szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(!hSrcFile || hSrcFile == INVALID_HANDLE_VALUE)
	{
		res = (int) GetLastError();
		fprintf(stderr, "Warning: Failed to open the file \"%S\" for backup--Skipping.  Error %d\n", szFile, res);
		res = 0;
		goto Exit;
	}

	if(verbosity > 0)
		printf("Backing up file: \"%S\"\n", szFile);

	res = WriteBackupData(&fileID, 1, "the file start marker");
	if(res)
		goto Exit;
	
	res = WriteBackupData(&cfi->m_dwAttr, sizeof(DWORD), "the file attributes");
	if(res)
		goto Exit;

	WORD wLen = (WORD) ((dwSrcLen + 1) * sizeof(WCHAR));
	res = WriteBackupData(&wLen, sizeof(WORD), "the file name length");
	if(res)
		goto Exit;

	res = WriteBackupData(cfi->m_szName, wLen, "the file name");
	if(res)
		goto Exit;

	DWORD dwRead = 0;
	BOOL fSuccessful = true;
	for(	fSuccessful = BackupRead(hSrcFile, copyBuffer, sizeof(copyBuffer), &dwRead, false, false, &lpContext);
			fSuccessful && dwRead != 0;
			fSuccessful = BackupRead(hSrcFile, copyBuffer, sizeof(copyBuffer), &dwRead, false, false, &lpContext))
	{
		res = WriteBackupData(copyBuffer, dwRead, "the stream data");
		if(res)
			goto Exit;
	}
	if(!fSuccessful)
		ERRORWITHGOTO(res, "Failed to use BackupRead on the file.", Exit);

	res = backupSecurity(hSrcFile, szFile);

	if(res)
		goto Exit;

	++bkpHeader.dwFileCount;

Exit:
	if(szFile)
		free(szFile);
	if(lpContext)
		BackupRead(hSrcFile, 0, 0, 0, true, false, &lpContext);
	if(hSrcFile && hSrcFile != INVALID_HANDLE_VALUE)
		CloseHandle(hSrcFile);
	return res;
}

int backupDirectory(const CFileInfo *cfi)
{
	stackFiles tempStack;
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	memset(&wfad, 0, sizeof(wfad));
	HANDLE hSearch = INVALID_HANDLE_VALUE;
	HANDLE hSrcFile = INVALID_HANDLE_VALUE;
	char DirID = BackupID_Directory;
	LPVOID lpContext = 0;
	int res = 0;
	WCHAR *szSearchPath = 0;
	WCHAR *szNewSrcPath = 0;
	DWORD dwCurDir = (ssCurDir.Peek() ? wcslen(ssCurDir.Peek()) + 1: 0);
	DWORD dwSrcLen = wcslen(cfi->m_szName);
	DWORD dwFileLen = dwSrcLen + dwCurDir + 1;
	WCHAR *szFile = (WCHAR *) malloc(dwFileLen * sizeof(WCHAR));
	if(dwCurDir)
	{
		wcscpy(szFile, (ssCurDir.Peek() ? ssCurDir.Peek() : L""));
		szFile[dwCurDir - 1] = L'\\';
	}
	wcscpy(szFile + dwCurDir, cfi->m_szName);

	hSrcFile = CreateFile(szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(!hSrcFile || hSrcFile == INVALID_HANDLE_VALUE)
	{
		res = (int) GetLastError();
		fprintf(stderr, "Warning: Failed to open the directory \"%S\" for backup--Skipping.  Error %d\n", szFile, res);
		res = 0;
		goto Exit;
	}

	if(verbosity > 0)
		printf("Backing up directory: \"%S\"\n", szFile);

	res = WriteBackupData(&DirID, 1, "the directory start marker");
	if(res)
		goto Exit;
	
	res = WriteBackupData(&cfi->m_dwAttr, sizeof(DWORD), "the file attributes");
	if(res)
		goto Exit;

	WORD wLen = (WORD) ((dwSrcLen + 1) * sizeof(WCHAR));
	res = WriteBackupData(&wLen, sizeof(WORD), "the file name length");
	if(res)
		goto Exit;

	res = WriteBackupData(cfi->m_szName, wLen, "the directory name");
	if(res)
		goto Exit;

	DWORD dwRead = 0;
	BOOL fSuccessful = true;
	for(	fSuccessful = BackupRead(hSrcFile, copyBuffer, sizeof(copyBuffer), &dwRead, false, false, &lpContext);
			fSuccessful && dwRead != 0;
			fSuccessful = BackupRead(hSrcFile, copyBuffer, sizeof(copyBuffer), &dwRead, false, false, &lpContext))
	{
		res = WriteBackupData(copyBuffer, dwRead, "the stream data");
		if(res)
			goto Exit;
	}
	if(!fSuccessful)
		ERRORWITHGOTO(res, "Failed to use BackupRead on the directory.", Exit);

	res = backupSecurity(hSrcFile, szFile);
	if(res)
		goto Exit;

	if(hSrcFile && hSrcFile != INVALID_HANDLE_VALUE)
		CloseHandle(hSrcFile);
	hSrcFile = INVALID_HANDLE_VALUE;

	++bkpHeader.dwDirectoryCount;

	// For each file in the directory recurse
	{
		size_t dwLen = wcslen(szFile), dwLen2 = 0;
		WIN32_FIND_DATA wfd;
		szSearchPath = new WCHAR[(dwLen + 3) * sizeof(WCHAR)];
		wcscpy(szSearchPath, szFile);
		wcscpy(szSearchPath + dwLen, L"\\*");
		memset(&wfd, 0, sizeof(wfd));

		hSearch = FindFirstFile(szSearchPath, &wfd);
		if(hSearch == INVALID_HANDLE_VALUE)
		{
			res = (int) GetLastError();
			fprintf(stderr, "Error: Failed to enumerate files in the directory \"%S\" for backup.  Error %d\n", szFile, res);
			goto Exit;
		}

		delete [] szSearchPath;
		szSearchPath = 0;

		{	// Add the indicator to create an end marker
			szNewSrcPath = new WCHAR[3];
			szNewSrcPath[0] = szNewSrcPath[1] = L'.';
			szNewSrcPath[2] = 0;
			CFileInfo *newCFI = new CFileInfo();
			newCFI->m_szName = szNewSrcPath;
			ssTodo.Push(newCFI);
		}
		ssCurDir.Push(szFile);
		do
		{
			if(wcscmp(wfd.cFileName, L".") && wcscmp(wfd.cFileName, L".."))
			{
				CFileInfo *newCFI = new CFileInfo();
				newCFI->m_dwAttr = wfd.dwFileAttributes;
				dwLen2 = wcslen(wfd.cFileName);
				szNewSrcPath = new WCHAR[(dwLen2 + 1) * sizeof(WCHAR)];
				wcscpy(szNewSrcPath, wfd.cFileName);
				newCFI->m_szName = szNewSrcPath;
				if((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)
					tempStack.Push(newCFI);
				else
					ssTodo.Push(newCFI);
			}
		} while(FindNextFile(hSearch, &wfd));

		{
			CFileInfo *newCFI = 0;
			while(!tempStack.IsEmpty())
				ssTodo.Push((newCFI = tempStack.Pop()));
		}
		res = (int) GetLastError();
		if(res != ERROR_NO_MORE_FILES)
		{
			fprintf(stderr, "Error: Failed to enumerate the next file in the directory \"%S\" for backup.  Error %d\n", szFile, res);
			goto Exit;
		}
	}

	res = 0;

Exit:
	if(hSearch != INVALID_HANDLE_VALUE)
		FindClose(hSearch);
	if(szSearchPath)
		delete [] szSearchPath;
	if(lpContext)
		BackupRead(hSrcFile, 0, 0, 0, true, false, &lpContext);
	if(hSrcFile && hSrcFile != INVALID_HANDLE_VALUE)
		CloseHandle(hSrcFile);
	return res;
}

int backupSecurity(HANDLE hObject, const WCHAR *szHelpString)
{
	SECURITY_DESCRIPTOR *pSD = 0;
	SECURITY_INFORMATION secInf =	DACL_SECURITY_INFORMATION | 
									GROUP_SECURITY_INFORMATION | 
									OWNER_SECURITY_INFORMATION | 
									PROTECTED_DACL_SECURITY_INFORMATION | 
									UNPROTECTED_DACL_SECURITY_INFORMATION;

	DWORD len = 0;
	DWORD res = GetKernelObjectSecurity(hObject, secInf, pSD, len, &len);

	if(len)
	{
		pSD = (SECURITY_DESCRIPTOR *) malloc(len);

		res = GetKernelObjectSecurity(hObject, secInf, pSD, len, &len);
		if(!res)
		{
			fprintf(stderr, "Error: Failed to get security information from the file\"%S\".  Error: %d\n", szHelpString, res);
			goto Exit;
		}

		if(SubmitUsersFromSecurityDescriptor(pSD, idList, szHelpString))
			goto Exit;

		DWORD SecID = BACKUP_SECURITY_DATA;
		res = WriteBackupData(&SecID, sizeof(DWORD), "the security descriptor start marker");
		if(res)
			goto Exit;

		res = WriteBackupData(&len, sizeof(DWORD), "the size of the security descriptor");
		if(res)
			goto Exit;

		res = WriteBackupData(pSD, len, "the security descriptor");
		if(res)
			goto Exit;

		FILETIME ftCreated, ftAccessed, ftLastWrite;
		if(!GetFileTime(hObject, &ftCreated, &ftAccessed, &ftLastWrite))
		{
			res = GetLastError();
			fprintf(stderr, "Error: Failed to get the file times from \"%S\".  Error: %d\n", szHelpString, res);
			goto Exit;
		}

		res = WriteBackupData(&ftCreated, sizeof(FILETIME), "creation time");
		if(res)
			goto Exit;

		res = WriteBackupData(&ftAccessed, sizeof(FILETIME), "last access time");
		if(res)
			goto Exit;

		res = WriteBackupData(&ftLastWrite, sizeof(FILETIME), "last write time");
		if(res)
			goto Exit;
	}
	else if(res)
	{
		fprintf(stderr, "Error: Failed to get the security descriptor size for the object. Error %d\n", res);
		goto Exit;
	}

	DWORD EndID = BACKUP_INVALID;
	res = WriteBackupData(&EndID, sizeof(DWORD), "the end file/directory marker");
	if(res)
		goto Exit;

Exit:
	if(pSD)
		free(pSD);
	return res;
}

int backupUsers()
{
	DWORD dwLen = 0;
	LPBYTE pbData = idList.Serialize(dwLen);
	DWORD res = WriteBackupData(pbData, dwLen, "the user list");
	bkpHeader.dwUserCount = idList.Count();
	if(verbosity > 0)
        idList.Display();
	else
		printf("User/Group count: %d\n", idList.Count());
	return (int) res;
}

static CHAR szDateTime[256];

CHAR *GetDateTime()
{
	SYSTEMTIME st;
	GetLocalTime(&st);	
	
	szDateTime[sizeof(szDateTime) - 1] = 0;
	sprintf(szDateTime, "Date & Time %4d-%02d-%02d %d:%02d %s",
						st.wYear, st.wMonth, st.wDay,
						((st.wHour % 12 == 0) ? 12 : (st.wHour % 12)), st.wMinute, (st.wHour > 11 ? "PM" : "AM"));
	return szDateTime;
}

static CHAR szComputerName[256];

CHAR *GetHostName()
{
	DWORD dwLen = sizeof(szComputerName) - 1;
	BOOL res = GetComputerNameExA(ComputerNamePhysicalDnsFullyQualified, szComputerName, &dwLen);

	dwLen = sizeof(szComputerName) - 1;
	szComputerName[dwLen] = 0;

	if(res)
		return strlwr(szComputerName);

	res = GetComputerNameExA(ComputerNameDnsFullyQualified, szComputerName, &dwLen);

	dwLen = sizeof(szComputerName) - 1;
	if(res)
		return strlwr(szComputerName);


	res = GetComputerNameA(szComputerName, &dwLen);
	if(res)
		return strlwr(szComputerName);

	sprintf(szComputerName, "<Unavailable.  Error %d>", GetLastError());
	return szComputerName;
}