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
#ifndef _PRIVATEPROFILE_H_ 
#define _PRIVATEPROFILE_H_ 

 /* ================================
  *  ValueType / ParsedValue
  * ================================ */

 typedef enum {
     VALUE_TYPE_HEX_ARRAY,
     VALUE_TYPE_INT,
     VALUE_TYPE_UINT32,
     VALUE_TYPE_BOOL,
     VALUE_TYPE_FLOAT,
     VALUE_TYPE_STRING,
	 VALUE_TYPE_COUNT,
 } ValueType;

 typedef struct {
     ValueType type;

     void *buf;
     int   max_len;

     union {
         struct {
             int out_len;
         } hex;

         int         i32;
         uint32_t    u32;
         bool        b;
         float       f;
     } v;

 } ParsedValue;

 void set_line_buffer_size(size_t size);

 bool IsValidFileSpec(const S_M1_file_info *f, const char* ext);
 bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* version, const char *file_path);

 int get_private_profile_hex_count(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_uint(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_int(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_string(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_bool(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_hex(ParsedValue *val, const char *entry, const char *file_name);
 int get_private_profile_float(ParsedValue *val, const char *entry, const char *file_name);
 int write_private_profile_string(const char *entry, const char *buffer, const char *file_name);

#define GetPrivateProfileHexCount	get_private_profile_hex_count
#define GetPrivateProfileHex 		get_private_profile_hex
#define GetPrivateProfileUint 		get_private_profile_uint
#define GetPrivateProfileInt 		get_private_profile_int
#define GetPrivateProfileString 	get_private_profile_string
#define WritePrivateProfileString 	write_private_profile_string

 /* ================================
  *  Batched (session) reads
  * ================================
  * A caller that needs several keys out of the SAME file (e.g. a header
  * with a dozen optional fields) can open it once, look up every key
  * against that one open handle (each lookup still does its own linear
  * scan from the top -- same semantics as get_private_profile(), just
  * without a repeated f_open/f_close per key), then close once. This
  * turns N independent open+scan+close cycles into 1 open + N scans + 1
  * close. Every existing single-shot GetPrivateProfileXxx() call is
  * unaffected -- this is purely additive. */
 typedef struct {
     FIL  fp;
     bool is_open;
 } ProfileSession;

 bool profile_session_open(ProfileSession *sess, const char *file_name);
 void profile_session_close(ProfileSession *sess);

 bool isValidHeaderFieldSession(ParsedValue *data, const char* filetype, const char* version, ProfileSession *sess);
 int get_private_profile_session_string(ParsedValue *val, const char *entry, ProfileSession *sess);
 int get_private_profile_session_hex(ParsedValue *val, const char *entry, ProfileSession *sess);
 int get_private_profile_session_uint(ParsedValue *val, const char *entry, ProfileSession *sess);

#define GetPrivateProfileStringS	get_private_profile_session_string
#define GetPrivateProfileHexS		get_private_profile_session_hex
#define GetPrivateProfileUintS		get_private_profile_session_uint

 /* Purely-additive counter: real f_open() calls made by this module (one
  * per single-shot GetPrivateProfileXxx() call, one per profile_session_open()
  * regardless of how many keys are then read through it). Surfaced via
  * "m1_mtest 5" alongside the file-browser perf counters, so a hardware
  * tester can directly see the reduction a session conversion produces. */
 uint32_t get_private_profile_open_count(void);
 void reset_private_profile_open_count(void);
#endif
  
