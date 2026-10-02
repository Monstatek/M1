/* Active decoder-module registry + drain helpers. Modules own their own state;
 * this layer just fans pulses out and polls for results. */
#include "subghz_decoder_module.h"

#define SUBGHZ_MAX_MODULES 8
static SubGHz_DecoderModule_t *_mods[SUBGHZ_MAX_MODULES];
static uint16_t _n_mods = 0;

void subghz_modules_init(void)
{
    _n_mods = 0;
    _mods[_n_mods++] = subghz_module_linear();     /* Linear (raw-feed module) */
    /* future: Princeton, CAME, Security+2.0 modules added here */
}

void subghz_modules_reset(void)
{
    for (uint16_t i=0;i<_n_mods;i++) _mods[i]->reset(_mods[i]);
}

void subghz_modules_feed(uint8_t level, uint32_t duration_us)
{
    for (uint16_t i=0;i<_n_mods;i++) _mods[i]->feed(_mods[i], level, duration_us);
}

/* returns (index+1) and fills out if a module has a result; 0 if none */
uint16_t subghz_modules_poll(SubGHz_Result_t *out)
{
    for (uint16_t i=0;i<_n_mods;i++)
        if (_mods[i]->is_ready(_mods[i]) && _mods[i]->get_result(_mods[i], out))
            return i+1;
    return 0;
}
