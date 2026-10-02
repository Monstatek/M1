/* Host-test stub of rfal_nfc.h -- only the symbols m1_desfire.c actually
 * uses (rfalNfcDevice.rfInterface, RFAL_NFC_INTERFACE_ISODEP, and the
 * three data-exchange functions). Value copied verbatim from the real
 * NFC/Middlewares/ST/rfal/Inc/rfal_nfc.h. The real function PROTOTYPES are
 * declared here (matching the real header exactly, verified by direct
 * comparison); rfal_mock.c provides the actual (scripted) implementations
 * -- this is the production seam the test drives. Not used by the
 * firmware build -- host analysis only. */
#ifndef RFAL_NFC_STUB_H_
#define RFAL_NFC_STUB_H_

#include "rfal_utils.h"
#include <stdint.h>

typedef enum {
    RFAL_NFC_INTERFACE_ISODEP = 1
} rfalNfcRfInterface;

typedef struct {
    rfalNfcRfInterface rfInterface;
} rfalNfcDevice;

void       rfalNfcWorker(void);
ReturnCode rfalNfcDataExchangeStart(uint8_t *txData, uint16_t txDataLen, uint8_t **rxData, uint16_t **rvdLen, uint32_t fwt);
ReturnCode rfalNfcDataExchangeGetStatus(void);

#endif
