
#include "layout.h"
#include "xmove.h"
#include "bkpFile.h"
#include "sdhelpers.h"
#include "identitylist.h"
#include "sddl.h"

int users(const WCHAR *szBackupFile)
{
	int res = 1;
	sHeaderRegion hdr;
	BYTE *pbData = 0;
	CIdentityList idList;

	CBackupFileRead *cbfr = CBackupFileRead::Open(szBackupFile);
	
	if(cbfr == 0 || (res = cbfr->LastError()) != 0)
		goto Exit;

	cbfr->BeginBlock(false);

	if(!cbfr->Read((BYTE *) &hdr, sizeof(hdr)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	if(0 != (res = CheckHeader(hdr)))
		return res;

	// hdr.szDesc[strlen(hdr.szDesc) - 1] = 0;
	// puts(hdr.szDesc);

	DWORDLONG dwl = cbfr->Seek(hdr.dwlUserOffset, FILE_BEGIN);
	if(dwl != hdr.dwlUserOffset)
	{
		res = cbfr->LastError();
		goto Exit;
	}

	cbfr->BeginBlock(true);

	DWORD dwSize = 0;
	if(!cbfr->Read((BYTE *) &dwSize, sizeof(DWORD)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	pbData = (BYTE *) malloc(dwSize);
	*((DWORD *) pbData) = dwSize;

	if(!cbfr->Read(pbData + sizeof(DWORD), dwSize - sizeof(DWORD)))
	{
		res = cbfr->LastError();
		goto Exit;
	}

	idList.Deserialize(pbData);
	idList.Display(verbosity < 0 ? 0 : (verbosity > 2 ? 2 : verbosity));

Exit:
	if(pbData)
		free(pbData);
	if(cbfr)
		delete cbfr;
	return res;
}