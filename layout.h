

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include <windows.h>
#include "xmove.h"

/*
	The layout of the backup file consists of the following:
	
	HEADERREGION
	DATABACKUP
	USERIDENTIFICATION
	FOOTERREGION

	The header region is broken up as follows:

	HEADERREGION := DESCRIPTION					(256 bytes--ascii text)
					0x1A424F58					(4 bytes)
					XMBVERSION					(4 bytes)
					DATABACKUPOFFSET			(4 bytes)
                    USERIDENTIFICATIONOFFSET	(8 bytes)
					DIRECTORYCOUNT				(4 bytes)
					FILECOUNT					(4 bytes)
					USERCOUNT					(4 bytes)

	DATABACKUP := 		[FILEBACKUP|DIRECTORYBACKUP]
						ENDBACKUPID				(1 byte)
	FILEBACKUP :=		FILEID					(1 byte)
						NAMELEN					(2 bytes) -- includes null
						NAME					(FILENAMELEN bytes) -- WCHAR *
						STREAMBACKUP			(* bytes) -- all data returned from BackupRead	
						SECURITYBACKUP 
						ENDBACKUPID				(4 bytes) == BACKUP_INVALID
	DIRECTORYBACKUP :=	DIRECTORYID				(1 byte)
						NAMELEN					(2 bytes) -- includes null
						NAME					(FILENAMELEN bytes) -- WCHAR *
						STREAMBACKUP			(* bytes) -- all data returned from BackupRead
						SECURITYBACKUP 
						ENDBACKUPID				(4 bytes) == BACKUP_INVALID
						[FILEBACKUP]*
						[DIRECTORYBACKUP]*
						ENDDIRECTORYID			(1 byte)
	SECURITYBACKUP :=	SECURITYID				(4 bytes)
						SECURITYDESCRIPTORLENGTH(4 bytes)
						SECURITYDESCRIPTOR		(SECURITYDESCRIPTORLENGTH bytes) -- only need explicit unless the first entry

	USERIDENTIFICATION :=	TOTALLENGTH			(4 bytes)
							SIDTYPE				(2 bytes)
							SIDLENGTH			(2 bytes)
							SID					(SIDLENGTH bytes)
							DOMAINLENGTH		(1 byte)
							DOMAIN				(DOMAINLENGTH bytes)
							LOGINLENGTH			(1 byte)
							LOGIN				(LOGINLENGTH bytes)
							NAMELENGTH			(2 bytes)
							NAME				(NAMELENGTH bytes)

*/


#define HELP_TEXT_BUFFER_SIZE 512
#define AUTO_TEXT_BUFFER_SIZE 512

#pragma pack(push, 1)
struct sHeaderRegion
{
	CHAR szDesc[HELP_TEXT_BUFFER_SIZE + AUTO_TEXT_BUFFER_SIZE];
	DWORD dwHeaderID; // == 0x1A424F58
	DWORD dwVersion;  // == XMB_VERSION in xmove.h
	DWORD dwDataOffset;
	DWORDLONG dwlUserOffset;
	DWORD dwDirectoryCount;
	DWORD dwFileCount;
	DWORD dwUserCount;
};
#pragma pack(pop)

inline void InitializeHeader(sHeaderRegion &header)
{
	memset(&header, 0, sizeof(sHeaderRegion));
	header.dwHeaderID = 0x1A424F58;
	header.dwVersion = XMB_VERSION;
	header.dwDataOffset = sizeof(sHeaderRegion);
}

inline int CheckHeader(const sHeaderRegion &header)
{
	if(header.dwHeaderID != 0x1A424F58)
	{
		fputs("The header is invalid.  Bad ID.\n", stderr);
		return -1;
	}
	if(header.dwVersion != XMB_VERSION)
	{
		fputs("The header is invalid.  Bad Version.\n", stderr);
		return -1;
	}
	if(header.dwDataOffset != sizeof(sHeaderRegion))
	{
		fputs("The header is invalid.  Bad data offset.\n", stderr);
		return -1;
	}
	if(header.dwlUserOffset <= header.dwDataOffset)
	{
		fputs("The header is invalid.  Bad user offset.\n", stderr);
		return -1;
	}
	return 0;
}

enum BackupIDs
{
	BackupID_Directory,
	BackupID_File,
	BackupID_End,
	BackupID_EndDirectory
};
