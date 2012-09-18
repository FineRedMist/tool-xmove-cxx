
#include "sdhelpers.h"
#include <stdio.h>

#include "xmove.h"

DWORD SubmitUsersFromSecurityDescriptor(const SECURITY_DESCRIPTOR *pSD, CIdentityList &idList, const WCHAR *szHelpString)
{
	DWORD res = 0;
	DWORD dwRevision;
	SECURITY_DESCRIPTOR_CONTROL sdCtrl;
	res = GetSecurityDescriptorControl((PSECURITY_DESCRIPTOR) pSD, &sdCtrl, &dwRevision);
/*	if(res)
		DisplayRevisionControl(sdCtrl, dwRevision);
*/

	PSID owner;
	BOOL ownerDefaulted;
	res = GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR) pSD, &owner, &ownerDefaulted);
	if(!res)
		goto Exit;

	if(idList.Submit(owner, SidUse_Owner, szHelpString))
	{
		puts("Failed to lookup the owner.");
	}

	PSID group;
	BOOL groupDefaulted;
	res = GetSecurityDescriptorGroup((PSECURITY_DESCRIPTOR) pSD, &group, &groupDefaulted);
	if(!res)
		goto Exit;
    
	if(idList.Submit(group, SidUse_Group, szHelpString))
	{
		puts("Failed to lookup the group.");
	}

	if(BIT_TEST(sdCtrl, SE_DACL_PRESENT))
	{
		BOOL daclPresent, daclDefaulted;
		PACL pDacl;
		res = GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR) pSD, &daclPresent, &pDacl, &daclDefaulted);
		if(!res)
			goto Exit;

		res = SubmitUsersFromACL(pDacl, idList, true, szHelpString);
		if(res)
			goto Exit;
	}

	if(BIT_TEST(sdCtrl, SE_SACL_PRESENT))
	{
		BOOL saclPresent, saclDefaulted;
		PACL pSacl;
		res = GetSecurityDescriptorSacl((PSECURITY_DESCRIPTOR) pSD, &saclPresent, &pSacl, &saclDefaulted);
		if(!res)
			goto Exit;

		res = SubmitUsersFromACL(pSacl, idList, false, szHelpString);
		if(res)
			goto Exit;
	}

Exit:
	return res;
}


DWORD SubmitUsersFromACL(const PACL pAcl, CIdentityList &idList, bool isDacl, const WCHAR *szHelpString)
{
	DWORD res = 0;

	ACL_SIZE_INFORMATION asi;

	if(!GetAclInformation(pAcl, &asi, sizeof(asi), AclSizeInformation))
	{
		res = GetLastError();
		goto Exit;
	}
	
	for(DWORD i = 0; i < asi.AceCount; ++i)
	{
		ACE_HEADER *pAce;
		if(!GetAce(pAcl, i, (LPVOID *) &pAce))
		{
			res = GetLastError();
			goto Exit;
		}
		res = SubmitUsersFromACE(pAce, idList, isDacl, szHelpString);
		if(res)
			goto Exit;
	}
Exit:
	return res;
}

void DisplayRevisionControl(SECURITY_DESCRIPTOR_CONTROL sdCtrl, DWORD dwRevision)
{
	printf("SD Revision: %d\n", dwRevision);
	fputs("SD Control: ", stdout);
	if(BIT_TEST(sdCtrl, SE_DACL_AUTO_INHERITED))
		fputs("SE_DACL_AUTO_INHERITED ", stdout);
	if(BIT_TEST(sdCtrl, SE_DACL_DEFAULTED))
		fputs("SE_DACL_DEFAULTED ", stdout);
	if(BIT_TEST(sdCtrl, SE_DACL_PRESENT))
		fputs("SE_DACL_PRESENT ", stdout);
	if(BIT_TEST(sdCtrl, SE_DACL_PROTECTED))
		fputs("SE_DACL_PROTECTED ", stdout);
	if(BIT_TEST(sdCtrl, SE_GROUP_DEFAULTED))
		fputs("SE_GROUP_DEFAULTED ", stdout);
	if(BIT_TEST(sdCtrl, SE_OWNER_DEFAULTED))
		fputs("SE_OWNER_DEFAULTED ", stdout);
	if(BIT_TEST(sdCtrl, SE_SELF_RELATIVE))
		fputs("SE_SELF_RELATIVE ", stdout);
	if(BIT_TEST(sdCtrl, SE_RM_CONTROL_VALID))
		fputs("SE_RM_CONTROL_VALID ", stdout);
	if(BIT_TEST(sdCtrl, SE_SACL_AUTO_INHERITED))
		fputs("SE_SACL_AUTO_INHERITED", stdout);
	if(BIT_TEST(sdCtrl, SE_SACL_DEFAULTED))
		fputs("SE_SACL_DEFAULTED", stdout);
	if(BIT_TEST(sdCtrl, SE_SACL_PRESENT))
		fputs("SE_SACL_PRESENT", stdout);
	if(BIT_TEST(sdCtrl, SE_SACL_PROTECTED))
		fputs("SE_SACL_PROTECTED", stdout);

	
	SECURITY_DESCRIPTOR_CONTROL sdc = sdCtrl & (~(SE_DACL_AUTO_INHERITED |
													SE_DACL_DEFAULTED |
													SE_DACL_PRESENT |
													SE_DACL_PROTECTED |
													SE_GROUP_DEFAULTED |
													SE_OWNER_DEFAULTED |
													SE_SELF_RELATIVE |
													SE_RM_CONTROL_VALID |
													SE_SACL_AUTO_INHERITED |
													SE_SACL_DEFAULTED |
													SE_SACL_PRESENT |
													SE_SACL_PROTECTED));
	printf(" -- remaining: 0x%04x\n", sdc);
}



#define ACE_CASE(objName)												\
	case objName##_TYPE:												\
		{																\
			objName *aaa = (objName *) pAce;							\
			if(verbosity > 3)											\
			{															\
				printf("ACE Flags: %08x, Mask: %08x\n", pAce->AceFlags, aaa->Mask);							\
			}															\
			res = idList.Submit((PSID) &(aaa->SidStart), (isDacl ? SidUse_DACL : SidUse_SACL), szHelpString);	\
			break;														\
		}

DWORD SubmitUsersFromACE(const ACE_HEADER *pAce, CIdentityList &idList, bool isDacl, const WCHAR *szHelpString)
{
	DWORD res = 0;
	switch(pAce->AceType)
	{
		ACE_CASE(ACCESS_ALLOWED_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_ALLOWED_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_ACE);
		ACE_CASE(SYSTEM_ALARM_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_ALARM_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_OBJECT_ACE);
	default:
		fprintf(stderr, "Warning: ACE type 0x%04X was not checked for users.  This will prevent future mappings.\n", pAce->AceType);
	};

	return res;
}

#undef ACE_CASE
#define ACE_CASE(objName)												\
	case objName##_TYPE:												\
		{																\
			objName *aaa = (objName *) pAce;							\
			return (PSID) &(aaa->SidStart);								\
		}

PSID GetSid(PACE_HEADER pAce)
{
	switch(pAce->AceType)
	{
		ACE_CASE(ACCESS_ALLOWED_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_ALLOWED_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_ACE);
		ACE_CASE(SYSTEM_ALARM_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_ALARM_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_OBJECT_ACE);
	default:
		fprintf(stderr, "Warning: ACE type 0x%04X could not have a SID retrieved for mapping.  This user won't be mapped.\n", pAce->AceType);
		return 0;
	};
}


#undef ACE_CASE
#define ACE_CASE(objName)												\
	case objName##_TYPE:												\
		{																\
			objName *aaa = (objName *) pAce;							\
			CopySid(dwLen, (PSID) &(aaa->SidStart), pSid);				\
			return;														\
		}


void SetSid(PACE_HEADER pAce, PSID pSid, DWORD dwLen)
{
	switch(pAce->AceType)
	{
		ACE_CASE(ACCESS_ALLOWED_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_ALLOWED_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_ACE);
		ACE_CASE(SYSTEM_ALARM_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_ALARM_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_OBJECT_ACE);
	};
}

#undef ACE_CASE
#define ACE_CASE(objName)												\
	case objName##_TYPE:												\
		{																\
			objName *dst = (objName *) pAceDest;						\
			objName *src = (objName *) pAceSrc;							\
			dst->Mask |= src->Mask;										\
			return;														\
		}

void AceMaskOr(PACE_HEADER pAceDest, PACE_HEADER pAceSrc)
{
	switch(pAceDest->AceType)
	{
		ACE_CASE(ACCESS_ALLOWED_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_ACE);
		ACE_CASE(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_ALLOWED_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_ACE);
		ACE_CASE(ACCESS_DENIED_CALLBACK_OBJECT_ACE);
		ACE_CASE(ACCESS_DENIED_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_ACE);
		ACE_CASE(SYSTEM_ALARM_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_ACE);
		ACE_CASE(SYSTEM_ALARM_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_ALARM_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_ACE);
		ACE_CASE(SYSTEM_AUDIT_CALLBACK_OBJECT_ACE);
		ACE_CASE(SYSTEM_AUDIT_OBJECT_ACE);
	};
}

WCHAR *SidTypeToString(SID_NAME_USE sidUse)
{
	switch((DWORD) sidUse)
	{
	case 0:
		return L"LookupFailed";
	case SidTypeUser:
		return L"User";
	case SidTypeGroup:
		return L"Group";
	case SidTypeDomain:
		return L"Domain";
	case SidTypeAlias:
		return L"Alias";
	case SidTypeWellKnownGroup:
		return L"WellKnownGroup";
	case SidTypeDeletedAccount:
		return L"DeletedAccount";
	case SidTypeInvalid:
		return L"Invalid";
	case SidTypeUnknown:
		return L"Unknown";
	case SidTypeComputer:
		return L"Computer";
	default:
		return L"Other";
	};
}

TRUSTEE_TYPE SidTypeToTrusteeType(SID_NAME_USE sidUse)
{
	switch((DWORD) sidUse)
	{
	case 0:
		return TRUSTEE_IS_UNKNOWN;
	case SidTypeUser:
		return TRUSTEE_IS_USER;
	case SidTypeGroup:
		return TRUSTEE_IS_GROUP;
	case SidTypeDomain:
		return TRUSTEE_IS_DOMAIN;
	case SidTypeAlias:
		return TRUSTEE_IS_ALIAS;
	case SidTypeWellKnownGroup:
		return TRUSTEE_IS_WELL_KNOWN_GROUP;
	case SidTypeDeletedAccount:
		return TRUSTEE_IS_DELETED;
	case SidTypeInvalid:
		return TRUSTEE_IS_INVALID;
	case SidTypeUnknown:
		return TRUSTEE_IS_UNKNOWN;
	case SidTypeComputer:
		return TRUSTEE_IS_COMPUTER;
	default:
		return TRUSTEE_IS_UNKNOWN;
	};
}