

#include "AceList.h"
#include "sdhelpers.h"
#include "stdio.h"

/*
	DWORD m_dwCount;
	DWORD m_dwSize;
	PPACE_HEADER m_ppAces;
*/

CAceList::CAceList()
{
	m_dwCount = 0;
	m_dwSize = 5;
	m_ppAces = (PPACE_HEADER) malloc(m_dwSize * sizeof(PACE_HEADER));
}

CAceList::~CAceList()
{
	if(m_ppAces)
		free(m_ppAces);
}

DWORD CAceList::AddAce(PACE_HEADER pAce, PSID pNewSid)
{
	DWORD dwRes = 0;
	PSID pAceSid = GetSid(pAce);
	PSID pSidToUse = (pNewSid) ? pNewSid : pAceSid;
	// If I couldn't get a valid SID, bail.
	if(!pSidToUse || (pNewSid && !pAceSid))
		return dwRes;

	// Find out if I have a matching ACE for this user.  If so, OR the masks.
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(pAce->AceType == m_ppAces[i]->AceType &&
			pAce->AceFlags == m_ppAces[i]->AceFlags &&
			EqualSid(pSidToUse, GetSid(m_ppAces[i])))
		{
			AceMaskOr(m_ppAces[i], pAce);
			return dwRes;
		}
	}

	// Didn't find a match, so now if there is a new SID to use, create a new ACE, otherwise make a copy of the existing one.
	WORD wSize = pAce->AceSize - ((WORD) GetLengthSid(pAceSid)) + ((WORD) GetLengthSid(pSidToUse));
    PACE_HEADER pNewAce = (PACE_HEADER) malloc(wSize);
	memcpy(pNewAce, pAce, pAce->AceSize);
	if(pNewSid)
	{
		SetSid(pNewAce, pNewSid, GetLengthSid(pNewSid));
	}
	pNewAce->AceSize = wSize;

	// If I've run out of space for adding another entry...
	if(m_dwCount == m_dwSize)
	{
		m_dwSize += 5;
		PPACE_HEADER ppaces = (PPACE_HEADER) realloc(m_ppAces, m_dwSize * sizeof(PACE_HEADER));
		if(!ppaces)
		{
			dwRes = (DWORD) E_OUTOFMEMORY;
			goto Exit;
		}
		m_ppAces = ppaces;
	}

	m_ppAces[m_dwCount++] = pNewAce;

Exit:
	return dwRes;
}

DWORD CAceList::Count()
{
	return m_dwCount;
}

PACL CAceList::GetAcl()
{
	DWORD dwRes;
	DWORD dwAclSize = sizeof(ACL);
	DWORD dwAceListSize = 0;
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		dwAceListSize += m_ppAces[i]->AceSize;
	}
	dwAclSize += dwAceListSize;

	PACL pAcl = (PACL) malloc(dwAclSize);
	if(!InitializeAcl(pAcl, dwAclSize, ACL_REVISION))
	{
		dwRes = GetLastError();
		fprintf(stderr, "Error: Failed to initialize a new ACL. Error: %d\n", dwRes);
		goto Error;
	}
	for(DWORD i = 0; i < m_dwCount; ++i)
	{
		if(!::AddAce(pAcl, ACL_REVISION, MAXDWORD, m_ppAces[i], m_ppAces[i]->AceSize))
		{
			dwRes = GetLastError();
			fprintf(stderr, "Error: Failed to add the ACE to the ACL.  Error: %d\n", dwRes);
			goto Error;
		}
	}
	if(!IsValidAcl(pAcl))
	{
		dwRes = 1;
		fputs("The ACL created is invalid.\n", stderr);
		goto Error;
	}

	return pAcl;

Error:
	if(pAcl)
		free(pAcl);
	return 0;
}

