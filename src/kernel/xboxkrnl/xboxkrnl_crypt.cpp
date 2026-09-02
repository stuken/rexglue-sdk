/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2022 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*
* @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
*/

// Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <tuple>

#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

#if REX_PLATFORM_WIN32
#include <windows.h>

#include <bcrypt.h>
#endif

#include "crypto/TinySHA1.hpp"
#include "crypto/des/des.cpp"
#include "crypto/des/des.h"
#include "crypto/des/des3.h"
#include "crypto/des/descbc.h"
#include "crypto/sha256.cpp"
#include "crypto/sha256.h"

extern "C" {
#include "aes_128/aes.h"
#include "FFmpeg/libavutil/md5.h"
#include "FFmpeg/libavutil/mem.h"
#include "FFmpeg/libavutil/sha512.h"
}

namespace rex::kernel::xboxkrnl {

typedef struct {
  uint8_t S[256];  // 0x0
  uint8_t i;       // 0x100
  uint8_t j;       // 0x101
} XECRYPT_RC4_STATE;
static_assert_size(XECRYPT_RC4_STATE, 0x102);

void XeCryptRc4Key_entry(ppc_ptr_t<XECRYPT_RC4_STATE> rc4_ctx, mapped_void key, u32 key_size) {
  // Setup RC4 state
  rc4_ctx->i = rc4_ctx->j = 0;
  for (uint32_t x = 0; x < 0x100; x++) {
    rc4_ctx->S[x] = (uint8_t)x;
  }

  uint32_t idx = 0;
  for (uint32_t x = 0; x < 0x100; x++) {
    idx = (idx + rc4_ctx->S[x] + key[x % 0x10]) % 0x100;
    uint8_t temp = rc4_ctx->S[idx];
    rc4_ctx->S[idx] = rc4_ctx->S[x];
    rc4_ctx->S[x] = temp;
  }
}

void XeCryptRc4Ecb_entry(ppc_ptr_t<XECRYPT_RC4_STATE> rc4_ctx, mapped_void data, u32 size) {
  // Crypt data
  for (uint32_t idx = 0; idx < size; idx++) {
    rc4_ctx->i = (rc4_ctx->i + 1) % 0x100;
    rc4_ctx->j = (rc4_ctx->j + rc4_ctx->S[rc4_ctx->i]) % 0x100;
    uint8_t temp = rc4_ctx->S[rc4_ctx->i];
    rc4_ctx->S[rc4_ctx->i] = rc4_ctx->S[rc4_ctx->j];
    rc4_ctx->S[rc4_ctx->j] = temp;

    uint8_t a = data[idx];
    uint8_t b = rc4_ctx->S[(rc4_ctx->S[rc4_ctx->i] + rc4_ctx->S[rc4_ctx->j]) % 0x100];
    data[idx] = (uint8_t)(a ^ b);
  }
}

void XeCryptRc4_entry(mapped_void key, u32 key_size, mapped_void data, u32 size) {
  XECRYPT_RC4_STATE rc4_ctx;
  XeCryptRc4Key_entry(ppc_ptr_t<XECRYPT_RC4_STATE>::from_host(&rc4_ctx), key, key_size);
  XeCryptRc4Ecb_entry(ppc_ptr_t<XECRYPT_RC4_STATE>::from_host(&rc4_ctx), data, size);
}

typedef struct {
  rex::be<uint32_t> count;     // 0x0
  rex::be<uint32_t> state[5];  // 0x4
  uint8_t buffer[64];          // 0x18
} XECRYPT_SHA_STATE;
static_assert_size(XECRYPT_SHA_STATE, 0x58);

void InitSha1(sha1::SHA1* sha, const XECRYPT_SHA_STATE* state) {
  uint32_t digest[5];
  std::copy(std::begin(state->state), std::end(state->state), digest);

  sha->init(digest, state->buffer, state->count);
}

void StoreSha1(const sha1::SHA1* sha, XECRYPT_SHA_STATE* state) {
  std::copy_n(sha->getDigest(), rex::countof(state->state), state->state);

  state->count = static_cast<uint32_t>(sha->getByteCount());
  std::copy_n(sha->getBlock(), sha->getBlockByteIndex(), state->buffer);
}

void XeCryptShaInit_entry(ppc_ptr_t<XECRYPT_SHA_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x67452301;
  sha_state->state[1] = 0xEFCDAB89;
  sha_state->state[2] = 0x98BADCFE;
  sha_state->state[3] = 0x10325476;
  sha_state->state[4] = 0xC3D2E1F0;
}

void XeCryptShaUpdate_entry(ppc_ptr_t<XECRYPT_SHA_STATE> sha_state, mapped_void input,
                            u32 input_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  sha.processBytes(input, input_size);

  StoreSha1(&sha, sha_state);
}

void XeCryptShaFinal_entry(ppc_ptr_t<XECRYPT_SHA_STATE> sha_state, ppc_ptr_t<uint8_t> out,
                           u32 out_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  uint8_t digest[0x14];
  sha.finalize(digest);

  std::copy_n(digest, std::min<size_t>(rex::countof(digest), out_size), static_cast<uint8_t*>(out));
  std::copy_n(sha.getDigest(), rex::countof(sha_state->state), sha_state->state);
}

void XeCryptSha_entry(mapped_void input_1, u32 input_1_size, mapped_void input_2, u32 input_2_size,
                      mapped_void input_3, u32 input_3_size, mapped_void output, u32 output_size) {
  sha1::SHA1 sha;

  if (input_1 && input_1_size) {
    sha.processBytes(input_1, input_1_size);
  }
  if (input_2 && input_2_size) {
    sha.processBytes(input_2, input_2_size);
  }
  if (input_3 && input_3_size) {
    sha.processBytes(input_3, input_3_size);
  }

  uint8_t digest[0x14];
  sha.finalize(digest);
  std::copy_n(digest, std::min<size_t>(rex::countof(digest), output_size), output.as<uint8_t*>());
}

typedef struct {
  XECRYPT_SHA_STATE sha_state[2];
} XECRYPT_HMACSHA_STATE;
static_assert_size(XECRYPT_HMACSHA_STATE, 0xB0);

void XeCryptHmacShaInit_entry(ppc_ptr_t<XECRYPT_HMACSHA_STATE> sha_state_ptr, mapped_void key_ptr,
                              u32 key_size) {
  sha_state_ptr.Zero();

  const uint8_t* key_data_ptr = key_ptr.as<const uint8_t*>();
  const uint32_t key_size_ = key_size > 64 ? 64 : key_size;

  auto sha_state_0 = ppc_ptr_t<XECRYPT_SHA_STATE>::from_host(&sha_state_ptr->sha_state[0]);
  auto sha_state_1 = ppc_ptr_t<XECRYPT_SHA_STATE>::from_host(&sha_state_ptr->sha_state[1]);

  XeCryptShaInit_entry(sha_state_0);
  XeCryptShaInit_entry(sha_state_1);

  std::array<uint8_t, 64> key_1 = {};
  std::array<uint8_t, 64> key_2 = {};

  std::copy_n(key_data_ptr, key_size_, key_1.begin());
  std::copy_n(key_data_ptr, key_size_, key_2.begin());

  for (uint32_t i = 0; i < key_size_; i += 1) {
    key_1[i] ^= 0x5C;
    key_2[i] ^= 0x36;
  }

  XeCryptShaUpdate_entry(sha_state_0, mapped_void::from_host(key_2.data()), key_size_);
  XeCryptShaUpdate_entry(sha_state_1, mapped_void::from_host(key_1.data()), key_size_);
}

void XeCryptHmacShaUpdate_entry(ppc_ptr_t<XECRYPT_HMACSHA_STATE> sha_state_ptr, mapped_void input,
                                u32 input_size) {
  auto sha_state_0 = ppc_ptr_t<XECRYPT_SHA_STATE>::from_host(&sha_state_ptr->sha_state[0]);
  XeCryptShaUpdate_entry(sha_state_0, input, input_size);
}

void XeCryptHmacShaFinal_entry(ppc_ptr_t<XECRYPT_HMACSHA_STATE> sha_state_ptr, ppc_ptr_t<uint8_t> out,
                               u32 out_size) {
  auto sha_state_0 = ppc_ptr_t<XECRYPT_SHA_STATE>::from_host(&sha_state_ptr->sha_state[0]);
  auto sha_state_1 = ppc_ptr_t<XECRYPT_SHA_STATE>::from_host(&sha_state_ptr->sha_state[1]);

  XeCryptShaFinal_entry(sha_state_0, ppc_ptr_t<uint8_t>(), 0);
  XeCryptShaUpdate_entry(sha_state_1, mapped_void::from_host(sha_state_ptr->sha_state[0].state),
                         sizeof(XECRYPT_SHA_STATE::state));
  XeCryptShaFinal_entry(sha_state_1, out, out_size);
}

// TODO: Size of this struct hasn't been confirmed yet.
typedef struct {
  rex::be<uint32_t> count;     // 0x0
  rex::be<uint32_t> state[8];  // 0x4
  uint8_t buffer[64];          // 0x24
} XECRYPT_SHA256_STATE;

void XeCryptSha256Init_entry(ppc_ptr_t<XECRYPT_SHA256_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x6a09e667;
  sha_state->state[1] = 0xbb67ae85;
  sha_state->state[2] = 0x3c6ef372;
  sha_state->state[3] = 0xa54ff53a;
  sha_state->state[4] = 0x510e527f;
  sha_state->state[5] = 0x9b05688c;
  sha_state->state[6] = 0x1f83d9ab;
  sha_state->state[7] = 0x5be0cd19;
}

void XeCryptSha256Update_entry(ppc_ptr_t<XECRYPT_SHA256_STATE> sha_state, mapped_void input,
                               u32 input_size) {
  sha256::SHA256 sha;
  std::copy(std::begin(sha_state->state), std::end(sha_state->state), sha.getHashValues());
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer), sha.getBuffer());
  sha.setTotalSize(sha_state->count);

  sha.add(input, input_size);

  std::copy_n(sha.getHashValues(), rex::countof(sha_state->state), sha_state->state);
  std::copy_n(sha.getBuffer(), rex::countof(sha_state->buffer), sha_state->buffer);
  sha_state->count = static_cast<uint32_t>(sha.getTotalSize());
}

void XeCryptSha256Final_entry(ppc_ptr_t<XECRYPT_SHA256_STATE> sha_state, ppc_ptr_t<uint8_t> out,
                              u32 out_size) {
  sha256::SHA256 sha;
  std::copy(std::begin(sha_state->state), std::end(sha_state->state), sha.getHashValues());
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer), sha.getBuffer());
  sha.setTotalSize(sha_state->count);

  uint8_t hash[32];
  sha.getHash(hash);

  std::copy_n(hash, std::min<size_t>(rex::countof(hash), out_size), static_cast<uint8_t*>(out));
  std::copy(std::begin(hash), std::end(hash), sha_state->buffer);
}

// TODO: Size of this struct hasn't been confirmed yet.
typedef struct {
  rex::be<uint64_t> count;     // 0x0
  rex::be<uint64_t> state[8];  // 0x8
  uint8_t buffer[128];         // 0x48
} XECRYPT_SHA512_STATE;

// Mirrors FFmpeg's internal AVSHA512 layout (libavutil/sha512.c) so the state can be bridged
// into/out of av_sha512_* between guest-visible Update/Final calls.
struct SHA512_STATE {
  uint8_t digest_len;
  uint64_t count;
  uint8_t buffer[128];
  uint64_t state[8];
};

void XeCryptSha512Init_entry(ppc_ptr_t<XECRYPT_SHA512_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x6a09e667f3bcc908;
  sha_state->state[1] = 0xbb67ae8584caa73b;
  sha_state->state[2] = 0x3c6ef372fe94f82b;
  sha_state->state[3] = 0xa54ff53a5f1d36f1;
  sha_state->state[4] = 0x510e527fade682d1;
  sha_state->state[5] = 0x9b05688c2b3e6c1f;
  sha_state->state[6] = 0x1f83d9abfb41bd6b;
  sha_state->state[7] = 0x5be0cd19137e2179;
}

void XeCryptSha512Update_entry(ppc_ptr_t<XECRYPT_SHA512_STATE> sha_state, mapped_void input,
                               u32 input_size) {
  AVSHA512* sha = av_sha512_alloc();
  av_sha512_init(sha, 512);

  SHA512_STATE* sha2 = reinterpret_cast<SHA512_STATE*>(sha);
  std::copy(std::begin(sha_state->state), std::end(sha_state->state), sha2->state);
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer), sha2->buffer);
  sha2->count = sha_state->count;

  av_sha512_update(sha, input, input_size);

  std::copy_n(sha2->state, rex::countof(sha_state->state), sha_state->state);
  std::copy_n(sha2->buffer, rex::countof(sha_state->buffer), sha_state->buffer);
  sha_state->count = sha2->count;

  av_free(sha);
}

void XeCryptSha512Final_entry(ppc_ptr_t<XECRYPT_SHA512_STATE> sha_state, ppc_ptr_t<uint8_t> out,
                              u32 out_size) {
  AVSHA512* sha = av_sha512_alloc();
  av_sha512_init(sha, 512);

  SHA512_STATE* sha2 = reinterpret_cast<SHA512_STATE*>(sha);
  std::copy(std::begin(sha_state->state), std::end(sha_state->state), sha2->state);
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer), sha2->buffer);
  sha2->count = sha_state->count;

  uint8_t hash[64];
  av_sha512_final(sha, hash);

  std::copy_n(hash, std::min<size_t>(rex::countof(hash), out_size), static_cast<uint8_t*>(out));
  std::copy(std::begin(hash), std::end(hash), sha_state->buffer);

  av_free(sha);
}

// TODO: Size of this struct hasn't been confirmed yet.
typedef struct {
  rex::be<uint64_t> count;
  rex::be<uint32_t> state[4];
  uint8_t buffer[64];
} XECRYPT_MD5_STATE;

// Mirrors FFmpeg's internal AVMD5 layout (libavutil/md5.c) so the state can be bridged into/out
// of av_md5_* between guest-visible Update/Final calls.
struct MD5_STATE {
  uint64_t len;
  uint8_t block[64];
  uint32_t ABCD[4];
};

void XeCryptMd5_entry(mapped_void input_1, u32 input_1_size, mapped_void input_2, u32 input_2_size,
                      mapped_void input_3, u32 input_3_size, mapped_void output, u32 output_size) {
  AVMD5* md5 = av_md5_alloc();
  av_md5_init(md5);

  if (input_1 && input_1_size) {
    av_md5_update(md5, input_1, input_1_size);
  }
  if (input_2 && input_2_size) {
    av_md5_update(md5, input_2, input_2_size);
  }
  if (input_3 && input_3_size) {
    av_md5_update(md5, input_3, input_3_size);
  }

  uint8_t digest[16];
  av_md5_final(md5, digest);

  std::copy_n(digest, std::min<size_t>(rex::countof(digest), output_size), output.as<uint8_t*>());

  av_free(md5);
}

void XeCryptMd5Init_entry(ppc_ptr_t<XECRYPT_MD5_STATE> md5_state) {
  md5_state.Zero();

  // must match av_md5_init
  md5_state->state[0] = 0x10325476;
  md5_state->state[1] = 0x98badcfe;
  md5_state->state[2] = 0xefcdab89;
  md5_state->state[3] = 0x67452301;
}

void XeCryptMd5Update_entry(ppc_ptr_t<XECRYPT_MD5_STATE> md5_state, mapped_void input,
                            u32 input_size) {
  AVMD5* md5 = av_md5_alloc();
  av_md5_init(md5);

  MD5_STATE* md5_internal = reinterpret_cast<MD5_STATE*>(md5);
  std::copy(std::begin(md5_state->state), std::end(md5_state->state), md5_internal->ABCD);
  std::copy(std::begin(md5_state->buffer), std::end(md5_state->buffer), md5_internal->block);
  md5_internal->len = md5_state->count;

  av_md5_update(md5, input, input_size);

  std::copy_n(md5_internal->ABCD, rex::countof(md5_state->state), md5_state->state);
  std::copy_n(md5_internal->block, rex::countof(md5_state->buffer), md5_state->buffer);
  md5_state->count = md5_internal->len;

  av_free(md5);
}

void XeCryptMd5Final_entry(ppc_ptr_t<XECRYPT_MD5_STATE> md5_state, ppc_ptr_t<uint8_t> out,
                           u32 out_size) {
  AVMD5* md5 = av_md5_alloc();
  av_md5_init(md5);

  MD5_STATE* md5_internal = reinterpret_cast<MD5_STATE*>(md5);
  std::copy(std::begin(md5_state->state), std::end(md5_state->state), md5_internal->ABCD);
  std::copy(std::begin(md5_state->buffer), std::end(md5_state->buffer), md5_internal->block);
  md5_internal->len = md5_state->count;

  uint8_t hash[16];
  av_md5_final(md5, hash);

  std::copy_n(hash, std::min<size_t>(rex::countof(hash), out_size), static_cast<uint8_t*>(out));
  std::copy(std::begin(hash), std::end(hash), md5_state->buffer);

  av_free(md5);
}

// Byteswaps each 8 bytes
void XeCryptBnQw_SwapDwQwLeBe_entry(ppc_ptr_t<uint64_t> qw_inp, ppc_ptr_t<uint64_t> qw_out,
                                    u32 size) {
  memory::copy_and_swap<uint64_t>(qw_out, qw_inp, size);
}

typedef struct {
  rex::be<uint32_t> size;  // size of modulus in 8 byte units
  rex::be<uint32_t> public_exponent;
  rex::be<uint64_t> pad_8;

  // followed by modulus, followed by any private-key data
} XECRYPT_RSA;
static_assert_size(XECRYPT_RSA, 0x10);

u32 XeCryptBnQwNeRsaPubCrypt_entry(ppc_ptr_t<uint64_t> qw_a, ppc_ptr_t<uint64_t> qw_b,
                                   ppc_ptr_t<XECRYPT_RSA> rsa) {
  // 0 indicates failure (but not a BOOL return value)
#if !REX_PLATFORM_WIN32
  REXKRNL_ERROR(
      "XeCryptBnQwNeRsaPubCrypt called but no implementation available for "
      "this platform!");
  assert_always();
  return 1;
#else
  uint32_t modulus_size = rsa->size * 8;

  // Convert XECRYPT blob into BCrypt format
  ULONG key_size = sizeof(BCRYPT_RSAKEY_BLOB) + sizeof(uint32_t) + modulus_size;
  auto key_buf = std::make_unique<uint8_t[]>(key_size);
  auto* key_header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(key_buf.get());

  key_header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
  key_header->BitLength = modulus_size * 8;
  key_header->cbPublicExp = sizeof(uint32_t);
  key_header->cbModulus = modulus_size;
  key_header->cbPrime1 = key_header->cbPrime2 = 0;

  // Copy in exponent/modulus, luckily these are BE inside BCrypt blob
  uint32_t* key_exponent = reinterpret_cast<uint32_t*>(&key_header[1]);
  *key_exponent = rsa->public_exponent.value;

  // ...except modulus needs to be reversed in 64-bit chunks for BCrypt to make
  // use of it properly for some reason
  uint64_t* key_modulus = reinterpret_cast<uint64_t*>(&key_exponent[1]);
  uint64_t* xecrypt_modulus = reinterpret_cast<uint64_t*>(&rsa[1]);
  std::reverse_copy(xecrypt_modulus, xecrypt_modulus + rsa->size, key_modulus);

  BCRYPT_ALG_HANDLE hAlgorithm = NULL;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RSA_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);

  if (!BCRYPT_SUCCESS(status)) {
    REXKRNL_ERROR(
        "XeCryptBnQwNeRsaPubCrypt: BCryptOpenAlgorithmProvider failed with "
        "status {:#X}!",
        status);
    return 0;
  }

  BCRYPT_KEY_HANDLE hKey = NULL;
  status = BCryptImportKeyPair(hAlgorithm, NULL, BCRYPT_RSAPUBLIC_BLOB, &hKey, key_buf.get(),
                               key_size, 0);

  if (!BCRYPT_SUCCESS(status)) {
    REXKRNL_ERROR(
        "XeCryptBnQwNeRsaPubCrypt: BCryptImportKeyPair failed with status "
        "{:#X}!",
        status);

    if (hAlgorithm) {
      BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    }

    return 0;
  }

  // Byteswap & reverse the input into output, as BCrypt wants MSB first
  uint64_t* output = qw_b;
  uint8_t* output_bytes = reinterpret_cast<uint8_t*>(output);
  memory::copy_and_swap<uint64_t>(output, qw_a, rsa->size);
  std::reverse(output_bytes, output_bytes + modulus_size);

  // BCryptDecrypt only works with private keys, fortunately BCryptEncrypt
  // performs the right actions needed for us to decrypt the input
  ULONG result_size = 0;
  status = BCryptEncrypt(hKey, output_bytes, modulus_size, nullptr, nullptr, 0, output_bytes,
                         modulus_size, &result_size, BCRYPT_PAD_NONE);

  assert(result_size == modulus_size);

  if (!BCRYPT_SUCCESS(status)) {
    REXKRNL_ERROR("XeCryptBnQwNeRsaPubCrypt: BCryptEncrypt failed with status {:#X}!", status);
  } else {
    // Reverse data & byteswap again so data is as game expects
    std::reverse(output_bytes, output_bytes + modulus_size);
    memory::copy_and_swap(output, output, rsa->size);
  }

  if (hKey) {
    BCryptDestroyKey(hKey);
  }
  if (hAlgorithm) {
    BCryptCloseAlgorithmProvider(hAlgorithm, 0);
  }

  return BCRYPT_SUCCESS(status) ? 1 : 0;
#endif
}
#if REX_PLATFORM_WIN32

#else

#endif

u32 XeCryptBnDwLePkcs1Verify_entry(mapped_void hash, mapped_void sig, u32 size) {
  // BOOL return value
  return 1;
}

void XeCryptRandom_entry(mapped_void buf, u32 buf_size) {
  std::memset(buf, 0xFD, buf_size);
}

struct XECRYPT_DES_STATE {
  uint32_t keytab[16][2];
};

void XeCryptDesKey_entry(ppc_ptr_t<XECRYPT_DES_STATE> state_ptr, mapped_u64 key) {
  DES des(key[0]);
  std::memcpy(state_ptr->keytab, des.get_sub_key(), 128);
}

void XeCryptDesEcb_entry(ppc_ptr_t<XECRYPT_DES_STATE> state_ptr, mapped_u64 inp, mapped_u64 out,
                         u32 encrypt) {
  DES des(reinterpret_cast<uint64_t*>(state_ptr->keytab));
  if (encrypt) {
    *out = des.encrypt(*inp);
  } else {
    *out = des.decrypt(*inp);
  }
}

// Sets bit 0 to make the parity odd
void XeCryptDesParity_entry(mapped_void inp, u32 inp_size, mapped_void out_ptr) {
  DES::set_parity(inp, inp_size, out_ptr);
}

struct XECRYPT_DES3_STATE {
  XECRYPT_DES_STATE des_state[3];
};

void XeCryptDes3Key_entry(ppc_ptr_t<XECRYPT_DES3_STATE> state_ptr, mapped_u64 key) {
  DES3 des3(key[0], key[1], key[2]);
  DES* des = des3.getDES();

  // Store our DES state into the state.
  for (int i = 0; i < 3; i++) {
    std::memcpy(state_ptr->des_state[i].keytab, des[i].get_sub_key(), 128);
  }
}

void XeCryptDes3Ecb_entry(ppc_ptr_t<XECRYPT_DES3_STATE> state_ptr, mapped_u64 inp, mapped_u64 out,
                          u32 encrypt) {
  DES3 des3((ui64*)state_ptr->des_state[0].keytab, (ui64*)state_ptr->des_state[1].keytab,
            (ui64*)state_ptr->des_state[2].keytab);

  if (encrypt) {
    *out = des3.encrypt(*inp);
  } else {
    *out = des3.decrypt(*inp);
  }
}

void XeCryptDes3Cbc_entry(ppc_ptr_t<XECRYPT_DES3_STATE> state_ptr, mapped_u64 inp, u32 inp_size,
                          mapped_u64 out, mapped_u64 feed, u32 encrypt) {
  DES3 des3((ui64*)state_ptr->des_state[0].keytab, (ui64*)state_ptr->des_state[1].keytab,
            (ui64*)state_ptr->des_state[2].keytab);

  // DES can only do 8-byte chunks at a time!
  assert_true(inp_size % 8 == 0);

  uint64_t last_block = *feed;
  for (uint32_t i = 0; i < inp_size / 8; i++) {
    uint64_t block = inp[i];
    if (encrypt) {
      last_block = des3.encrypt(block ^ last_block);
      out[i] = last_block;
    } else {
      out[i] = des3.decrypt(block) ^ last_block;
      last_block = block;
    }
  }

  *feed = last_block;
}

struct XECRYPT_AES_STATE {
  uint8_t keytabenc[11][4][4];  // 0x0
  uint8_t keytabdec[11][4][4];  // 0xB0
};
static_assert_size(XECRYPT_AES_STATE, 0x160);

static inline uint8_t xeXeCryptAesMul2(uint8_t a) {
  return (a & 0x80) ? ((a << 1) ^ 0x1B) : (a << 1);
}

void XeCryptAesKey_entry(ppc_ptr_t<XECRYPT_AES_STATE> state_ptr, mapped_void key) {
  aes_key_schedule_128(key, reinterpret_cast<uint8_t*>(state_ptr->keytabenc));
  // Decryption key schedule not needed by openluopworld/aes_128, but generated
  // to fill the context structure properly.
  std::memcpy(state_ptr->keytabdec[0], state_ptr->keytabenc[10], 16);
  // Inverse MixColumns.
  for (uint32_t i = 1; i < 10; ++i) {
    const uint8_t* enc = reinterpret_cast<const uint8_t*>(state_ptr->keytabenc[10 - i]);
    uint8_t* dec = reinterpret_cast<uint8_t*>(state_ptr->keytabdec[i]);
    uint8_t t, u, v;
    t = enc[0] ^ enc[1] ^ enc[2] ^ enc[3];
    dec[0] = t ^ enc[0] ^ xeXeCryptAesMul2(enc[0] ^ enc[1]);
    dec[1] = t ^ enc[1] ^ xeXeCryptAesMul2(enc[1] ^ enc[2]);
    dec[2] = t ^ enc[2] ^ xeXeCryptAesMul2(enc[2] ^ enc[3]);
    dec[3] = t ^ enc[3] ^ xeXeCryptAesMul2(enc[3] ^ enc[0]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[0] ^ enc[2]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[1] ^ enc[3]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[0] ^= t ^ u;
    dec[1] ^= t ^ v;
    dec[2] ^= t ^ u;
    dec[3] ^= t ^ v;
    t = enc[4] ^ enc[5] ^ enc[6] ^ enc[7];
    dec[4] = t ^ enc[4] ^ xeXeCryptAesMul2(enc[4] ^ enc[5]);
    dec[5] = t ^ enc[5] ^ xeXeCryptAesMul2(enc[5] ^ enc[6]);
    dec[6] = t ^ enc[6] ^ xeXeCryptAesMul2(enc[6] ^ enc[7]);
    dec[7] = t ^ enc[7] ^ xeXeCryptAesMul2(enc[7] ^ enc[4]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[4] ^ enc[6]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[5] ^ enc[7]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[4] ^= t ^ u;
    dec[5] ^= t ^ v;
    dec[6] ^= t ^ u;
    dec[7] ^= t ^ v;
    t = enc[8] ^ enc[9] ^ enc[10] ^ enc[11];
    dec[8] = t ^ enc[8] ^ xeXeCryptAesMul2(enc[8] ^ enc[9]);
    dec[9] = t ^ enc[9] ^ xeXeCryptAesMul2(enc[9] ^ enc[10]);
    dec[10] = t ^ enc[10] ^ xeXeCryptAesMul2(enc[10] ^ enc[11]);
    dec[11] = t ^ enc[11] ^ xeXeCryptAesMul2(enc[11] ^ enc[8]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[8] ^ enc[10]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[9] ^ enc[11]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[8] ^= t ^ u;
    dec[9] ^= t ^ v;
    dec[10] ^= t ^ u;
    dec[11] ^= t ^ v;
    t = enc[12] ^ enc[13] ^ enc[14] ^ enc[15];
    dec[12] = t ^ enc[12] ^ xeXeCryptAesMul2(enc[12] ^ enc[13]);
    dec[13] = t ^ enc[13] ^ xeXeCryptAesMul2(enc[13] ^ enc[14]);
    dec[14] = t ^ enc[14] ^ xeXeCryptAesMul2(enc[14] ^ enc[15]);
    dec[15] = t ^ enc[15] ^ xeXeCryptAesMul2(enc[15] ^ enc[12]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[12] ^ enc[14]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[13] ^ enc[15]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[12] ^= t ^ u;
    dec[13] ^= t ^ v;
    dec[14] ^= t ^ u;
    dec[15] ^= t ^ v;
  }
  std::memcpy(state_ptr->keytabdec[10], state_ptr->keytabenc[0], 16);
  // TODO(Triang3l): Verify the order in keytabenc and everything in keytabdec.
}

void XeCryptAesEcb_entry(ppc_ptr_t<XECRYPT_AES_STATE> state_ptr, mapped_void inp_ptr,
                         mapped_void out_ptr, u32 encrypt) {
  const uint8_t* keytab = reinterpret_cast<const uint8_t*>(state_ptr->keytabenc);
  if (encrypt) {
    aes_encrypt_128(keytab, inp_ptr, out_ptr);
  } else {
    aes_decrypt_128(keytab, inp_ptr, out_ptr);
  }
}

void XeCryptAesCbc_entry(ppc_ptr_t<XECRYPT_AES_STATE> state_ptr, mapped_void inp_ptr, u32 inp_size,
                         mapped_void out_ptr, mapped_void feed_ptr, u32 encrypt) {
  const uint8_t* keytab = reinterpret_cast<const uint8_t*>(state_ptr->keytabenc);
  const uint8_t* inp = inp_ptr.as<const uint8_t*>();
  uint8_t* out = out_ptr.as<uint8_t*>();
  uint8_t* feed = feed_ptr.as<uint8_t*>();
  if (encrypt) {
    for (uint32_t i = 0; i < inp_size; i += 16) {
      for (uint32_t j = 0; j < 16; ++j) {
        feed[j] ^= inp[j];
      }
      aes_encrypt_128(keytab, feed, feed);
      std::memcpy(out, feed, 16);
      inp += 16;
      out += 16;
    }
  } else {
    for (uint32_t i = 0; i < inp_size; i += 16) {
      // In case inp == out.
      uint8_t tmp[16];
      std::memcpy(tmp, inp, 16);
      aes_decrypt_128(keytab, inp, out);
      for (uint32_t j = 0; j < 16; ++j) {
        out[j] ^= feed[j];
      }
      std::memcpy(feed, tmp, 16);
      inp += 16;
      out += 16;
    }
  }
}

void XeCryptHmacSha_entry(mapped_void key, u32 key_size_in, mapped_void inp_1, u32 inp_1_size,
                          mapped_void inp_2, u32 inp_2_size, mapped_void inp_3, u32 inp_3_size,
                          mapped_void out, u32 out_size) {
  uint32_t key_size = key_size_in;
  sha1::SHA1 sha;
  uint8_t kpad_i[0x40];
  uint8_t kpad_o[0x40];
  uint8_t tmp_key[0x40];
  std::memset(kpad_i, 0x36, 0x40);
  std::memset(kpad_o, 0x5C, 0x40);

  // Setup HMAC key
  // If > block size, use its hash
  if (key_size > 0x40) {
    sha1::SHA1 sha_key;
    sha_key.processBytes(key, key_size);
    sha_key.finalize((uint8_t*)tmp_key);

    key_size = 0x14u;
  } else {
    std::memcpy(tmp_key, key, key_size);
  }

  for (uint32_t i = 0; i < key_size; i++) {
    kpad_i[i] = tmp_key[i] ^ 0x36;
    kpad_o[i] = tmp_key[i] ^ 0x5C;
  }

  // Inner
  sha.processBytes(kpad_i, 0x40);

  if (inp_1_size) {
    sha.processBytes(inp_1, inp_1_size);
  }

  if (inp_2_size) {
    sha.processBytes(inp_2, inp_2_size);
  }

  if (inp_3_size) {
    sha.processBytes(inp_3, inp_3_size);
  }

  uint8_t digest[0x14];
  sha.finalize(digest);
  sha.reset();

  // Outer
  sha.processBytes(kpad_o, 0x40);
  sha.processBytes(digest, 0x14);
  sha.finalize(digest);

  std::memcpy(out, digest, std::min((uint32_t)out_size, 0x14u));
}

// Keys
// TODO: Array of keys we need

// Retail key 0x19
static const uint8_t key19[] = {0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
                                0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3};

u32 XeKeysHmacSha_entry(u32 key_num, mapped_void inp_1, u32 inp_1_size, mapped_void inp_2,
                        u32 inp_2_size, mapped_void inp_3, u32 inp_3_size, mapped_void out,
                        u32 out_size) {
  const uint8_t* key = nullptr;
  if (key_num == 0x19) {
    key = key19;
  }

  if (key) {
    XeCryptHmacSha_entry(mapped_void::from_host((void*)key), 0x10, inp_1, inp_1_size, inp_2,
                         inp_2_size, inp_3, inp_3_size, out, out_size);

    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
}

static const uint8_t xe_key_obfuscation_key[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

u32 XeKeysAesCbcUsingKey_entry(mapped_void obscured_key, mapped_void inp_ptr, u32 inp_size,
                               mapped_void out_ptr, mapped_void feed_ptr, u32 encrypt) {
  uint8_t key[16];

  // Deobscure key
  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes),
                      mapped_void::from_host((void*)xe_key_obfuscation_key));
  XeCryptAesEcb_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes), obscured_key,
                      mapped_void::from_host(key), 0);

  // Run CBC using deobscured key
  XeCryptAesKey_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes), mapped_void::from_host(key));
  XeCryptAesCbc_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes), inp_ptr, inp_size, out_ptr,
                      feed_ptr, encrypt);

  return X_STATUS_SUCCESS;
}

u32 XeKeysObscureKey_entry(mapped_void input, mapped_void output) {
  // Based on HvxKeysObscureKey
  // Seems to encrypt input with per-console KEY_OBFUSCATION_KEY (key 0x18)

  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes),
                      mapped_void::from_host((void*)xe_key_obfuscation_key));
  XeCryptAesEcb_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes), input, output, 1);

  return X_STATUS_SUCCESS;
}

u32 XeKeysHmacShaUsingKey_entry(mapped_void obscured_key, mapped_void inp_1, u32 inp_1_size,
                                mapped_void inp_2, u32 inp_2_size, mapped_void inp_3,
                                u32 inp_3_size, mapped_void out, u32 out_size) {
  if (!obscured_key) {
    return X_STATUS_INVALID_PARAMETER;
  }

  uint8_t key[16];

  // Deobscure key
  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes),
                      mapped_void::from_host((void*)xe_key_obfuscation_key));
  XeCryptAesEcb_entry(ppc_ptr_t<XECRYPT_AES_STATE>::from_host(&aes), obscured_key,
                      mapped_void::from_host(key), 0);

  XeCryptHmacSha_entry(mapped_void::from_host(key), 0x10, inp_1, inp_1_size, inp_2, inp_2_size,
                       inp_3, inp_3_size, out, out_size);
  return X_STATUS_SUCCESS;
}

u32 XeKeysConsolePrivateKeySign_entry(mapped_void hash, mapped_void signature) {
  REXKRNL_DEBUG("XeKeysConsolePrivateKeySign - stub");
  return 0;  // Success
}

u32 XeKeysConsoleSignatureVerification_entry(mapped_void hash, mapped_void signature,
                                             mapped_void pubkey) {
  REXKRNL_DEBUG("XeKeysConsoleSignatureVerification - stub");
  return 0;  // Success (signature valid)
}

// https://github.com/emoose/ExCrypt/blob/master/src/exkeys.cpp#L7
enum X_KEY_INDEX : uint32_t {
  MANUFACTURING_MODE = 0x0,
  ALTERNATE_KEY_VAULT = 0x1,
  RESTRICTED_PRIVILEGES_FLAGS = 0x2,
  RESERVED_BYTE3 = 0x3,
  ODD_FEATURES = 0x4,
  ODD_AUTHTYPE = 0x5,
  RESTRICTED_HVEXT_LOADER = 0x6,
  POLICY_FLASH_SIZE = 0x7,
  POLICY_BUILTIN_USBMU_SIZE = 0x8,
  RESERVED_DWORD4 = 0x9,
  RESTRICTED_PRIVILEGES = 0xA,
  RESERVED_QWORD2 = 0xB,
  RESERVED_QWORD3 = 0xC,
  RESERVED_QWORD4 = 0xD,
  RESERVED_KEY1 = 0xE,
  RESERVED_KEY2 = 0xF,
  RESERVED_KEY3 = 0x10,
  RESERVED_KEY4 = 0x11,
  RESERVED_RANDOM_KEY1 = 0x12,
  RESERVED_RANDOM_KEY2 = 0x13,
  CONSOLE_SERIAL_NUMBER = 0x14,
  MOBO_SERIAL_NUMBER = 0x15,
  GAME_REGION = 0x16,
  CONSOLE_OBFUSCATION_KEY = 0x17,
  KEY_OBFUSCATION_KEY = 0x18,
  ROAMABLE_OBFUSCATION_KEY = 0x19,
  DVD_KEY = 0x1A,
  PRIMARY_ACTIVATION_KEY = 0x1B,
  SECONDARY_ACTIVATION_KEY = 0x1C,
  GLOBAL_DEVICE_2DES_KEY1 = 0x1D,
  GLOBAL_DEVICE_2DES_KEY2 = 0x1E,
  WIRELESS_CONTROLLER_MS_2DES_KEY1 = 0x1F,
  WIRELESS_CONTROLLER_MS_2DES_KEY2 = 0x20,
  WIRED_WEBCAM_MS_2DES_KEY1 = 0x21,
  WIRED_WEBCAM_MS_2DES_KEY2 = 0x22,
  WIRED_CONTROLLER_MS_2DES_KEY1 = 0x23,
  WIRED_CONTROLLER_MS_2DES_KEY2 = 0x24,
  MEMORY_UNIT_MS_2DES_KEY1 = 0x25,
  MEMORY_UNIT_MS_2DES_KEY2 = 0x26,
  OTHER_XSM3_DEVICE_MS_2DES_KEY1 = 0x27,
  OTHER_XSM3_DEVICE_MS_2DES_KEY2 = 0x28,
  WIRELESS_CONTROLLER_3P_2DES_KEY1 = 0x29,
  WIRELESS_CONTROLLER_3P_2DES_KEY2 = 0x2A,
  WIRED_WEBCAM_3P_2DES_KEY1 = 0x2B,
  WIRED_WEBCAM_3P_2DES_KEY2 = 0x2C,
  WIRED_CONTROLLER_3P_2DES_KEY1 = 0x2D,
  WIRED_CONTROLLER_3P_2DES_KEY2 = 0x2E,
  MEMORY_UNIT_3P_2DES_KEY1 = 0x2F,
  MEMORY_UNIT_3P_2DES_KEY2 = 0x30,
  OTHER_XSM3_DEVICE_3P_2DES_KEY1 = 0x31,
  OTHER_XSM3_DEVICE_3P_2DES_KEY2 = 0x32,
  CONSOLE_PRIVATE_KEY = 0x33,
  XEIKA_PRIVATE_KEY = 0x34,
  CARDEA_PRIVATE_KEY = 0x35,
  CONSOLE_CERTIFICATE = 0x36,
  XEIKA_CERTIFICATE = 0x37,
  CARDEA_CERTIFICATE = 0x38,
  MAX_KEY_INDEX = 0x39,
};

static const std::map<uint32_t, std::tuple<uint32_t, uint32_t>> X_Key_Properties = {
    {MANUFACTURING_MODE, {0x8, 0x1}},
    {ALTERNATE_KEY_VAULT, {0x9, 0x1}},
    {RESTRICTED_PRIVILEGES_FLAGS, {0xA, 0x1}},
    {RESERVED_BYTE3, {0xB, 0x1}},
    {ODD_FEATURES, {0xC, 0x2}},
    {ODD_AUTHTYPE, {0xE, 0x2}},
    {RESTRICTED_HVEXT_LOADER, {0x10, 0x4}},
    {POLICY_FLASH_SIZE, {0x14, 0x4}},
    {POLICY_BUILTIN_USBMU_SIZE, {0x18, 0x4}},
    {RESERVED_DWORD4, {0x1C, 0x4}},
    {RESTRICTED_PRIVILEGES, {0x20, 0x8}},
    {RESERVED_QWORD2, {0x28, 0x8}},
    {RESERVED_QWORD3, {0x30, 0x8}},
    {RESERVED_QWORD4, {0x38, 0x8}},
    {RESERVED_KEY1, {0x40, 0x10}},
    {RESERVED_KEY2, {0x50, 0x10}},
    {RESERVED_KEY3, {0x60, 0x10}},
    {RESERVED_KEY4, {0x70, 0x10}},
    {RESERVED_RANDOM_KEY1, {0x80, 0x10}},
    {RESERVED_RANDOM_KEY2, {0x90, 0x10}},
    {CONSOLE_SERIAL_NUMBER, {0xA0, 0xC}},
    {MOBO_SERIAL_NUMBER, {0xAC, 0xC}},
    {GAME_REGION, {0xB8, 0x2}},
    {CONSOLE_OBFUSCATION_KEY, {0xC0, 0x10}},
    {KEY_OBFUSCATION_KEY, {0xD0, 0x10}},
    {ROAMABLE_OBFUSCATION_KEY, {0xE0, 0x10}},
    {DVD_KEY, {0xF0, 0x10}},
    {PRIMARY_ACTIVATION_KEY, {0x100, 0x18}},
    {SECONDARY_ACTIVATION_KEY, {0x118, 0x10}},
    {GLOBAL_DEVICE_2DES_KEY1, {0x128, 0x10}},
    {GLOBAL_DEVICE_2DES_KEY2, {0x138, 0x10}},
    {WIRELESS_CONTROLLER_MS_2DES_KEY1, {0x148, 0x10}},
    {WIRELESS_CONTROLLER_MS_2DES_KEY2, {0x158, 0x10}},
    {WIRED_WEBCAM_MS_2DES_KEY1, {0x168, 0x10}},
    {WIRED_WEBCAM_MS_2DES_KEY2, {0x178, 0x10}},
    {WIRED_CONTROLLER_MS_2DES_KEY1, {0x188, 0x10}},
    {WIRED_CONTROLLER_MS_2DES_KEY2, {0x198, 0x10}},
    {MEMORY_UNIT_MS_2DES_KEY1, {0x1A8, 0x10}},
    {MEMORY_UNIT_MS_2DES_KEY2, {0x1B8, 0x10}},
    {OTHER_XSM3_DEVICE_MS_2DES_KEY1, {0x1C8, 0x10}},
    {OTHER_XSM3_DEVICE_MS_2DES_KEY2, {0x1D8, 0x10}},
    {WIRELESS_CONTROLLER_3P_2DES_KEY1, {0x1E8, 0x10}},
    {WIRELESS_CONTROLLER_3P_2DES_KEY2, {0x1F8, 0x10}},
    {WIRED_WEBCAM_3P_2DES_KEY1, {0x208, 0x10}},
    {WIRED_WEBCAM_3P_2DES_KEY2, {0x218, 0x10}},
    {WIRED_CONTROLLER_3P_2DES_KEY1, {0x228, 0x10}},
    {WIRED_CONTROLLER_3P_2DES_KEY2, {0x238, 0x10}},
    {MEMORY_UNIT_3P_2DES_KEY1, {0x248, 0x10}},
    {MEMORY_UNIT_3P_2DES_KEY2, {0x258, 0x10}},
    {OTHER_XSM3_DEVICE_3P_2DES_KEY1, {0x268, 0x10}},
    {OTHER_XSM3_DEVICE_3P_2DES_KEY2, {0x278, 0x10}},
    {CONSOLE_PRIVATE_KEY, {0x288, 0x1D0}},
    {XEIKA_PRIVATE_KEY, {0x458, 0x390}},
    {CARDEA_PRIVATE_KEY, {0x7E8, 0x1D0}},
    {CONSOLE_CERTIFICATE, {0x9B8, 0x1A8}},
    {XEIKA_CERTIFICATE, {0xB60, 0x1288}},
    {CARDEA_CERTIFICATE, {0x1EE8, 0x2108}},
};

u32 XeKeysGetKeyProperties_entry(u32 key) {
  auto it = X_Key_Properties.find(key);
  if (it != X_Key_Properties.end()) {
    return std::get<1>(it->second);
  }
  REXKRNL_WARN("XeKeysGetKeyProperties: key 0x{:04X} not implemented", static_cast<uint16_t>(key));
  return 0;
}

enum XConsoleType : uint32_t {
  ConsoleType_Invalid = 0,
  ConsoleType_Devkit = 1,
  ConsoleType_Retail = 2,
  ConsoleType_Testkit = 0x40000001,
  ConsoleType_RecoveredDevkit = 0x80000001,
  ConsoleType_Pre1888Devkit = 0x80000002,
};

u32 XeKeysGetConsoleType_entry(mapped_u32 type_out) {
  *type_out = ConsoleType_Retail;
  return 0;
}

#pragma pack(push, 1)
struct XE_CONSOLE_ID {
  union {
    struct {
      uint8_t RefurbBits : 4;
      uint8_t ManufactureMonth : 4;
      uint32_t ManufactureYear : 4;
      uint32_t MacIndex3 : 8;
      uint32_t MacIndex4 : 8;
      uint32_t MacIndex5 : 8;
      uint32_t Crc : 4;
    };
    uint8_t Data[5];
  };
};
#pragma pack(pop)

u32 XeKeysGetConsoleID_entry(ppc_ptr_t<XE_CONSOLE_ID> raw_bytes, mapped_string hex_string) {
  // We don't care about the key vault or using official keys.
  if (raw_bytes) {
    raw_bytes.Zero();
    raw_bytes->RefurbBits = 0b0011;
    raw_bytes->ManufactureMonth = 0b1001;
    raw_bytes->ManufactureYear = 0b0001;
    raw_bytes->MacIndex3 = 0b01000000;
    raw_bytes->MacIndex4 = 0b01100110;
    raw_bytes->MacIndex5 = 0b01111110;
    raw_bytes->Crc = 0b0000;
  }
  if (hex_string) {
    std::string key = "245149100000";
    std::memcpy(hex_string, key.c_str(), 0xC);
  }
  return X_STATUS_SUCCESS;
}

REX_EXPORT_STUB(__imp__XeKeysGetConsoleCertificate);
REX_EXPORT_STUB(__imp__XeCryptBnDwLeDhEqualBase);
REX_EXPORT_STUB(__imp__XeCryptBnDwLeDhInvalBase);
REX_EXPORT_STUB(__imp__XeCryptBnDwLeDhModExp);
REX_EXPORT_STUB(__imp__XeCryptBnDw_Copy);
REX_EXPORT_STUB(__imp__XeCryptBnDw_SwapLeBe);
REX_EXPORT_STUB(__imp__XeCryptBnDw_Zero);
REX_EXPORT_STUB(__imp__XeCryptBnDwLePkcs1Format);
REX_EXPORT_STUB(__imp__XeCryptBnQwBeSigCreate);
REX_EXPORT_STUB(__imp__XeCryptBnQwBeSigFormat);
REX_EXPORT_STUB(__imp__XeCryptBnQwBeSigVerify);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeModExp);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeModExpRoot);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeModInv);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeModMul);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeRsaKeyGen);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeRsaPrvCrypt);
REX_EXPORT_STUB(__imp__XeCryptBnQw_Copy);
REX_EXPORT_STUB(__imp__XeCryptBnQw_SwapDwQw);
REX_EXPORT_STUB(__imp__XeCryptBnQw_SwapLeBe);
REX_EXPORT_STUB(__imp__XeCryptBnQw_Zero);
REX_EXPORT_STUB(__imp__XeCryptChainAndSumMac);
REX_EXPORT_STUB(__imp__XeCryptDesCbc);
REX_EXPORT_STUB(__imp__XeCryptHmacMd5Init);
REX_EXPORT_STUB(__imp__XeCryptHmacMd5Update);
REX_EXPORT_STUB(__imp__XeCryptHmacMd5Final);
REX_EXPORT_STUB(__imp__XeCryptHmacMd5);
REX_EXPORT_STUB(__imp__XeCryptHmacShaVerify);
REX_EXPORT_STUB(__imp__XeCryptParveEcb);
REX_EXPORT_STUB(__imp__XeCryptParveCbcMac);
REX_EXPORT_STUB(__imp__XeCryptRotSumSha);
REX_EXPORT_STUB(__imp__XeCryptSha256);
REX_EXPORT_STUB(__imp__XeCryptSha384Init);
REX_EXPORT_STUB(__imp__XeCryptSha384Update);
REX_EXPORT_STUB(__imp__XeCryptSha384Final);
REX_EXPORT_STUB(__imp__XeCryptSha384);
REX_EXPORT_STUB(__imp__XeCryptSha512);
REX_EXPORT_STUB(__imp__XeCryptBnQwNeCompare);
REX_EXPORT_STUB(__imp__XeKeysGetFactoryChallenge);
REX_EXPORT_STUB(__imp__XeKeysSetFactoryResponse);
REX_EXPORT_STUB(__imp__XeKeysInitializeFuses);
REX_EXPORT_STUB(__imp__XeKeysSaveBootLoader);
REX_EXPORT_STUB(__imp__XeKeysSaveKeyVault);
REX_EXPORT_STUB(__imp__XeKeysGetStatus);
REX_EXPORT_STUB(__imp__XeKeysGeneratePrivateKey);
REX_EXPORT_STUB(__imp__XeKeysSetKey);
REX_EXPORT_STUB(__imp__XeKeysGenerateRandomKey);
REX_EXPORT_STUB(__imp__XeKeysGetKey);
REX_EXPORT_STUB(__imp__XeKeysGetDigest);
REX_EXPORT_STUB(__imp__XeKeysQwNeRsaPrvCrypt);
REX_EXPORT_STUB(__imp__XeKeysAesCbc);
REX_EXPORT_STUB(__imp__XeKeysDes2Cbc);
REX_EXPORT_STUB(__imp__XeKeysDesCbc);
REX_EXPORT_STUB(__imp__XeKeysSaveBootLoaderEx);
REX_EXPORT_STUB(__imp__XeKeysDes2CbcUsingKey);
REX_EXPORT_STUB(__imp__XeKeysDesCbcUsingKey);
REX_EXPORT_STUB(__imp__XeKeysObfuscate);
REX_EXPORT_STUB(__imp__XeKeysUnObfuscate);
REX_EXPORT_STUB(__imp__XeKeysVerifyRSASignature);
REX_EXPORT_STUB(__imp__XeKeysSaveSystemUpdate);
REX_EXPORT_STUB(__imp__XeKeysLockSystemUpdate);
REX_EXPORT_STUB(__imp__XeKeysExecute);
REX_EXPORT_STUB(__imp__XeKeysGetVersions);
REX_EXPORT_STUB(__imp__XeKeysSetRevocationList);
REX_EXPORT_STUB(__imp__XeKeysExSaveKeyVault);
REX_EXPORT_STUB(__imp__XeKeysExSetKey);
REX_EXPORT_STUB(__imp__XeKeysExGetKey);
REX_EXPORT_STUB(__imp__XeKeysSecurityInitialize);
REX_EXPORT_STUB(__imp__XeKeysSecurityLoadSettings);
REX_EXPORT_STUB(__imp__XeKeysSecuritySaveSettings);
REX_EXPORT_STUB(__imp__XeKeysSecuritySetDetected);
REX_EXPORT_STUB(__imp__XeKeysSecurityGetDetected);
REX_EXPORT_STUB(__imp__XeKeysSecuritySetActivated);
REX_EXPORT_STUB(__imp__XeKeysSecurityGetActivated);
REX_EXPORT_STUB(__imp__XeKeysDvdAuthAP25InstallTable);
REX_EXPORT_STUB(__imp__XeKeysDvdAuthAP25GetTableVersion);
REX_EXPORT_STUB(__imp__XeKeysGetProtectedFlag);
REX_EXPORT_STUB(__imp__XeKeysSetProtectedFlag);
REX_EXPORT_STUB(__imp__XeKeysGetUpdateSequence);
REX_EXPORT_STUB(__imp__XeKeysDvdAuthExActivate);
REX_EXPORT_STUB(__imp__XeKeysRevokeSaveSettings);
REX_EXPORT_STUB(__imp__XeKeysGetMediaID);
REX_EXPORT_STUB(__imp__XeKeysLoadKeyVault);
REX_EXPORT_STUB(__imp__XeKeysRevokeUpdateDynamic);
REX_EXPORT_STUB(__imp__XeKeysSecuritySetStat);
REX_EXPORT_STUB(__imp__XeKeysFcrtLoad);
REX_EXPORT_STUB(__imp__XeKeysFcrtSave);
REX_EXPORT_STUB(__imp__XeKeysFcrtSet);
REX_EXPORT_STUB(__imp__XeKeysRevokeIsDeviceRevoked);
REX_EXPORT_STUB(__imp__XeKeysDvdAuthExSave);
REX_EXPORT_STUB(__imp__XeKeysDvdAuthExInstall);
REX_EXPORT_STUB(__imp__XeKeysObfuscateEx);
REX_EXPORT_STUB(__imp__XeKeysUnObfuscateEx);
REX_EXPORT_STUB(__imp__XeKeysVerifyPIRSSignature);
REX_EXPORT_STUB(__imp__XeCryptAesCtr);
REX_EXPORT_STUB(__imp__XeCryptAesCbcMac);
REX_EXPORT_STUB(__imp__XeCryptAesDmMac);
REX_EXPORT_STUB(__imp__XeKeysGetTruncatedSecondaryConsoleId);
REX_EXPORT_STUB(__imp__XeCryptSha224Init);
REX_EXPORT_STUB(__imp__XeCryptAesCreateKeySchedule);
REX_EXPORT_STUB(__imp__XeCryptAesEncryptOne);
REX_EXPORT_STUB(__imp__XeCryptAesDecryptOne);
REX_EXPORT_STUB(__imp__XeCryptAesCbcEncrypt);
REX_EXPORT_STUB(__imp__XeCryptAesCbcDecrypt);
REX_EXPORT_STUB(__imp__XeCryptAesGcmInitialize);
REX_EXPORT_STUB(__imp__XeCryptAesGcmUpdate);
REX_EXPORT_STUB(__imp__XeCryptAesGcmFinalize);
REX_EXPORT_STUB(__imp__XeCryptEccGetCurveParameters);
REX_EXPORT_STUB(__imp__XeCryptEccEcdhGenerateKeypair);
REX_EXPORT_STUB(__imp__XeCryptEccEcdhExponentiate);
REX_EXPORT_STUB(__imp__XeCryptEccEcdsaGenerateSignature);
REX_EXPORT_STUB(__imp__XeCryptEccEcdsaVerifySignature);

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__XeCryptRc4Key, rex::kernel::xboxkrnl::XeCryptRc4Key_entry)
REX_EXPORT(__imp__XeCryptRc4Ecb, rex::kernel::xboxkrnl::XeCryptRc4Ecb_entry)
REX_EXPORT(__imp__XeCryptRc4, rex::kernel::xboxkrnl::XeCryptRc4_entry)
REX_EXPORT(__imp__XeCryptShaInit, rex::kernel::xboxkrnl::XeCryptShaInit_entry)
REX_EXPORT(__imp__XeCryptShaUpdate, rex::kernel::xboxkrnl::XeCryptShaUpdate_entry)
REX_EXPORT(__imp__XeCryptShaFinal, rex::kernel::xboxkrnl::XeCryptShaFinal_entry)
REX_EXPORT(__imp__XeCryptSha, rex::kernel::xboxkrnl::XeCryptSha_entry)
REX_EXPORT(__imp__XeCryptSha256Init, rex::kernel::xboxkrnl::XeCryptSha256Init_entry)
REX_EXPORT(__imp__XeCryptSha256Update, rex::kernel::xboxkrnl::XeCryptSha256Update_entry)
REX_EXPORT(__imp__XeCryptSha256Final, rex::kernel::xboxkrnl::XeCryptSha256Final_entry)
REX_EXPORT(__imp__XeCryptBnQw_SwapDwQwLeBe, rex::kernel::xboxkrnl::XeCryptBnQw_SwapDwQwLeBe_entry)
REX_EXPORT(__imp__XeCryptBnQwNeRsaPubCrypt, rex::kernel::xboxkrnl::XeCryptBnQwNeRsaPubCrypt_entry)
REX_EXPORT(__imp__XeCryptBnDwLePkcs1Verify, rex::kernel::xboxkrnl::XeCryptBnDwLePkcs1Verify_entry)
REX_EXPORT(__imp__XeCryptRandom, rex::kernel::xboxkrnl::XeCryptRandom_entry)
REX_EXPORT(__imp__XeCryptDesParity, rex::kernel::xboxkrnl::XeCryptDesParity_entry)
REX_EXPORT(__imp__XeCryptDes3Key, rex::kernel::xboxkrnl::XeCryptDes3Key_entry)
REX_EXPORT(__imp__XeCryptDes3Ecb, rex::kernel::xboxkrnl::XeCryptDes3Ecb_entry)
REX_EXPORT(__imp__XeCryptDes3Cbc, rex::kernel::xboxkrnl::XeCryptDes3Cbc_entry)
REX_EXPORT(__imp__XeCryptAesKey, rex::kernel::xboxkrnl::XeCryptAesKey_entry)
REX_EXPORT(__imp__XeCryptAesEcb, rex::kernel::xboxkrnl::XeCryptAesEcb_entry)
REX_EXPORT(__imp__XeCryptAesCbc, rex::kernel::xboxkrnl::XeCryptAesCbc_entry)
REX_EXPORT(__imp__XeCryptHmacSha, rex::kernel::xboxkrnl::XeCryptHmacSha_entry)
REX_EXPORT(__imp__XeCryptHmacShaInit, rex::kernel::xboxkrnl::XeCryptHmacShaInit_entry)
REX_EXPORT(__imp__XeCryptHmacShaUpdate, rex::kernel::xboxkrnl::XeCryptHmacShaUpdate_entry)
REX_EXPORT(__imp__XeCryptHmacShaFinal, rex::kernel::xboxkrnl::XeCryptHmacShaFinal_entry)
REX_EXPORT(__imp__XeCryptMd5, rex::kernel::xboxkrnl::XeCryptMd5_entry)
REX_EXPORT(__imp__XeCryptMd5Init, rex::kernel::xboxkrnl::XeCryptMd5Init_entry)
REX_EXPORT(__imp__XeCryptMd5Update, rex::kernel::xboxkrnl::XeCryptMd5Update_entry)
REX_EXPORT(__imp__XeCryptMd5Final, rex::kernel::xboxkrnl::XeCryptMd5Final_entry)
REX_EXPORT(__imp__XeCryptSha512Init, rex::kernel::xboxkrnl::XeCryptSha512Init_entry)
REX_EXPORT(__imp__XeCryptSha512Update, rex::kernel::xboxkrnl::XeCryptSha512Update_entry)
REX_EXPORT(__imp__XeCryptSha512Final, rex::kernel::xboxkrnl::XeCryptSha512Final_entry)
REX_EXPORT(__imp__XeCryptDesKey, rex::kernel::xboxkrnl::XeCryptDesKey_entry)
REX_EXPORT(__imp__XeCryptDesEcb, rex::kernel::xboxkrnl::XeCryptDesEcb_entry)
REX_EXPORT(__imp__XeKeysGetKeyProperties, rex::kernel::xboxkrnl::XeKeysGetKeyProperties_entry)
REX_EXPORT(__imp__XeKeysGetConsoleType, rex::kernel::xboxkrnl::XeKeysGetConsoleType_entry)
REX_EXPORT(__imp__XeKeysGetConsoleID, rex::kernel::xboxkrnl::XeKeysGetConsoleID_entry)
REX_EXPORT(__imp__XeKeysHmacSha, rex::kernel::xboxkrnl::XeKeysHmacSha_entry)
REX_EXPORT(__imp__XeKeysAesCbcUsingKey, rex::kernel::xboxkrnl::XeKeysAesCbcUsingKey_entry)
REX_EXPORT(__imp__XeKeysObscureKey, rex::kernel::xboxkrnl::XeKeysObscureKey_entry)
REX_EXPORT(__imp__XeKeysHmacShaUsingKey, rex::kernel::xboxkrnl::XeKeysHmacShaUsingKey_entry)
REX_EXPORT(__imp__XeKeysConsolePrivateKeySign,
           rex::kernel::xboxkrnl::XeKeysConsolePrivateKeySign_entry)
REX_EXPORT(__imp__XeKeysConsoleSignatureVerification,
           rex::kernel::xboxkrnl::XeKeysConsoleSignatureVerification_entry)

REX_EXPORT_STUB(__imp__DevAuthGetStatistics);
REX_EXPORT_STUB(__imp__DevAuthShouldAlwaysEnforce);
