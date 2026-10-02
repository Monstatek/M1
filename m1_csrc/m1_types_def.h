/* See COPYING.txt for license details. */

/*
*
*  m1_types_def.h
*
*  Custom data types defined for M1
*
* M1 Project
*
*/

#ifndef M1_TYPES_DEF_H_
#define M1_TYPES_DEF_H_

#include <stdint.h>

#ifndef TRUE
#define TRUE	1
#endif // #ifndef TRUE

#ifndef true
#ifdef TRUE
#define true	TRUE
#endif // #ifdef TRUE
#endif // #ifndef true

#ifndef FALSE
#define FALSE 	0
#endif // #ifndef FALSE

#ifndef false
#ifdef FALSE
#define false 	FALSE
#endif // #ifdef FALSE
#endif // #ifndef false

#ifndef BOOL
typedef uint8_t BOOL;
#endif // #ifndef BOOL

#ifndef bool
typedef BOOL bool;
#endif // #ifndef bool

#ifndef U8
typedef uint8_t		U8;
#endif // #ifndef U8

#ifndef U16
typedef uint16_t	U16;
#endif // #ifndef U16

#ifndef u8
typedef uint8_t		u8;
#endif // #ifndef u8

#ifndef u16
typedef uint16_t	u16;
#endif // #ifndef u16

#ifndef u32
typedef uint32_t	u32;
#endif // #ifndef u32

#ifndef u64
typedef uint64_t	u64;
#endif // #ifndef u64

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

/*
 * For ESP32 based code only!
struct spinlock {
    atomic_int locked;
};
typedef struct spinlock_t portMUX_TYPE;
 */

#endif /* M1_TYPES_DEF_H_ */
