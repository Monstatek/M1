/* See COPYING.txt for license details. */

/*
*
* m1_crypto.h
*
* AES-256-CBC encryption for credential storage
*
* M1 Project
*
*/

#ifndef M1_CRYPTO_H_
#define M1_CRYPTO_H_

#include <stdint.h>
#include <stdbool.h>

#define M1_CRYPTO_AES_BLOCK_SIZE   16
#define M1_CRYPTO_AES_KEY_SIZE     32   /* 256-bit key */
#define M1_CRYPTO_IV_SIZE          16

/* Derive a device-specific encryption key from STM32H5 96-bit UID */
void m1_crypto_derive_key(uint8_t key[M1_CRYPTO_AES_KEY_SIZE]);

/* Generate a random IV using device UID + tick counter */
void m1_crypto_generate_iv(uint8_t iv[M1_CRYPTO_IV_SIZE]);

/* Encrypt data in-place with AES-256-CBC + PKCS7 padding
 * Input: plaintext in buf, plaintext_len bytes
 * Output: [IV (16 bytes)] + [ciphertext with PKCS7 padding] in buf
 * buf must have room for: 16 + ((plaintext_len / 16) + 1) * 16 bytes
 * Returns total output length, or 0 on error */
uint32_t m1_crypto_encrypt(uint8_t *buf, uint32_t plaintext_len, uint32_t buf_size);

/* Decrypt data in-place
 * Input: [IV (16 bytes)] + [ciphertext] in buf, total_len bytes
 * Output: plaintext in buf (overwriting IV area)
 * Returns plaintext length, or 0 on error */
uint32_t m1_crypto_decrypt(uint8_t *buf, uint32_t total_len);

#endif /* M1_CRYPTO_H_ */
