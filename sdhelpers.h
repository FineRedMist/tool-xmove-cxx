

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include "IdentityList.h"

DWORD SubmitUsersFromSecurityDescriptor(const SECURITY_DESCRIPTOR *pSD, CIdentityList &idList, const WCHAR *szHelpString);


void DisplayRevisionControl(SECURITY_DESCRIPTOR_CONTROL sdCtrl, DWORD dwRevision);
void DisplayAce(ACE_HEADER *pAce, CIdentityList &idList);

DWORD SubmitUsersFromACL(const PACL pAcl, CIdentityList &idList, bool isDacl, const WCHAR *szHelpString);
DWORD SubmitUsersFromACE(const ACE_HEADER *pAce, CIdentityList &idList, bool isDacl, const WCHAR *szHelpString);

WCHAR *SidTypeToString(SID_NAME_USE sidUse);
TRUSTEE_TYPE SidTypeToTrusteeType(SID_NAME_USE sidUse);

PSID GetSid(PACE_HEADER pAce);
void SetSid(PACE_HEADER pAce, PSID pSid, DWORD dwLen);
void AceMaskOr(PACE_HEADER pAceDest, PACE_HEADER pAceSrc);
