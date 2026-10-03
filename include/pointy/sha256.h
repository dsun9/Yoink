#ifndef POINTY_SHA256_H
#define POINTY_SHA256_H
#include <stddef.h>
#include <stdint.h>
/* FIPS 180-4 SHA-256 digest of the borrowed byte buffer. */
void pp_sha256(const uint8_t *data,size_t size,uint8_t digest[32]);
#endif
