
#include "layout.h"
#include "xmove.h"
#include "sdhelpers.h"
#include "identitymap.h"
#include "bkpfile.h"
#include "stack.h"


int restore(const WCHAR *szDstPath, const WCHAR *szBackupFile, const WCHAR *szMapFile)
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
	DWORD dwFileCount = 0;
	DWORD dwDirCount = 0;
	SECURITY_DESCRIPTOR *sd = 0;
	stackString ssCurDir;
	DWORD dwInitUserCount = 0;
	bool fErrors = false;
	WCHAR *szFullName = 0;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	bool fFirst = true;

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

	{
		WCHAR *szDP = (WCHAR *) szDstPath;
		ssCurDir.Push(szDP);
	}

	// Read in the files and verify...
	dwl = cbfr->Seek(hdr.dwDataOffset, FILE_BEGIN);
	if(dwl != hdr.dwDataOffset)
	{
		res = cbfr->LastError();
		goto Exit;
	}

	if(fErrors)
		goto Exit;

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
			if(ssCurDir.Count() > 1)
			{
				fputs("Error: There are directories on the stack still though there is an end indicator for the file.  The backup is invalid.\n", stderr);
				fErrors = true;
			}
			break;
		}
		if(typeIndicator == BackupID_EndDirectory)
		{
			if(ssCurDir.Count() > 1)
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

		DWORD dwNameLen = wcslen(szName);
		DWORD dwCurDirLen = (ssCurDir.Peek() ? wcslen(ssCurDir.Peek()) + 1 : 0);
		if(szFullName)
			free(szFullName);
		szFullName = (WCHAR *) malloc((dwNameLen + dwCurDirLen + 1) * sizeof(WCHAR));

		if(dwCurDirLen)
		{
			wcscpy(szFullName, ssCurDir.Peek());
			szFullName[dwCurDirLen - 1] = L'\\';
		}
		wcscpy(szFullName + dwCurDirLen, szName);

		if(typeIndicator == BackupID_Directory)
		{
			WCHAR *sz2 = (WCHAR *) malloc((dwNameLen + dwCurDirLen + 1) * sizeof(WCHAR));
			wcscpy(sz2, szFullName);
			WIN32_FILE_ATTRIBUTE_DATA wfad;
			memset(&wfad, 0, sizeof(wfad));
			if(verbosity > 0)
				printf("Restoring directory: \"%S\"\n", sz2);
			if((!GetFileAttributesEx(sz2, GetFileExInfoStandard, &wfad) || !BIT_TEST(wfad.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
				&& !CreateDirectory(sz2, 0))
			{
				res = GetLastError();
				fprintf(stderr, "Error: Failed to create the directory \"%S\" to restore.  Error: %d\n", sz2, res);
				goto Exit;
			}
			if(!SetFileAttributes(sz2, dwAttributes))
			{
				res = GetLastError();
				fprintf(stderr, "Error: Failed to set the directory attributes for \"%S\".  Error: %d\n", sz2, res);
				goto Exit;
			}
			ssCurDir.Push(sz2);
		}
		else
		{
			if(verbosity > 0)
				printf("Restoring file: \"%S\"\n", szFullName);
		}

		// Okay, create the file here (if it exists, overwrite)
		LPVOID pContext = 0;
		bool fWritten = false;
		hFile = CreateFile(szFullName, GENERIC_WRITE | WRITE_OWNER | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, ((typeIndicator == BackupID_Directory) ? OPEN_EXISTING : CREATE_ALWAYS), FILE_FLAG_BACKUP_SEMANTICS | dwAttributes, 0);
		if(!hFile || hFile == INVALID_HANDLE_VALUE)
		{
			res = GetLastError();
			fprintf(stderr, "Error: Failed to open the file \"%S\" to restore.  Error: %d\n", szFullName, res);
			goto Exit;
		}

		for(;;)
		{
			DWORD dwStartAddress = sizeof(DWORD) * 3 + sizeof(LARGE_INTEGER);
			WIN32_STREAM_ID *pID = (WIN32_STREAM_ID *) copyBuffer;

			if(!cbfr->Read((BYTE *) &pID->dwStreamId, sizeof(DWORD)))
			{
				res = cbfr->LastError();
				if(fWritten)
					BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
				goto Exit;
			}

			if(pID->dwStreamId == BACKUP_INVALID)
				break;

			if(pID->dwStreamId > BACKUP_SPARSE_BLOCK)
			{
				fputs("Error: The backup file is invalid.  The stream ID is invalid.\n", stderr);
				fErrors = true;
				if(fWritten)
					BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
				goto Exit;
			}

			if(pID->dwStreamId != BACKUP_SECURITY_DATA)
			{
				if(!cbfr->Read((BYTE *) &pID->dwStreamAttributes, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				if(!cbfr->Read((BYTE *) &pID->Size, sizeof(DWORDLONG)))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}
				
				if(!cbfr->Read((BYTE *) &pID->dwStreamNameSize, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				if(!cbfr->Read((BYTE *) pID->cStreamName, pID->dwStreamNameSize))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				dwStartAddress += pID->dwStreamNameSize;

				DWORDLONG dwlToRead = pID->Size.QuadPart;
				while(dwlToRead > 0)
				{
					DWORD dwToRead = (DWORD) min(sizeof(copyBuffer) - dwStartAddress, dwlToRead);
					if(!cbfr->Read(&(copyBuffer[dwStartAddress]), dwToRead))
					{
						res = cbfr->LastError();
						if(fWritten)
							BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
						goto Exit;
					}
					DWORD dwWritten = 0;
					if(!BackupWrite(hFile, copyBuffer, dwToRead + dwStartAddress, &dwWritten, FALSE, FALSE, &pContext) || 
								(dwWritten != dwToRead + dwStartAddress))
					{
						res = GetLastError();
						fprintf(stderr, "Error: Failed to write to the file \"%S\".  Error: %d\n", szFullName, res);
						if(fWritten)
							BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
						goto Exit;
					}
					fWritten = true;
					dwlToRead -= dwToRead;
					dwStartAddress = 0;
				}
			}
			else // dwStreamID == BACKUP_SECURITY
			{
				DWORD dwSecDescLen = 0;
				if(!cbfr->Read((BYTE *) &dwSecDescLen, sizeof(DWORD)))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				sd = (SECURITY_DESCRIPTOR *) malloc(dwSecDescLen);
				if(!cbfr->Read((BYTE *) sd, dwSecDescLen))
				{
					res = cbfr->LastError();
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				DWORD dwc = idMap.Count();
				res = SubmitUsersFromSecurityDescriptor(sd, idMap, L"Backup File Verification");
				if(res)
				{
					if(fWritten)
						BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);
					goto Exit;
				}

				if(dwc != idMap.Count())
				{
					fprintf(stderr, "Error: There was a user in the security descriptor for %S that was not in the user list.\n", szName);
					fErrors = true;
				}

				if(!fErrors)
				{
					idMap.MapSecurityDescriptor(sd, hFile, fFirst);
				}
				fFirst = false;

				if(sd)
					free(sd);
				sd = 0;

				FILETIME ftdate[3];
				for(int i = 0; i < 3; ++i)
				{
					if(!cbfr->Read((BYTE *) &ftdate[i], sizeof(FILETIME)))
					{
						res = cbfr->LastError();
						goto Exit;
					}
				}
				if(!SetFileTime(hFile, &ftdate[0], &ftdate[1], &ftdate[2]))
				{
					res = GetLastError();
					fprintf(stderr, "Warning: Failed to set the file \"%S\" creation, last access, and last written times.  Error: %d\n", szName, res);
				}
			}  // dwStreamID == BACKUP_SECURITY

			dwlTotalSize += pID->Size.QuadPart;
		} // while(1) -- for each stream

		if(fWritten)
			BackupWrite(hFile, 0, 0, 0, TRUE, FALSE, &pContext);

		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;

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

		if(!verbosity)
			printf("Restoring: %d of %d files, %d of %d directories.\r", dwFileCount, hdr.dwFileCount, dwDirCount, hdr.dwDirectoryCount);

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
	if(hFile && hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	if(sd)
		free(sd);
	if(szFullName)
		free(szFullName);
	if(szName)
		free(szName);
	if(pbData)
		free(pbData);
	if(cbfr)
		delete cbfr;
	return res;
}