// Host test of sniffer/sigverify.h against an image signed by tools/ota-sign.py (see tools/test-signing.sh):
//   sigverify-test SIGNED.bin VERSION LANG      compile with -I<dir of signing_key.h> -I<sniffer> -lmbedcrypto
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "signing_key.h"
#include "sigverify.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static bool run(const std::vector<uint8_t>& file, size_t chunk, uint32_t version, const char* lang, const char** why = nullptr) {
  OtaVerifier v;
  v.begin();
  for (size_t i = 0; i < file.size(); i += chunk) v.feed(file.data() + i, std::min(chunk, file.size() - i));
  return v.finish(HN_PUBKEY, version, lang, why);
}

int main(int argc, char** argv) {
  if (argc < 4) { printf("usage: %s SIGNED.bin VERSION LANG\n", argv[0]); return 2; }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
  std::vector<uint8_t> file;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
  fclose(f);
  uint32_t version = (uint32_t)atoi(argv[2]);
  const char* lang = argv[3];

  for (size_t chunk : {(size_t)1, (size_t)7, (size_t)73, (size_t)74, (size_t)75, (size_t)1000, file.size()}) {
    const char* why = "";
    CHECK(run(file, chunk, version, lang, &why), "a valid image passes whatever the piece size");
    if (failures) { printf("  (chunk %zu: %s)\n", chunk, why); break; }
  }
  { auto t = file; t[100] ^= 1; CHECK(!run(t, 1000, version, lang), "a changed image byte is refused"); }
  { auto t = file; t[t.size() - 5] ^= 1; CHECK(!run(t, 1000, version, lang), "a changed signature is refused"); }
  CHECK(!run(file, 1000, version + 1, lang), "another version than the signed one is refused");
  CHECK(!run(file, 1000, version, lang[0] == 'P' ? "EN" : "PL"), "another language than the signed one is refused");
  { auto t = file; t[t.size() - 74] = 'X'; CHECK(!run(t, 1000, version, lang), "a missing magic is refused"); }
  { std::vector<uint8_t> t(file.begin(), file.end() - 74); CHECK(!run(t, 1000, version, lang), "an unsigned image is refused"); }
  { std::vector<uint8_t> t(file.begin(), file.begin() + 30); CHECK(!run(t, 1000, version, lang), "a tiny file is refused"); }
  printf(failures ? "%d failure(s)\n" : "sigverify: all tests passed\n", failures);
  return failures ? 1 : 0;
}
