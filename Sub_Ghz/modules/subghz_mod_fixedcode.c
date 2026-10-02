/* PT2262-family fixed-code classifier module: CAME (12/24), Gate TX (24), Nice FLO (12/24).
 * Robust batch decode: accumulate bit-HIGH pulses per packet, on the guard (long low)
 * self-calibrate te_short = min high, classify each high (short=0 / long=1), then classify
 * the protocol by measured te_short + exact bit count. Decision is by measurement, not order.
 * Clean-room. M1 Project. */
#include "subghz_decoder_module.h"

#define FC_GUARD_MIN   4000u   /* long low >= this ends a packet (proven gap; bit-lows <=1400) */
#define FC_MAX_HI      30

typedef struct {
    uint16_t highs[FC_MAX_HI]; uint8_t nhigh;
    uint8_t ready; uint32_t rkey; uint8_t rbits; const char *rname;
    uint16_t d_te; uint32_t d_guard; uint8_t d_bits; uint32_t d_fin;  /* diagnostics */
} FcState;

static void fc_pkt_reset(FcState *s){ s->nhigh=0; }
static void fc_reset(SubGHz_DecoderModule_t *self){ FcState *s=self->state; fc_pkt_reset(s); s->ready=0; }

static const char *fc_classify(uint16_t te, uint8_t bits)
{
    if (bits!=12 && bits!=24) return 0;
    if (te >= 550) return "Nice FLO";              /* te ~700 */
    if (te < 240 || te > 470) return 0;            /* outside CAME/GateTX range */
    if (bits==12) return "CAME";                   /* Gate TX is 24-bit only */
    return (te < 335) ? "CAME" : "Gate TX";        /* CAME te~320 vs Gate TX te~350 */
}

static void fc_finalize(FcState *s, uint32_t guard)
{
    s->d_fin++;
    if (s->nhigh>=8 && s->nhigh<=FC_MAX_HI){
        uint16_t te=0xFFFF;
        for (uint8_t i=0;i<s->nhigh;i++) if (s->highs[i]<te) te=s->highs[i];
        uint32_t code=0;
        for (uint8_t i=0;i<s->nhigh;i++){
            uint8_t bit = ((uint32_t)s->highs[i]*2 >= (uint32_t)te*3) ? 1 : 0; /* hi >= 1.5*te => long => 1 */
            code=(code<<1)|bit;
        }
        s->d_te=te; s->d_guard=guard; s->d_bits=s->nhigh;
        const char *nm=fc_classify(te, s->nhigh);
        if (nm){ s->ready=1; s->rkey=code; s->rbits=s->nhigh; s->rname=nm; }
    }
    fc_pkt_reset(s);
}

static void fc_feed(SubGHz_DecoderModule_t *self, uint8_t level, uint32_t dur)
{
    FcState *s=self->state;
    if (level==SUBGHZ_LEVEL_HIGH){
        if (dur < FC_GUARD_MIN){ if (s->nhigh<FC_MAX_HI) s->highs[s->nhigh++]=(uint16_t)dur; else fc_pkt_reset(s); }
        return;                                   /* ignore guard-length "highs" (misaligned edge) */
    }
    if (dur >= FC_GUARD_MIN) fc_finalize(s, dur); /* packet boundary */
    /* normal bit-low: ignored (highs carry the data) */
}

static bool fc_is_ready(SubGHz_DecoderModule_t *self){ return ((FcState*)self->state)->ready; }
static bool fc_get_result(SubGHz_DecoderModule_t *self, SubGHz_Result_t *out){
    FcState *s=self->state; if(!s->ready) return false;
    out->key=s->rkey; out->bit_len=s->rbits; out->name=s->rname; s->ready=0; return true;
}

static FcState _fc_state;
static SubGHz_DecoderModule_t _fc_module = { "FixedCode", fc_reset, fc_feed, fc_is_ready, fc_get_result, &_fc_state };
SubGHz_DecoderModule_t *subghz_module_fixedcode(void){ return &_fc_module; }
void subghz_fc_last(uint16_t *te, uint32_t *guard, uint8_t *bits){ *te=_fc_state.d_te; *guard=_fc_state.d_guard; *bits=_fc_state.d_bits; }
void subghz_fc_diag(uint16_t *te, uint32_t *guard, uint8_t *bits, uint32_t *fin){ *te=_fc_state.d_te; *guard=_fc_state.d_guard; *bits=_fc_state.d_bits; *fin=_fc_state.d_fin; }
