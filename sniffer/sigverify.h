// Optional signature check of firmware images (ECDSA P-256 over SHA-256), used by the OTA update when the build has a
// public key (sniffer/signing_key.h, made by tools/ota-sign.py keygen). Header only, only mbedtls: the same code runs on
// the controller and in the host test (tools/sigverify-test.cpp).
//
// A signed image is the normal .bin followed by a 74 byte trailer:
//     "HNSG" | version (4 bytes, big endian) | language (2 ASCII letters, e.g. "PL") | signature (r, s: 2 x 32 bytes)
// The signature is made over  SHA-256( image || version || language )  with the private key that stays at the maker of
// the firmware. Binding the version and the language to the signature means that a server, however it was taken over,
// can neither offer an old image as a new one nor give a controller a build of another language.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_NUMBER >= 0x03000000  // the ESP32 core has mbedtls 3
#define HN_SHA256_START(c) mbedtls_sha256_starts(c, 0)
#define HN_SHA256_UPDATE(c, p, n) mbedtls_sha256_update(c, p, n)
#define HN_SHA256_FINISH(c, out) mbedtls_sha256_finish(c, out)
#else  // mbedtls 2 (the host test on a Debian image) has the same functions under other names
#define HN_SHA256_START(c) mbedtls_sha256_starts_ret(c, 0)
#define HN_SHA256_UPDATE(c, p, n) mbedtls_sha256_update_ret(c, p, n)
#define HN_SHA256_FINISH(c, out) mbedtls_sha256_finish_ret(c, out)
#endif

class OtaVerifier {
 public:
  static constexpr size_t TRAILER = 4 + 4 + 2 + 64;

  OtaVerifier() { mbedtls_sha256_init(&sha_); }
  ~OtaVerifier() { mbedtls_sha256_free(&sha_); }
  OtaVerifier(const OtaVerifier&) = delete;
  OtaVerifier& operator=(const OtaVerifier&) = delete;

  void begin() {
    HN_SHA256_START(&sha_);
    tailLen_ = 0;
  }

  // The bytes of the file as they arrive, in pieces of any size. Everything except the last TRAILER bytes goes into the hash.
  void feed(const uint8_t* p, size_t n) {
    size_t total = tailLen_ + n;
    if (total <= TRAILER) {
      memcpy(tail_ + tailLen_, p, n);
      tailLen_ = total;
      return;
    }
    size_t hashN = total - TRAILER;
    size_t fromTail = hashN < tailLen_ ? hashN : tailLen_;
    update(tail_, fromTail);
    size_t fromP = hashN - fromTail;
    update(p, fromP);
    uint8_t next[TRAILER];
    size_t keepTail = tailLen_ - fromTail;
    memcpy(next, tail_ + fromTail, keepTail);
    memcpy(next + keepTail, p + fromP, n - fromP);
    memcpy(tail_, next, TRAILER);
    tailLen_ = TRAILER;
  }

  // true when the trailer is there, its signature is valid for `pubkey` (65 bytes, uncompressed point), and it names exactly
  // this version and language. `why` gets a short English reason when it is not.
  bool finish(const uint8_t* pubkey, uint32_t version, const char* lang, const char** why = nullptr) {
    const char* dummy;
    if (!why) why = &dummy;
    uint8_t digest[32];
    if (tailLen_ != TRAILER || memcmp(tail_, "HNSG", 4) != 0) { *why = "no signature trailer"; return false; }
    uint32_t v = ((uint32_t)tail_[4] << 24) | ((uint32_t)tail_[5] << 16) | ((uint32_t)tail_[6] << 8) | tail_[7];
    if (v != version) { *why = "signed version differs from the offered one"; return false; }
    if (memcmp(tail_ + 8, lang, 2) != 0) { *why = "signed language differs"; return false; }
    update(tail_ + 4, 6);  // version and language are part of what was signed
    HN_SHA256_FINISH(&sha_, digest);
    bool ok = verify(pubkey, digest, tail_ + 10);
    if (!ok) *why = "bad signature";
    return ok;
  }

 private:
  mbedtls_sha256_context sha_;
  uint8_t tail_[TRAILER];
  size_t tailLen_ = 0;

  void update(const uint8_t* p, size_t n) {
    if (n) HN_SHA256_UPDATE(&sha_, p, n);
  }

  static bool verify(const uint8_t* pub, const uint8_t* digest, const uint8_t* sig) {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&grp, &q, pub, 65) == 0 &&
              mbedtls_mpi_read_binary(&r, sig, 32) == 0 &&
              mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
              mbedtls_ecdsa_verify(&grp, digest, 32, &q, &r, &s) == 0;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ok;
  }
};
