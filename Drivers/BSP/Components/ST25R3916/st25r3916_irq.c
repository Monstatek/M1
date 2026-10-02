/**
  ******************************************************************************
  * @file    st25r3916_irq.c
  * @author  MMY Application Team
  * @brief   ST25R3916 Interrupt handling
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */


/*
******************************************************************************
* INCLUDES
******************************************************************************
*/

#include "st25r3916_irq.h"
#include "st25r3916_com.h"
#include "st25r3916_led.h"
#include "st25r3916.h"
#include "rfal_utils.h"

#if defined(M1_MFC_RAW_EMULATION)
#include "stm32h5xx.h"                 /* DWT cycle counter (RXE timestamp) */
extern volatile uint32_t g_m1_rxe_cyc; /* DWT->CYCCNT at the last RXE       */
extern volatile uint8_t  g_m1_rxe_seen;
#endif

#if defined(M1_MFC_AT_RX_DIAG)
/* Reader-side {At}-reception truth: latch which RF events fired during the
 * poller's {At} receive window (reset by nfc_poller before the transceive). */
extern volatile uint8_t g_m1_rdr_rxs;  /* start-of-receive (subcarrier seen) */
extern volatile uint8_t g_m1_rdr_rxe;  /* end-of-receive                     */
extern volatile uint8_t g_m1_rdr_nre;  /* no-response timer expired          */
#endif

/*
 ******************************************************************************
 * LOCAL DATA TYPES
 ******************************************************************************
 */

/*! Holds current and previous interrupt callback pointer as well as current Interrupt status and mask */
typedef struct
{
    void      (*prevCallback)(void); /*!< call back function for ST25R3916 interrupt          */
    void      (*callback)(void);     /*!< call back function for ST25R3916 interrupt          */
    uint32_t  status;                /*!< latest interrupt status                             */
    uint32_t  mask;                  /*!< Interrupt mask. Negative mask = ST25R3916 mask regs */
} st25r3916Interrupt;


/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/

/*! Length of the interrupt registers       */
#define ST25R3916_INT_REGS_LEN          ( (ST25R3916_REG_IRQ_TARGET - ST25R3916_REG_IRQ_MAIN) + 1U )

/*
******************************************************************************
* GLOBAL VARIABLES
******************************************************************************
*/

static volatile st25r3916Interrupt   st25r3916interrupt; /*!< Instance of ST25R3916 interrupt */

/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/
void st25r3916InitInterrupts( void )
{
    platformIrqST25RPinInitialize();
    platformIrqST25RSetCallback( st25r3916Isr );
    
    
    st25r3916interrupt.callback     = NULL;
    st25r3916interrupt.prevCallback = NULL;
    st25r3916interrupt.status       = ST25R3916_IRQ_MASK_NONE;
    st25r3916interrupt.mask         = ST25R3916_IRQ_MASK_NONE;
}


/*******************************************************************************/
void st25r3916Isr( void )
{
    st25r3916CheckForReceivedInterrupts();
    
    // Check if callback is set and run it
    if( NULL != st25r3916interrupt.callback )
    {
        st25r3916interrupt.callback();
    }
}


/*******************************************************************************/
void st25r3916CheckForReceivedInterrupts( void )
{
    uint8_t  iregs[ST25R3916_INT_REGS_LEN];
    uint32_t irqStatus;
    
    /* Initialize iregs */
    irqStatus = ST25R3916_IRQ_MASK_NONE;
    RFAL_MEMSET( iregs, (int32_t)(ST25R3916_IRQ_MASK_ALL & 0xFFU), ST25R3916_INT_REGS_LEN );
    
    
    /* In case the IRQ is Edge (not Level) triggered read IRQs until done */
   while( platformGpioIsHigh( ST25R_INT_PORT, ST25R_INT_PIN ) )
   {
       st25r3916ReadMultipleRegisters( ST25R3916_REG_IRQ_MAIN, iregs, ST25R3916_INT_REGS_LEN );
       
       irqStatus |= (uint32_t)iregs[0];
       irqStatus |= (uint32_t)iregs[1]<<8;
       irqStatus |= (uint32_t)iregs[2]<<16;
       irqStatus |= (uint32_t)iregs[3]<<24;
   }

#if defined(M1_MFC_RAW_EMULATION)
   /* Timestamp the end-of-receive so the {At} FDT can be measured against the
    * true RF event (not the software poll point). ~a few us after the physical
    * RXE due to the SPI register read above; a constant offset. */
   if( (irqStatus & ST25R3916_IRQ_MASK_RXE) != 0U )
   {
       g_m1_rxe_cyc  = DWT->CYCCNT;
       g_m1_rxe_seen = 1U;
   }
#endif

#if defined(M1_MFC_AT_RX_DIAG)
   /* Latch the reader's {At}-window RF events (read-only observation). */
   if( (irqStatus & ST25R3916_IRQ_MASK_RXS) != 0U ) { g_m1_rdr_rxs = 1U; }
   if( (irqStatus & ST25R3916_IRQ_MASK_RXE) != 0U ) { g_m1_rdr_rxe = 1U; }
   if( (irqStatus & ST25R3916_IRQ_MASK_NRE) != 0U ) { g_m1_rdr_nre = 1U; }
#endif

   /* Forward all interrupts, even masked ones to application */
   platformProtectST25RIrqStatus();
   st25r3916interrupt.status |= irqStatus;
   platformUnprotectST25RIrqStatus();
   
   /* Send an IRQ event to LED handling */
   st25r3916ledEvtIrq( st25r3916interrupt.status );
}


/*******************************************************************************/
void st25r3916ModifyInterrupts(uint32_t clr_mask, uint32_t set_mask)
{
    uint8_t  i;
    uint32_t old_mask;
    uint32_t new_mask;
    

    old_mask = st25r3916interrupt.mask;
    new_mask = ((~old_mask & set_mask) | (old_mask & clr_mask));
    st25r3916interrupt.mask &= ~clr_mask;
    st25r3916interrupt.mask |= set_mask;
    
    for(i=0; i<ST25R3916_INT_REGS_LEN; i++)
    { 
        if( ((new_mask >> (8U*i)) & 0xFFU) == 0U )
        {
            continue;
        }
        
        st25r3916WriteRegister(ST25R3916_REG_IRQ_MASK_MAIN + i, (uint8_t)((st25r3916interrupt.mask>>(8U*i)) & 0xFFU) );
    }
    return;
}


/*******************************************************************************/
uint32_t st25r3916WaitForInterruptsTimed( uint32_t mask, uint16_t tmo )
{
    uint32_t tmrDelay;
    uint32_t status;
    
    tmrDelay = platformTimerCreate( tmo );
    
    /* Run until specific interrupt has happen or the timer has expired */
    do 
    {
        status = (st25r3916interrupt.status & mask);
    } while( ( (!platformTimerIsExpired( tmrDelay )) || (tmo == 0U)) && (status == 0U) );
    
    platformTimerDestroy( tmrDelay );

    status = st25r3916interrupt.status & mask;
    
    platformProtectST25RIrqStatus();
    st25r3916interrupt.status &= ~status;
    platformUnprotectST25RIrqStatus();
    
    return status;
}


/*******************************************************************************/
uint32_t st25r3916GetInterrupt( uint32_t mask )
{
    uint32_t irqs;

    irqs = (st25r3916interrupt.status & mask);
    if(irqs != ST25R3916_IRQ_MASK_NONE)
    {
        platformProtectST25RIrqStatus();
        st25r3916interrupt.status &= ~irqs;
        platformUnprotectST25RIrqStatus();
    }

    return irqs;
}


/*******************************************************************************/
void st25r3916ClearAndEnableInterrupts( uint32_t mask )
{
    st25r3916GetInterrupt( mask );
    st25r3916EnableInterrupts( mask );
}


/*******************************************************************************/
void st25r3916EnableInterrupts(uint32_t mask)
{
    st25r3916ModifyInterrupts(mask, 0);
}


/*******************************************************************************/
void st25r3916DisableInterrupts(uint32_t mask)
{
    st25r3916ModifyInterrupts(0, mask);
}

/*******************************************************************************/
void st25r3916ClearInterrupts( void )
{
    uint8_t iregs[ST25R3916_INT_REGS_LEN];

    st25r3916ReadMultipleRegisters(ST25R3916_REG_IRQ_MAIN, iregs, ST25R3916_INT_REGS_LEN);

    platformProtectST25RIrqStatus();
    st25r3916interrupt.status = ST25R3916_IRQ_MASK_NONE;
    platformUnprotectST25RIrqStatus();
    return;
}

/*******************************************************************************/
void st25r3916IRQCallbackSet( void (*cb)(void) )
{
    st25r3916interrupt.prevCallback = st25r3916interrupt.callback;
    st25r3916interrupt.callback     = cb;
}

/*******************************************************************************/
void st25r3916IRQCallbackRestore( void )
{
    st25r3916interrupt.callback     = st25r3916interrupt.prevCallback;
    st25r3916interrupt.prevCallback = NULL;
}

