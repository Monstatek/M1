/* See COPYING.txt for license details. */

/*
*
*  m1_crypto.c
*
*  Software AES-256-CBC encryption with PKCS7 padding.
*  Device-bound key derived from STM32H5 96-bit UID.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "m1_crypto.h"
#include "stm32h5xx_hal.h"

/*************************** D E F I N E S ************************************/

/* STM32H5 Unique ID registers (96-bit UID at 0x08FFF800) */
#define UID_WORD0  (*(volatile uint32_t *)(0x08FFF800))
#define UID_WORD1  (*(volatile uint32_t *)(0x08FFF804))
#define UID_WORD2  (*(volatile uint32_t *)(0x08FFF808))

/* AES constants */
#define AES_BLOCK_SIZE   16
#define AES_KEY_SIZE     32  /* 256-bit */
#define AES_ROUNDS       14  /* 14 rounds for AES-256 */
#define AES_KEY_WORDS    8   /* 32 bytes / 4 */
#define AES_ROUND_KEYS   60  /* 4 * (AES_ROUNDS + 1) */

/* Key derivation magic constant */
#define KEY_DERIVE_MAGIC  0x4D314B45594D4147ULL  /* "M1KEYMAG" in ASCII */

/* PRNG state for IV generation */
#define IV_PRNG_MAGIC     0x4956474E52414E44ULL  /* "IVGNRAND" in ASCII */

//************************** S T R U C T U R E S *******************************

typedef struct {
	uint32_t round_key[AES_ROUND_KEYS];
} aes256_ctx_t;

//****************************** V A R I A B L E S *****************************/

/* Static PRNG counter for IV uniqueness (monotonically increasing) */
static uint32_t s_iv_counter = 0;

/* Rijndael S-box */
static const uint8_t s_sbox[256] = {
	0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5,
	0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
	0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
	0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
	0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC,
	0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
	0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A,
	0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
	0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
	0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
	0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B,
	0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
	0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85,
	0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
	0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
	0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
	0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17,
	0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
	0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88,
	0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
	0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
	0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
	0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9,
	0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
	0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6,
	0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
	0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
	0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
	0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94,
	0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
	0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68,
	0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

/* Inverse S-box for decryption */
static const uint8_t s_inv_sbox[256] = {
	0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38,
	0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
	0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87,
	0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
	0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D,
	0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
	0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2,
	0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
	0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
	0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA,
	0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
	0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A,
	0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
	0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02,
	0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
	0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA,
	0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
	0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85,
	0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
	0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89,
	0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
	0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20,
	0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
	0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31,
	0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
	0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D,
	0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
	0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0,
	0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26,
	0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
};

/* Round constants for key expansion */
static const uint8_t s_rcon[11] = {
	0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void aes256_key_expansion(aes256_ctx_t *ctx, const uint8_t key[AES_KEY_SIZE]);
static void aes256_encrypt_block(const aes256_ctx_t *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE]);
static void aes256_decrypt_block(const aes256_ctx_t *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE]);

static void sub_bytes(uint8_t state[AES_BLOCK_SIZE]);
static void inv_sub_bytes(uint8_t state[AES_BLOCK_SIZE]);
static void shift_rows(uint8_t state[AES_BLOCK_SIZE]);
static void inv_shift_rows(uint8_t state[AES_BLOCK_SIZE]);
static void mix_columns(uint8_t state[AES_BLOCK_SIZE]);
static void inv_mix_columns(uint8_t state[AES_BLOCK_SIZE]);
static void add_round_key(uint8_t state[AES_BLOCK_SIZE], const uint32_t *rk);

static uint8_t gf_mul(uint8_t a, uint8_t b);
static uint32_t sub_word(uint32_t w);
static uint32_t rot_word(uint32_t w);
static uint64_t splitmix64(uint64_t *state);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/



/*============================================================================*/
/*
 * GF(2^8) multiplication used in MixColumns
 */
/*============================================================================*/
static uint8_t gf_mul(uint8_t a, uint8_t b)
{
	uint8_t result = 0;
	uint8_t hi_bit;
	uint8_t i;

	for (i = 0; i < 8; i++)
	{
		if (b & 1)
			result ^= a;
		hi_bit = a & 0x80;
		a <<= 1;
		if (hi_bit)
			a ^= 0x1B; /* x^8 + x^4 + x^3 + x + 1 (AES irreducible polynomial) */
		b >>= 1;
	}
	return result;
} // static uint8_t gf_mul(...)



/*============================================================================*/
/*
 * Apply S-box substitution to each byte of a 32-bit word
 */
/*============================================================================*/
static uint32_t sub_word(uint32_t w)
{
	return ((uint32_t)s_sbox[(w >> 24) & 0xFF] << 24) |
	       ((uint32_t)s_sbox[(w >> 16) & 0xFF] << 16) |
	       ((uint32_t)s_sbox[(w >> 8) & 0xFF] << 8) |
	       ((uint32_t)s_sbox[w & 0xFF]);
} // static uint32_t sub_word(...)



/*============================================================================*/
/*
 * Rotate a 32-bit word left by 8 bits
 */
/*============================================================================*/
static uint32_t rot_word(uint32_t w)
{
	return (w << 8) | (w >> 24);
} // static uint32_t rot_word(...)



/*============================================================================*/
/*
 * splitmix64 PRNG for key derivation and IV generation.
 * Produces good avalanche from sequential seeds.
 */
/*============================================================================*/
static uint64_t splitmix64(uint64_t *state)
{
	uint64_t z;

	*state += 0x9E3779B97F4A7C15ULL;
	z = *state;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	z = z ^ (z >> 31);
	return z;
} // static uint64_t splitmix64(...)



/*============================================================================*/
/*
 * SubBytes: substitute each byte in state using the S-box
 */
/*============================================================================*/
static void sub_bytes(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t i;
	for (i = 0; i < AES_BLOCK_SIZE; i++)
		state[i] = s_sbox[state[i]];
} // static void sub_bytes(...)



/*============================================================================*/
/*
 * InvSubBytes: substitute each byte in state using the inverse S-box
 */
/*============================================================================*/
static void inv_sub_bytes(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t i;
	for (i = 0; i < AES_BLOCK_SIZE; i++)
		state[i] = s_inv_sbox[state[i]];
} // static void inv_sub_bytes(...)



/*============================================================================*/
/*
 * ShiftRows: cyclically shift rows of the state matrix
 * State is in column-major order: state[row + 4*col]
 */
/*============================================================================*/
static void shift_rows(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t tmp;

	/* Row 1: shift left by 1 */
	tmp = state[1];
	state[1] = state[5];
	state[5] = state[9];
	state[9] = state[13];
	state[13] = tmp;

	/* Row 2: shift left by 2 */
	tmp = state[2];
	state[2] = state[10];
	state[10] = tmp;
	tmp = state[6];
	state[6] = state[14];
	state[14] = tmp;

	/* Row 3: shift left by 3 (= shift right by 1) */
	tmp = state[13 + 2]; /* state[15] */
	/* Corrected: Row 3 shift left by 3 */
	tmp = state[15];
	state[15] = state[11];
	state[11] = state[7];
	state[7] = state[3];
	state[3] = tmp;
} // static void shift_rows(...)



/*============================================================================*/
/*
 * InvShiftRows: inverse cyclical shift of rows
 */
/*============================================================================*/
static void inv_shift_rows(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t tmp;

	/* Row 1: shift right by 1 */
	tmp = state[13];
	state[13] = state[9];
	state[9] = state[5];
	state[5] = state[1];
	state[1] = tmp;

	/* Row 2: shift right by 2 */
	tmp = state[2];
	state[2] = state[10];
	state[10] = tmp;
	tmp = state[6];
	state[6] = state[14];
	state[14] = tmp;

	/* Row 3: shift right by 3 (= shift left by 1) */
	tmp = state[3];
	state[3] = state[7];
	state[7] = state[11];
	state[11] = state[15];
	state[15] = tmp;
} // static void inv_shift_rows(...)



/*============================================================================*/
/*
 * MixColumns: mix columns of the state matrix
 */
/*============================================================================*/
static void mix_columns(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t col;
	uint8_t a[4];

	for (col = 0; col < 4; col++)
	{
		uint8_t base = col * 4;

		a[0] = state[base + 0];
		a[1] = state[base + 1];
		a[2] = state[base + 2];
		a[3] = state[base + 3];

		state[base + 0] = gf_mul(a[0], 2) ^ gf_mul(a[1], 3) ^ a[2] ^ a[3];
		state[base + 1] = a[0] ^ gf_mul(a[1], 2) ^ gf_mul(a[2], 3) ^ a[3];
		state[base + 2] = a[0] ^ a[1] ^ gf_mul(a[2], 2) ^ gf_mul(a[3], 3);
		state[base + 3] = gf_mul(a[0], 3) ^ a[1] ^ a[2] ^ gf_mul(a[3], 2);
	}
} // static void mix_columns(...)



/*============================================================================*/
/*
 * InvMixColumns: inverse column mixing for decryption
 */
/*============================================================================*/
static void inv_mix_columns(uint8_t state[AES_BLOCK_SIZE])
{
	uint8_t col;
	uint8_t a[4];

	for (col = 0; col < 4; col++)
	{
		uint8_t base = col * 4;

		a[0] = state[base + 0];
		a[1] = state[base + 1];
		a[2] = state[base + 2];
		a[3] = state[base + 3];

		state[base + 0] = gf_mul(a[0], 14) ^ gf_mul(a[1], 11) ^ gf_mul(a[2], 13) ^ gf_mul(a[3], 9);
		state[base + 1] = gf_mul(a[0], 9)  ^ gf_mul(a[1], 14) ^ gf_mul(a[2], 11) ^ gf_mul(a[3], 13);
		state[base + 2] = gf_mul(a[0], 13) ^ gf_mul(a[1], 9)  ^ gf_mul(a[2], 14) ^ gf_mul(a[3], 11);
		state[base + 3] = gf_mul(a[0], 11) ^ gf_mul(a[1], 13) ^ gf_mul(a[2], 9)  ^ gf_mul(a[3], 14);
	}
} // static void inv_mix_columns(...)



/*============================================================================*/
/*
 * AddRoundKey: XOR state with round key
 */
/*============================================================================*/
static void add_round_key(uint8_t state[AES_BLOCK_SIZE], const uint32_t *rk)
{
	uint8_t col;

	for (col = 0; col < 4; col++)
	{
		uint8_t base = col * 4;
		uint32_t w = rk[col];

		state[base + 0] ^= (uint8_t)(w >> 24);
		state[base + 1] ^= (uint8_t)(w >> 16);
		state[base + 2] ^= (uint8_t)(w >> 8);
		state[base + 3] ^= (uint8_t)(w);
	}
} // static void add_round_key(...)



/*============================================================================*/
/*
 * AES-256 key expansion
 * Expands a 256-bit (32-byte) key into 60 round key words
 */
/*============================================================================*/
static void aes256_key_expansion(aes256_ctx_t *ctx, const uint8_t key[AES_KEY_SIZE])
{
	uint32_t temp;
	uint8_t i;

	/* First 8 words are the original key */
	for (i = 0; i < AES_KEY_WORDS; i++)
	{
		ctx->round_key[i] = ((uint32_t)key[4 * i] << 24) |
		                     ((uint32_t)key[4 * i + 1] << 16) |
		                     ((uint32_t)key[4 * i + 2] << 8) |
		                     ((uint32_t)key[4 * i + 3]);
	}

	/* Expand remaining words */
	for (i = AES_KEY_WORDS; i < AES_ROUND_KEYS; i++)
	{
		temp = ctx->round_key[i - 1];

		if (i % AES_KEY_WORDS == 0)
		{
			temp = sub_word(rot_word(temp)) ^ ((uint32_t)s_rcon[i / AES_KEY_WORDS] << 24);
		}
		else if (i % AES_KEY_WORDS == 4)
		{
			temp = sub_word(temp);
		}

		ctx->round_key[i] = ctx->round_key[i - AES_KEY_WORDS] ^ temp;
	}
} // static void aes256_key_expansion(...)



/*============================================================================*/
/*
 * Encrypt a single 16-byte block with AES-256
 */
/*============================================================================*/
static void aes256_encrypt_block(const aes256_ctx_t *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE])
{
	uint8_t state[AES_BLOCK_SIZE];
	uint8_t round;

	memcpy(state, in, AES_BLOCK_SIZE);

	/* Initial round key addition */
	add_round_key(state, &ctx->round_key[0]);

	/* Rounds 1 through 13 */
	for (round = 1; round < AES_ROUNDS; round++)
	{
		sub_bytes(state);
		shift_rows(state);
		mix_columns(state);
		add_round_key(state, &ctx->round_key[round * 4]);
	}

	/* Final round (no MixColumns) */
	sub_bytes(state);
	shift_rows(state);
	add_round_key(state, &ctx->round_key[AES_ROUNDS * 4]);

	memcpy(out, state, AES_BLOCK_SIZE);
} // static void aes256_encrypt_block(...)



/*============================================================================*/
/*
 * Decrypt a single 16-byte block with AES-256
 */
/*============================================================================*/
static void aes256_decrypt_block(const aes256_ctx_t *ctx, const uint8_t in[AES_BLOCK_SIZE], uint8_t out[AES_BLOCK_SIZE])
{
	uint8_t state[AES_BLOCK_SIZE];
	uint8_t round;

	memcpy(state, in, AES_BLOCK_SIZE);

	/* Initial round key addition (last round key) */
	add_round_key(state, &ctx->round_key[AES_ROUNDS * 4]);

	/* Rounds 13 down to 1 */
	for (round = AES_ROUNDS - 1; round >= 1; round--)
	{
		inv_shift_rows(state);
		inv_sub_bytes(state);
		add_round_key(state, &ctx->round_key[round * 4]);
		inv_mix_columns(state);
	}

	/* Final inverse round (no InvMixColumns) */
	inv_shift_rows(state);
	inv_sub_bytes(state);
	add_round_key(state, &ctx->round_key[0]);

	memcpy(out, state, AES_BLOCK_SIZE);
} // static void aes256_decrypt_block(...)



/*============================================================================*/
/*
 * Derive a device-specific 256-bit encryption key from STM32H5 UID.
 * Uses splitmix64 to expand the 96-bit UID + magic constant into 32 bytes.
 */
/*============================================================================*/
void m1_crypto_derive_key(uint8_t key[M1_CRYPTO_AES_KEY_SIZE])
{
	uint64_t state;
	uint64_t val;
	uint8_t i;

	/* Combine UID words with magic constant to create seed */
	state = ((uint64_t)UID_WORD0 << 32) | (uint64_t)UID_WORD1;
	state ^= KEY_DERIVE_MAGIC;
	state ^= ((uint64_t)UID_WORD2 << 16);

	/* Generate 32 bytes (4 x 8 bytes) of key material */
	for (i = 0; i < M1_CRYPTO_AES_KEY_SIZE; i += 8)
	{
		val = splitmix64(&state);
		key[i + 0] = (uint8_t)(val >> 56);
		key[i + 1] = (uint8_t)(val >> 48);
		key[i + 2] = (uint8_t)(val >> 40);
		key[i + 3] = (uint8_t)(val >> 32);
		key[i + 4] = (uint8_t)(val >> 24);
		key[i + 5] = (uint8_t)(val >> 16);
		key[i + 6] = (uint8_t)(val >> 8);
		key[i + 7] = (uint8_t)(val);
	}
} // void m1_crypto_derive_key(...)



/*============================================================================*/
/*
 * Generate a random-ish IV using device UID, HAL tick counter,
 * and a monotonically increasing counter for uniqueness.
 * Uses xorshift128+ seeded from these entropy sources.
 */
/*============================================================================*/
void m1_crypto_generate_iv(uint8_t iv[M1_CRYPTO_IV_SIZE])
{
	uint64_t state;
	uint64_t val;
	uint32_t tick;

	/* Increment monotonic counter */
	s_iv_counter++;

	/* Get current tick */
	tick = HAL_GetTick();

	/* Seed from UID + tick + counter + magic */
	state = ((uint64_t)UID_WORD0 << 32) | (uint64_t)UID_WORD2;
	state ^= IV_PRNG_MAGIC;
	state ^= ((uint64_t)tick << 16);
	state ^= ((uint64_t)s_iv_counter << 48);
	state ^= ((uint64_t)UID_WORD1);

	/* Generate 16 bytes of IV */
	val = splitmix64(&state);
	iv[0]  = (uint8_t)(val >> 56);
	iv[1]  = (uint8_t)(val >> 48);
	iv[2]  = (uint8_t)(val >> 40);
	iv[3]  = (uint8_t)(val >> 32);
	iv[4]  = (uint8_t)(val >> 24);
	iv[5]  = (uint8_t)(val >> 16);
	iv[6]  = (uint8_t)(val >> 8);
	iv[7]  = (uint8_t)(val);

	val = splitmix64(&state);
	iv[8]  = (uint8_t)(val >> 56);
	iv[9]  = (uint8_t)(val >> 48);
	iv[10] = (uint8_t)(val >> 40);
	iv[11] = (uint8_t)(val >> 32);
	iv[12] = (uint8_t)(val >> 24);
	iv[13] = (uint8_t)(val >> 16);
	iv[14] = (uint8_t)(val >> 8);
	iv[15] = (uint8_t)(val);
} // void m1_crypto_generate_iv(...)



/*============================================================================*/
/*
 * Encrypt data in-place with AES-256-CBC + PKCS7 padding.
 *
 * Input:  plaintext in buf[0..plaintext_len-1]
 * Output: [IV (16 bytes)] + [ciphertext with PKCS7 padding] in buf
 * buf_size must be >= 16 + ((plaintext_len / 16) + 1) * 16
 *
 * Returns total output length, or 0 on error.
 */
/*============================================================================*/
uint32_t m1_crypto_encrypt(uint8_t *buf, uint32_t plaintext_len, uint32_t buf_size)
{
	aes256_ctx_t ctx;
	uint8_t key[M1_CRYPTO_AES_KEY_SIZE];
	uint8_t iv[M1_CRYPTO_IV_SIZE];
	uint8_t prev_ct[AES_BLOCK_SIZE];
	uint8_t block_in[AES_BLOCK_SIZE];
	uint8_t block_out[AES_BLOCK_SIZE];
	uint32_t padded_len;
	uint32_t total_out;
	uint32_t num_blocks;
	uint32_t i, j;
	uint8_t pad_byte;

	if (buf == NULL || plaintext_len == 0)
		return 0;

	/* Calculate padded length (PKCS7: always adds at least 1 byte of padding) */
	pad_byte = AES_BLOCK_SIZE - (plaintext_len % AES_BLOCK_SIZE);
	padded_len = plaintext_len + pad_byte;
	total_out = M1_CRYPTO_IV_SIZE + padded_len;

	if (total_out > buf_size)
		return 0;

	num_blocks = padded_len / AES_BLOCK_SIZE;

	/* Derive key and generate IV */
	m1_crypto_derive_key(key);
	m1_crypto_generate_iv(iv);

	/* Expand key */
	aes256_key_expansion(&ctx, key);

	/* Move plaintext to make room for IV at the beginning.
	 * We work backwards to avoid overwriting data we still need. */
	/* First, apply PKCS7 padding to the plaintext in its current position */
	for (i = plaintext_len; i < padded_len; i++)
		buf[i] = pad_byte;

	/* Now shift the padded plaintext right by IV_SIZE bytes */
	for (i = padded_len; i > 0; i--)
		buf[i - 1 + M1_CRYPTO_IV_SIZE] = buf[i - 1];

	/* Place IV at the start */
	memcpy(buf, iv, M1_CRYPTO_IV_SIZE);

	/* CBC encrypt in-place (operating on buf + IV_SIZE region) */
	memcpy(prev_ct, iv, AES_BLOCK_SIZE);

	for (i = 0; i < num_blocks; i++)
	{
		uint32_t offset = M1_CRYPTO_IV_SIZE + (i * AES_BLOCK_SIZE);

		/* XOR plaintext block with previous ciphertext (CBC) */
		for (j = 0; j < AES_BLOCK_SIZE; j++)
			block_in[j] = buf[offset + j] ^ prev_ct[j];

		/* Encrypt block */
		aes256_encrypt_block(&ctx, block_in, block_out);

		/* Write ciphertext back */
		memcpy(&buf[offset], block_out, AES_BLOCK_SIZE);
		memcpy(prev_ct, block_out, AES_BLOCK_SIZE);
	}

	/* Clear sensitive data from stack */
	memset(key, 0, sizeof(key));
	memset(&ctx, 0, sizeof(ctx));

	return total_out;
} // uint32_t m1_crypto_encrypt(...)



/*============================================================================*/
/*
 * Decrypt data in-place.
 *
 * Input:  [IV (16 bytes)] + [ciphertext] in buf, total_len bytes
 * Output: plaintext in buf[0..plaintext_len-1]
 *
 * Returns plaintext length (after removing PKCS7 padding), or 0 on error.
 */
/*============================================================================*/
uint32_t m1_crypto_decrypt(uint8_t *buf, uint32_t total_len)
{
	aes256_ctx_t ctx;
	uint8_t key[M1_CRYPTO_AES_KEY_SIZE];
	uint8_t iv[M1_CRYPTO_IV_SIZE];
	uint8_t prev_ct[AES_BLOCK_SIZE];
	uint8_t cur_ct[AES_BLOCK_SIZE];
	uint8_t block_out[AES_BLOCK_SIZE];
	uint32_t ct_len;
	uint32_t num_blocks;
	uint32_t i, j;
	uint8_t pad_byte;
	uint32_t plaintext_len;

	if (buf == NULL || total_len < M1_CRYPTO_IV_SIZE + AES_BLOCK_SIZE)
		return 0;

	ct_len = total_len - M1_CRYPTO_IV_SIZE;

	/* Ciphertext must be a multiple of block size */
	if (ct_len % AES_BLOCK_SIZE != 0)
		return 0;

	num_blocks = ct_len / AES_BLOCK_SIZE;

	/* Extract IV from the beginning */
	memcpy(iv, buf, M1_CRYPTO_IV_SIZE);

	/* Derive key */
	m1_crypto_derive_key(key);

	/* Expand key */
	aes256_key_expansion(&ctx, key);

	/* CBC decrypt */
	memcpy(prev_ct, iv, AES_BLOCK_SIZE);

	for (i = 0; i < num_blocks; i++)
	{
		uint32_t offset = M1_CRYPTO_IV_SIZE + (i * AES_BLOCK_SIZE);

		/* Save current ciphertext block (needed for CBC XOR) */
		memcpy(cur_ct, &buf[offset], AES_BLOCK_SIZE);

		/* Decrypt block */
		aes256_decrypt_block(&ctx, cur_ct, block_out);

		/* XOR with previous ciphertext (CBC) */
		for (j = 0; j < AES_BLOCK_SIZE; j++)
			block_out[j] ^= prev_ct[j];

		/* Write plaintext to the output position (shifted left to overwrite IV area) */
		memcpy(&buf[i * AES_BLOCK_SIZE], block_out, AES_BLOCK_SIZE);

		/* Update previous ciphertext */
		memcpy(prev_ct, cur_ct, AES_BLOCK_SIZE);
	}

	/* Validate and strip PKCS7 padding */
	pad_byte = buf[ct_len - 1];
	if (pad_byte == 0 || pad_byte > AES_BLOCK_SIZE)
	{
		memset(key, 0, sizeof(key));
		memset(&ctx, 0, sizeof(ctx));
		return 0; /* Invalid padding */
	}

	/* Verify all padding bytes */
	for (i = 0; i < pad_byte; i++)
	{
		if (buf[ct_len - 1 - i] != pad_byte)
		{
			memset(key, 0, sizeof(key));
			memset(&ctx, 0, sizeof(ctx));
			return 0; /* Invalid padding */
		}
	}

	plaintext_len = ct_len - pad_byte;

	/* Clear sensitive data from stack */
	memset(key, 0, sizeof(key));
	memset(&ctx, 0, sizeof(ctx));

	return plaintext_len;
} // uint32_t m1_crypto_decrypt(...)
