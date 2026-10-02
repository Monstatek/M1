/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h5xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "m1_fault_report.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

volatile uint32_t g_hardfault_r0;
volatile uint32_t g_hardfault_r1;
volatile uint32_t g_hardfault_r2;
volatile uint32_t g_hardfault_r3;
volatile uint32_t g_hardfault_r12;
volatile uint32_t g_hardfault_lr;
volatile uint32_t g_hardfault_pc;
volatile uint32_t g_hardfault_psr;
volatile uint32_t g_hardfault_cfsr;
volatile uint32_t g_hardfault_hfsr;
volatile uint32_t g_hardfault_bfar;
volatile uint32_t g_hardfault_mmfar;
volatile uint32_t g_hardfault_sp;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern IWDG_HandleTypeDef hiwdg;
extern RTC_HandleTypeDef hrtc;
extern SD_HandleTypeDef hsd1;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
    uint32_t eccdr_value = FLASH->ECCDR; // Read ECCDR to clear the NMI
    // if (/* ECCDR indicates uninitialized memory access */)
    if (1) {
        // Clear the specific ECCD flag
        // Exit
    } else {
        // Handle a real data corruption error
    }
  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}


void prvGetRegistresFromFault(uint32_t *pulFaultStackAddress) {
    /* These are the registers stacked by the Cortex-M hardware on exception entry */
    volatile uint32_t r0  = pulFaultStackAddress[0];
    volatile uint32_t r1  = pulFaultStackAddress[1];
    volatile uint32_t r2  = pulFaultStackAddress[2];
    volatile uint32_t r3  = pulFaultStackAddress[3];
    volatile uint32_t r12 = pulFaultStackAddress[4];
    volatile uint32_t lr  = pulFaultStackAddress[5]; /* Link Register (return address) */
    volatile uint32_t pc  = pulFaultStackAddress[6]; /* Program Counter (fault address) */
    volatile uint32_t psr = pulFaultStackAddress[7];

    /* Force a breakpoint here so the IDE stops with variables loaded */
    __asm volatile("bkpt #0");

    while(1);
}


/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
//  __asm volatile (
//    "tst lr, #4            \n"
//    "ite eq                \n"
//    "mrseq r0, msp         \n"
//    "mrsne r0, psp         \n"
//    "b HardFault_Handler_C \n"
//  );

    __asm volatile (
        "tst lr, #4 \n"          /* Check EXC_RETURN bit 2 to see which stack was used */
        "ite eq \n"
        "mrseq r0, msp \n"       /* Enters here if MSP was used (e.g. inside an ISR) */
        "mrsne r0, psp \n"       /* Enters here if PSP was used (inside a FreeRTOS task) */
        "ldr r1, [r0, #24] \n"   /* Read saved PC (Program Counter) from the stack frame */
        "ldr r2, =prvGetRegistresFromFault \n"
        "bx r2 \n"
    );
}

void HardFault_Handler_C(uint32_t *stacked_regs)
{
  g_hardfault_r0 = stacked_regs[0];
  g_hardfault_r1 = stacked_regs[1];
  g_hardfault_r2 = stacked_regs[2];
  g_hardfault_r3 = stacked_regs[3];
  g_hardfault_r12 = stacked_regs[4];
  g_hardfault_lr = stacked_regs[5];
  g_hardfault_pc = stacked_regs[6];
  g_hardfault_psr = stacked_regs[7];
  g_hardfault_sp = (uint32_t)stacked_regs;
  g_hardfault_cfsr = SCB->CFSR;
  g_hardfault_hfsr = SCB->HFSR;
  g_hardfault_bfar = SCB->BFAR;
  g_hardfault_mmfar = SCB->MMFAR;
  /* USER CODE BEGIN HardFault_IRQn 0 */
	static volatile int _go_db;

	/* Best-effort: show the fault status + capture checkpoint on the display so
	 * the crash is diagnosable on-device (see m1_fault_report / g_cap_checkpoint). */
	m1_fault_report("HARDFAULT");

	_go_db = 0;

	while( _go_db==0 )
		;
	return;
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 *
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  g_hardfault_cfsr = SCB->CFSR;
  g_hardfault_hfsr = SCB->HFSR;
  g_hardfault_mmfar = SCB->MMFAR;
  g_hardfault_bfar = SCB->BFAR;
  m1_fault_report("MEMMANAGE");
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  g_hardfault_cfsr = SCB->CFSR;
  g_hardfault_hfsr = SCB->HFSR;
  g_hardfault_mmfar = SCB->MMFAR;
  g_hardfault_bfar = SCB->BFAR;
  m1_fault_report("BUSFAULT");
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  g_hardfault_cfsr = SCB->CFSR;
  g_hardfault_hfsr = SCB->HFSR;
  g_hardfault_mmfar = SCB->MMFAR;
  g_hardfault_bfar = SCB->BFAR;
  m1_fault_report("USAGEFAULT");
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles RTC non-secure interrupt.
  */
void RTC_IRQHandler(void)
{
  /* USER CODE BEGIN RTC_IRQn 0 */

  /* USER CODE END RTC_IRQn 0 */
  HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
  /* USER CODE BEGIN RTC_IRQn 1 */

  /* USER CODE END RTC_IRQn 1 */
}

/**
  * @brief This function handles IWDG global interrupt.
  */
void IWDG_IRQHandler(void)
{
  /* USER CODE BEGIN IWDG_IRQn 0 */

  /* USER CODE END IWDG_IRQn 0 */
  HAL_IWDG_IRQHandler(&hiwdg);
  /* USER CODE BEGIN IWDG_IRQn 1 */

  /* USER CODE END IWDG_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt.
  */
void TIM6_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_IRQn 0 */

  /* USER CODE END TIM6_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_IRQn 1 */

  /* USER CODE END TIM6_IRQn 1 */
}

/**
  * @brief This function handles SDMMC1 global interrupt.
  */
void SDMMC1_IRQHandler(void)
{
  /* USER CODE BEGIN SDMMC1_IRQn 0 */

  /* USER CODE END SDMMC1_IRQn 0 */
  HAL_SD_IRQHandler(&hsd1);
  /* USER CODE BEGIN SDMMC1_IRQn 1 */

  /* USER CODE END SDMMC1_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/**
  * @brief This function handles RTC non-secure interrupt.
  */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN RTC_IRQn 0 */

  /* USER CODE END RTC_IRQn 0 */
  HAL_EXTI_IRQHandler(&H_EXTI_0);
  /* USER CODE BEGIN RTC_IRQn 1 */

  /* USER CODE END RTC_IRQn 1 */
}
/* USER CODE END 1 */
