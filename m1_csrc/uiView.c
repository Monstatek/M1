/* See COPYING.txt for license details. */

/*
 * uiView.c
 *
 *      Author: Thomas
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_rfid.h"
#include "m1_virtual_kb.h"
#include "m1_storage.h"
#include "uiView.h"

/***************************** V A R I A B L E S ******************************/

static  uint8_t uiview_previous_mode;
static  uint8_t uiview_current_mode; // Initialized in init function
static TimerHandle_t       s_screenTimer      = NULL;
static screen_timeout_cb_t s_timeoutCallback  = NULL;

static S_M1_uiview_t uiview_view_list[VIEW_MODE_END];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void m1_uiView_functions_register(uint8_t nMode,
        void (*create) (uint8_t param),
        void (*update) (uint8_t param),
        void (*destroy)(uint8_t param),
        int (*message) (void));
void m1_uiView_functions_init(int size, const view_func_t *table);
void m1_uiView_display_switch(uint8_t mode, uint32_t lParam);
void m1_uiView_display_update(uint32_t param);
static void prvScreenTimeoutTimerCb(TimerHandle_t xTimer);
void uiScreen_timeout_init(void);
void uiScreen_timeout_start(uint32_t timeout_ms, screen_timeout_cb_t cb);
void uiScreen_timeout_cancel(void);
void uiScreen_timeout_restart(uint32_t timeout_ms);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_uiView_functions_register( uint8_t nMode,
        void (*create) (uint8_t param),
        void (*update) (uint8_t param),
        void (*destroy)(uint8_t param),
        int (*message) (void))
{
	uiview_view_list[nMode].create = create;
	uiview_view_list[nMode].update = update;
	uiview_view_list[nMode].destroy = destroy;
	uiview_view_list[nMode].message = message;
} // void m1_uiView_functions_register(...)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_uiView_functions_init(int size, const view_func_t *table)
{
	int i;

	uiview_current_mode = 0;
	memset(uiview_view_list, 0, sizeof(uiview_view_list));

	for (i = 0; i < size; i++)
	{
		if ( table[i] )
		{
			uiview_view_list[i].init = table[i];
			table[i]();
		}
	} // for (i = 0; i < size; i++)

} // void m1_uiView_functions_init(int size, const view_func_t *table)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_uiView_display_switch(uint8_t mode, uint32_t lParam)
{
	uiScreen_timeout_cancel();

#if 0
	if (uiview_current_mode == mode)
		return;
#endif
	if(uiview_current_mode)
	{
		if (uiview_view_list[uiview_current_mode].destroy)
			uiview_view_list[uiview_current_mode].destroy(lParam);
	}

	uiview_previous_mode = uiview_current_mode;
	uiview_current_mode = mode;
	if( mode )
	{
		if (uiview_view_list[mode].create)
			uiview_view_list[mode].create(lParam);
		//m1_uiView_display_update(lParam);
	}

} // void m1_uiView_display_switch(uint8_t mode, uint32_t lParam)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
int m1_uiView_q_message_process(void)
{
   	if(uiview_current_mode)
   	{
  		if (uiview_view_list[uiview_current_mode].message)
   		{
  			return uiview_view_list[uiview_current_mode].message();
   		}
  	}
 	return 1;
} // int m1_uiView_q_message_process(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_uiView_display_update(uint32_t param)
{
	if(uiview_current_mode)
	{
		if (uiview_view_list[uiview_current_mode].update)
		{
			uiview_view_list[uiview_current_mode].update(param);
		}
	}
} // void m1_uiView_display_update(uint32_t param)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/

static void prvScreenTimeoutTimerCb(TimerHandle_t xTimer)
{
    (void)xTimer;

    m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_TIMEOUT);
    if (s_timeoutCallback) {
        s_timeoutCallback();
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

void uiScreen_timeout_init(void)
{
    if (s_screenTimer == NULL) {
        s_screenTimer = xTimerCreate(
            "ScrTimeout",
            pdMS_TO_TICKS(1000),
            pdFALSE,
            NULL,
            prvScreenTimeoutTimerCb
        );
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

void uiScreen_timeout_start(uint32_t timeout_ms, screen_timeout_cb_t cb)
{
    if (s_screenTimer == NULL) {
        uiScreen_timeout_init();
    }

    s_timeoutCallback = cb;

    if (s_screenTimer) {
        xTimerStop(s_screenTimer, 0);
        xTimerChangePeriod(s_screenTimer, pdMS_TO_TICKS(timeout_ms), 0);
        xTimerStart(s_screenTimer, 0);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

void uiScreen_timeout_cancel(void)
{
    if (s_screenTimer) {
        xTimerStop(s_screenTimer, 0);
    }
    s_timeoutCallback = NULL;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

void uiScreen_timeout_restart(uint32_t timeout_ms)
{
    if (s_screenTimer == NULL || s_timeoutCallback == NULL) {
        return;
    }

    xTimerStop(s_screenTimer, 0);
    xTimerChangePeriod(s_screenTimer, pdMS_TO_TICKS(timeout_ms), 0);
    xTimerStart(s_screenTimer, 0);
}
