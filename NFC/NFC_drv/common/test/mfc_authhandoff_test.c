/* Host test for the authoritative pending-AUTH ownership state machine.
 * Mirrors the exact accept/take/clear/generation logic in m1_mfc_raw_listener.c
 * (m1_mfc_pauth_set / m1_mfc_pauth_take / m1_mfc_pauth_clear + session gen), which
 * cannot be linked directly on host (FreeRTOS/DWT/RFAL deps). Deterministic. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- exact logic under test (kept byte-for-byte equivalent to the firmware) ---- */
typedef struct {
    uint8_t  cmd[2]; uint16_t bits;
    uint32_t sess_gen, rx_gen, rx_cyc;
    uint8_t  valid, consumed;
} pauth_t;

static pauth_t   P;
static uint32_t  SESS = 0, RXG = 0;
static uint8_t   ARMED = 0;

static void sess_start(void){ SESS++; memset(&P,0,sizeof P); }        /* begin() */
static void pa_clear(void){ P.valid = 0; P.consumed = 0; }            /* clear    */

static void pa_set(const uint8_t *raw, uint16_t bits){               /* pauth_set */
    if(!ARMED || raw==NULL) return;
    if(bits != 16u) return;
    if(raw[0]!=0x60u && raw[0]!=0x61u) return;                       /* 30/anything rejected */
    P.cmd[0]=raw[0]; P.cmd[1]=raw[1]; P.bits=bits;
    P.sess_gen=SESS; P.rx_gen=++RXG; P.rx_cyc=1; P.valid=1; P.consumed=0;
}
static uint8_t pa_take(uint8_t *c0, uint8_t *c1, uint32_t *g){       /* pauth_take */
    if(!P.valid || P.consumed) return 0;
    if(P.sess_gen != SESS){ P.valid=0; return 0; }                  /* stale session */
    P.consumed=1;
    if(c0)*c0=P.cmd[0]; if(c1)*c1=P.cmd[1]; if(g)*g=P.rx_gen;
    return 1;
}
static uint8_t pa_peek(uint8_t out[2], uint32_t *g){                 /* pauth_peek: no consume */
    if(!P.valid || P.consumed) return 0;
    if(P.sess_gen != SESS){ P.valid=0; return 0; }
    out[0]=P.cmd[0]; out[1]=P.cmd[1]; if(g)*g=P.rx_gen; return 1;
}
static void pa_dispatched(uint32_t g){                              /* consume on accept */
    if(!P.valid || P.consumed) return;
    if(P.rx_gen != g || P.sess_gen != SESS) return;
    P.consumed=1;
}

/* ---- harness ---- */
static int fails=0, total=0;
#define CHECK(c,msg) do{ total++; if(!(c)){ printf("  FAIL: %s\n",(msg)); fails++; } }while(0)

int main(void){
    uint8_t c0,c1; uint32_t g;
    uint8_t pk[2];                       /* 2-byte buffer for pa_peek (writes out[0..1]) */
    const uint8_t f30[2]={0x30,0x00}, f60[2]={0x60,0x00}, f61[2]={0x61,0x04};

    /* 1) 30 00 ignored, then 60 00 captured + dispatched */
    ARMED=1; sess_start();
    pa_set(f30,16); CHECK(P.valid==0,"30 must not populate pending");
    pa_set(f60,16); CHECK(P.valid==1 && P.cmd[0]==0x60,"60 must be captured after ignored 30");
    CHECK(pa_take(&c0,&c1,&g)==1 && c0==0x60 && c1==0x00,"60 dispatched with correct bytes");

    /* 2) 61 xx Key-B activation */
    sess_start(); pa_set(f61,16);
    CHECK(P.valid==1 && P.cmd[0]==0x61 && P.cmd[1]==0x04,"61 (Key B) captured");
    CHECK(pa_take(&c0,&c1,&g)==1 && c0==0x61,"61 dispatched");

    /* 3) AUTH as the first command */
    sess_start(); pa_set(f60,16);
    CHECK(P.valid==1,"AUTH-first captured");

    /* 4) stale generation rejected (captured in prior session) */
    sess_start(); pa_set(f60,16);        /* captured in session S */
    sess_start();                         /* new session S+1 */
    CHECK(pa_take(&c0,&c1,&g)==0,"stale-session pending rejected");

    /* 5) same generation dispatched exactly once */
    sess_start(); pa_set(f60,16);
    CHECK(pa_take(&c0,&c1,&g)==1,"first take succeeds");
    CHECK(pa_take(&c0,&c1,&g)==0,"second take of same generation rejected");

    /* 6) not armed => never populates (raw MFC persona gate) */
    ARMED=0; sess_start(); pa_set(f60,16);
    CHECK(P.valid==0,"disarmed persona must not populate pending");
    ARMED=1;

    /* 7) wrong bit length rejected */
    sess_start(); { uint8_t r[2]={0x60,0x00}; pa_set(r,7); }
    CHECK(P.valid==0,"non-16-bit AUTH rejected");

    /* 8) field loss / clear drops pending before dispatch */
    sess_start(); pa_set(f60,16); pa_clear();
    CHECK(pa_take(&c0,&c1,&g)==0,"cleared pending not dispatched");

    /* 9) PEEK does not consume; only dispatched consumes (first-frame handoff) */
    sess_start(); pa_set(f60,16);
    CHECK(pa_peek(pk,&g)==1 && pk[0]==0x60,"peek returns 60 without consuming");
    CHECK(pa_peek(pk,&g)==1,"second peek still returns (not consumed)");
    pa_dispatched(g);
    CHECK(P.consumed==1,"dispatched marks consumed");
    CHECK(pa_peek(pk,&g)==0,"peek after dispatch returns 0");

    /* 10) dispatched is generation-guarded + single: wrong gen no-op, right gen once */
    sess_start(); pa_set(f60,16); (void)pa_peek(pk,&g);
    pa_dispatched(g + 99u);  CHECK(P.consumed==0,"wrong-generation dispatch is a no-op");
    pa_dispatched(g);        CHECK(P.consumed==1,"correct-generation dispatch consumes");
    pa_dispatched(g);        CHECK(1,"second dispatch of same generation is a no-op (idempotent)");

    if(fails==0) printf("mfc_authhandoff_test: %d passed, 0 failed\n", total);
    else         printf("mfc_authhandoff_test: %d passed, %d FAILED\n", total-fails, fails);
    return fails ? 1 : 0;
}
