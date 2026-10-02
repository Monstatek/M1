/* Linear (10-bit, 3:1 PWM, te~500/1500us) self-contained decoder module.
 * Framing: bit1=long-high+short-low, bit0=short-high+long-low; packet ends on a
 * guard (low >= guard_min). Valid length: exactly 10 bits. Clean-room. M1 Project. */
#include "subghz_decoder_module.h"
#include <stdlib.h>

typedef struct {
    /* params (module-owned) */
    uint16_t te_short, te_long; uint8_t tol_pct, max_bits; uint32_t guard_min;
    /* framing state */
    uint16_t pending_hi; uint8_t have_hi; uint32_t code; uint8_t bits;
    /* result */
    uint8_t ready; uint32_t rkey; uint8_t rbits;
} LinearState;

static uint16_t adiff(uint16_t a, uint16_t b){ return a>b?a-b:b-a; }
static uint8_t near_us(uint32_t v, uint16_t ref, uint8_t tol){ return adiff((uint16_t)v,ref) <= (uint16_t)(((uint32_t)ref*tol)/100); }

static void lin_accum_reset(LinearState *s){ s->have_hi=0; s->code=0; s->bits=0; }

static void lin_reset(SubGHz_DecoderModule_t *self){ LinearState *s=self->state; lin_accum_reset(s); s->ready=0; }

static void lin_feed(SubGHz_DecoderModule_t *self, uint8_t level, uint32_t dur){
    LinearState *s=self->state;
    if (level==SUBGHZ_LEVEL_HIGH){ s->pending_hi=(uint16_t)dur; s->have_hi=1; return; }
    if (!s->have_hi) return;                      /* unpaired low */
    s->have_hi=0;
    uint16_t hi=s->pending_hi; uint8_t bit;
    if      (near_us(hi, s->te_long,  s->tol_pct)) bit=1;   /* long high  */
    else if (near_us(hi, s->te_short, s->tol_pct)) bit=0;   /* short high */
    else { lin_accum_reset(s); return; }                   /* invalid high */
    uint8_t is_guard = (dur >= s->guard_min);
    if (!is_guard){
        uint16_t exp_lo = bit ? s->te_short : s->te_long;  /* complementary low */
        if (!near_us(dur, exp_lo, s->tol_pct)){ lin_accum_reset(s); return; }
    }
    s->code=(s->code<<1)|bit; s->bits++;
    if (is_guard){
        if (s->bits==s->max_bits){ s->ready=1; s->rkey=s->code; s->rbits=s->bits; }
        s->code=0; s->bits=0;                              /* boundary -> next packet */
    } else if (s->bits > s->max_bits){ lin_accum_reset(s); }
}

static bool lin_is_ready(SubGHz_DecoderModule_t *self){ return ((LinearState*)self->state)->ready; }
static bool lin_get_result(SubGHz_DecoderModule_t *self, SubGHz_Result_t *out){
    LinearState *s=self->state; if(!s->ready) return false;
    out->key=s->rkey; out->bit_len=s->rbits; out->name=self->name; s->ready=0; return true;
}

static LinearState _linear_state = { 500,1500,30,10,4000, 0,0,0,0, 0,0,0 };
static SubGHz_DecoderModule_t _linear_module = {
    "Linear", lin_reset, lin_feed, lin_is_ready, lin_get_result, &_linear_state
};
SubGHz_DecoderModule_t *subghz_module_linear(void){ return &_linear_module; }
