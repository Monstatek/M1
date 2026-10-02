/* Per-protocol decoder module interface (market-ready architecture).
 * Each module owns its params, framing, guard/sync, valid bit-lengths, state,
 * result-ready state, result extraction, and reset. No global packet-gap threshold;
 * each module recognizes its own packet boundaries. */
#ifndef SUBGHZ_DECODER_MODULE_H
#define SUBGHZ_DECODER_MODULE_H
#include <stdint.h>
#include <stdbool.h>

#define SUBGHZ_LEVEL_LOW   0
#define SUBGHZ_LEVEL_HIGH  1

typedef struct { uint64_t key; uint16_t bit_len; const char *name; } SubGHz_Result_t;

struct SubGHz_DecoderModule;
typedef struct SubGHz_DecoderModule {
    const char *name;
    void (*reset)(struct SubGHz_DecoderModule *self);
    void (*feed)(struct SubGHz_DecoderModule *self, uint8_t level, uint32_t duration_us);
    bool (*is_ready)(struct SubGHz_DecoderModule *self);
    bool (*get_result)(struct SubGHz_DecoderModule *self, SubGHz_Result_t *out);
    void *state;   /* module-private state (owned by the module) */
} SubGHz_DecoderModule_t;

/* registry of module pointers */
extern SubGHz_DecoderModule_t * const subghz_modules[];
extern const uint16_t subghz_n_modules;

/* factory for the Linear module */
SubGHz_DecoderModule_t *subghz_module_linear(void);
SubGHz_DecoderModule_t *subghz_module_fixedcode(void);
void subghz_fc_last(uint16_t *te, uint32_t *guard, uint8_t *bits);
void subghz_fc_diag(uint16_t *te, uint32_t *guard, uint8_t *bits, uint32_t *fin);

void subghz_modules_init(void);
void subghz_modules_reset(void);
void subghz_modules_feed(uint8_t level, uint32_t duration_us);
uint16_t subghz_modules_poll(SubGHz_Result_t *out);

#endif
