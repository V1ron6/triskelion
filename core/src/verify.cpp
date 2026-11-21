// All OpenSSL use lives in this file, so swapping the crypto backend touches nothing else.
#include "triskelion/verify.hpp"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>

namespace triskelion {

namespace {
struct MdCtxFree { void operator()(EVP_MD_CTX* c) const { EVP_MD_CTX_free(c); } };
struct PkeyFree { void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); } };
using MdCtx = std::unique_ptr<EVP_MD_CTX, MdCtxFree>;
using Pkey = std::unique_ptr<EVP_PKEY, PkeyFree>;

int b64_val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
}  // namespace

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view s) {
  if (s.empty() || s.size() % 4 != 0) return std::nullopt;
  std::vector<std::uint8_t> out;
  out.reserve(s.size() / 4 * 3);
  for (std::size_t i = 0; i < s.size(); i += 4) {
    unsigned v[4];
    int pad = 0;
    for (int j = 0; j < 4; j++) {
      const char c = s[i + j];
      if (c == '=') {
        if (i + 4 != s.size() || j < 2) return std::nullopt;  // padding only in the last quad, positions 2-3
        v[j] = 0;
        pad++;
      } else {
        if (pad) return std::nullopt;  // data after padding
        const int x = b64_val(c);
        if (x < 0) return std::nullopt;
        v[j] = static_cast<unsigned>(x);
      }
    }
    const unsigned n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out.push_back(static_cast<std::uint8_t>(n >> 16));
    if (pad < 2) out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xff));
    if (pad < 1) out.push_back(static_cast<std::uint8_t>(n & 0xff));
  }
  return out;
}

std::optional<std::string> sha256_file_hex(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  MdCtx ctx(EVP_MD_CTX_new());
  if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) return std::nullopt;

  std::array<char, 64 * 1024> buf;
  while (in.read(buf.data(), buf.size()) || in.gcount() > 0) {
    if (EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<std::size_t>(in.gcount())) != 1) return std::nullopt;
  }
  if (in.bad()) return std::nullopt;

  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  if (EVP_DigestFinal_ex(ctx.get(), md, &len) != 1) return std::nullopt;

  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (unsigned int i = 0; i < len; i++) {
    out.push_back(hex[md[i] >> 4]);
    out.push_back(hex[md[i] & 0xf]);
  }
  return out;
}

std::string manifest_message(const Manifest& m) {
  return "triskelion:v1\n" + m.app + "\n" + m.platform + "\n" + m.version + "\n" + m.sha256_hex + "\n" +
         std::to_string(m.size);
}

bool verify_manifest(std::string_view public_key_b64, const Manifest& m) {
  const auto key = base64_decode(public_key_b64);
  const auto sig = base64_decode(m.signature_b64);
  if (!key || key->size() != 32 || !sig || sig->size() != 64) return false;

  Pkey pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key->data(), key->size()));
  MdCtx ctx(EVP_MD_CTX_new());
  if (!pkey || !ctx) return false;
  if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1) return false;

  const std::string msg = manifest_message(m);
  return EVP_DigestVerify(ctx.get(), sig->data(), sig->size(),
                          reinterpret_cast<const unsigned char*>(msg.data()), msg.size()) == 1;
}

}  // namespace triskelion
