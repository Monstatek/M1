/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_dict_resume.c
 * @brief   See mfc_dict_resume.h.
 */
/*============================================================================*/
#include "mfc_dict_resume.h"
#include <string.h>

void mfc_dict_resume_reset(mfc_dict_resume_t *r)
{
    if (r == NULL) return;
    memset(r, 0, sizeof(*r));
}

void mfc_dict_resume_reset_system_only(mfc_dict_resume_t *r)
{
    if (r == NULL) return;
    r->sys_valid      = false;
    r->sys_path[0]    = '\0';
    r->sys_file_size  = 0U;
    r->sys_file_date  = 0U;
    r->sys_file_time  = 0U;
    r->sys_byte_offset = 0U;
    /* seen[]/seen_n/seen_overflowed deliberately untouched -- built-in/User
     * dedup and every already-recovered key/block survive a System-only
     * reset intact. */
}

bool mfc_dict_resume_system_identity_matches(const mfc_dict_resume_t *r,
                                              const char *path, uint32_t size,
                                              uint16_t fdate, uint16_t ftime)
{
    if ((r == NULL) || (path == NULL)) return false;
    if (!r->sys_valid) return false;
    if (strncmp(r->sys_path, path, sizeof(r->sys_path)) != 0) return false;
    /* A path that doesn't fit in sys_path (never NUL-terminated within it)
     * cannot be the same string that produced sys_path -- strncmp above
     * already returns non-zero for that case via the embedded NUL
     * difference, so no separate length check is needed. */
    if (r->sys_file_size != size) return false;
    if (r->sys_file_date != fdate) return false;
    if (r->sys_file_time != ftime) return false;
    return true;
}
