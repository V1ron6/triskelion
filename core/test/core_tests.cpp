#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "check.hpp"
#include "triskelion/decide.hpp"
#include "triskelion/verify.hpp"
#include "triskelion/version.hpp"

using namespace triskelion;
namespace fs = std::filesystem;

static const fs::path kFixtures = FIXTURE_DIR;

static std::map<std::string, std::string> load_vector() {
  std::map<std::string, std::string> kv;
  std::ifstream in(kFixtures / "vector.txt");
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return kv;
}

static std::string read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void write_all(const fs::path& p, const std::string& bytes) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static void test_version() {
  const auto v = parse_version("10.0.99");
  CHECK(v && v->major == 10 && v->minor == 0 && v->patch == 99);
  for (const char* bad : {"", "1.2", "1.2.3.4", "1.2.3-beta", "a.b.c", "1..3", " 1.2.3", "1.2.3 ", "1234567890.0.0", "-1.0.0"})
    CHECK(!parse_version(bad));
  CHECK(compare(*parse_version("1.2.3"), *parse_version("1.2.3")) == 0);
  CHECK(compare(*parse_version("1.2.3"), *parse_version("1.10.0")) < 0);
  CHECK(compare(*parse_version("2.0.0"), *parse_version("1.99.99")) > 0);
  CHECK(compare(*parse_version("1.0.10"), *parse_version("1.0.9")) > 0);
}

static void test_base64() {
  CHECK((base64_decode("AQID") == std::vector<std::uint8_t>{1, 2, 3}));
  CHECK((base64_decode("AQI=") == std::vector<std::uint8_t>{1, 2}));
  CHECK((base64_decode("AQ==") == std::vector<std::uint8_t>{1}));
  for (const char* bad : {"", "AQI", "AQ=A", "A===", "AQID=", "AQ!D", "AQ==AQID", "AQ\nD"})
    CHECK(!base64_decode(bad));
}

static void test_sha256(const fs::path& tmp) {
  write_all(tmp / "abc.bin", "abc");
  CHECK(sha256_file_hex(tmp / "abc.bin") == std::optional<std::string>("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  write_all(tmp / "empty.bin", "");
  CHECK(sha256_file_hex(tmp / "empty.bin") == std::optional<std::string>("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  // larger than the 64 KiB read buffer, to exercise the chunk loop
  write_all(tmp / "big.bin", std::string(200000, 'x'));
  CHECK(sha256_file_hex(tmp / "big.bin") == std::optional<std::string>("91e3faafd322bcdf160f3f0ce886acb092b9b9e2a1e8526b40f21a8898a8700b"));  // from sha256sum
  CHECK(!sha256_file_hex(tmp / "does-not-exist.bin"));
}

static void test_verify_and_decide(const fs::path& tmp) {
  const auto kv = load_vector();
  CHECK(kv.size() == 7);
  Manifest m{kv.at("app"), kv.at("platform"), kv.at("version"), kv.at("sha256"), std::stoull(kv.at("size")), kv.at("signature")};
  const std::string pub = kv.at("publicKey");
  const fs::path artifact = kFixtures / "artifact.bin";

  // The message must be byte-identical to what the Node side signed.
  CHECK(manifest_message(m) == "triskelion:v1\nlocked\nandroid\n1.1.0\n" + m.sha256_hex + "\n4096");
  CHECK(verify_manifest(pub, m));

  // Any changed field, wrong key, or damaged signature fails verification.
  { Manifest t = m; t.version = "1.1.1"; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.sha256_hex[0] = t.sha256_hex[0] == 'a' ? 'b' : 'a'; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.size += 1; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.platform = "windows"; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.app = "hollow"; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.signature_b64[10] = t.signature_b64[10] == 'A' ? 'B' : 'A'; CHECK(!verify_manifest(pub, t)); }
  { Manifest t = m; t.signature_b64 = "not base64!"; CHECK(!verify_manifest(pub, t)); }
  CHECK(!verify_manifest("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", m));
  CHECK(!verify_manifest("short", m));

  const Policy policy{"locked", "android", "1.0.0", pub};
  CHECK(decide(policy, m, artifact) == Decision::Install);

  // Version rules
  { Policy p = policy; p.current_version = "1.1.0"; CHECK(decide(p, m, artifact) == Decision::RejectDowngrade); }
  { Policy p = policy; p.current_version = "1.2.0"; CHECK(decide(p, m, artifact) == Decision::RejectDowngrade); }
  { Policy p = policy; p.current_version = "banana"; CHECK(decide(p, m, artifact) == Decision::RejectBadVersion); }
  { Manifest t = m; t.version = "1.1"; CHECK(decide(policy, t, artifact) == Decision::RejectBadVersion); }

  // Target comes from the build, not the server
  { Policy p = policy; p.app = "hollow"; CHECK(decide(p, m, artifact) == Decision::RejectWrongTarget); }
  { Policy p = policy; p.platform = "windows"; CHECK(decide(p, m, artifact) == Decision::RejectWrongTarget); }

  // Signature failures
  { Policy p = policy; p.public_key_b64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="; CHECK(decide(p, m, artifact) == Decision::RejectBadSignature); }
  { Manifest t = m; t.version = "1.1.1"; CHECK(decide(policy, t, artifact) == Decision::RejectBadSignature); }
  { Manifest t = m; t.size += 1; CHECK(decide(policy, t, artifact) == Decision::RejectBadSignature); }

  // File problems, with a valid signature
  const std::string bytes = read_all(artifact);
  { std::string b = bytes; b[100] = static_cast<char>(b[100] ^ 0x01); write_all(tmp / "flipped.bin", b);
    CHECK(decide(policy, m, tmp / "flipped.bin") == Decision::RejectHashMismatch); }
  write_all(tmp / "short.bin", bytes.substr(0, bytes.size() - 1));
  CHECK(decide(policy, m, tmp / "short.bin") == Decision::RejectSizeMismatch);
  write_all(tmp / "long.bin", bytes + "x");
  CHECK(decide(policy, m, tmp / "long.bin") == Decision::RejectSizeMismatch);
  CHECK(decide(policy, m, tmp / "missing.bin") == Decision::RejectFileUnreadable);
}

int main() {
  const fs::path tmp = fs::temp_directory_path() / "triskelion_core_tests";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  test_version();
  test_base64();
  test_sha256(tmp);
  test_verify_and_decide(tmp);

  fs::remove_all(tmp);
  return report();
}
