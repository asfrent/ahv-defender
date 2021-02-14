#ifndef AHV_DEFENDER_BCRYPT_HASHER_H_
#define AHV_DEFENDER_BCRYPT_HASHER_H_

#include <cstring>
#include <string>

#define BCRYPT_INPUT_LEN 16
#define BCRYPT_HASH_LEN 64
#define BCRYPT_FACTOR 4

extern "C" {
char *crypt_gensalt_rn(__const char *prefix, unsigned long count, __const char *input, int size, char *output, int output_size);
char *crypt_rn(__const char *key, __const char *setting, void *data, int size);
}

class BCryptHasher {
 public:
  BCryptHasher() {
    memset(setting, 0, BCRYPT_HASH_LEN);
    crypt_gensalt_rn("$2a$", BCRYPT_FACTOR, (char*) input, BCRYPT_INPUT_LEN, setting, BCRYPT_HASH_LEN);
  }

  std::string ComputeHash(const std::string& plaintext) const {
    char hash[BCRYPT_HASH_LEN] = {0};
    crypt_rn(plaintext.c_str(), setting, hash, BCRYPT_HASH_LEN);
    std::string result(hash);
    result = result.substr(result.size() - 31);
    return result;
  }

 private:
  char setting[BCRYPT_HASH_LEN];
  static const unsigned char input[BCRYPT_INPUT_LEN];
};

const unsigned char BCryptHasher::input[16] = {
  0xfb, 0xf5, 0x7f, 0xb4, 0x30, 0xe7, 0x07, 0x10,
  0xa3, 0x1a, 0x8a, 0x03, 0x06, 0x26, 0xd0, 0xcb,
};

#endif  // AHV_DEFENDER_BCRYPT_HASHER_H_