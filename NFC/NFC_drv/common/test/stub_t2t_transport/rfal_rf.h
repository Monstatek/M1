/* Host-test stub of rfal_rf.h -- structurally faithful for the exact types/
 * constants/functions m1_t2t_transport.c actually uses. NOT the real
 * header: the real one transitively pulls rfal_platform.h (the full STM32/
 * ST25R3916 HAL stack), impractical to host-compile -- same constraint
 * documented throughout this project's other host tests (see e.g.
 * mfc_emu_image_test.c / t2t_emu_image_test.c's stub directories).
 * Field/constant values below are copied verbatim from the real
 * NFC/Middlewares/ST/rfal/Inc/rfal_rf.h (grep-verified while writing this
 * file). The actual RFAL behavior each function represents is provided by
 * rfal_mock.c in this same directory, under full test-program control.
 * Not used by the firmware build -- host analysis only. */
#ifndef RFAL_RF_STUB_H_
#define RFAL_RF_STUB_H_

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t ReturnCode;
#define RFAL_ERR_NONE       0U
#define RFAL_ERR_BUSY       1U
#define RFAL_ERR_LINK_LOSS  2U   /* real rfal_rf.h value differs; m1_t2t_transport.c
                                  * never compares against this constant by name --
                                  * it only checks (!= RFAL_ERR_NONE) / (== RFAL_ERR_BUSY) --
                                  * so the exact numeric value here is test-only and safe. */
#define RFAL_ERR_TIMEOUT    3U
#define RFAL_ERR_CRC        4U
#define RFAL_ERR_PARAM      5U

#define RFAL_FWT_NONE       0xFFFFFFFFU

#define RFAL_LM_MASK_NFCA   1U

typedef enum {
    RFAL_LM_NFCID_LEN_04 = 4,
    RFAL_LM_NFCID_LEN_07 = 7,
} rfalLmNfcidLen;

typedef enum {
    RFAL_LM_STATE_NOT_INIT   = 0x00,
    RFAL_LM_STATE_POWER_OFF  = 0x01,
    RFAL_LM_STATE_IDLE       = 0x02,
    RFAL_LM_STATE_READY_A    = 0x04,
    RFAL_LM_STATE_ACTIVE_A   = 0x06,
    RFAL_LM_STATE_CARDEMU_4A = 0x07,
    RFAL_LM_STATE_SLEEP_A    = 0x0C,
    RFAL_LM_STATE_READY_Ax   = 0x0E,
    RFAL_LM_STATE_ACTIVE_Ax  = 0x0F,
} rfalLmState;

#define RFAL_LM_SENS_RES_LEN 2U
#define RFAL_NFCID1_TRIPLE_LEN 10U

typedef struct {
    rfalLmNfcidLen nfcidLen;
    uint8_t        nfcid[RFAL_NFCID1_TRIPLE_LEN];
    uint8_t        SENS_RES[RFAL_LM_SENS_RES_LEN];
    uint8_t        SEL_RES;
} rfalLmConfPA;

/* Passive B/F configs are unused by m1_t2t_transport.c (always passed NULL),
 * but rfalListenStart()'s real signature takes pointers to them -- declare
 * opaque placeholder types so the signature transcribes exactly. */
typedef struct { uint8_t _unused; } rfalLmConfPB;
typedef struct { uint8_t _unused; } rfalLmConfPF;

#define RFAL_TXRX_FLAGS_DEFAULT 0U

typedef struct {
    uint8_t  *txBuf;
    uint16_t  txBufLen;
    uint8_t  *rxBuf;
    uint16_t  rxBufLen;
    uint16_t *rxRcvdLen;
    uint32_t  flags;
    uint32_t  fwt;
} rfalTransceiveContext;

ReturnCode  rfalListenStart(uint32_t lmMask, const rfalLmConfPA *confA, const rfalLmConfPB *confB,
                            const rfalLmConfPF *confF, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen);
ReturnCode  rfalListenSleepStart(rfalLmState sleepSt, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen);
ReturnCode  rfalListenStop(void);
rfalLmState rfalListenGetState(bool *dataFlag, void *lastBR);

ReturnCode  rfalStartTransceive(rfalTransceiveContext *ctx);
ReturnCode  rfalGetTransceiveStatus(void);
void        rfalWorker(void);

static inline uint16_t rfalConvBytesToBits(uint16_t bytes) { return (uint16_t)(bytes * 8U); }
static inline uint16_t rfalConvBitsToBytes(uint16_t bits)  { return (uint16_t)((bits + 7U) / 8U); }

#endif
