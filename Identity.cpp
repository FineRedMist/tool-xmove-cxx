
#include "Identity.h"
#include "sdhelpers.h"
#include <stdio.h>
#include <sddl.h>

void SidBreakdown(PSID sid, CHAR *szBuf)
{
#define SIDDISPLAY(str, buf, pos)									\
	{																\
		if(buf != 0)												\
		{															\
			strcpy(buf + pos, str);									\
			pos += strlen(str);										\
		}															\
		else														\
		{															\
			fputs(str, stderr);										\
		}															\
	}

	DWORD dwPos = 0;
	PSID_IDENTIFIER_AUTHORITY ida = GetSidIdentifierAuthority(sid);
	PUCHAR pcCount = GetSidSubAuthorityCount(sid);
	SID_IDENTIFIER_AUTHORITY a = SECURITY_NULL_SID_AUTHORITY;
	if(!memcmp(ida, &a, sizeof(SID_IDENTIFIER_AUTHORITY)))
		SIDDISPLAY("SECURITY_NULL_SID_AUTHORITY ", szBuf, dwPos);
	SID_IDENTIFIER_AUTHORITY b = SECURITY_WORLD_SID_AUTHORITY;
	if(!memcmp(ida, &b, sizeof(SID_IDENTIFIER_AUTHORITY)))
		SIDDISPLAY("SECURITY_WORLD_SID_AUTHORITY ", szBuf, dwPos);
	SID_IDENTIFIER_AUTHORITY c = SECURITY_LOCAL_SID_AUTHORITY;
	if(!memcmp(ida, &c, sizeof(SID_IDENTIFIER_AUTHORITY)))
		SIDDISPLAY("SECURITY_LOCAL_SID_AUTHORITY ", szBuf, dwPos);
	SID_IDENTIFIER_AUTHORITY d = SECURITY_CREATOR_SID_AUTHORITY;
	if(!memcmp(ida, &d, sizeof(SID_IDENTIFIER_AUTHORITY)))
		SIDDISPLAY("SECURITY_CREATOR_SID_AUTHORITY ", szBuf, dwPos);
	SID_IDENTIFIER_AUTHORITY e = SECURITY_NT_AUTHORITY;
	if(!memcmp(ida, &e, sizeof(SID_IDENTIFIER_AUTHORITY)))
		SIDDISPLAY("SECURITY_NT_AUTHORITY ", szBuf, dwPos);

#define SIDCASESTEP(name)										\
	case name:													\
	SIDDISPLAY(#name, szBuf, dwPos);							\
	SIDDISPLAY(" ", szBuf, dwPos);								\
	break;

	if(*pcCount > 0)
	{
		switch(*GetSidSubAuthority(sid, 0))
		{
			SIDCASESTEP(SECURITY_DIALUP_RID);
			SIDCASESTEP(SECURITY_NETWORK_RID);
			SIDCASESTEP(SECURITY_BATCH_RID);
			SIDCASESTEP(SECURITY_INTERACTIVE_RID);
			SIDCASESTEP(SECURITY_LOGON_IDS_RID);
			SIDCASESTEP(SECURITY_SERVICE_RID);
			SIDCASESTEP(SECURITY_ANONYMOUS_LOGON_RID);
			SIDCASESTEP(SECURITY_PROXY_RID);
			SIDCASESTEP(SECURITY_ENTERPRISE_CONTROLLERS_RID);
			SIDCASESTEP(SECURITY_PRINCIPAL_SELF_RID);
			SIDCASESTEP(SECURITY_AUTHENTICATED_USER_RID);
			SIDCASESTEP(SECURITY_RESTRICTED_CODE_RID);
			SIDCASESTEP(SECURITY_TERMINAL_SERVER_RID);
			SIDCASESTEP(SECURITY_LOCAL_SYSTEM_RID);
			SIDCASESTEP(SECURITY_NT_NON_UNIQUE);
			SIDCASESTEP(SECURITY_BUILTIN_DOMAIN_RID);
		}
	}

	for(UCHAR i = 0; i < *pcCount; ++i)
	{
		PDWORD pdwSub = GetSidSubAuthority(sid, i);
		switch(*pdwSub)
		{
			SIDCASESTEP(SECURITY_CREATOR_GROUP_RID);
			SIDCASESTEP(DOMAIN_USER_RID_ADMIN);
			SIDCASESTEP(DOMAIN_USER_RID_GUEST);
			SIDCASESTEP(DOMAIN_GROUP_RID_ADMINS);
			SIDCASESTEP(DOMAIN_GROUP_RID_USERS);
			SIDCASESTEP(DOMAIN_GROUP_RID_GUESTS);
			SIDCASESTEP(DOMAIN_GROUP_RID_COMPUTERS);
			SIDCASESTEP(DOMAIN_GROUP_RID_CONTROLLERS);
			SIDCASESTEP(DOMAIN_GROUP_RID_CERT_ADMINS);
			SIDCASESTEP(DOMAIN_GROUP_RID_SCHEMA_ADMINS);
			SIDCASESTEP(DOMAIN_GROUP_RID_ENTERPRISE_ADMINS);
			SIDCASESTEP(DOMAIN_GROUP_RID_POLICY_ADMINS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_ADMINS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_USERS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_GUESTS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_POWER_USERS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_ACCOUNT_OPS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_SYSTEM_OPS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_PRINT_OPS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_BACKUP_OPS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_REPLICATOR);
			SIDCASESTEP(DOMAIN_ALIAS_RID_RAS_SERVERS);
			SIDCASESTEP(DOMAIN_ALIAS_RID_PREW2KCOMPACCESS);
			;
		}
	}

	if(!szBuf)
		fputs("\n", stderr);
	else
		szBuf[strlen(szBuf) - 1] = 0;
}


CIdentity::CIdentity()
{
	m_szDomain = m_szName = 0;
	m_szFullName = 0;
	memset(m_sid, 0, sizeof(m_sid));
	m_pbStream = 0;
	m_dwError = 0;
	m_dwErrorSource = 0;
}

CIdentity::~CIdentity()
{
	if(m_pbStream)
		free(m_pbStream);
}

#define ARRAYSIZE(arr)	(sizeof(arr)/sizeof(arr[0]))

CIdentity *CIdentity::Lookup(const PSID sid, SidUsage su, DWORD &dwError)
{
	CIdentity *pID = 0;
	CHAR szDomain[256], szName[256];
	WCHAR wszDomain[256], wszName[256];
	DWORD dwDomainLen = ARRAYSIZE(szDomain) - 1, dwNameLen = ARRAYSIZE(szName) - 1;
	SID_NAME_USE sidUse = (SID_NAME_USE) 0;

	dwError = 0;
	szDomain[dwDomainLen] = szName[dwNameLen] = 0;

	WCHAR *szDC = 0;
	USER_INFO_2 *ui2 = 0;

	if(!IsValidSid(sid))
	{
		LPWSTR szSidString = 0;
		if(!ConvertSidToStringSid(sid, &szSidString))
		{
			fprintf(stderr, "Error: Failed to convert the SID to a string.  Error: %d\n", GetLastError());
		}
		fprintf(stderr, "Error: The SID: \"%S\" is invalid.\n", (szSidString ? szSidString : L""));

		if(szSidString)
			LocalFree(szSidString);
		goto Exit;
	}

	if(!LookupAccountSidA(0, sid, szName, &dwNameLen, szDomain, &dwDomainLen, &sidUse))
	{
		CHAR szSidBreakdown[256];
		LPWSTR szSidString = 0;
		dwError = GetLastError();
		SidBreakdown(sid, szSidBreakdown);
		if(!ConvertSidToStringSid(sid, &szSidString))
		{
			fprintf(stderr, "Error: Failed to convert the SID to a string.  Error: %d\n", GetLastError());
		}
		if(sidUse != 0)
		{
			fprintf(stderr, "Warning: Failed to lookup the domain and username for the SID \"%S\"(sidType=%S), breakdown: \"%s\".  Error %d\n", (szSidString ? szSidString : L""), SidTypeToString(sidUse), szSidBreakdown, dwError);
		}
		else
		{
			fprintf(stderr, "Warning: Failed to lookup the domain and username for the SID \"%S\", breakdown: \"%s\".  Error %d\n", (szSidString ? szSidString : L""), szSidBreakdown, dwError);
		}
		if(szSidString)
			LocalFree(szSidString);
		dwDomainLen = dwNameLen = 0;
		dwError = 0;
	}
	else
	{
		++dwDomainLen;
		++dwNameLen;
	}

	// The classes of accounts that aren't to have a full name lookup
	if(SidTypeUser != sidUse)
		goto BuildStream;

	MultiByteToWideChar(CP_ACP, 0, szDomain, -1, wszDomain, sizeof(wszDomain)/sizeof(WCHAR));
	MultiByteToWideChar(CP_ACP, 0, szName, -1, wszName, sizeof(wszName)/sizeof(WCHAR));
	dwError = NetGetDCName(0, wszDomain, (LPBYTE *) &szDC);

	// If attempting to get the user's full name fails, it's no big, we still have enough information to construct the identity object.
	if(dwError != NERR_Success)
	{
		NetUserGetInfo(0, wszName, 2, (LPBYTE *) &ui2);
	}
	else
	{
		NetUserGetInfo(szDC, wszName, 2, (LPBYTE *) &ui2);
	}
	dwError = 0;

BuildStream:
	pID = Create(sid, su, sidUse, ((DWORD) sidUse == 0) ? 0 : szDomain, ((DWORD) sidUse == 0) ? 0 : szName, (ui2 && ui2->usri2_full_name) ? ui2->usri2_full_name : 0);

Exit:
	if(szDC)
		NetApiBufferFree(szDC);
	if(ui2)
		NetApiBufferFree(ui2);

	return pID;
}

CIdentity *CIdentity::Generate(const LPBYTE stream)
{
	CIdentity *pID = new CIdentity();
	LPBYTE pos = 0;
	WORD sidType = 0;
	BYTE sidUse = 0;
	DWORD dwTotalLen = *((DWORD *) stream);
	pID->m_pbStream = (LPBYTE) malloc(dwTotalLen);
	memcpy(pID->m_pbStream, stream, dwTotalLen);

	pos = pID->m_pbStream + sizeof(DWORD);
	sidType = *((WORD *) (pos));
	pos += sizeof(WORD);
	sidUse = *((BYTE *) (pos));
	pos += sizeof(BYTE);
	WORD wSidLen = *((WORD *) (pos));
	memcpy(pID->m_sid, pos + sizeof(WORD), (wSidLen) ? wSidLen : 0);
	pos += sizeof(WORD) + wSidLen;
	BYTE bDomainLen = *((BYTE *) (pos));
	pID->m_szDomain = (CHAR *) ((bDomainLen) ? pos + sizeof(BYTE) : 0);
	pos += sizeof(BYTE) + bDomainLen;
	BYTE bNameLen = *((BYTE *) (pos));
	pID->m_szName = (CHAR *) ((bNameLen) ? pos + sizeof(BYTE) : 0);
	pos += sizeof(BYTE) + bNameLen;
	WORD wFullNameLen = *((WORD *) (pos));
	pID->m_szFullName = (WCHAR *) ((wFullNameLen) ? pos + sizeof(WORD) : 0);
	return pID;
}

const PSID CIdentity::SID()
{
	return m_sid;
}

const CHAR *CIdentity::Domain()
{
	return m_szDomain;
}

const CHAR *CIdentity::Name()
{
	return m_szName;
}

const WCHAR *CIdentity::FullName()
{
	return m_szFullName;
}

DWORD CIdentity::DisplayName(CHAR *szDisplayName, DWORD dwLen)
{
	DWORD dwRes = 0;
	if((!Name() || !Name()[0]) && (!Domain() || !Domain()[0]))
	{
		LPSTR szSid = 0;
		ConvertSidToStringSidA(SID(), &szSid);
		if(strlen(szSid) >= dwLen)
			dwRes = (DWORD) E_INVALIDARG;
		else
			strcpy(szDisplayName, szSid);
		if(szSid)
			LocalFree(szSid);
	}
	else if(!Domain() || !Domain()[0])
	{
		if(strlen(Name()) >= dwLen)
			dwRes = (DWORD) E_INVALIDARG;
		else
			strcpy(szDisplayName, Name());
	}
	else
	{
		DWORD dwDomainLen = strlen(Domain()), dwNameLen = strlen(Name());
		if(dwDomainLen + dwNameLen + 1 >= dwLen)
			dwRes = (DWORD) E_INVALIDARG;
		else
		{
			strcpy(szDisplayName, Domain());
			szDisplayName[dwDomainLen] = '\\';
			strcpy(szDisplayName + dwDomainLen + 1, Name());
		}
	}
	return dwRes;
}

DWORD CIdentity::StreamLength()
{
	return (m_pbStream) ? *((DWORD *) m_pbStream) : 0;
}

const LPBYTE CIdentity::Stream()
{
	return m_pbStream;
}

SID_NAME_USE CIdentity::SidType()
{
	return (SID_NAME_USE) ((m_pbStream) ? *((WORD *) (m_pbStream + sizeof(DWORD))) : 0);
}

CIdentity *CIdentity::Create(const PSID sid, SidUsage sidUse, SID_NAME_USE sidType, const CHAR *szDomain, const CHAR *szName, const WCHAR *szFullName)
{
	WORD wSidLen = (WORD) GetLengthSid(sid);
	WORD wFullNameLen = (szFullName) ? (WORD) wcslen(szFullName) + 1 : 0;
	BYTE bDomainLen = (szDomain) ? (BYTE) strlen(szDomain) + 1 : 0;
	BYTE bNameLen = (szName) ? (BYTE) strlen(szName) + 1 : 0;
	DWORD dwTotalLen = sizeof(DWORD) + 3 * sizeof(WORD) + 3 * sizeof(BYTE) + wSidLen + bDomainLen + bNameLen + wFullNameLen * sizeof(WCHAR);
	CIdentity *pID = new CIdentity();

	LPBYTE pos = (LPBYTE) malloc(dwTotalLen);
	pID->m_pbStream = pos;

	// Total Length
	*((DWORD *) pos) = dwTotalLen;
	pos += sizeof(DWORD);

	// Sid Type
	*((WORD *) pos) = (WORD) sidType;
	pos += sizeof(WORD);

	// Sid Usage
	*((BYTE *) pos) = (BYTE) sidUse;
	pos += sizeof(BYTE);

	// SID
	*((WORD *) pos) = wSidLen;
	pos += sizeof(WORD);
	CopySid(wSidLen, pos, sid);
	CopySid(wSidLen, pID->m_sid, sid);
	pos += wSidLen;

	// Domain
	*((BYTE *) pos) = bDomainLen;
	pos += sizeof(BYTE);
	if(bDomainLen)
	{
		memcpy(pos, szDomain, bDomainLen);
		pID->m_szDomain = (CHAR *) pos;
		pos += bDomainLen;
	}

	// Name
	*((BYTE *) pos) = bNameLen;
	pos += sizeof(BYTE);
	if(bNameLen)
	{
		memcpy(pos, szName, bNameLen);
		pID->m_szName = (CHAR *) pos;
		pos += bNameLen;
	}

	// Full name
	*((WORD *) pos) = wFullNameLen;
	pos += sizeof(WORD);
	if(wFullNameLen)
	{
		memcpy(pos, szFullName, wFullNameLen * sizeof(WCHAR));
		pID->m_szFullName = (WCHAR *) pos;
	}
	
	return pID;
}

BYTE CIdentity::SidUse()
{
	return *(m_pbStream + sizeof(DWORD) + sizeof(WORD));
}

void CIdentity::AddUse(SidUsage sidUse)
{
	*(m_pbStream + sizeof(DWORD) + sizeof(WORD)) |= (BYTE) sidUse;
}

