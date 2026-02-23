/* See COPYING.txt for license details. */

/*
 * Res_String.c
 */

#include "Res_String.h"

const char *szStringTable[]={
    "Emulate",
    "Write",
    "Edit",
    "Rename",
	"Delete",
	"Info",
	"Save",
	"Reading",
	"Writing",
	"Emulating",
	"Retry",
	"More",
	"Unsaved",
	"Cancel",
	"Name: ",
	"Enter hex data:",
	"Enter filename:",
	"Hold card next to\nM1's back",
	"Unsupported file\nformat",
	"The file has been\n successfully deleted\n from the system",
	"Duplicate file name.\nEnter a new name",
	"< Back",
	"REBOOT",
	"Reboot",
	"POWER OFF",
	"Power OFF",
	"Success",
	"Error",

	"Consumption",
	"Pre-charge",
	"Charging",
	"Complete",
	"Input Fault",
	"Thermal Shutdown",
	"Safety Time Exp.",
	"L",
	"T",
	"V",
	"H",
	"",
};

const char * res_string(int str_id)
{
	//char **pRES_StringTable = (char**)RES_LanguageTable[gstSystem.Language];
	if(str_id < IDS_USER_END)
	{
		return szStringTable[str_id];
	}
	else
	{
		return szStringTable[IDS_USER_DEFINED];
	}
}
