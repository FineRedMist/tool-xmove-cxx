

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include "IdentityList.h"
#include "AceList.h"

class CIdentityMap : public CIdentityList
{
public:
	CIdentityMap();
	~CIdentityMap();

	CIdentityList *NewIDs(DWORD dwIndex);

	DWORD LoadMaps(const WCHAR *szMapFile);
	
	DWORD MapSecurityDescriptor(const SECURITY_DESCRIPTOR *pOldSD, HANDLE hTargetFile, bool fFirst);

protected:
	CIdentityList *GetMapForIdentity(PSID pSID, CIdentity ** orig = 0);
	DWORD BuildExplicitAccess(PACL pAcl, CAceList& pAces, SidUsage sidUse, bool fFirst);
	CIdentityList **m_ppMaps;
	DWORD m_dwMapSize;
	DWORD m_dwMapped;
};