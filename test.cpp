
#include "layout.h"
#include "xmove.h"
#include "sdhelpers.h"
#include "identitymap.h"
#include "bkpfile.h"
#include "stack.h"

int test(const WCHAR *szBackupFile, const WCHAR *szMapFile, bool fUsersOnly)
{
	// Verify that the file and directory counts match
	// Verify that the user counts and SIDs match (Load the users first, then ensure that the # of users never change while loading each file)
	// If given a map, verifies that only legitimate users are mapped (User, Group, and Computer)
	// If given a map, ensures that owner and group labeled users are only mapped to one other user
	// If given a map, verifies the specified users can be found (lookup for their SID)
	// If not given a map, check to see if the users can be looked up (may be in the wrong domain, or deleted users)
	// Verify (implicitly) decompression worked
	int res = 1;
	sHeaderRegion hdr;
	BYTE *pbData = 0;
	CIdentityMap idMap;
	WCHAR *szName = 0;
	WCHAR *szStreamName = 0;
	DWORD dwFileCount = 0;
	DWORD dwDirCount = 0;
	SECURITY_DESCRIPTOR *sd = 0;
	stackString ssCurDir;
	DWORD dwInitUserCount = 0;
	bool fErrors = false;
	// Read in the header
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

	// hdr.szDesc[strlen(hdr.szDesc) - 1] = 0;
	// puts(hdr.szDesc);

	DWORDLONG dwl = cbfr->Seek(hdr.dwlUserOffset, FILE_BEGIN);
	if(dwl != hdr.dwlUserOffset)
	{
		res = cbfr->LastError();
		goto Exit;
	}

	// Read in the user list
	cbfr->BeginBlock(true);

	DWORD dwSize = 0;
	if(!cbfr->Read((BYTE *) &dwSize, sizeof(DWORD)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	pbData = (BYTE *) malloc(dwSize);
	*((DWORD *) pbData) = dwSize;

	if(!cbfr->Read(pbData + sizeof(DWORD), dwSize - sizeof(DWORD)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	idMap.Deserialize(pbData);

	// Verify the number of users.
	if(idMap.Count() != hdr.dwUserCount)
	{
		fprintf(stderr, "The number of users in the header block (%d) does not match the number of users in the file (%d).\n", hdr.dwUserCount, idMap.Count());
		fErrors = true;
	}
	else
	{
		printf("%d users were successfully read from the backup file.\n", hdr.dwUserCount);
	}

	dwInitUserCount = idMap.Count();

	// Verify the map here:
	if(idMap.LoadMaps(szMapFile))
		fErrors = true;
	else if(szMapFile != 0)
		printf("User mapping file verified successfully!\n");

	if(fUsersOnly)
		goto Exit;

	// Read in the files and verify...
	dwl = cbfr->Seek(hdr.dwDataOffset, FILE_BEGIN);
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
		{
			if(ssCurDir.Count() > 0)
			{
				fputs("Error: There are directories on the stack still though there is an end indicator for the file.  The backup is invalid.\n", stderr);
				fErrors = true;
			}
			break;
		}
		if(typeIndicator == BackupID_EndDirectory)
		{
			if(ssCurDir.Count())
			{
				WCHAR *szTemp = ssCurDir.Pop();
				free(szTemp);
			}
			else
			{
				fputs("Error: There are no directories on the stack yet a directory end indicator for the file occurred.  The backup is invalid.\n", stderr);
				fErrors = true;
			}
			continue;
		}

		if(typeIndicator != BackupID_File && typeIndicator != BackupID_Directory)
		{
			fprintf(stderr, "Error: The backup file is invalid.  The backup type is invalid (%d).\n", typeIndicator);
			fErrors = true;
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
				fErrors = true;
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

				sd = (SECURITY_DESCRIPTOR *) malloc(dwSecDescLen);
				if(!cbfr->Read((BYTE *) sd, dwSecDescLen))
				{
					res = cbfr->LastError();
					goto Exit;
				}

				DWORD dwc = idMap.Count();
				res = SubmitUsersFromSecurityDescriptor(sd, idMap, L"Backup File Verification");
				if(res)
					goto Exit;

				if(dwc != idMap.Count())
				{
					fprintf(stderr, "Error: There was a user in the security descriptor for %S that was not in the user list.\n", szName);
					fErrors = true;
				}
				if(sd)
					free(sd);
				sd = 0;

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

			if(szStreamName)
				free(szStreamName);
			szStreamName = 0;
		} // while(1) -- for each stream

		switch(typeIndicator)
		{
		case BackupID_File:
			dwFileCount++;
			break;
		case BackupID_Directory:
			dwDirCount++;
			break;
		};

		if(dwFileCount > hdr.dwFileCount && typeIndicator == BackupID_File)
		{
			fputs("Error: More files in the backup than indicated by the header!\n", stderr);
			fErrors = true;
		}
		else if(dwDirCount > hdr.dwDirectoryCount && typeIndicator == BackupID_Directory)
		{
			fputs("Error: More directories in the backup than indicated by the header!\n", stderr);
			fErrors = true;
		}

		printf("Verifying: %d of %d files, %d of %d directories.\r", dwFileCount, hdr.dwFileCount, dwDirCount, hdr.dwDirectoryCount);

		if(szName)
			free(szName);
		szName = 0;
	} // while(1) -- for each file/dir

	if(dwFileCount < hdr.dwFileCount)
	{
		fputs("The header indicates there should be more files than found in the backup!\n", stderr);
		fErrors = true;
	}

	if(dwDirCount < hdr.dwDirectoryCount)
	{
		fputs("The header indicates there should be more directories than found in the backup!\n", stderr);
		fErrors = true;
	}

	if(dwDirCount == hdr.dwDirectoryCount && dwFileCount == hdr.dwFileCount && dwInitUserCount == idMap.Count())
	{
		puts("\nFile list read successfully!");
	}

	if(!fErrors)
	{
		fputs("\nThe backup file is valid.", stdout);
	}

Exit:
	if(sd)
		free(sd);
	if(szStreamName)
		free(szStreamName);
	if(szName)
		free(szName);
	if(pbData)
		free(pbData);
	if(cbfr)
		delete cbfr;
	return res;
}