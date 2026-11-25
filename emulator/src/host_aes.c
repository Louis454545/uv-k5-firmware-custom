#include <openssl/aes.h>
#include <string.h>

#include "driver/aes.h"

void AES_Encrypt(const void *pKey, const void *pIv, const void *pIn, void *pOut, uint8_t NumBlocks)
{
	AES_KEY key;
	uint8_t iv[16];
	uint8_t inblock[16];

	AES_set_encrypt_key((const unsigned char *)pKey, 128, &key);
	memcpy(iv, pIv, 16);

	for (uint8_t block = 0; block < NumBlocks; block++) {
		const uint8_t *in = (const uint8_t *)pIn + block * 16;
		uint8_t *out = (uint8_t *)pOut + block * 16;
		for (unsigned i = 0; i < 16; i++)
			inblock[i] = in[i] ^ iv[i];
		AES_encrypt(inblock, out, &key);
		memcpy(iv, out, 16);
	}
}
