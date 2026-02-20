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
 int get_private_profile(ParsedValue *val, const char *entry, const char *file_name)
 {
	FIL fp;
    //char buff[MAX_LINE_LENGTH];
	char *line_buff;
    char *ep;
    //char value[6];
    int len = strlen(entry);
    //int i;

    if( f_open(&fp, file_name,FA_READ) != FR_OK )
		return(0);
#if 1
    line_buff = malloc(g_linebuf_size);
    if(line_buff == NULL)
    	return 0;
#endif
    /* Now that the section has been found, find the entry. */
    do
    {
    	if( !read_line(&fp,line_buff,g_linebuf_size))
        {
#if 1
    		safe_free((void*)&line_buff);
#endif
      		f_close(&fp);
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
    	f_close(&fp);
    	return 0;
    }

    ep++;
    if( !strlen(ep) )
    {
#if 1
    	safe_free((void*)&line_buff);
#endif
      	f_close(&fp);
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
    f_close(&fp);                /* Clean up and return the value */

	return 1;
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
    char tmp_name[] = "temp.rfid";
    //char buff[MAX_LINE_LENGTH];
    char *line_buff;
    int len = strlen(entry);
	
	 if ((f_open(&rfp, file_name, FA_READ)) != FR_OK)	
    {  
		if ((f_open(&wfp, file_name, FA_CREATE_NEW|FA_WRITE)) != FR_OK)
        {   return(0);   }
		
		f_printf(&wfp,"%s: %s\n",entry,buffer);
		f_close(&wfp);
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
            f_printf(&wfp,"%s: %s\n",entry,buffer);
            /* Clean up and rename */
#if 1
            safe_free((void*)&line_buff);
#endif
            f_close(&rfp);
            f_close(&wfp);
            f_unlink(file_name);
            f_rename(tmp_name,file_name);
            return(1);
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

    /* Clean up and rename */
#if 1
    safe_free((void*)&line_buff);
#endif
    f_close(&wfp);
    f_close(&rfp);
    f_unlink(file_name);
    f_rename(tmp_name,file_name);
    return(1);
 }
  
 #undef MAX_LINE_LENGTH

