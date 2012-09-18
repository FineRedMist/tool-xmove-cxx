
#include "sdhelpers.h"
#include "identitylist.h"
#include <stdio.h>
#include <lm.h>
#include <sddl.h>
#include "xmove.h"

void SearchGroupsForSid(const WCHAR *szDomain, PSID sid);

CIdentityList::CIdentityList()
{
	m_dwSize = 10;
	m_ppList = (CIdentity **) malloc(m_dwSize * sizeof(CIdentity *));
	memset(m_ppList, 0, m_dwSize * sizeof(CIdentity *));
	m_dwCount = 0;
}

CIdentityList::~CIdentityList()
{
	for(DWORD i = 0; i < m_dwCount; ++i)
		delete m_ppList[i];
	free(m_ppList);
}

DWORD CIdentityList::Submit(const PSID sid, SidUsage sidUse, const WCHAR *szHelpString)
{
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(EqualSid(sid, m_ppList[i]->SID()))
		{
			m_ppList[i]->AddUse(sidUse);
			if((((DWORD) m_ppList[i]->SidType()) == 0) && szHelpString && !testing)
				printf("Warning: Identity lookup failed.  Location: %S\n", szHelpString);
			return 0;
		}
	}
	DWORD dwError = 0;
	CIdentity * pID = CIdentity::Lookup(sid, sidUse, dwError);
	if(dwError)
		goto Exit;

	if(((DWORD) pID->SidType()) == 0)
	{
		WCHAR *szDomain = 0;
		if(szHelpString)
			printf("Warning: Identity lookup failed.  Location: %S\n", szHelpString);
		SearchGroupsForSid(0, sid);
		if(!NetGetDCName(0, 0, (LPBYTE *) &szDomain))
		{
			SearchGroupsForSid(szDomain, sid);
		}
		else
		{
			fprintf(stderr, "Warning: Failed to get the domain name of the local machine. Error %d\n", GetLastError());
		}
		if(szDomain)
			NetApiBufferFree(szDomain);
	}

	if(m_dwCount == m_dwSize)
	{
		m_dwSize = m_dwSize + 5;
		m_ppList = (CIdentity **) realloc(m_ppList, m_dwSize * sizeof(CIdentity *));
		for(DWORD i = m_dwCount; i < m_dwSize; ++i)
			m_ppList[i] = 0;
	}

	m_ppList[m_dwCount] = pID;
	m_dwCount++;

Exit:
	return dwError;
}

DWORD CIdentityList::Deserialize(const LPBYTE pbStream)
{
	LPBYTE pos = pbStream;
	// DWORD dwLen = *((DWORD *) pos);
	pos += sizeof(DWORD);
	DWORD dwCount = *((DWORD *) pos);
	pos += sizeof(DWORD);

	for(DWORD i = 0; i < m_dwCount; ++i)
		delete m_ppList[i];
	free(m_ppList);

	m_dwSize = m_dwCount = dwCount;
	m_ppList = (CIdentity **) malloc(m_dwSize * sizeof(CIdentity *));
	memset(m_ppList, 0, m_dwSize * sizeof(CIdentity *));

	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		m_ppList[i] = CIdentity::Generate(pos);
		pos += m_ppList[i]->StreamLength();
	}

	return 0;
}

LPBYTE CIdentityList::Serialize(DWORD &dwLen)
{
	dwLen = sizeof(DWORD) * 2;

	for(DWORD i = 0; i < m_dwCount; ++i)
		dwLen += m_ppList[i]->StreamLength();

	LPBYTE pbRes = (LPBYTE) malloc(dwLen);
	LPBYTE pos = pbRes;
	*((DWORD *) pos) = dwLen;
	pos += sizeof(DWORD);
	*((DWORD *) pos) = m_dwCount;
	pos += sizeof(DWORD);
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		memcpy(pos, m_ppList[i]->Stream(), m_ppList[i]->StreamLength());
		pos += m_ppList[i]->StreamLength();
	}

	return pbRes;
}

CIdentity *CIdentityList::operator[](DWORD dwIndex)
{
	if(dwIndex > m_dwCount)
		return 0;
	return m_ppList[dwIndex];
}

DWORD CIdentityList::Count()
{
	return m_dwCount;
}

void CIdentityList::Display(int level)
{
	CHAR szBuf[256];
	if(level == 0)
	{
		for(DWORD i = 0; i < m_dwCount; ++i)
		{
			if(0 != m_ppList[i]->DisplayName(szBuf, sizeof(szBuf)))
			{
				fputs("Error: Failed to get the display name for this user.  Something is terribly wrong!\n", stderr);
				continue;
			}
			printf("\"%s\"\t<place comma delimited mappings here>\n", szBuf);
		}
		return;
	}

	if(level == 1)
	{
		puts("User List:");
		for(DWORD i = 0; i < m_dwCount; ++i)
		{
			if(m_ppList[i]->SidType() == 0)
			{
				WCHAR *szSid = 0;
				ConvertSidToStringSid(m_ppList[i]->SID(), &szSid);
				SidBreakdown(m_ppList[i]->SID(), szBuf);
				printf("\t%S: \"%S\": %s\n", SidTypeToString(m_ppList[i]->SidType()), (szSid ? szSid : L"Invalid"), szBuf);
				if(szSid)
					LocalFree(szSid);
			}
			else if(m_ppList[i]->FullName())
				printf("\t%S: \"%s\\%s\" (%S)\n", SidTypeToString(m_ppList[i]->SidType()), m_ppList[i]->Domain(), m_ppList[i]->Name(), m_ppList[i]->FullName());
			else if(m_ppList[i]->Domain() && m_ppList[i]->Domain()[0])
				printf("\t%S: \"%s\\%s\"\n", SidTypeToString(m_ppList[i]->SidType()), m_ppList[i]->Domain(), m_ppList[i]->Name());
			else
				printf("\t%S: \"%s\"\n", SidTypeToString(m_ppList[i]->SidType()), m_ppList[i]->Name());
		}
		return;
	}
	if(level == 2)
	{
		for(DWORD i = 0; i < m_dwCount; ++i)
		{
			LPWSTR szStringSid = 0;
			ConvertSidToStringSid(m_ppList[i]->SID(), &szStringSid);
			CHAR szUse[5] = "----";
			szUse[0] = BIT_TEST(m_ppList[i]->SidUse(), SidUse_Owner) ? 'O' : '-';
			szUse[1] = BIT_TEST(m_ppList[i]->SidUse(), SidUse_Group) ? 'G' : '-';
			szUse[2] = BIT_TEST(m_ppList[i]->SidUse(), SidUse_DACL) ? 'D' : '-';
			szUse[3] = BIT_TEST(m_ppList[i]->SidUse(), SidUse_SACL) ? 'S' : '-';
			printf(" %s %S: D(%s), N(%s), FN(%S), SID(%S)\n", 
				szUse,
				SidTypeToString(m_ppList[i]->SidType()),
				m_ppList[i]->Domain(),
				m_ppList[i]->Name(),
				m_ppList[i]->FullName(),
				szStringSid
				);
			if(szStringSid)
				LocalFree(szStringSid);
		}
		return;
	}
}


void SearchGroupsForSid(const WCHAR *szDomain, PSID sid)
{
	// Search local groups, then search domain groups
	DWORD dwEntryCount, dwEntriesTotal;
	DWORD_PTR pdwResumeHandle = 0;
	LPBYTE pbData = 0;
	GROUP_INFO_2 *gi;
	NET_API_STATUS nas = ERROR_MORE_DATA;
	PUCHAR pcSubCount = GetSidSubAuthorityCount(sid);
	while(nas == ERROR_MORE_DATA)
	{
		nas = NetGroupEnum(szDomain, 2, &pbData, MAX_PREFERRED_LENGTH, &dwEntryCount, &dwEntriesTotal, &pdwResumeHandle);
		if(nas != ERROR_SUCCESS && nas != ERROR_MORE_DATA)
		{
			fprintf(stderr, "Warning: Failed to enumerate groups for \"%S\".  Error: %d\n", ((szDomain) ? szDomain : L"local machine"), nas);
			break;
		}
		gi = (GROUP_INFO_2 *) pbData;
		for(DWORD i = 0; i < dwEntryCount; ++i)
		{
			for(DWORD j = 0; j < *pcSubCount; ++j)
			{
				if(*GetSidSubAuthority(sid, j) == gi[i].grpi2_group_id)
				{
					printf("Help: Potential match (based on %d): Domain: \"%S\", Account \"%S\", Comment: \"%S\"\n", gi[i].grpi2_group_id, (szDomain ? szDomain : L"Local"), gi[i].grpi2_name, gi[i].grpi2_comment);
				} // if the subauthority id in the group matches that in the sid
			} // for each subauthority in the sid
		} // For each entry in the group array

		if(pbData)
			NetApiBufferFree(pbData);
		pbData = 0;
	}

	if(pbData)
		NetApiBufferFree(pbData);
	pbData = 0;
}

CIdentity *CIdentityList::Lookup(PSID pSid)
{
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(EqualSid(m_ppList[i]->SID(), pSid))
			return m_ppList[i];
	}
	return 0;
}

