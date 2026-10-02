/*============================================================================*/
/**
 * @file    mfc_detect.h
 * @brief   MIFARE Classic "Detect Reader" authentication-capture RF handler.
 *
 * Phase 1A (Technique A, best-effort): while the M1 emulates a MIFARE Classic
 * 1K card via the existing NFC-A listener, this observes a reader's AUTH
 * exchange and captures {cuid, sector, key_type, nt, nr, ar} into the RAM store
 * (mfc_capture.*). It composes the EXISTING raw manual-parity RFAL transceive
 * primitives (rfalStartTransceive + PAR_TX_NONE/PAR_RX_KEEP/CRC_RX_MANUAL) that
 * nfc_poller.c already uses as initiator — here on the listener side.
 *
 * NO key recovery. NO Crypto1 verification is required to record a capture
 * (emitted as soon as Nr+Ar are
 * received, regardless of whether the reader's key is correct).
 *
 * TIMING CAVEAT: the card->reader Nt response is issued over SPI from the
 * (interrupt-assisted) listener context; whether that meets a real reader's
 * MIFARE FDT is the Phase-1A hardware-test question (TEST C/D). If it fails on
 * strict readers, the fix is Technique B (transparent-mode + DMA waveform).
 */
/*============================================================================*/
#ifndef MFC_DETECT_H
#define MFC_DETECT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Detect Reader session state machine (kept small + explicit). */
typedef enum {
    MFC_DR_IDLE = 0,      /* not running                                      */
    MFC_DR_WAIT_READER,   /* listener up, no reader field yet                 */
    MFC_DR_READER_ACTIVE, /* test04: >=1 genuine inbound reader frame reached SW */
    MFC_DR_AUTH_ACTIVE,   /* a MIFARE AUTH (0x60/0x61) was observed           */
    MFC_DR_DONE,          /* nonce target reached                             */
} mfc_dr_state_t;

/* Start a Detect Reader session: reset the capture store, seed the emulated
 * CUID, and enter WAIT_READER. Call once when the listener is (re)initialised
 * for the MFC-detect persona. */
void mfc_detect_begin(void);

/* End the session (BACK / stop / teardown). Idempotent; clears transient state.
 * Does not itself touch RF hardware — the listener owns field-off. */
void mfc_detect_end(void);

/* Mark that a reader has activated the emulated card (called from the listener
 * ACTIVATED transition). Moves WAIT_READER -> READER_ACTIVE. */
void mfc_detect_on_activated(void);

/* Mark reader/field loss (called from the listener deactivation/field-loss
 * path). Discards only the in-flight transaction; completed captures are kept
 * and the state returns to WAIT_READER. */
void mfc_detect_on_field_lost(void);

/* Offer one received listener frame. If it is a MIFARE AUTH (0x60/0x61 + block)
 * this generates+sends Nt and captures the reader's {Nr,Ar} via a raw
 * transceive, stores the context, and returns true (frame consumed). Returns
 * false for any non-AUTH frame so the caller can handle it normally.
 * `rxLenBits` is the received length in BITS as reported by RFAL. */
bool mfc_detect_service_frame(const uint8_t *rx, uint16_t rxLenBits);

/* Read-only status for the UI. */
mfc_dr_state_t mfc_detect_state(void);
uint32_t       mfc_detect_cuid(void);

#ifdef __cplusplus
}
#endif

#endif /* MFC_DETECT_H */
