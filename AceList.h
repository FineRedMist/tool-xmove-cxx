

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include "IdentityList.h"

typedef PACE_HEADER * PPACE_HEADER;

class CAceList
{
public:
	CAceList();
	~CAceList();

	DWORD AddAce(PACE_HEADER pAce, PSID pNewSid);
	DWORD Count();
	PACL GetAcl();

protected:
	DWORD m_dwCount;
	DWORD m_dwSize;
	PPACE_HEADER m_ppAces;
};