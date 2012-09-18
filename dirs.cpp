
#include "layout.h"
#include "xmove.h"
#include "bkpFile.h"
#include "identitylist.h"
#include "stack.h"

static stackString ssCurDir;

int dir(const WCHAR *szBackupFile)
{
	int res = 1;
	sHeaderRegion hdr;
	WCHAR *szName = 0;
	WCHAR *szStreamName = 0;
	CIdentityList idList;

	CBackupFileRead *cbfr = CBackupFileRead::Open(szBackupFile);
	
	if(cbfr == 0 || (res = cbfr->LastError()) != 0)
		goto Exit;

	cbfr->BeginBlock(false);

	if(!cbfr->Read((BYTE *) &hdr, sizeof(hdr)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	if(0 != (res = CheckHeader(hdr)))
		return res;

	hdr.szDesc[strlen(hdr.szDesc) - 1] = 0;
	puts(hdr.szDesc);

	DWORDLONG dwl = cbfr->Seek(hdr.dwDataOffset, FILE_BEGIN);
	if(dwl != hdr.dwDataOffset)
	{
		res = cbfr->LastError();
		goto Exit;
	}

	cbfr->BeginBlock(true);

	for(;;)
	{
		DWORDLONG dwlTotalSize = 0;
		BYTE typeIndicator = 0;
		if(!cbfr->Read((BYTE *) &typeIndicator, sizeof(BYTE)))
		{
			res = cbfr->LastError();
			goto Exit;
		}

		if(typeIndicator == BackupID_End)
			break;

		if(typeIndicator == BackupID_EndDirectory)
		{
			if(ssCurDir.Count())
			{
				WCHAR *szTemp = ssCurDir.Pop();
				free(szTemp);
			}
			continue;
		}

		if(typeIndicator != BackupID_File && typeIndicator != BackupID_Directory)
		{
			fputs("Error: The backup file is invalid.  The backup type is invalid.\n", stderr);
			goto Exit;
		}

		DWORD dwAttributes = 0;
		if(!cbfr->Read((BYTE *) &dwAttributes, sizeof(DWORD)))
		{
			res = cbfr->LastError();
			goto Exit;
		}

		WORD wSize = 0;
		if(!cbfr->Read((BYTE *) &wSize, sizeof(WORD)))
		{
			res = cbfr->LastError();
			goto Exit;
		}

		szName = (WCHAR *) malloc(wSize);

		if(!cbfr->Read((BYTE *) szName, wSize))
		{
			res = cbfr->LastError();
			goto Exit;
		}

		if(typeIndicator == BackupID_Directory)
		{
			DWORD dwNameLen = wcslen(szName);
			DWORD dwCurDirLen = (ssCurDir.Peek() ? wcslen(ssCurDir.Peek()) + 1 : 0);
			WCHAR *szFullName = (WCHAR *) malloc((dwNameLen + dwCurDirLen + 1) * sizeof(WCHAR));
			if(dwCurDirLen)
			{
				wcscpy(szFullName, ssCurDir.Peek());
				szFullName[dwCurDirLen - 1] = L'\\';
			}
			wcscpy(szFullName + dwCurDirLen, szName);
			ssCurDir.Push(szFullName);
			printf("Directory: \"%S\"\n", szFullName);
		}

		for(;;)
		{
			DWORD dwStreamID = 0, dwStreamAttributes = 0, dwStreamNameSize = 0;
			DWORDLONG dwlSize = 0;

			if(!cbfr->Read((BYTE *) &dwStreamID, sizeof(DWORD)))
			{
				res = cbfr->LastError();
				goto Exit;
			}

			if(dwStreamID == BACKUP_INVALID)
				break;

			if(dwStreamID > BACKUP_SPARSE_BLOCK)
			{
				fputs("Error: The backup file is invalid.  The stream ID is invalid.\n", stderr);
				goto Exit;
			}

			if(dwStreamID != BACKUP_SECURITY_DATA)
			{
				if(!cbfr->Read((BYTE *) &dwStreamAttributes, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					goto Exit;
				}

				if(!cbfr->Read((BYTE *) &dwlSize, sizeof(DWORDLONG)))
				{
					res = cbfr->LastError();
					goto Exit;
				}
				
				if(!cbfr->Read((BYTE *) &dwStreamNameSize, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					goto Exit;
				}

				szStreamName = (WCHAR *) malloc(dwStreamNameSize + sizeof(WCHAR));
				szStreamName[dwStreamNameSize / sizeof(WCHAR)] = 0;
				if(!cbfr->Read((BYTE *) szStreamName, dwStreamNameSize))
				{
					res = cbfr->LastError();
					goto Exit;
				}

				DWORDLONG dwlToRead = dwlSize;
				while(dwlToRead > 0)
				{
					DWORD dwToRead = (DWORD) min(sizeof(copyBuffer), dwlToRead);
					if(!cbfr->Read(copyBuffer, dwToRead))
					{
						res = cbfr->LastError();
						goto Exit;
					}
					dwlToRead -= dwToRead;
				}
			}
			else // dwStreamID == BACKUP_SECURITY
			{
				DWORD dwSecDescLen = 0;
				if(!cbfr->Read((BYTE *) &dwSecDescLen, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					goto Exit;
				}

				DWORDLONG dwlToRead = dwSecDescLen;
				while(dwlToRead > 0)
				{
					DWORD dwToRead = (DWORD) min(sizeof(copyBuffer), dwlToRead);
					if(!cbfr->Read(copyBuffer, dwToRead))
					{
						res = cbfr->LastError();
						goto Exit;
					}
					dwlToRead -= dwToRead;
				}

				FILETIME ftdate;
				for(int i = 0; i < 3; ++i)
				{
					if(!cbfr->Read((BYTE *) &ftdate, sizeof(FILETIME)))
					{
						res = cbfr->LastError();
						goto Exit;
					}
				}
			}  // dwStreamID == BACKUP_SECURITY

			dwlTotalSize += dwlSize;

			if(typeIndicator == BackupID_File && (dwStreamID == BACKUP_DATA || dwStreamID == BACKUP_ALTERNATE_DATA))
			{
				if(dwStreamID == BACKUP_DATA)
				{
					printf("\t     File Stream: %15I64d bytes \"%S\"\n", dwlSize, szName);
				}
				else
				{
					printf("\tAlternate Stream: %15I64d bytes \"%S%S\"\n", dwlSize, szName, szStreamName);
				}
			}

			if(szStreamName)
				free(szStreamName);
			szStreamName = 0;
		} // while(1) -- for each stream

		if(szName)
			free(szName);
		szName = 0;
	} // while(1) -- for each file/dir

Exit:
	if(szStreamName)
		free(szStreamName);
	if(szName)
		free(szName);
	if(cbfr)
		delete cbfr;
	return res;
}
