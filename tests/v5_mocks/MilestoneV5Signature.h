#pragma once
// Deterministic test doubles, NOT cryptography. These exercise the production
// receiver's transaction ordering; signing-tool tests exercise real signatures.
#include <MilestoneV5Manifest.h>
#include <MilestoneV5Protocol.h>
#include <string.h>
struct mbedtls_sha256_context {uint32_t value;};
inline void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=2166136261U;}
inline void mbedtls_sha256_free(mbedtls_sha256_context *){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context *c,int){c->value=2166136261U;return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context *c,const uint8_t *p,size_t n){while(n--){c->value^=*p++;c->value*=16777619U;}return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *c,uint8_t *out){for(unsigned i=0;i<32;++i)out[i]=c->value>>((i%4)*8);return 0;}
inline int mbedtls_sha256(const uint8_t *p,size_t n,uint8_t *out,int){mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_update(&c,p,n);return mbedtls_sha256_finish(&c,out);}
namespace MilestoneV5 {
inline bool verifyImageSignature(const uint8_t *,size_t,const uint8_t *signature,size_t n){return n==1&&signature[0]==42;}
}
