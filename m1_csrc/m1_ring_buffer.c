/* See COPYING.txt for license details. */

/*
*
* m1_ring_buffer.c
*
* Library for ring buffer
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ring_buffer.h"

/*************************** D E F I N E S ************************************/

#define GET_MIN_NUM(m, n)                   ((m) < (n) ? (m) : (n))
#define GET_MAX_NUM(m, n)					((m) > (n) ? (m) : (n))
#define IS_BUFFER_VALID(pbuffer)            ((pbuffer!=NULL) && (pbuffer->pdata!=NULL) && (pbuffer->len > 0))

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************


/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_ringbuffer_init(S_M1_RingBuffer *prb_handle, uint8_t *ring_buffer, uint16_t n_elements, uint8_t data_size);
void m1_ringbuffer_reset(S_M1_RingBuffer *prb_handle);
uint16_t m1_ringbuffer_write(S_M1_RingBuffer *prb_handle, uint8_t *indata, uint16_t n_slots);
uint16_t m1_ringbuffer_insert(S_M1_RingBuffer *prb_handle, uint8_t *indata);
uint16_t m1_ringbuffer_read(S_M1_RingBuffer *prb_handle, uint8_t *outdata, uint16_t n_slots);
uint8_t m1_ringbuffer_check_empty_state(S_M1_RingBuffer *prb_handle);
uint32_t ringbuffer_get_empty_slots(S_M1_RingBuffer *prb_handle);
uint32_t ringbuffer_get_data_slots(S_M1_RingBuffer *prb_handle);
uint16_t m1_ringbuffer_advance_read(S_M1_RingBuffer *prb_handle, uint16_t n_slots);
uint16_t m1_ringbuffer_get_read_len(S_M1_RingBuffer *prb_handle);
uint8_t *m1_ringbuffer_get_read_address(S_M1_RingBuffer *prb_handle);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/*
 * This functions initializes the ring buffer
 */
/******************************************************************************/
void m1_ringbuffer_init(S_M1_RingBuffer *prb_handle, uint8_t *ring_buffer, uint16_t n_elements, uint8_t data_size)
{
	assert(prb_handle != NULL);
	assert(ring_buffer != NULL);
	assert(data_size != 0);
    prb_handle->pdata = ring_buffer;
    prb_handle->len = n_elements;
    prb_handle->data_size = data_size;
    prb_handle->end_index = prb_handle->len*prb_handle->data_size;
    prb_handle->head = 0;
    prb_handle->tail = 0;

} // void m1_ringbuffer_init(S_M1_RingBuffer *prb_handle, uint8_t *ring_buffer, uint16_t n_elements, uint8_t data_size)



/******************************************************************************/
/*
 * This functions reset the ring buffer
 */
/******************************************************************************/
void m1_ringbuffer_reset(S_M1_RingBuffer *prb_handle)
{
	assert(prb_handle != NULL);

	taskENTER_CRITICAL();
    prb_handle->head = 0;
    prb_handle->tail = 0;
    taskEXIT_CRITICAL();
} // void m1_ringbuffer_reset(S_M1_RingBuffer *prb_handle)


/*============================================================================*/
/*
 * This function returns the empty slots in the ring buffer
 *
 */
/*============================================================================*/
uint32_t ringbuffer_get_empty_slots(S_M1_RingBuffer *prb_handle)
{
    uint32_t n_free, head, tail;

    head = prb_handle->head;
    tail = prb_handle->tail;

    n_free = prb_handle->end_index; // Default, the buffer is empty
    if (head > tail)
    	n_free -= (head - tail);
    else if (tail > head) // Overflow with head index?
        n_free = tail - head;

    // Return the number of empty slots minus 1
    // This is to avoid the case when head and tail have the same index if the buffer is full.
    // In that case, it will make this function understand that the buffer is empty, instead of being full!
    return (n_free/prb_handle->data_size - 1);
} // uint32_t ringbuffer_get_empty_slots(S_M1_RingBuffer *prb_handle)



/*============================================================================*/
/*
 * This function returns the data slots available in the ring buffer
 *
 */
/*============================================================================*/
uint32_t ringbuffer_get_data_slots(S_M1_RingBuffer *prb_handle)
{
    uint32_t head, tail, n_slots;

    if ( !IS_BUFFER_VALID(prb_handle) )
    	return 0;

    head = prb_handle->head;
    tail = prb_handle->tail;

    n_slots = 0; // Default, the buffer is fully empty
    if (head > tail)
    {
    	n_slots = head - tail;
    }
    else if (tail > head)
    {
        n_slots = prb_handle->end_index - (tail - head); // overflow case
    }

    return n_slots/prb_handle->data_size;
} // static uint32_t ringbuffer_get_data_slots(S_M1_RingBuffer *prb_handle)



/*============================================================================*/
/**
 *
 * This function copies data from input data buffer to ring buffer
 *
 */
/*============================================================================*/
uint16_t m1_ringbuffer_write(S_M1_RingBuffer *prb_handle, uint8_t *indata, uint16_t n_slots)
{
    uint32_t n_copy, n_free;
    uint8_t data_size;

    if ( !IS_BUFFER_VALID(prb_handle) )
    	return 0;

    if ( indata==NULL )
    	return 0;

    if ( n_slots==0 )
    	return 0;

    n_free = ringbuffer_get_empty_slots(prb_handle);
    if ( n_free==0 ) // No empty space in the buffer?
    	return 0;

    data_size = prb_handle->data_size;

    n_slots = GET_MIN_NUM(n_free, n_slots); // Update the maximum number of slots that can be written to the buffer
    n_copy = prb_handle->end_index - prb_handle->head; // Get empty slots from current head index to the end of the buffer
    n_copy /= data_size;
    n_copy = GET_MIN_NUM(n_copy, n_slots); // Get maximum slots that can be written to the buffer from the head index
    memcpy(&prb_handle->pdata[prb_handle->head], indata, n_copy*data_size); // Copy to the buffer

    prb_handle->head += n_copy*data_size; // Update the head index
    n_slots -= n_copy; // Update the remainder
    if (prb_handle->head >= prb_handle->end_index) // Head index got an overflow?
    {
        prb_handle->head = 0;
    }

    if (n_slots > 0) // Remainder needs to be copied?
    {
        memcpy(prb_handle->pdata, &indata[n_copy*data_size], n_slots*data_size); // Copy the remainder to the start of the buffer
        prb_handle->head = n_slots*data_size; // Update the head index
    }

    return (n_copy + n_slots);
} // uint16_t m1_ringbuffer_write(S_M1_RingBuffer *prb_handle, uint8_t *indata, uint16_t n_slots)



/*============================================================================*/
/**
 *
 * This function copies one data item from input data buffer to ring buffer
 * If the buffer is full, it will remove the oldest item and add the new item.
 *
 */
/*============================================================================*/
uint16_t m1_ringbuffer_insert(S_M1_RingBuffer *prb_handle, uint8_t *indata)
{
    uint16_t n_free;

    if ( !IS_BUFFER_VALID(prb_handle) )  /* route2: re-enabled — NULL/invalid buffer must fail safely */
    	return 0;

    if ( indata==NULL )
    	return 0;
    n_free = ringbuffer_get_empty_slots(prb_handle);
    if ( n_free==0 ) // No empty space in the buffer?
    {
        prb_handle->tail += prb_handle->data_size; // Update the read index
        if (prb_handle->tail >= prb_handle->end_index) // If overflow occurs, update the read index again
        	prb_handle->tail = 0;
    }

    memcpy(&prb_handle->pdata[prb_handle->head], indata, prb_handle->data_size); // Copy to the buffer
    prb_handle->head += prb_handle->data_size; // Update the head index
    if (prb_handle->head >= prb_handle->end_index) // Head index got an overflow?
    {
        prb_handle->head = 0;
    }

    return 1;
} // uint16_t m1_ringbuffer_insert(S_M1_RingBuffer *prb_handle, uint8_t *indata)


/*============================================================================*/
/**
 *
 * This function reads data from the ring buffer to the output buffer
 *
 */
/*============================================================================*/
uint16_t m1_ringbuffer_read(S_M1_RingBuffer *prb_handle, uint8_t *outdata, uint16_t n_slots)
{
	uint32_t n_read, n_linear;
	uint8_t *padd_r;

	if ( !IS_BUFFER_VALID(prb_handle) )
    	return 0;

	assert(outdata != NULL);

    do {
        // Get the number of data slots available to read
        n_read =  ringbuffer_get_data_slots(prb_handle);
        n_read = GET_MIN_NUM(n_slots, n_read);

        if ( !n_read )
        	break;

        n_linear = m1_ringbuffer_get_read_len(prb_handle);
        padd_r = m1_ringbuffer_get_read_address(prb_handle); // Get read address
        if ( n_linear >= n_read ) // Linear data block is more than enough to complete in one read?
        {
        	memcpy(outdata, padd_r, n_read*prb_handle->data_size);
        	m1_ringbuffer_advance_read(prb_handle, n_read); // Update read index
        	break;
        } // if ( n_linear >= n_read )
        memcpy(outdata, padd_r, n_linear*prb_handle->data_size); // Read first linear part
        m1_ringbuffer_advance_read(prb_handle, n_linear); // Update read index
        padd_r = m1_ringbuffer_get_read_address(prb_handle); // Get new read address
        /* n_linear is an ELEMENT count (m1_ringbuffer_get_read_len() returns
         * n_slots/data_size); outdata is uint8_t*, so the destination for
         * the post-wrap part must be offset by n_linear*data_size BYTES,
         * not n_linear bytes. For data_size==1 these are numerically
         * identical (which is why this was invisible on single-byte ring
         * buffers); for data_size>1 (e.g. Sub-GHz raw sample capture,
         * data_size==2) this placed the second part n_linear*(data_size-1)
         * bytes too early, corrupting bytes the first memcpy already wrote
         * and leaving the true tail of the caller's buffer stale. */
        memcpy(&outdata[(size_t)n_linear * prb_handle->data_size], padd_r,
               (n_read - n_linear)*prb_handle->data_size); // Read second linear part
        m1_ringbuffer_advance_read(prb_handle, n_read - n_linear); // Update read index
    } while(0);

    return n_read;
} // uint16_t m1_ringbuffer_read(S_M1_RingBuffer *prb_handle, uint8_t *outdata, uint16_t n_slots)



/*============================================================================*/
/*
 *
 * This function advances the buffer read index by n_slots
 *
 */
/*============================================================================*/
uint16_t m1_ringbuffer_advance_read(S_M1_RingBuffer *prb_handle, uint16_t n_slots)
{
    uint32_t n_avail;

    if ( !IS_BUFFER_VALID(prb_handle) )
    	return 0;

    if ( n_slots==0 )
    	return 0;

    n_avail = ringbuffer_get_data_slots(prb_handle); // Get available data slots in the buffer
    n_slots = GET_MIN_NUM(n_slots, n_avail); // Get the maximum number of slots that can be advanced for the read index

    prb_handle->tail += n_slots*prb_handle->data_size; // Update the read index
    if (prb_handle->tail >= prb_handle->end_index) // If overflow occurs, update the read index again
    	prb_handle->tail -= prb_handle->end_index;

    return n_slots;
} // uint16_t m1_ringbuffer_advance_read(S_M1_RingBuffer *prb_handle, uint16_t n_slots)



/*============================================================================*/
/**
 *	This function returns the maximum number of data slots starting from the
 *	current read index to the end of the buffer.
 *  This is a linear block of data slots that can be read.
 */
/*============================================================================*/
uint16_t m1_ringbuffer_get_read_len(S_M1_RingBuffer *prb_handle)
{
    uint32_t head, tail, n_slots;

    if ( !IS_BUFFER_VALID(prb_handle) )
    	return 0;

    head = prb_handle->head;
    tail = prb_handle->tail;

    n_slots = 0; // Default, no data to read.
    if (head > tail)
    {
    	n_slots = head - tail; // Read from head to tail
    }
    else if (tail > head)
    {
    	n_slots = prb_handle->end_index - tail; // Read from tail to end of buffer
    }

    return n_slots/prb_handle->data_size;
} // uint16_t m1_ringbuffer_get_read_len(S_M1_RingBuffer *prb_handle)



/*============================================================================*/
/**
 *	This function returns the address of the linear block of the buffer
 *	starting from the read index.
 *	This address will be used by the DMA to transmit data to UART.
 *
 */
/*============================================================================*/
uint8_t *m1_ringbuffer_get_read_address(S_M1_RingBuffer *prb_handle)
{
	if ( !IS_BUFFER_VALID(prb_handle) )
		return NULL;

	return (prb_handle->pdata + prb_handle->tail);
} // uint8_t *m1_ringbuffer_get_read_address(S_M1_RingBuffer *prb_handle)




/*============================================================================*/
/*
 * This function returns true if the ring buffer is empty
 *
 */
/*============================================================================*/
uint8_t m1_ringbuffer_check_empty_state(S_M1_RingBuffer *prb_handle)
{
	uint32_t head, tail;

	head = prb_handle->head;
	tail = prb_handle->tail;

	if ( head==tail ) // buffer is empty?
		return 1;

	return 0;
} // uint8_t m1_ringbuffer_check_empty_state(S_M1_RingBuffer *prb_handle)
