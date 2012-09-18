
#include "sdhelpers.h"
#include "identitymap.h"
#include <stdio.h>
#include <lm.h>
#include <sddl.h>
#include "xmove.h"
#include "AceList.h"

#pragma warning( disable : 4701 )

CIdentityMap::CIdentityMap()
{
	m_dwMapSize = 0;
	m_dwMapped = 0;
	m_ppMaps = 0;
}

CIdentityMap::~CIdentityMap()
{
	if(m_ppMaps)
	{
		for(DWORD i = 0; i < m_dwMapSize; ++i)
		{
			if(m_ppMaps[i])
				delete m_ppMaps[i];
		}
		free(m_ppMaps);
	}
}

CIdentityList *CIdentityMap::NewIDs(DWORD dwIndex)
{
	if(!m_ppMaps)
		return 0;
	if(dwIndex >= m_dwMapSize)
		return 0;

	return m_ppMaps[dwIndex];
}

DWORD CIdentityMap::LoadMaps(const WCHAR *szMapFile)
{
	CHAR szName[256], szDomain[256];
	WIN32_FILE_ATTRIBUTE_DATA fad;
	DWORD dwRes = 0;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = 0;
	CHAR *pszMap = 0, *pos;
	bool fBad = false;

	if(!Count())
		return 1;

	if(!szMapFile)
		goto Verify;

	if(!GetFileAttributesEx(szMapFile, GetFileExInfoStandard, &fad))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to get attributes on the map file: %d\n", dwRes);
		goto Exit;
	}

	hFile = CreateFile(szMapFile, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
	if(!hFile || hFile == INVALID_HANDLE_VALUE)
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to open the map file: %d\n", dwRes);
		goto Exit;
	}

	hFileMapping = CreateFileMapping(hFile, 0, PAGE_WRITECOPY, fad.nFileSizeHigh, fad.nFileSizeLow, 0);
	if(!hFile || hFile == INVALID_HANDLE_VALUE)
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to create a memory map (step 1) of the map file: %d\n", dwRes);
		goto Exit;
	}

	pszMap = (CHAR *) MapViewOfFile(hFileMapping, FILE_MAP_COPY, 0, 0, 0);
	if(!pszMap)
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to create a memory map (step 2) of the map file: %d\n", dwRes);
		goto Exit;
	}

	CHAR *szOriginal, *pos2, *szLine;

	m_dwMapSize = Count();
	m_ppMaps = (CIdentityList **) malloc(sizeof(CIdentityList *) * m_dwMapSize);
	memset(m_ppMaps, 0, m_dwMapSize * sizeof(CIdentityList *));

	pos = pszMap;
	while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow)
	{
		// Skip all whitespace characters.
		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && (*pos == ' ' || *pos == '\t'))
			++pos;

		szLine = pos;
		if(((DWORD) (pos - pszMap)) >= fad.nFileSizeLow)
			break;

		// Skip (mostly) empty lines (done this way to not have newlines in the szLine var)
		if(*pos == '\r' || *pos == '\n')
		{
			while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && (*pos == '\n' || *pos == '\r'))
				++pos;
			continue;
		}
		
		SidUsage thisUsage = SidUse_None;
		bool fProcessingUsage = true;
		while(fProcessingUsage && *pos != '"')
		{
			switch(*pos)
			{
			case 'O':
			case 'o':
				thisUsage = (SidUsage) ((int) thisUsage | (int) SidUse_Owner);
				break;
			case 'G':
			case 'g':
				thisUsage = (SidUsage) ((int) thisUsage | (int) SidUse_Group);
				break;
			case 'D':
			case 'd':
				thisUsage = (SidUsage) ((int) thisUsage | (int) SidUse_DACL);
				break;
			case 'S':
			case 's':
				thisUsage = (SidUsage) ((int) thisUsage | (int) SidUse_SACL);
				break;
			default:
				fProcessingUsage  = false;
			};
			if(fProcessingUsage)
				++pos;
		}

		// All identities start with double quote
		if(*pos != '"')
		{
			pos2 = pos;
			while(((DWORD) (pos2 - pszMap)) < fad.nFileSizeLow && *pos2 != '\n' && *pos2 != '\r')
				++pos2;
			if(((DWORD) (pos2 - pszMap)) < fad.nFileSizeLow)
				*pos2 = 0;

			fprintf(stderr, "Warning: identity to map does not start with a double quote (this line is being skipped).  Line: %s\n", szLine);
			pos = pos2 + 1;
			continue;
		}

		// We are now at the beginning of an identity...
		++pos;
		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && *pos == ' ')
			++pos;
		szOriginal = pos;
		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && *pos != '"')
			++pos;

		if(((DWORD) (pos - pszMap)) >= fad.nFileSizeLow)
		{
			fprintf(stderr, "Warning: identity to map does not end with a double quote (this line is being skipped).  Line: %s\n", szLine);
			break;		// Break because we are done anyway
		}

		// Terminate the string
		*pos = 0;
		++pos;
		DWORD dwUserIdx = 0xFFFFFFFF;

		// Lookup the identity in the identity list:
		for(DWORD i = 0; dwUserIdx == 0xFFFFFFFF && i < Count(); ++i)
		{
			// Three possible ways to match:
			//	SID
			//	Name
			//	Domain\\Name
			if(m_ppList[i]->SidType() == 0)
			{
				LPSTR szStringSid = 0;
				ConvertSidToStringSidA(m_ppList[i]->SID(), &szStringSid);
				if(!stricmp(szOriginal, szStringSid))
					dwUserIdx = i;
				if(szStringSid)
					LocalFree(szStringSid);
			}
			else if(m_ppList[i]->Name() && (m_ppList[i]->Domain() == 0 || m_ppList[i]->Domain()[0] == 0))
			{
				if(stricmp(szOriginal, m_ppList[i]->Name()))
					continue;
				dwUserIdx = i;
			}
			else if(m_ppList[i]->Name() && m_ppList[i]->Name()[0] && m_ppList[i]->Domain() && m_ppList[i]->Domain()[0])
			{
				DWORD dwDomainLen = strlen(m_ppList[i]->Domain());
				DWORD dwNameLen = strlen(m_ppList[i]->Name());
				DWORD dwOrigLen = strlen(szOriginal);
				if(dwOrigLen != dwDomainLen + dwNameLen + 1)
					continue;
				if(szOriginal[dwDomainLen] != '\\')
					continue;
				if(strnicmp(szOriginal, m_ppList[i]->Domain(), dwDomainLen))
					continue;
				if(strnicmp(szOriginal + dwDomainLen + 1, m_ppList[i]->Name(), dwNameLen))
					continue;
				dwUserIdx = i;
			}

			if(dwUserIdx == 0xFFFFFFFF)
				continue;

			if(verbosity)
				printf("Matched user: %s\n", szOriginal);
			break;
		}

		if(dwUserIdx == 0xFFFFFFFF)
		{
			fprintf(stderr, "Warning: \"%s\" cannot be found in the user list.  Skipped.\n", szOriginal);
			goto EOL;
		}

/*
		if(m_ppList[dwUserIdx]->SidType() == SidTypeAlias || m_ppList[dwUserIdx]->SidType() == SidTypeWellKnownGroup)
		{
			fprintf(stderr, "Error: \"%s\" cannot be mapped.  It is an alias or well known group.\n", szOriginal);
			fBad = true;
			goto EOL;
		}
*/
		if(!m_ppMaps[dwUserIdx])
		{
			m_ppMaps[dwUserIdx] = new CIdentityList();
			++m_dwMapped;
		}

		BYTE pbBuf[1024];
		SID * pSid = (SID *) pbBuf;
		DWORD dwSidSize = sizeof(pbBuf);
		SID_NAME_USE snu;
		CHAR szData[1024];

		// for each entry in the list of users to map to...
		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow)
		{
			// Skip white space
			while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && (*pos == ' ' || *pos == '\t'))
				++pos;

			if(((DWORD) (pos - pszMap)) >= fad.nFileSizeLow || *pos == '\r' || *pos == '\n')
				break;

			// Match |"<domain>\<name>",| or |"<name>",|
			if(*pos != '"')
			{
				pos2 = pos;
				while(((DWORD) (pos2 - pszMap)) < fad.nFileSizeLow && *pos2 != '\n' && *pos2 != '\r')
					++pos2;
				if(((DWORD) (pos2 - pszMap)) < fad.nFileSizeLow)
					*pos2 = 0;

				fprintf(stderr, "Warning: identity to map to does not start with a double quote (the rest of this line is being skipped).  Original: \"%s\"\n", szOriginal);
				pos = pos2 + 1;
				break;
			}
			++pos;

			pos2 = pos;
			while(((DWORD) (pos2 - pszMap)) < fad.nFileSizeLow && *pos2 != '"')
				++pos2;

			if(((DWORD) (pos2 - pszMap)) >= fad.nFileSizeLow)
			{
				fprintf(stderr, "Warning: identity to map to does not end with a double quote (this entry is being skipped).  Entry: \"%s\"\n", pos);
				break;		// Break because we are done anyway
			}
			
			*pos2 = 0;
			++pos2;	// Where to continue from...

			if(m_ppMaps[dwUserIdx]->Count() > 1 && (BIT_TEST(m_ppList[dwUserIdx]->SidUse(), SidUse_Owner) || BIT_TEST(m_ppList[dwUserIdx]->SidUse(), SidUse_Group)))
			{
				fBad = true;
				fprintf(stderr, "Error: \"%s\" already has a mapping and is an owner.\n", szOriginal);
				goto EOL;
			}

			DWORD dwRequiredSize = sizeof(szData);
			if(!LookupAccountNameA(0, pos, pSid, &dwSidSize, szData, &dwRequiredSize, &snu))
			{
				DWORD dwError = GetLastError();
				fprintf(stderr, "Warning: failed to lookup identity \"%s\" to map to.  Error: %d\n", pos, dwError);
				goto NextEntry;
			}
			
			// If this sid is used as a group owner, we are mapping the group with this line, etc...
			if(BIT_TEST(m_ppList[dwUserIdx]->SidUse(), SidUse_Owner) && BIT_TEST(thisUsage, SidUse_Owner) && snu != SidTypeUser && snu != SidTypeAlias)
			{
				fBad = true;
				fprintf(stderr, "Error: File owner \"%s\" cannot be mapped to \"%s\" as it is neither a user nor alias.\n", szOriginal, pos);
				goto NextEntry;
			}

			// If this sid is used as a group owner, we are mapping the group with this line, etc...
			if(BIT_TEST(m_ppList[dwUserIdx]->SidUse(), SidUse_Group) && BIT_TEST(thisUsage, SidUse_Group) && snu != SidTypeGroup && snu != SidTypeWellKnownGroup)
			{
				fBad = true;
				fprintf(stderr, "Error: File owner \"%s\" cannot be mapped to \"%s\" as it is neither a group nor well known group.\n", szOriginal, pos);
				goto NextEntry;
			}

			{
				if(verbosity)
				{
					printf("\tFound user \"%s\" in the system.  ", pos);
				}

				DWORD dwError = m_ppMaps[dwUserIdx]->Submit(pSid, thisUsage, 0);
				if(dwError != 0)
				{
					puts("");
					fprintf(stderr, "Warning: Lookup failed for \"%s\" to be mapped to from \"%s\"\n", pos, szOriginal);
					goto NextEntry;
				}
				
				if(verbosity)
				{
					puts("User added.");
				}
			}

NextEntry:
			pos = pos2;
			while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && (*pos == ' ' || *pos == '\t'))
				++pos;

			if(((DWORD) (pos - pszMap)) >= fad.nFileSizeLow || *pos == '\r' || *pos == '\n')
				break;

			if(*pos != ',' && *pos != ';')
			{
				fprintf(stderr, "Warning: Invalid separator between map to entries (rest of the entries will be skipped). Original: \"%s\"\n", szOriginal);
				break;
			}
			++pos;
		} // foreach map to entry to process

EOL:
		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && *pos != '\n' && *pos != '\r')
			++pos;

		while(((DWORD) (pos - pszMap)) < fad.nFileSizeLow && (*pos == '\n' || *pos == '\r'))
			++pos;
	}  // For each line in the input...

Verify:
	// Verify that if a mapping was specified for group and user owners, there is exactly one entry (note that a null entry means to map to oneself)
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(m_ppMaps && m_ppMaps[i] != 0)
		{
			unsigned int uiGroupCount = 0, uiOwnerCount = 0;
			CIdentityList *lst = m_ppMaps[i];
			SidUsage curDefnUse = SidUse_None;
			for(unsigned mappedIdx = 0; mappedIdx < lst->Count(); ++mappedIdx)
			{
				curDefnUse = (SidUsage) ((int) lst->operator [](mappedIdx)->SidUse() | (int) curDefnUse);
			}
			// Update the usage of undeclared mappings
			for(unsigned mappedIdx = 0; mappedIdx < lst->Count(); ++mappedIdx)
			{
				if(verbosity > 7)
					printf("Promoting usage from %02x to %02x\n", curDefnUse, (((int) c_MaxUsage) & ~((int) curDefnUse)));
				lst->Submit(lst->operator [](mappedIdx)->SID(), (SidUsage) (((int) c_MaxUsage) & ~((int) curDefnUse)), 0);
				if(BIT_TEST((*lst)[mappedIdx]->SidUse(), SidUse_Owner))
					uiOwnerCount++;
				if(BIT_TEST((*lst)[mappedIdx]->SidUse(), SidUse_Group))
					uiGroupCount++;
			}

			if(BIT_TEST(m_ppList[i]->SidUse(), SidUse_Owner) && uiOwnerCount != 1)
			{
				fBad = true;
				if(0 != m_ppList[i]->DisplayName(szName, sizeof(szName)))
				{
					fputs("Error: Failed to generate the display name for this user.  Very bad!\n", stderr);
				}
				else
				{
					fprintf(stderr, "Error: \"%s\" is a file owner but has %d users mapped (must be 1).\n", szName, uiOwnerCount);
				}
			}

			if(BIT_TEST(m_ppList[i]->SidUse(), SidUse_Group) && uiGroupCount != 1)
			{
				fBad = true;
				if(0 != m_ppList[i]->DisplayName(szName, sizeof(szName)))
				{
					fputs("Error: Failed to generate the display name for this user.  Very bad!\n", stderr);
				}
				else
				{
					fprintf(stderr, "Error: \"%s\" is a file owner but has %d users mapped (must be 1).\n", szName, uiGroupCount);
				}
			}

			// Verify that all users have a use...
			for(unsigned mappedIdx = 0; mappedIdx < lst->Count(); ++mappedIdx)
			{
				CIdentity *cid = lst->operator [](mappedIdx);
				if(cid->SidUse() == SidUse_None)
				{
					if(0 != cid->DisplayName(szName, sizeof(szName)) || 0 != m_ppList[i]->DisplayName(szDomain, sizeof(szDomain)))
					{
						fputs("Error: Failed to generate the display name for this user.  Very bad!\n", stderr);
					}
					else
					{
						fprintf(stderr, "Warning: Mapping from \"%s\" to \"%s\" will not occur, no uses specified.\n", szDomain, szName);
					}
				}
				// If this sid is used as a group owner, we are mapping the group with this line, etc...
				if(BIT_TEST(m_ppList[i]->SidUse(), SidUse_Owner) && BIT_TEST(cid->SidUse(), SidUse_Owner) && cid->SidType() != SidTypeUser && cid->SidType() != SidTypeAlias)
				{
					fBad = true;
					if(0 != cid->DisplayName(szName, sizeof(szName)) || 0 != m_ppList[i]->DisplayName(szDomain, sizeof(szDomain)))
					{
						fputs("Error: Failed to generate the display name for this user.  Very bad!\n", stderr);
					}
					else
					{
						fprintf(stderr, "Error: Cannot map from owner \"%s\" to \"%s\": the latter is not a user or alias.\n", szDomain, szName);
					}
				}

				// If this sid is used as a group owner, we are mapping the group with this line, etc...
				if(BIT_TEST(m_ppList[i]->SidUse(), SidUse_Group) && BIT_TEST(cid->SidUse(), SidUse_Group) && cid->SidType() != SidTypeGroup && cid->SidType() != SidTypeWellKnownGroup)
				{
					fBad = true;
					if(0 != cid->DisplayName(szName, sizeof(szName)) || 0 != m_ppList[i]->DisplayName(szDomain, sizeof(szDomain)))
					{
						fputs("Error: Failed to generate the display name for this user.  Very bad!\n", stderr);
					}
					else
					{
						fprintf(stderr, "Error: Cannot map from owner \"%s\" to \"%s\": the latter is not a group or well known group.\n", szDomain, szName);
					}
				}
			} // verifying all users have a use...
		}

		if(!m_ppMaps || !m_ppMaps[i])	// Attempt to lookup the user on the current machine to verify identity and complain if it fails
		{
			DWORD dwName = sizeof(szName), dwDomain = sizeof(szDomain);
			SID_NAME_USE seUse = (SID_NAME_USE) 0;
			if(!LookupAccountSidA(0, m_ppList[i]->SID(), szName, &dwName, szDomain, &dwDomain, &seUse))
			{
				fBad = true;
				if(0 != m_ppList[i]->DisplayName(szName, sizeof(szName)))
				{
					fputs("Error: Failed to create the display name for this user.  Very bad!\n", stderr);
				}
				else
				{
					fprintf(stderr, "Error: \"%s\" connot be found in the system.\n", szName);
				}
			}
		}
	}

Exit:
	if(pszMap)
		UnmapViewOfFile(pszMap);
	if(hFileMapping && hFileMapping != INVALID_HANDLE_VALUE)
		CloseHandle(hFileMapping);
	if(hFile && hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	if(fBad && !dwRes)
		dwRes = 1;
	return dwRes;
}


DWORD CIdentityMap::MapSecurityDescriptor(const SECURITY_DESCRIPTOR *pOldSD, HANDLE hTargetFile, bool fFirst)
{
	DWORD dwRes = 0;
	CAceList alAudit;
	CAceList alAccess;
	PACL pAclAccess = 0, pAclAudit = 0;

	// Get the owner and group
	PSID pOwner = 0, pGroup = 0;
	BOOL fDefaultedOwner = FALSE, fDefaultedGroup = FALSE;
	if(!GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR) pOldSD, &pOwner, &fDefaultedOwner))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to get the owner of the file.  Error: %d\n", dwRes);
		goto Exit;
	}

	if(!GetSecurityDescriptorGroup((PSECURITY_DESCRIPTOR) pOldSD, &pGroup, &fDefaultedGroup))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to get the group of the file.  Error: %d\n", dwRes);
		goto Exit;
	}

	// Map the owner in the sd
	CIdentityList *clOwner = GetMapForIdentity(pOwner);
	for(DWORD i = 0; clOwner && i < clOwner->Count(); ++i)
	{
		if(BIT_TEST((*clOwner)[i]->SidUse(), SidUse_Owner))
		{
			pOwner = (*clOwner)[i]->SID();
			break;
		}
	}

	// Map the group in the sd
	CIdentityList *clGroup = GetMapForIdentity(pGroup);
	for(DWORD i = 0; clGroup && i < clGroup->Count(); ++i)
	{
		if(BIT_TEST((*clGroup)[i]->SidUse(), SidUse_Group))
		{
			pGroup = (*clGroup)[i]->SID();
			break;
		}
	}

	// Map the dacls in the sd
	BOOL fDaclPresent = FALSE, fDefaultedDacl = FALSE;
	PACL pDacl = 0;

	if(!GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR) pOldSD, &fDaclPresent, &pDacl, &fDefaultedDacl))
	{
		// If this fails, something is seriously wrong
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to retrieve the DACL from the old SD: %d\n", dwRes);
		goto Exit;
	}

	if(fDaclPresent)
	{
		dwRes = BuildExplicitAccess(pDacl, alAccess, SidUse_DACL, fFirst);
		if(dwRes)
			goto Exit;
		// Get the new Dacl
		pAclAccess = alAccess.GetAcl();
		if(!pAclAccess)
		{
			dwRes = 1;
			goto Exit;
		}
		if(!IsValidAcl(pAclAccess))
		{
			fputs("The generated DACL is invalid.\n", stderr);
			dwRes = 1;
			goto Exit;
		}
	}

	// Map the sacls in the sd
	BOOL fSaclPresent = FALSE, fDefaultedSacl = FALSE;
	PACL pSacl = 0;

	if(!GetSecurityDescriptorSacl((PSECURITY_DESCRIPTOR) pOldSD, &fSaclPresent, &pSacl, &fDefaultedSacl))
	{
		// If this fails, something is seriously wrong
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to retrieve the SACL from the old SD: %d\n", dwRes);
		goto Exit;
	}

	if(fSaclPresent)
	{
		dwRes = BuildExplicitAccess(pSacl, alAudit, SidUse_SACL, fFirst);
		if(dwRes)
			goto Exit;
		// Get the new Sacl
		pAclAudit = alAudit.GetAcl();
		if(!pAclAudit)
		{
			dwRes = 1;
			goto Exit;
		}
		if(!IsValidAcl(pAclAudit))
		{
			fputs("The generated SACL is invalid.\n", stderr);
			dwRes = 1;
			goto Exit;
		}
	}

	SECURITY_DESCRIPTOR_CONTROL sdc = 0;
	DWORD dwRevision = 0;
	if(!GetSecurityDescriptorControl((PSECURITY_DESCRIPTOR) pOldSD, &sdc, &dwRevision))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to get the control bits from the security descriptor. Error: %d\n", dwRes);
		goto Exit;
	}

	// Apply the new SD to the target file.
	SECURITY_INFORMATION newSecInf = GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION;
	newSecInf |= (!fDaclPresent) ? 0 : DACL_SECURITY_INFORMATION;
	newSecInf |= (!fSaclPresent) ? 0 : SACL_SECURITY_INFORMATION;

	SECURITY_DESCRIPTOR_CONTROL maskSDC = (fDaclPresent ? (SE_DACL_AUTO_INHERIT_REQ | SE_DACL_PROTECTED) : 0) | 
											(fSaclPresent ? (SE_SACL_AUTO_INHERIT_REQ | SE_SACL_PROTECTED) : 0);
	SECURITY_DESCRIPTOR_CONTROL newSDC = (fDaclPresent ? SE_DACL_AUTO_INHERIT_REQ : 0) |
											(fSaclPresent ? SE_SACL_AUTO_INHERIT_REQ : 0);

	newSDC |= ((fFirst && fDaclPresent) ? SE_DACL_PROTECTED : 0) |
				((fFirst && fSaclPresent) ? SE_SACL_PROTECTED : 0);

	{	// Create the security descriptor
		SECURITY_DESCRIPTOR sd;
		if(!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to initialize the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(!SetSecurityDescriptorOwner(&sd, pOwner, !fFirst && fDefaultedOwner))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to set the owner of the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(!SetSecurityDescriptorGroup(&sd, pGroup, !fFirst && fDefaultedGroup))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to set the group of the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(!SetSecurityDescriptorDacl(&sd, fDaclPresent, pAclAccess, !fFirst && fDefaultedDacl))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to set the DACL of the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(!SetSecurityDescriptorSacl(&sd, fSaclPresent, pAclAudit, !fFirst && fDefaultedSacl))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to set the SACL of the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(!SetSecurityDescriptorControl(&sd, maskSDC, newSDC))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to set the control bits of the security descriptor.  Error: %d\n", dwRes);
			goto Exit;
		}

		if(hTargetFile && !SetKernelObjectSecurity(hTargetFile, newSecInf, &sd))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to apply the security descriptor to the file.  Error: %d\n", dwRes);
			goto Exit;
		}
	}
Exit:
	if(pAclAudit)
		free(pAclAudit);
	if(pAclAccess)
		free(pAclAccess);
	return dwRes;
}

DWORD CIdentityMap::BuildExplicitAccess(PACL pAcl, CAceList& pAces, SidUsage sidUse, bool fFirst)
{
	DWORD dwRes = 0;
	ACL_SIZE_INFORMATION asi;
	if(!pAcl)
		return 0;

	if(!GetAclInformation(pAcl, &asi, sizeof(asi), AclSizeInformation))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to get the Acl information for the old SD: %d\n", dwRes);
		goto Exit;
	}

	// for each ACE in the ACL
	for(DWORD i = 0; i < asi.AceCount; ++i)
	{
		ACE_HEADER *pAce;
		if(!GetAce(pAcl, i, (LPVOID *) &pAce))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to get the ACE information from the old SD: %d\n", dwRes);
			goto Exit;
		}

		if(fFirst)
		{
			pAce->AceFlags &= ~INHERITED_ACE;
		}

		// for each user in the identity list add it to the map
		CIdentity *orig = 0;
		CIdentityList *cil = GetMapForIdentity(GetSid(pAce), &orig);
		
		// If there are no entries, map the existing user
		if(!cil)
		{
			dwRes = pAces.AddAce(pAce, 0);
			if(dwRes)
				goto Exit;
		}
		else  // Otherwise map the users of the list (note that if the list is empty is does the correct behaviour of deleting this user)
		{
			for(DWORD j = 0; j < cil->Count(); ++j)
			{
				if(BIT_TEST((*cil)[j]->SidUse(), sidUse))
				{
					dwRes = pAces.AddAce(pAce, (*cil)[j]->SID());
					if(dwRes)
						goto Exit;
				}
			}
		}
	}

Exit:
	return dwRes;
}


CIdentityList *CIdentityMap::GetMapForIdentity(PSID pSID, CIdentity ** orig)
{
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(EqualSid(pSID, m_ppList[i]->SID()))
		{
			if(orig)
				*orig = m_ppList[i];	
			return (m_ppMaps) ? m_ppMaps[i] : 0;
		}
	}
	return 0;
}
