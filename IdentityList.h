

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include "Identity.h"

class CIdentityList
{
public:
	CIdentityList();
	~CIdentityList();

	DWORD Submit(const PSID sid, SidUsage sidUse, const WCHAR *szHelpString);
	DWORD Deserialize(const LPBYTE pbStream);
	LPBYTE Serialize(DWORD &dwLen);

	CIdentity *operator[](DWORD dwIndex);
	CIdentity *Lookup(PSID pSid);

	DWORD Count();

	void Display(int level = 1);

protected:
	CIdentity **m_ppList;
	DWORD m_dwSize;
	DWORD m_dwCount;
};