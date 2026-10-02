/* See COPYING.txt for license details. */

/*
 * uiView.h
 *
 *
 */

#ifndef UIVIEW_H_
#define UIVIEW_H_

enum {
	VIEW_MODE_IDLE,
	VIEW_MODE_END = 22   /* shared uiview_view_list[] size; must be >= the largest
	                      * per-feature VIEW_MODE_*_END (NFC = 19 with MFC Write +
	                      * T2T Unlock + PWD dictionary management; a few spare
	                      * slots kept as headroom -- bump this, not the per-
	                      * feature enum, if a future addition overflows it) */
};

#define REG_SFLAG_CLIP          	0x20    /* if set then variable has min/max */
#define REG_SFLAG_CYCLE         	0x40    /* if set then value is in cycle */
#define REG_SFLAG_SIGNED        	0x80    /* if set then register is signed */

#define UI_SCREEN_TIMEOUT			(2000)

typedef void (*view_func_t)(void);

typedef struct {
        void (*create) (uint8_t param);
        void (*update) (uint8_t param);
        void (*destroy)(uint8_t param);
        int (*message) (void);
        void (*init)(void);
} S_M1_uiview_t;

void m1_uiView_functions_register (uint8_t nMode,
        void (*create) (uint8_t param),
        void (*update) (uint8_t param),
        void (*destroy)(uint8_t param),
        int (*message) (void));
void m1_uiView_display_update(uint32_t param);
int m1_uiView_q_message_process(void);
void m1_uiView_functions_init(int size, const view_func_t *table);
void m1_uiView_display_switch(uint8_t mode, uint32_t lParam);

typedef void (*screen_timeout_cb_t)(void);;
void uiScreen_timeout_start(uint32_t timeout_ms, screen_timeout_cb_t cb);
void  uiScreen_timeout_cancel(void);
void  uiScreen_timeout_restart(uint32_t timeout_ms);

#endif /* UIVIEW_H_ */
