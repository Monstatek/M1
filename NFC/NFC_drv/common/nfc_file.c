/* See COPYING.txt for license details. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_sdcard_man.h"
#include "m1_sdcard_provision.h"
#include "m1_file_browser.h"
#include "m1_file_util.h"
#include "m1_virtual_kb.h"
#include "m1_save_filename.h"
#include "m1_storage.h"
#include "nfc_file.h"
#include "nfc_storage.h"
#include "nfc_ctx.h"
#include "uiView.h"
#include "privateprofilestring.h"
#include "m1_nfc.h"
#include "logger.h"
#include "res_string.h"

#define DRIVE0_NFC     M1_SD_DIR_NFC
#define NFC_FILE_EXTENSION_TMP		"nfc"
#define NFC_FILE_PREFIX			"nfc_"
#define NFC_FILE_EXTENSION		".nfc"

// External dump buffers (defined in m1_nfc.c)
extern uint8_t g_nfc_dump_buf[];
extern uint8_t g_nfc_valid_bits[];

/* One-shot provenance flag: set by nfc_file_mark_next_save_manual(), consumed by
 * the next nfc_profile_save() to emit a "# Created: manual" comment. */
static bool s_next_save_manual = false;
void nfc_file_mark_next_save_manual(void) { s_next_save_manual = true; }

/*============================================================================*/
/**
 * @brief Load NFC profile from file
 * 
 * Validates file extension and loads NFC card data from file.
 * Uses nfc_storage_load_file() internally to parse the file.
 * 
 * @param f File info structure from storage_browse()
 * @param ext File extension to validate (e.g., "nfc")
 * @return true on success, false on failure
 */
/*============================================================================*/
bool nfc_profile_load(const S_M1_file_info *f, const char* ext)
{
	char file_path[128];
	nfc_storage_result_t nfc_ret;
	//BaseType_t ret;

	if(IsValidFileSpec(f, ext))
	{
		fu_path_combine(file_path, sizeof(file_path), f->dir_name, f->file_name);

		// Load file using nfc_storage_load_file
		nfc_ret = nfc_storage_load_file(file_path, g_nfc_dump_buf, sizeof(g_nfc_dump_buf),
										g_nfc_valid_bits, sizeof(g_nfc_valid_bits));

		if (nfc_ret == NFC_STORAGE_OK)
		{
			// Save file path to context
			nfc_run_ctx_t* c = nfc_ctx_get();
			if (c) {
				strncpy(c->file.path, file_path, sizeof(c->file.path) - 1);
				c->file.path[sizeof(c->file.path) - 1] = '\0';
			}
			return true;
		}
		else
		{
			platformLog("nfc_storage_load_file('%s') failed: %d\r\n", file_path, nfc_ret);
		}
	}

	return false;
}

/*============================================================================*/
/**
 * @brief Save NFC profile to file
 * 
 * Saves NFC card context data to file in M1 NFC device format.
 * 
 * @param fp Full file path to save to
 * @param ctx NFC context containing card data
 * @return true on success, false on failure
 */
/*============================================================================*/
bool nfc_profile_save(const char *fp, PCNFC_RUN_CTX ctx)
{
	FIL nfc_file;
	char line[128];
	uint8_t ret;

	if (!fp || !ctx) {
		return false;
	}

	ret = m1_fb_open_new_file(&nfc_file, fp);
	if (ret) {
		platformLog("nfc_profile_save: Error creating file '%s'\r\n", fp);
		return false;
	}

	// Write header
	strcpy(line, "Filetype: M1 NFC device\r\n");
	m1_fb_write_to_file(&nfc_file, line, strlen(line));
	strcpy(line, "Version: 4\r\n");
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// Device type
	const char* devtype = "NFC";
	switch (ctx->head.tech) {
	case NFC_TX_A:
		if (ctx->head.a.ats_len > 0) devtype = "ISO14443-4A";
		else if (ctx->head.family == M1NFC_FAM_CLASSIC) devtype = "Classic";
		else if (ctx->head.family == M1NFC_FAM_ULTRALIGHT) devtype = "Ultralight/NTAG";
		else if (ctx->head.family == M1NFC_FAM_DESFIRE) devtype = "DESFire";
		else devtype = "ISO14443A";
		break;
	case NFC_TX_B:  devtype = "ISO14443B";   break;
	case NFC_TX_F:  devtype = "Felica";      break;
	case NFC_TX_V:  devtype = "ISO15693";    break;
	default:        devtype = "NFC";         break;
	}
	snprintf(line, sizeof(line), "Device type: %s\r\n", devtype);
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// Format UID with spaces (like RFID does) for proper parsing
	char uid_str[32];
	int pos = 0;
	for (uint8_t i = 0; i < ctx->head.uid_len && pos < (int)sizeof(uid_str) - 3; i++) {
		pos += snprintf(uid_str + pos, sizeof(uid_str) - pos,
						(i + 1 < ctx->head.uid_len) ? "%02X " : "%02X",
						ctx->head.uid[i]);
	}
	snprintf(line, sizeof(line), "UID: %s\r\n", uid_str);
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// ATQA and SAK (for Tech A)
	if (ctx->head.tech == NFC_TX_A) {
		snprintf(line, sizeof(line), "ATQA: %02X %02X\r\n",
				ctx->head.a.atqa[0], ctx->head.a.atqa[1]);
		m1_fb_write_to_file(&nfc_file, line, strlen(line));

		snprintf(line, sizeof(line), "SAK: %02X\r\n", ctx->head.a.sak);
		m1_fb_write_to_file(&nfc_file, line, strlen(line));
	}

	// Provenance: mark manually-created cards (comment; loaders skip '#' lines).
	if (s_next_save_manual) {
		strcpy(line, "# Created: manual\r\n");
		m1_fb_write_to_file(&nfc_file, line, strlen(line));
	}

	// Save MIFARE Classic block dump if available (unit_size 16).
	if ((ctx->head.tech == NFC_TX_A) && (ctx->head.family == M1NFC_FAM_CLASSIC) &&
		ctx->dump.has_dump && ctx->dump.data != NULL &&
		ctx->dump.unit_size == 16 && ctx->dump.unit_count > 0)
	{
		uint32_t blk_cnt   = ctx->dump.unit_count;
		const uint8_t *dump  = ctx->dump.data;
		const uint8_t *valid = ctx->dump.valid_bits;

		for (uint32_t i = 0; i < blk_cnt; i++)
		{
			if (valid != NULL) {
				if ((valid[i >> 3] & (1u << (i & 0x07))) == 0) continue;   // skip unread block
			}
			const uint8_t *b = &dump[i * 16u];
			snprintf(line, sizeof(line),
				"Block %03lu: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
				(unsigned long)i,
				b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
				b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}

		/* Save recovered sector keys, one line per key actually found. A key
		 * never read back over RF (Key A in particular -- the card never
		 * returns it) simply has no line, exactly like an unread block above.
		 * The loader must never treat a missing line as a default/zero key. */
		const nfc_mfc_info_t *mfc = &ctx->mfc;
		uint8_t mfc_sectors = (mfc->valid) ? mfc->sectors_total : 0U;
		for (uint8_t s = 0; s < mfc_sectors && s < M1NFC_MFC_SECTORS_MAX; s++)
		{
			const nfc_mfc_sector_t *sc = &mfc->sec[s];
			if (sc->key_a_found) {
				snprintf(line, sizeof(line),
					"Sector %02u KeyA: %02X %02X %02X %02X %02X %02X\r\n", (unsigned)s,
					sc->key_a[0], sc->key_a[1], sc->key_a[2],
					sc->key_a[3], sc->key_a[4], sc->key_a[5]);
				m1_fb_write_to_file(&nfc_file, line, strlen(line));
			}
			if (sc->key_b_found) {
				snprintf(line, sizeof(line),
					"Sector %02u KeyB: %02X %02X %02X %02X %02X %02X\r\n", (unsigned)s,
					sc->key_b[0], sc->key_b[1], sc->key_b[2],
					sc->key_b[3], sc->key_b[4], sc->key_b[5]);
				m1_fb_write_to_file(&nfc_file, line, strlen(line));
			}
		}
	}

	// Save Ultralight (NTAG) page dumps if available
	if ((ctx->head.tech == NFC_TX_A) && (ctx->head.family == M1NFC_FAM_ULTRALIGHT) &&
		ctx->dump.has_dump && ctx->dump.data != NULL &&
		ctx->dump.unit_size == 4 && ctx->dump.unit_count > 0)
	{
		uint32_t page_cnt  = ctx->dump.unit_count;
		uint32_t page_base = ctx->dump.origin;
		const uint8_t *dump = ctx->dump.data;
		const uint8_t *valid = ctx->dump.valid_bits;

		/* Explicit identity/geometry, persisted so reload never has to
		 * guess the variant back from how many "Page N:" lines happen to
		 * follow (see nfc_storage.c's t2t_resolve_and_validate_geometry()
		 * for why that guess is unsafe). Variant/version are only ever
		 * written when genuinely known/captured -- an unidentified tag or
		 * one that never answered GET_VERSION simply has no line, never a
		 * fabricated name or tuple. */
		uint8_t variant = nfc_ctx_get_t2t_variant();
		const char *variant_name = nfc_t2t_variant_name(variant);
		if (variant_name != NULL) {
			snprintf(line, sizeof(line), "T2T Variant: %s\r\n", variant_name);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}

		uint8_t ver[8];
		uint8_t ver_len = nfc_ctx_get_t2t_version(ver);
		if (ver_len == 8U) {
			snprintf(line, sizeof(line), "T2T Version: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
					ver[0], ver[1], ver[2], ver[3], ver[4], ver[5], ver[6], ver[7]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}

		/* DECLARED/EXPECTED total, not "however many pages happen to
		 * follow" -- nfc_ctx_get_t2t_expected_pages() is the full geometry
		 * the identified variant should have (set at live-read
		 * identification time, independent of how far the actual capture
		 * got). Falls back to the raw dump size only if a variant was
		 * never established at all (expected_pages == 0), so this line is
		 * never omitted outright for an old-style unidentified dump. */
		uint16_t expected_pages = nfc_ctx_get_t2t_expected_pages();
		snprintf(line, sizeof(line), "Pages: %lu\r\n",
				(unsigned long)((expected_pages > 0U) ? expected_pages : page_cnt));
		m1_fb_write_to_file(&nfc_file, line, strlen(line));

		/* Genuine originality signature / counters / tearing flags --
		 * never fabricated: each is written only if actually captured. */
		uint8_t sig[32];
		if (nfc_ctx_t2t_signature_valid() && nfc_ctx_get_t2t_signature(sig)) {
			snprintf(line, sizeof(line),
					"Signature: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
					"%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
					sig[0],  sig[1],  sig[2],  sig[3],  sig[4],  sig[5],  sig[6],  sig[7],
					sig[8],  sig[9],  sig[10], sig[11], sig[12], sig[13], sig[14], sig[15],
					sig[16], sig[17], sig[18], sig[19], sig[20], sig[21], sig[22], sig[23],
					sig[24], sig[25], sig[26], sig[27], sig[28], sig[29], sig[30], sig[31]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}

		for (uint8_t ci = 0; ci < 3U; ci++) {
			uint8_t cnt[3];
			if (nfc_ctx_t2t_counter_valid(ci) && nfc_ctx_get_t2t_counter(ci, cnt)) {
				snprintf(line, sizeof(line), "Counter%u: %02X %02X %02X\r\n",
						(unsigned)ci, cnt[0], cnt[1], cnt[2]);
				m1_fb_write_to_file(&nfc_file, line, strlen(line));
			}

			uint8_t tear = 0;
			if (nfc_ctx_t2t_tearing_valid(ci) && nfc_ctx_get_t2t_tearing(ci, &tear)) {
				snprintf(line, sizeof(line), "Tearing%u: %02X\r\n", (unsigned)ci, tear);
				m1_fb_write_to_file(&nfc_file, line, strlen(line));
			}
		}

		/* Genuine PWD_AUTH credential (Unlock feature) -- ONLY ever a
		 * password/PACK a real tag actually accepted, never the masked
		 * PWD/PACK page bytes and never a user-entered candidate that was
		 * never tried against the physical tag. Written as dedicated keys
		 * (not folded into an ordinary Page N: line) precisely so the
		 * parser can carry an explicit "verified" flag distinct from
		 * "some bytes happen to be present" -- see nfc_ctx_t2t_credential_
		 * valid(). */
		uint8_t pwd[4], pack[2];
		if (nfc_ctx_t2t_credential_valid() && nfc_ctx_get_t2t_pwd(pwd) && nfc_ctx_get_t2t_pack(pack)) {
			snprintf(line, sizeof(line), "PWD: %02X %02X %02X %02X\r\n", pwd[0], pwd[1], pwd[2], pwd[3]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
			snprintf(line, sizeof(line), "PACK: %02X %02X\r\n", pack[0], pack[1]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}

		for (uint32_t i = 0; i < page_cnt; i++)
		{
			uint8_t is_valid = 1;
			if (valid != NULL)
			{
				uint32_t byte_idx = (i >> 3);        // i / 8
				uint8_t  mask     = (1u << (i & 0x07)); // i % 8
				if ((valid[byte_idx] & mask) == 0)
				{
					is_valid = 0;
				}
			}

			if (!is_valid)
			{
				continue;
			}

			const uint8_t *page = &dump[i * ctx->dump.unit_size];
			uint32_t page_no = page_base + i;

			snprintf(line, sizeof(line),
					"Page %03lu: %02X %02X %02X %02X\r\n",
					(unsigned long)page_no,
					page[0], page[1], page[2], page[3]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}
	}

	// Save MIFARE DESFire identity (Tier 1) and deeper-read data if available.
	if ((ctx->head.tech == NFC_TX_A) && (ctx->head.family == M1NFC_FAM_DESFIRE) &&
		ctx->desfire.present)
	{
		char desf_line[300];

		{
			/* +1 slack, not a tight "- 3" capacity guard: a guard sized to
			 * exactly the worst-case remaining write is off by one at the
			 * final byte when the count divides evenly (proven by the
			 * DESFire persistence test) -- every byte instead always emits
			 * a trailing space, which parse_hex_bytes()/GetPrivateProfileHex()
			 * already skip as whitespace on load. */
			char v_str[3 * 28 + 1];
			int  pos = 0;
			for (uint8_t i = 0; i < 28U; i++) {
				pos += snprintf(v_str + pos, sizeof(v_str) - (size_t)pos, "%02X ", ctx->desfire.v[i]);
			}
			snprintf(desf_line, sizeof(desf_line), "DESFire Version: %s\r\n", v_str);
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
		}

		const nfc_desfire_deep_info_t *deep = &ctx->desfire_deep;

		if (deep->free_memory_valid) {
			snprintf(desf_line, sizeof(desf_line), "DESFire Free Memory: %lu\r\n",
					(unsigned long)deep->free_memory_bytes);
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
		}

		if (deep->master_key_settings_valid) {
			uint8_t settings_byte = (uint8_t)((deep->master_key_settings.change_key_id << 4) |
									(deep->master_key_settings.config_changeable   ? 0x08U : 0U) |
									(deep->master_key_settings.free_create_delete  ? 0x04U : 0U) |
									(deep->master_key_settings.free_directory_list ? 0x02U : 0U) |
									(deep->master_key_settings.key_changeable      ? 0x01U : 0U));
			uint8_t keycount_byte = (uint8_t)((deep->master_key_settings.flags << 4) | deep->master_key_settings.max_keys);
			snprintf(desf_line, sizeof(desf_line), "DESFire Master Key Settings: %02X %02X\r\n",
					settings_byte, keycount_byte);
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
		}

		if (deep->apps_truncated) {
			strcpy(desf_line, "DESFire Apps Truncated: 1\r\n");
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
		}
		if (deep->apps_protected) {
			strcpy(desf_line, "DESFire Apps Protected: 1\r\n");
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
		}

		for (uint8_t a = 0; a < deep->app_count && a < MF_DESFIRE_DEEP_MAX_APPS; a++) {
			const mf_desfire_app_t *app = &deep->apps[a];
			char aid_str[7];
			snprintf(aid_str, sizeof(aid_str), "%02X%02X%02X", app->id.id[0], app->id.id[1], app->id.id[2]);

			snprintf(desf_line, sizeof(desf_line), "DESFire Application %s: %d\r\n",
					aid_str, app->select_ok ? 1 : 0);
			m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));

			if (app->key_settings_valid) {
				uint8_t settings_byte = (uint8_t)((app->key_settings.change_key_id << 4) |
										(app->key_settings.config_changeable   ? 0x08U : 0U) |
										(app->key_settings.free_create_delete  ? 0x04U : 0U) |
										(app->key_settings.free_directory_list ? 0x02U : 0U) |
										(app->key_settings.key_changeable      ? 0x01U : 0U));
				uint8_t keycount_byte = (uint8_t)((app->key_settings.flags << 4) | app->key_settings.max_keys);
				snprintf(desf_line, sizeof(desf_line), "DESFire Application %s Key Settings: %02X %02X\r\n",
						aid_str, settings_byte, keycount_byte);
				m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
			}

			if (app->key_versions_truncated) {
				snprintf(desf_line, sizeof(desf_line), "DESFire Application %s Key Versions Truncated: 1\r\n", aid_str);
				m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
			}

			for (uint8_t k = 0; k < app->key_version_count && k < MF_DESFIRE_MAX_KEYS; k++) {
				snprintf(desf_line, sizeof(desf_line), "DESFire Application %s Key Version %02u: %02X\r\n",
						aid_str, (unsigned)k, app->key_versions[k]);
				m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
			}

			if (app->files_truncated) {
				snprintf(desf_line, sizeof(desf_line), "DESFire Application %s Files Truncated: 1\r\n", aid_str);
				m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
			}

			for (uint8_t f = 0; f < app->file_count && f < MF_DESFIRE_MAX_FILES_PER_APP; f++) {
				const mf_desfire_file_t *file = &app->files[f];
				const mf_desfire_file_settings_t *s = &file->settings;

				snprintf(desf_line, sizeof(desf_line),
						"DESFire Application %s File %02X: %d %d %d %04X %06lX %08lX %08lX %08lX %d %06lX %06lX %06lX %02X %02X %d %u\r\n",
						aid_str, file->id,
						file->settings_valid ? 1 : 0,
						(int)s->type, (int)s->comm, (unsigned)s->access_rights,
						(unsigned long)s->size,
						/* Cast through uint32_t first: value_*_limit are signed
						 * (a negative DESFire value limit is valid), and on a
						 * host build where "unsigned long" is 64-bit, casting
						 * a negative int32_t straight to unsigned long would
						 * sign-extend to a 16-hex-digit value instead of the
						 * intended 8. */
						(unsigned long)(uint32_t)s->value_lo_limit,
						(unsigned long)(uint32_t)s->value_hi_limit,
						(unsigned long)(uint32_t)s->value_limited_credit,
						s->value_limited_credit_enabled ? 1 : 0,
						(unsigned long)s->record_size, (unsigned long)s->record_max, (unsigned long)s->record_cur,
						s->tmac_key_option, s->tmac_key_version,
						(int)file->read_status, (unsigned)file->data_len);
				m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));

				if (file->data_len > 0U) {
					/* +1 slack, unconditional trailing space on every byte --
					 * see the DESFire Version block above for why a tight
					 * "- 3" capacity guard silently drops the last byte when
					 * data_len exactly fills the buffer (e.g. a Partial file
					 * at exactly MF_DESFIRE_DEEP_FILE_DATA_CAP bytes). */
					char data_str[3 * MF_DESFIRE_DEEP_FILE_DATA_CAP + 1];
					int  pos = 0;
					for (uint16_t i = 0; i < file->data_len; i++) {
						pos += snprintf(data_str + pos, sizeof(data_str) - (size_t)pos, "%02X ", file->data[i]);
					}
					snprintf(desf_line, sizeof(desf_line), "DESFire Application %s File %02X Data: %s\r\n",
							aid_str, file->id, data_str);
					m1_fb_write_to_file(&nfc_file, desf_line, strlen(desf_line));
				}
			}
		}
	}

	// Save the bounded card-interpretation result (e.g. Clipper) if a
	// supported transit card was recognized. Persisted directly (not
	// recomputed from the generic DESFire data on load) so a reloaded card
	// shows the identical interpreted summary without needing the ride
	// history's full byte capture -- which itself is never persisted
	// (transient, request-time-only) to keep the on-disk format small.
	if ((ctx->head.tech == NFC_TX_A) && (ctx->head.family == M1NFC_FAM_DESFIRE) &&
		(ctx->transit.card_id == NfcTransitCardClipper))
	{
		char t_line[64];

		strcpy(t_line, "Clipper Card: 1\r\n");
		m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));

		if (ctx->transit.card_type_label != NULL) {
			snprintf(t_line, sizeof(t_line), "Clipper Type: %s\r\n", ctx->transit.card_type_label);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.serial_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Serial: %lu\r\n", (unsigned long)ctx->transit.serial_number);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.balance_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Balance: %d\r\n", (int)ctx->transit.balance_cents);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.last_update_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Last Update: %lu\r\n", (unsigned long)ctx->transit.last_update_1900);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.last_terminal_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Terminal: %u\r\n", (unsigned)ctx->transit.last_terminal_id);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.last_txn_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Txn: %u\r\n", (unsigned)ctx->transit.last_txn_id);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.counter_valid) {
			snprintf(t_line, sizeof(t_line), "Clipper Counter: %u\r\n", (unsigned)ctx->transit.counter);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		if (ctx->transit.rides_truncated) {
			strcpy(t_line, "Clipper Rides Truncated: 1\r\n");
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
		for (uint8_t i = 0; i < ctx->transit.ride_count && i < NFC_TRANSIT_MAX_RIDES; i++) {
			const nfc_transit_ride_t *r = &ctx->transit.rides[i];
			snprintf(t_line, sizeof(t_line), "Clipper Ride %02u: %04X %d %u %lu %lu %u %u\r\n",
					(unsigned)i, (unsigned)r->agency_id, (int)r->fare_cents, (unsigned)r->vehicle_id,
					(unsigned long)r->time_on_1900, (unsigned long)r->time_off_1900,
					(unsigned)r->zone_on_id, (unsigned)r->zone_off_id);
			m1_fb_write_to_file(&nfc_file, t_line, strlen(t_line));
		}
	}

	m1_fb_close_file(&nfc_file);
	s_next_save_manual = false;   // consume the one-shot provenance flag
	return true;
}

/*============================================================================*/
/**
 * @brief Get filename from user and create full file path
 * 
 * Prompts user for filename using virtual keyboard, validates
 * SD card space, creates directory if needed, and checks for
 * duplicate filenames.
 * 
 * @param filepath Output buffer for full file path (can be NULL)
 * @param filepath_size Actual output capacity, including NUL
 * @return 0 on success
 * @return 1 Limited space available on SD card
 * @return 2 Storage/path error or insufficient output capacity
 * @return 3 User escaped (cancelled)
 */
/*============================================================================*/
uint8_t nfc_save_file_keyboard(char *filepath, size_t filepath_size)
{
    return m1_save_filename(filepath, filepath_size, DRIVE0_NFC, DRIVE0_NFC "/", NFC_FILE_PREFIX, NFC_FILE_EXTENSION);
}
