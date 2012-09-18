

#include <windows.h>
#include <stdio.h>
#include "layout.h"
#include "xmove.h"

void DisplayHelp();

int verbosity = 0;
int compressionfactor = 1;
bool testing = false;
BYTE copyBuffer[1048576];

enum xmove_option
{
	xm_none = -1,
	xm_firstPrimary = 0, xm_backup = 0,
	xm_restore,
	xm_test,
	xm_testusers,
	xm_dir,
	xm_users, xm_lastPrimary = xm_users,
	xm_map,
	xm_file,
	xm_help,
	xm_verbosity,
	xm_compressfactor
};

enum trichoice 
{
	yes,
	no,
	optional,
	maybe = optional
};

trichoice xmove_optionneedsmap[] =
{
	no,
	optional,
	optional,
	optional,
	no,
	no,
	yes,
	no,
	no,
	no,
	no
};

bool xmove_optionhasparam[] = 
{
	true,
	true,
	false,
	false,
	false,
	false,
	true,
	true,
	true,
	true,
	true
};

const WCHAR *xmove_optiontags[] =
{
	L"-b",
	L"-r",
	L"-t",
	L"-tu",
	L"-d",
	L"-u",
	L"-m",
	L"-f",
	L"-h",
	L"-v",
	L"-c"
};

int wmain(int argc, const WCHAR *argv[])
{
	/* Parse command line options.  Breakdown is:
		-b <pathtofileordir>
			backup
		-r <pathtorestoreto>
			restore
		-f <sourceordestfile>
			destination file on backup, source file on restore
		-m <filewithusermaps>
			user map file (format below)
		-t
			tests the backup file with the specified map file
		-d <path>
			does a directory listing for a path in the backup file as
				date attributes size refcounts name
		-u
			lists the users specified in the backup file
		-h	help text to include in the backup
		-v	verbosity level for debugging
		-c	[1, 2, 3] compression quality (aggressiveness)

		So -b c:\mydir will back up the mydir directory and -r d:\flaf with the same backup made will create a d:\flaf\mydir directory.
		The user map takes the login credentials from the original domain to a new domain.  The mapping is done as:
			<original login> <new login 1>, <new login 2>, ...
		To remove a mapping just have <original login> with no entries after.
		It is an error to not specify anything for a particular login.
		If no map file is specified, it is verified the restore is happening in the same domain.
	*/

	xmove_option xmo[6] = {xm_none, xm_none, xm_none, xm_none, xm_none, xm_none};
	const WCHAR *xmparam[6] = {0, 0, 0, 0, 0, 0};

	bool skipped;
	int pos = 0;
	for(int i = 1; i < argc; ++i)
	{
		skipped = true;
		for(int j = 0; j < ARRAYSIZE(xmove_optiontags); ++j)
		{
			if(pos >= ARRAYSIZE(xmo))
			{
				puts("Too many parameters!");
				DisplayHelp();
				return 1;
			}

			if(!wcsicmp(argv[i], xmove_optiontags[j]))
			{
				CHAR szBuf[80];
				sprintf(szBuf, "Matched \"%S\" as %d\n", argv[i], j);
				OutputDebugStringA(szBuf);
				if(xmove_optionhasparam[j] && i + 1 < argc)
				{
					xmparam[pos] = argv[++i];
				}
				xmo[pos++] = (xmove_option) j;
				skipped = false;
				break;
			}
		}
		if(skipped)
		{
			fprintf(stderr, "Warning: Option: \"%S\" skipped.\n", argv[i]);
		}
	}

	int primaryOpsCount = 0, mapCount = 0, fileCount = 0, helpCount = 0, verbCount = 0, compCount = 0;
	int primaryIdx = -1, mapIdx = -1, fileIdx = -1, helpIdx = -1, verbIdx = -1, compIdx = -1;
	bool fBad = false;
	for(int i = 0; i < ARRAYSIZE(xmo); ++i)
	{
		if(xmo[i] >= xm_firstPrimary && xmo[i] <= xm_lastPrimary)
		{
			primaryIdx = i;
			++primaryOpsCount;
		}
		else if(xmo[i] == xm_map)
		{
			mapIdx = i;
			++mapCount;
		}
		else if(xmo[i] == xm_file)
		{
			fileIdx = i;
			++fileCount;
		}
		else if(xmo[i] == xm_help)
		{
			helpIdx = i;
			++helpCount;
		}
		else if(xmo[i] == xm_verbosity)
		{
			verbIdx = i;
			++verbCount;
		}
		else if(xmo[i] == xm_compressfactor)
		{
			compIdx = i;
			++compCount;
		}
		if(xmove_optionhasparam[xmo[i]] && xmparam[i] == 0)
		{
			fprintf(stderr, "Error: %S requires an additional parameter.\n", xmove_optiontags[xmo[i]]);
			fBad = true;
		}
	}
	if(primaryOpsCount != 1)
	{
		puts("Exactly one of: -b, -r, -t, -d, or -u should be specified!");
		fBad = true;
	}
	if(fileCount != 1)
	{
		puts("Exactly one backup file should be specified!");
		fBad = true;
	}
	if(mapCount > 1)
	{
		puts("Exactly one user map should be specified!");
		fBad = true;
	}
	if(helpCount > 1)
	{
		puts("Exactly one help string should be specified!");
		fBad = true;
	}
	if(verbCount > 1)
	{
		puts("Exactly one verbosity level should be specified!");
		fBad = true;
	}
	if(compCount > 1)
	{
		puts("Exactly one compression quality factory should be specified!");
		fBad = true;
	}

	if(helpCount == 1 && wcslen(xmparam[helpIdx]) > HELP_TEXT_BUFFER_SIZE - 4)
	{
		fprintf(stderr, "Error: The help text specified is too long.  Max length is %d characters.\n", HELP_TEXT_BUFFER_SIZE - 4);
		fBad = true;
	}

	if(mapCount == 1 && xmove_optionneedsmap[xmo[primaryIdx]] == no)
	{
		puts("A user map was specified but is not needed for the specified operation.");
		puts("The user map file was ignored.");
	}
	
	if(helpCount == 1 && xmo[primaryIdx] != xm_backup)
	{
		puts("The help text specified is only relevant during backups.");
		puts("The help text was ignored.");
	}

	if(compCount > 0 && xmo[primaryIdx] != xm_backup)
	{
		puts("The compression quality factor is only relevant during backups.");
		puts("The compression quality factor was ignored.");
	}

	if(compCount == 1)
	{
		compressionfactor = _wtoi(xmparam[compIdx]);
		if(compressionfactor < 1)
		{
			puts("The compression quality factor was less than 1.  Setting to 1.");
			compressionfactor = 1;
		}
		if(compressionfactor > 3)
		{
			puts("The compression quality factor was greater than 3.  Setting to 3.");
			compressionfactor = 3;
		}
	}

	if(verbCount == 1)
	{
		verbosity = _wtoi(xmparam[verbIdx]);
	}

	if(fBad)
	{
		DisplayHelp();
		return 1;
	}

	switch(xmo[primaryIdx])
	{
	case xm_backup:
		return backup(xmparam[primaryIdx], xmparam[fileIdx], (helpIdx > -1) ? xmparam[helpIdx] : L"");
	case xm_restore:
		testing = true; // really restoring, but we don't want the identity lookup failed error to display.
		return restore(xmparam[primaryIdx], xmparam[fileIdx], (mapIdx > -1) ? xmparam[mapIdx] : 0);
	case xm_testusers:
	case xm_test:
		testing = true;
		return test(xmparam[fileIdx], (mapIdx > -1) ? xmparam[mapIdx] : 0, xmo[primaryIdx] == xm_testusers);
	case xm_dir:
		return dir(xmparam[fileIdx]);
	case xm_users:
		return users(xmparam[fileIdx]);
	}

	DisplayHelp();
	return 0;
}

void DisplayHelp()
{
	puts("XMove -- cross domain backup and restore tool.");
	puts("Usage:");
	puts("\t[[[-b|-r|-d] <path>]|[-u|-t]] -f <file> [-m <mapfile>]");
	puts("\t[-b|-r|-d] <path> -- backup the specified path");
	puts("\t                  -- restore to the specified path");
	puts("\t                  -- display the path in the backup file");
	puts("\t-u                -- list users in the file");
	puts("\t-t                -- test the backup file with the specified map");
	puts("\t-f                -- the backup file name");
	puts("\t-m <mapfile>      -- the name of the map file on restore or test only");
	puts("");
	puts("Examples:");
	puts("\tXMove -b c:\\mydir -f c:\\mydir.bkp");
	puts("\t -- backs up c:\\mydir directory into c:\\mydir.bkp (includes \"mydir\")");
	puts("\tXMove -r d:\\newdir -f c:\\mydir.bkp");
	puts("\t -- restores the mydir directory creating d:\\newdir\\mydir with the same\n\t    domain credentials as the original.");
	puts("\tXMove -d mydir -f c:\\mydir.bkp");
	puts("\t -- displays a directory listing of the files and directories in the\n\t    mydir directory of the mydir.bkp file.");
	puts("\tXMove -u -f c:\\mydir.bkp");
	puts("\t -- displays the users in <domain>\\<login> format that occur in the file\n\t    descriptions.");
	puts("\tXMove -r d:\\newdir -f c:\\mydir.bkp -m c:\\users.map");
	puts("\t -- restores the mydir directory creating d:\\newdir\\mydir with the new\n\t    credentials specified in the map file.");
	puts("");
	puts("Map file format:");
	puts("<originallogin> <newlogin1>, <newlogin2>, ...");
	puts("<originallogin>");
	puts("");
	puts("The first maps the original user to multiple target users.");
	puts("The second removes a user from the list of credentials for the target file.");
	puts("Note: Some mapping will result in conflicting permissions for a file or ");
	puts("directory. The -t option is supplied to ensure this possibility does not occur.");
	puts("");
}

