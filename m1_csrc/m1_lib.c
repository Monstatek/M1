/* See COPYING.txt for license details. */

/*
*
* m1_lib.c
*
* Common lib
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "m1_io_defs.h"
#include "m1_lib.h"

/*************************** D E F I N E S ************************************/

#define M1_GPIO_TEST_PIN		PD13_Pin
#define M1_GPIO_TEST_PORT		PD13_GPIO_Port

#define M1_GPIO_TEST2_PIN		PD12_Pin
#define M1_GPIO_TEST2_PORT		PD12_GPIO_Port

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void m1_test_gpio_pull_high(void);
void m1_test_gpio_pull_low(void);
void m1_test_gpio_toggle(void);
void m1_test2_gpio_pull_high(void);
void m1_test2_gpio_pull_low(void);
void m1_test2_gpio_toggle(void);
static void m1_gpio_toggle(GPIO_TypeDef *port, uint32_t pin, uint8_t set_reset);
void m1_strtolower(char *str);
void m1_strtoupper(char *str);
void m1_float_to_string(char *out_str, float in_val, uint8_t in_fraction);
float m1_float_convert(uint8_t value, float factor, float offset);
void m1_text_enable(char* out, uint8_t value, uint8_t mask);
int m1_strtob_with_base(const char *str, uint8_t *out, int max_len, int base);
void m1_byte_to_hextext(const uint8_t *src, int len, char *out);
void m1_vkb_set_initial_text(int len, char *out);
BaseType_t m1_app_send_q_message(QueueHandle_t Handle, S_M1_Q_Event_Type_t cmd);
uint8_t m1_datfile_keywords_check(char *instr, char *keyword, char delimiter);
void m1_hard_delay(uint32_t x);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void m1_gpio_toggle(GPIO_TypeDef *port, uint32_t pin, uint8_t set_reset)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = pin;
	if ( set_reset ) // Pull high?
	{
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
	}
	else // Pull low
	{
		GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	}
	HAL_GPIO_Init(port, &GPIO_InitStruct);

	if ( set_reset )
		HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
} // static void m1_gpio_toggle(GPIO_TypeDef *port, uint32_t pin, uint8_t set_reset)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test_gpio_pull_high(void)
{
	m1_gpio_toggle(M1_GPIO_TEST_PORT, M1_GPIO_TEST_PIN, GPIO_PIN_SET);
} // void m1_test_gpio_pull_high(void)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test_gpio_pull_low(void)
{
	m1_gpio_toggle(M1_GPIO_TEST_PORT, M1_GPIO_TEST_PIN, GPIO_PIN_RESET);
} // void m1_test_gpio_pull_low(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test_gpio_toggle(void)
{
	static uint8_t mode = 1;

	if ( mode )
		m1_test_gpio_pull_high();
	else
		m1_test_gpio_pull_low();

	mode ^= 1;
} // void m1_test_gpio_toggle(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test2_gpio_pull_high(void)
{
	m1_gpio_toggle(M1_GPIO_TEST2_PORT, M1_GPIO_TEST2_PIN, GPIO_PIN_SET);
} // void m1_test2_gpio_pull_high(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test2_gpio_pull_low(void)
{
	m1_gpio_toggle(M1_GPIO_TEST2_PORT, M1_GPIO_TEST2_PIN, GPIO_PIN_RESET);
} // void m1_test2_gpio_pull_low(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_test2_gpio_toggle(void)
{
	static uint8_t mode = 1;

	if ( mode )
		m1_test2_gpio_pull_high();
	else
		m1_test2_gpio_pull_low();

	mode ^= 1;
} // void m1_test_gpio_toggle(void)


/*============================================================================*/
/**
  * @brief  Convert a string to lowercase
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_strtolower(char *str)
{
	uint16_t i = 0;

	while ( str[i]!='\0' )
	{
		str[i] = tolower((uint8_t)str[i]);
		i++;
	} // while ( str[i]!='\0' )
} // void m1_strtolower(char *str)





/*============================================================================*/
/**
  * @brief  Convert a string to uppercase
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_strtoupper(char *str)
{
	uint16_t i = 0;

	while ( str[i]!='\0' )
	{
		str[i] = toupper((uint8_t)str[i]);
		i++;
	} // while ( str[i]!='\0' )
} // void m1_strtoupper(char *str)



/*============================================================================*/
/**
  * @brief  Convert a real number to a string
  * @param
  * @retval
  */
/*============================================================================*/
void m1_float_to_string(char *out_str, float in_val, uint8_t in_fraction)
{
	uint8_t 	fraction[5]; // '.xxx' plus End of String
	uint8_t 	format[7];
	uint32_t 	integer;

	if (in_val >= 1)
	{
		sprintf(out_str, "%lu", (uint32_t)in_val);
		integer = in_val; // Take the integer part
		in_val -= integer; // Take the fractional part
	} // if (in_val >= 1)
	else
		strcpy(out_str, "0");
	in_val *= pow(10, in_fraction);
	sprintf(format, ".%%0%dlu", in_fraction);
	sprintf(fraction, format, (uint32_t)in_val);

	strcat(out_str, (char *)fraction);
} // void m1_float_to_string(char *out_str, float in_val, uint8_t in_fraction)


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
float m1_float_convert(uint8_t value, float factor, float offset)
{
	return ((float)value * factor + offset);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void m1_text_enable(char* out, uint8_t value, uint8_t mask)
{
	if (value & mask)
		strcpy(out, "Enable");
	else
		strcpy(out, "Disable");
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
// str  : "11 22 33" 같은 문자열
// out  : 변환된 값이 저장될 배열
// max_len : out 배열의 최대 길이
// base : 10이면 십진, 16이면 십육진
int m1_strtob_with_base(const char *str, uint8_t *out, int max_len, int base)
{
    int count = 0;
    char buf[64];
    char *token;

    // strtok 때문에 원본 보존용 복사
    strncpy(buf, str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    token = strtok(buf, " ");   // 공백 기준으로 자르기

    while (token != NULL && count < max_len) {
        out[count++] = (uint8_t)strtol(token, NULL, base);
        token = strtok(NULL, " ");
    }

    return count;   // 변환된 개수
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_byte_to_hextext(const uint8_t *src, int len, char *out)
{
    for (int i = 0; i < len; i++) {
        sprintf(out + (i * 3), "%02X ", src[i]);  // "00 "
    }
    out[len * 3 - 1] = '\0';   // 마지막 공백 제거
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
// len : 항목 개수 (예: 5 → "00 00 00 00 00")
// out : 결과 문자열 버퍼
void m1_vkb_set_initial_text(int len, char *out)
{
    for (int i = 0; i < len; i++) {
        sprintf(out + i * 3, "00 ");  // "00 " 추가
    }
    out[len * 3 - 1] = '\0';  // 마지막 공백 제거하고 NULL 종료
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
BaseType_t m1_app_send_q_message(QueueHandle_t Handle, S_M1_Q_Event_Type_t cmd)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	if (Handle == NULL)
	{
		printf("[QUEUE][ERROR] drop event %d: NULL queue\r\n", (int)cmd);
		return pdFALSE;
	}
	q_item.q_evt_type = cmd;
	ret = xQueueSend(Handle, &q_item, pdMS_TO_TICKS(100));
	if (ret != pdTRUE)
		printf("[QUEUE][ERROR] drop event %d: send timeout\r\n", (int)cmd);
	return ret;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t m1_datfile_keywords_check(char *instr, char *keyword, char delimiter)
{
	uint8_t error_code = 0;
	char keywords[20], *runptr;

	sprintf(keywords, "%s%c", keyword, delimiter);
	runptr = strstr(instr, keywords);
	if ( runptr!=instr ) // Not found or found at the wrong position?
	{
		error_code = 1;
	} // if ( runptr != instr )

	return error_code;
} // uint8_t m1_datfile_keywords_check(char *instr, char *keyword, char delimiter)


/*============================================================================*/
/**
  * @brief  Delay without context switch
  * @param x in ms approximately
  * @retval
  */
/*============================================================================*/
void m1_hard_delay(uint32_t x)
{
    volatile uint32_t loop;

    for (loop=0; loop<6000*x; loop++) // 100
    {
    	;
    }
} // void m1_hard_delay(uint32_t x)
