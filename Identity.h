

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include <windows.h>
#include <lm.h>
#include <accctrl.h>
#include <aclapi.h>

/*Serialized format:
	DWORD			TotalLength (including TotalLength)
	WORD			SidType		-- could be a byte, but a WORD ensures SID is on a DWORD boundary.
	WORD			SidLength
	SidLength		SID
	BYTE			DomainLength (including terminating null, in WCHARs)
	DomainLength	Domain
	BYTE			NameLength (including terminating null, in WCHARs)
	NameLength		Name
	WORD			FullNameLength (including terminating null, in WCHARs)
	FullNameLength	FullName

*/

enum SidUsage
{
	SidUse_None = 0,
	SidUse_Owner = 1,
	SidUse_Group = 2,
	SidUse_DACL  = 4,
	SidUse_SACL  = 8
};

const SidUsage c_MaxUsage = (SidUsage) ((int) SidUse_Owner | (int) SidUse_Group | (int) SidUse_DACL | (int) SidUse_SACL);

void SidBreakdown(PSID sid, CHAR *szBuf);

class CIdentity
{
protected:
	CIdentity();

public:
	~CIdentity();

	static CIdentity *Lookup(const PSID sid, SidUsage sidUse, DWORD &dwError);
	static CIdentity *Generate(const LPBYTE stream);

	DWORD LastError();
	DWORD ErrorSource();

	const PSID SID();
	const CHAR *Domain();
	const CHAR *Name();
	const WCHAR *FullName();

	DWORD DisplayName(CHAR *szDisplayName, DWORD dwLen);

	DWORD StreamLength();
	const LPBYTE Stream();

	BYTE SidUse();
	void AddUse(SidUsage sidUse);

	SID_NAME_USE SidType();

protected:
	static CIdentity *Create(const PSID sid, SidUsage sidUse, SID_NAME_USE sidType, const CHAR *szDomain, const CHAR *szName, const WCHAR *szFullName);
	CHAR *m_szDomain;
	CHAR *m_szName;
	WCHAR *m_szFullName;
	BYTE m_sid[64];

	DWORD m_dwError;
	DWORD m_dwErrorSource;
	LPBYTE m_pbStream;
};