#pragma once
#include <MilestoneV5Manifest.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <string.h>
#ifndef MILESTONE_V5_RELEASE_PUBLIC_KEY
#define MILESTONE_V5_RELEASE_PUBLIC_KEY ""
#endif
namespace MilestoneV5 {
inline bool verifyImageSignature(const uint8_t *message, size_t length,
                                 const uint8_t *signature,
                                 size_t signatureLength) {
  const char *pem = MILESTONE_V5_RELEASE_PUBLIC_KEY;
  if (!pem[0] || !message || !length || length > 255 || !signature ||
      !signatureLength || signatureLength > 512)
    return false;
  uint8_t digest[32];
  if (mbedtls_sha256(message, length, digest, 0))
    return false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  bool valid =
      mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t *>(pem),
                                  strlen(pem) + 1) == 0 &&
      mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                        signature, signatureLength) == 0;
  mbedtls_pk_free(&key);
  return valid;
}
} // namespace MilestoneV5
