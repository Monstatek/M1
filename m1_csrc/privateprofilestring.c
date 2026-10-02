/*
 This file is part of OpenLogos/LogOSMaTrans.  Copyright (C) 2005 Globalware AG
  
 OpenLogos/LogOSMaTrans has two licensing options:
  
 The Commercial License, which allows you to provide commercial software
 licenses to your customers or distribute Logos MT based applications or to use
 LogOSMaTran for commercial purposes. This is for organizations who do not want
 to comply with the GNU General Public License (GPL) in releasing the source
 code for their applications as open source / free software.
  
 The Open Source License allows you to offer your software under an open source
 / free software license to all who wish to use, modify, and distribute it
 freely. The Open Source License allows you to use the software at no charge
 under the condition that if you use OpenLogos/LogOSMaTran in an application you
 redistribute, the complete source code for your application must be available
 and freely redistributable under reasonable conditions. GlobalWare AG bases its
 interpretation of the GPL on the Free Software Foundation's Frequently Asked
 Questions.
  
 OpenLogos is distributed in the hope that it will be useful, but WITHOUT ANY
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 PARTICULAR PURPOSE.  See the GNU General Public License for more details.
  
 You should have received a copy of the License conditions along with this
 program. If not, write to Globalware AG, Hospitalstra�e 6, D-99817 Eisenach.
  
 Linux port modifications and additions by Bernd Kiefer, Walter Kasper,
 Deutsches Forschungszentrum fuer kuenstliche Intelligenz (DFKI)
 Stuhlsatzenhausweg 3, D-66123 Saarbruecken
 */
 /* -*- Mode: C++ -*- */
  
 /***************************************************************************
  PORTABLE ROUTINES FOR WRITING PRIVATE PROFILE STRINGS --  by Joseph J. Graf
  Header file containing prototypes and compile-time configuration.
 ***************************************************************************/
  
/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "stm32h5xx_hal.h"
#include "main.h"

#include "lfrfid.h"
#include "m1_file_util.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "privateprofilestring.h"

/*************************** D E F I N E S ************************************/
 #define DEFUALT_LINE_LENGTH    (512)

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static size_t g_linebuf_size = DEFUALT_LINE_LENGTH;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
char* ltrim(char *str);
char* rtrim(char *str);
char* trim(char *str);

static int read_line(FIL *fp, char *buf, int size);

static int stricmp_nocase(const char *a, const char *b);
static int parse_hex_array_space(const char *str, uint8_t *out, int max_len);
static bool parse_bool_text(const char *str, bool *out);
static int parse_hex_count(const char *str);
static bool parse_value(const char *text, ParsedValue *val);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void set_line_buffer_size(size_t size)
{
	if(size)
		g_linebuf_size = size;
}


/*****************************************************************
* Function:     trim()
* Arguments:
*               <char *> str - a pointer to the copy buffer
* Returns:
******************************************************************/
char* ltrim(char *str)
{
    char *p = str;
    if (str == NULL) return NULL;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (p != str)
        memmove(str, p, strlen(p) + 1);

    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
char* rtrim(char *str)
{
    char *end;

    if (str == NULL) return NULL;

    end = str + strlen(str);

    while (end > str && isspace((unsigned char)*(end - 1)))
        end--;

    *end = '\0';
    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
char* trim(char *str)
{
    if (str == NULL) return NULL;

    rtrim(str);
    ltrim(str);

    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool IsValidFileSpec(const S_M1_file_info *f, const char* ext)
{
	uint8_t uret;
	const char *ext1;

	uret = strlen(f->file_name);
	if ( !uret )
		return false;

	ext1 = fu_get_file_extension(f->file_name);

	if ( ext1 == 0 || strcmp(ext1, ext) )
		return false;

	return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* version, const char *file_path)
{
	if(data == NULL || filetype == NULL || version == NULL || file_path == NULL){
		return false;
	}

	GetPrivateProfileString(data,"Filetype", file_path);
	if(strcmp(data->buf, filetype))
		return false;

	GetPrivateProfileString(data,"Version", file_path);
	//if(data->v.f != 0.8)
	if(strcmp(data->buf, version))
		return false;

	return true;
}


/*============================================================================*/
 /**
  * @brief Reads a single line from a file using FatFS f_gets (handles CR/LF/CRLF).
  *
  * This function reads one line of text from the specified FatFS file object.
  * It supports line termination by '\n', '\r', or a combination of both ("\r\n").
  * The resulting line is stored in the provided buffer without trailing
  * newline characters.
  *
  * @param fp    Pointer to the FatFS file object.
  * @param buf   Buffer to store the resulting line (null-terminated string).
  * @param size  Size of the buffer in bytes.
  *
  * @return
  * - 1 if a line was successfully read
  * - 0 if end-of-file was reached or no more lines are available
  */
/*============================================================================*/
 int read_line(FIL *fp, char *buf, int size)
 {
     char *p;

     if (size <= 0) return 0;

     p = f_gets(buf, size, fp);

     if (p == NULL) {
         return 0; // EOF or error
     }

     int len = strlen(buf);

     if (len > 0)
     {
         if (buf[len-1] == '\n') buf[--len] = '\0';
         if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
     }

     return 1;
 }


 /************************************************************************
 * Function:     get_private_profile_int()
 * Arguments:    <char *> section - the name of the section to search for
 *               <char *> entry - the name of the entry to find the value of
 *               <int> def - the default value in the event of a failed read
 *               <char *> file_name - the name of the .ini file to read from
 * Returns:      the value located at entry
 *************************************************************************/
#define TOKEN_COLON ':'

 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/

 static int stricmp_nocase(const char *a, const char *b)
 {
     unsigned char ca, cb;

     if (a == NULL || b == NULL) {
         return (a == b) ? 0 : (a ? 1 : -1);
     }

     while (*a && *b) {
         ca = (unsigned char)tolower((unsigned char)*a);
         cb = (unsigned char)tolower((unsigned char)*b);
         if (ca != cb) return (int)ca - (int)cb;
         a++;
         b++;
     }

     ca = (unsigned char)tolower((unsigned char)*a);
     cb = (unsigned char)tolower((unsigned char)*b);
     return (int)ca - (int)cb;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/

 static int parse_hex_array_space(const char *str, uint8_t *out, int max_len)
 {
     int count = 0;
     char token[16];

     if (str == NULL || out == NULL || max_len <= 0)
         return 0;

     while (*str && count < max_len) {

         while (*str == ' ')
             str++;

         if (*str == '\0')
             break;

         int t = 0;
         while (*str != ' ' && *str != '\0' && t < (int)sizeof(token) - 1) {
             token[t++] = *str++;
         }
         token[t] = '\0';

         unsigned int value;
         if (sscanf(token, "%x", &value) == 1) {
             out[count++] = (uint8_t)value;
         }
     }

     return count;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/

 static bool parse_bool_text(const char *str, bool *out)
 {
     if (str == NULL || out == NULL) return false;

     if (stricmp_nocase(str, "1") == 0 ||
         stricmp_nocase(str, "true") == 0 ||
         stricmp_nocase(str, "on") == 0) {
         *out = true;
         return true;
     }

     if (stricmp_nocase(str, "0") == 0 ||
         stricmp_nocase(str, "false") == 0 ||
         stricmp_nocase(str, "off") == 0) {
         *out = false;
         return true;
     }

     return false;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 static int parse_hex_count(const char *str)
 {
     int count = 0;

     char *tok = strtok((char*)str, " ");

     while (tok != NULL) {
    	 count++;
    	 tok = strtok(NULL, " ");
     }

     return count;
 }

 bool parse_value(const char *text, ParsedValue *val)
 {
     if (text == NULL || val == NULL)
         return false;

     switch (val->type)
     {
     case VALUE_TYPE_HEX_ARRAY:
         if (val->buf == NULL || val->max_len <= 0)
             return false;
         val->v.hex.out_len = parse_hex_array_space(
                                 text,
                                 (uint8_t*)val->buf,
                                 val->max_len);
         return (val->v.hex.out_len > 0);

     case VALUE_TYPE_INT:
     {
         int tmp;
         if (sscanf(text, "%d", &tmp) == 1) {
             val->v.i32 = tmp;
             return true;
         }
         return false;
     }

     case VALUE_TYPE_UINT32:
     {
         unsigned int tmp;
         if (sscanf(text, "%u", &tmp) == 1) {
             val->v.u32 = (uint32_t)tmp;
             return true;
         }
         return false;
     }

     case VALUE_TYPE_BOOL:
         return parse_bool_text(text, &val->v.b);
#if 1
     case VALUE_TYPE_FLOAT:
     {
    	 char *endptr;
         float f;
         errno = 0;

         f = strtod(text,&endptr);

         if (endptr == text) {

             return false;
         }

         if (errno == ERANGE) {

             return false;
         }

         if (*endptr != '\0') {
             //ex : "123.45abc")
             return false;
         }

         val->v.f = trunc(f * 1000.0) / 1000.0;
         return true;

     }
#endif
     case VALUE_TYPE_STRING:
         if (val->buf == NULL || val->max_len <= 0)
             return false;
         strncpy((char*)val->buf, text, val->max_len - 1);
         ((char*)val->buf)[val->max_len - 1] = '\0';
         return true;

     case VALUE_TYPE_COUNT:
    	 val->v.hex.out_len = parse_hex_count(text);
    	 return true;
     }

     return false;
 }


 /************************************************************************
 * Function:     get_private_profile()
 * Arguments:    <char *> section - the name of the section to search for
 *               <char *> entry - the name of the entry to find the value of
 *               <int> def - the default value in the event of a failed read
 *               <char *> file_name - the name of the .ini file to read from
 * Returns:      the value located at entry
 *************************************************************************/
 /* Purely-additive: counts real f_open() calls made by this module (one per
  * single-shot call below, one per profile_session_open() regardless of how
  * many keys are then read through it). Never read in any decision path. */
 static uint32_t s_profile_open_count = 0;

 uint32_t get_private_profile_open_count(void) { return s_profile_open_count; }
 void reset_private_profile_open_count(void) { s_profile_open_count = 0; }

 /* Core scan, operating on an ALREADY-OPEN file positioned wherever the
  * caller left it (get_private_profile() rewinds via f_open itself;
  * get_private_profile_session() rewinds explicitly first). Never opens or
  * closes fp -- that stays the caller's responsibility, which is exactly
  * what lets a session batch many lookups through one open/close pair.
  * Body is otherwise byte-for-byte the original get_private_profile() scan:
  * no parsing-semantics change, just relocated file lifetime. */
 static int get_private_profile_from_fp(FIL *fp, ParsedValue *val, const char *entry)
 {
	char *line_buff;
    char *ep;
    int len = strlen(entry);

#if 1
    line_buff = malloc(g_linebuf_size);
    if(line_buff == NULL)
    	return 0;
#endif
    /* Now that the section has been found, find the entry. */
    do
    {
	if( !read_line(fp,line_buff,g_linebuf_size))
        {
#if 1
    		safe_free((void*)&line_buff);
#endif
            return (0);
        }

        if (line_buff[0] == '#') {
          	continue;
        }


        char *colon_pos = strchr(line_buff, TOKEN_COLON);

        if (colon_pos != NULL) {

        	*colon_pos = '\0';

            char *current_key = trim(line_buff);

            if (strlen(current_key) == len && !strcmp(current_key, entry)) {

            	*colon_pos = TOKEN_COLON;
                break;
            }

            *colon_pos = TOKEN_COLON;
        }
    }while( 1 );

    ep = strrchr(line_buff,TOKEN_COLON);

    if (ep == NULL) {
#if 1
    	safe_free((void*)&line_buff);
#endif
    	return 0;
    }

    ep++;
    if( !strlen(ep) )
    {
#if 1
    	safe_free((void*)&line_buff);
#endif
        return(0);
    }
    /* Copy only numbers fail on characters */

	char *psz = trim((char*)ep);
	strcpy(line_buff,psz);
	//safe_debugprintf("search value =%s\r\n", buff);

	parse_value(line_buff, val);

#if 1
    safe_free((void*)&line_buff);
#endif

	return 1;
 }

 int get_private_profile(ParsedValue *val, const char *entry, const char *file_name)
 {
	FIL fp;
	int result;

    if( f_open(&fp, file_name,FA_READ) != FR_OK )
		return(0);
    s_profile_open_count++;

    result = get_private_profile_from_fp(&fp, val, entry);

    f_close(&fp);                /* Clean up and return the value */

	return result;
 }

 bool profile_session_open(ProfileSession *sess, const char *file_name)
 {
	if (sess == NULL)
		return false;

	sess->is_open = (f_open(&sess->fp, file_name, FA_READ) == FR_OK);
	if (sess->is_open)
		s_profile_open_count++;

	return sess->is_open;
 }

 void profile_session_close(ProfileSession *sess)
 {
	if (sess != NULL && sess->is_open)
	{
		f_close(&sess->fp);
		sess->is_open = false;
	}
 }

 /* Same scan as get_private_profile(), against an already-open session --
  * rewinds first so each lookup still scans from the top (identical match
  * semantics; only the repeated open/close is eliminated). */
 static int get_private_profile_session_core(ParsedValue *val, const char *entry, ProfileSession *sess)
 {
	if (sess == NULL || !sess->is_open)
		return 0;

	f_lseek(&sess->fp, 0);

	return get_private_profile_from_fp(&sess->fp, val, entry);
 }

 int get_private_profile_session_string(ParsedValue *val, const char *entry, ProfileSession *sess)
 {
	val->type = VALUE_TYPE_STRING;
	return get_private_profile_session_core(val, entry, sess);
 }

 int get_private_profile_session_hex(ParsedValue *val, const char *entry, ProfileSession *sess)
 {
	val->type = VALUE_TYPE_HEX_ARRAY;
	return get_private_profile_session_core(val, entry, sess);
 }

 int get_private_profile_session_uint(ParsedValue *val, const char *entry, ProfileSession *sess)
 {
	val->type = VALUE_TYPE_UINT32;
	return get_private_profile_session_core(val, entry, sess);
 }

 bool isValidHeaderFieldSession(ParsedValue *data, const char* filetype, const char* version, ProfileSession *sess)
 {
	if(data == NULL || filetype == NULL || version == NULL || sess == NULL || !sess->is_open){
		return false;
	}

	get_private_profile_session_string(data, "Filetype", sess);
	if(strcmp(data->buf, filetype))
		return false;

	get_private_profile_session_string(data, "Version", sess);
	if(strcmp(data->buf, version))
		return false;

	return true;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_hex_count(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_COUNT;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_hex(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_HEX_ARRAY;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_uint(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_UINT32;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_int(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_INT;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_string(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_STRING;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_bool(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_BOOL;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_float(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_FLOAT;
	return get_private_profile(val, entry, file_name);
 }

  
 /***** Routine for writing private profile strings --- by Joseph J. Graf *****/

 /* Closes the temp file (checking sync+close), closes the read file, and
  * only THEN performs the destructive original-replace (unlink+rename).
  * Both exit points of write_private_profile_string() below funnel
  * through this single checked sequence so they cannot drift apart.
  *
  * FatFs has no atomic rename-over-existing-file primitive (f_rename()
  * fails with FR_EXIST if the destination already exists), so the
  * unlink-then-rename two-step is an inherent filesystem limitation, not
  * something this fix can architect around without a much larger change
  * (e.g. a lock/journal file) that is out of scope here. What IS fixed:
  * the original is only ever unlinked AFTER the replacement has been
  * proven fully written (sync succeeded) and closed -- never before, and
  * never unconditionally. If f_unlink() itself fails, the original is
  * still completely intact (this function never reached the point of
  * touching it) and failure is reported truthfully. If f_rename() fails
  * AFTER a successful unlink, the original is genuinely gone; this is
  * reported as failure (never silently reported as success) rather than
  * papered over, since there is no further recovery FatFs itself offers
  * here -- it is the one residual window this filesystem's own API
  * cannot close, and callers must be able to tell it happened. */
 static int pps_commit_replace(FIL *wfp, FIL *rfp, const char *tmp_name, const char *file_name)
 {
     FRESULT close_rf;

     if (f_sync(wfp) != FR_OK)  { f_close(wfp); f_close(rfp); f_unlink(tmp_name); return 0; }
     if (f_close(wfp) != FR_OK) { f_close(rfp); f_unlink(tmp_name); return 0; }
     close_rf = f_close(rfp);
     (void)close_rf;   /* rfp is read-only; a close failure here doesn't invalidate the write */

     if (f_unlink(file_name) != FR_OK) {
         /* Original still fully intact -- clean up only our own temp file. */
         f_unlink(tmp_name);
         return 0;
     }
     if (f_rename(tmp_name, file_name) != FR_OK) {
         /* Residual FatFs-limitation window: original already unlinked and
          * rename failed. Report failure truthfully; do not claim success. */
         return 0;
     }
     return 1;
 }

 /*************************************************************************
  * Function:    write_private_profile_string()
  * Arguments:   <char *> section - the name of the section to search for
  *              <char *> entry - the name of the entry to find the value of
  *              <char *> buffer - pointer to the buffer that holds the string
  *              <char *> file_name - the name of the .ini file to read from
  * Returns:     TRUE if successful, otherwise FALSE
  *************************************************************************/
 int write_private_profile_string(const char *entry, const char *buffer, const char *file_name)
{
	FIL rfp, wfp;
    /* Temp file lives in the TARGET's own directory (not a single fixed
     * relative name shared by every RFID file on the card) and is named
     * from the target's own filename, so it is uniquely associated with
     * this specific operation rather than colliding with any other
     * saved-RFID file's own in-progress write. */
    char dir[128];
    char tmp_name[160];
    //char buff[MAX_LINE_LENGTH];
    char *line_buff;
    int len = strlen(entry);

    fu_get_directory_path(file_name, dir, sizeof(dir));
    {
        char tmp_basename[64];
        const char *base = fu_get_filename(file_name);
        snprintf(tmp_basename, sizeof(tmp_basename), ".%s.tmp", (base != NULL) ? base : "rfidsave");
        fu_path_combine(tmp_name, sizeof(tmp_name), dir, tmp_basename);
    }

	 if ((f_open(&rfp, file_name, FA_READ)) != FR_OK)
    {
		if ((f_open(&wfp, file_name, FA_CREATE_NEW|FA_WRITE)) != FR_OK)
        {   return(0);   }

		f_printf(&wfp,"%s: %s\n",entry,buffer);
		if (f_sync(&wfp) != FR_OK)  { f_close(&wfp); return(0); }
		if (f_close(&wfp) != FR_OK) { return(0); }
		return(1);
    }

	if ((f_open(&wfp, tmp_name, FA_CREATE_ALWAYS|FA_WRITE)) != FR_OK)
    {
		f_close(&rfp);
		return(0);
	}
     /* Move through the file one line at a time until a section is
      * matched or until EOF. Copy to temp file as it is read. */
	// serach key - write value
     /* Now that the section has been found, find the entry. Stop searching
      * upon leaving the section's area. Copy the file as it is read
      * and create an entry if one is not found.  */
#if 1
    line_buff = malloc(g_linebuf_size);
    if(line_buff == NULL)
    	return 0;
#endif

     while( 1 )
     {
    	if( !read_line(&rfp,line_buff,g_linebuf_size) )
        {   /* EOF without an entry so make one */
            int ok;
            f_printf(&wfp,"%s: %s\n",entry,buffer);
#if 1
            safe_free((void*)&line_buff);
#endif
            /* Checked sync+close+unlink+rename -- see pps_commit_replace()'s
             * own header comment. Never reports success after a failed
             * filesystem operation. */
            ok = pps_commit_replace(&wfp, &rfp, tmp_name, file_name);
            return(ok);
        }
  
     	if (line_buff[0] == '#') {
     		f_printf(&wfp, "%s\n", line_buff);
            continue;
        }

     	char *colon_pos = strchr(line_buff, TOKEN_COLON);

     	if (colon_pos != NULL) {

     	    *colon_pos = '\0';

     	    char *current_key = trim(line_buff);

     	    if (strlen(current_key) == len && !strcmp(current_key, entry)) {

     	    	*colon_pos = TOKEN_COLON;
     	        break;
     	    }

     	    *colon_pos = TOKEN_COLON;
     	}

     	if (line_buff[0] == '\0') {
     		break;
     	}

     	f_printf(&wfp,"%s\n",line_buff);
    }
  
    if( line_buff[0] == '\0' )
    {   f_printf(&wfp,"%s: %s\n",entry,buffer);
        do
        {
            f_printf(&wfp,"%s\n",line_buff);
        } while( read_line(&rfp,line_buff,g_linebuf_size) );
    }
    else
    {   f_printf(&wfp,"%s: %s\n",entry,buffer);
        while( read_line(&rfp,line_buff,g_linebuf_size) )
        {
            f_printf(&wfp,"%s\n",line_buff);
        }
    }

#if 1
    safe_free((void*)&line_buff);
#endif
    /* Checked sync+close+unlink+rename -- see pps_commit_replace()'s own
     * header comment. Never reports success after a failed filesystem
     * operation. */
    return pps_commit_replace(&wfp, &rfp, tmp_name, file_name);
 }
  
 #undef MAX_LINE_LENGTH

