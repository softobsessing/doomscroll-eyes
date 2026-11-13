#include "pairing.h"
#include <string.h>
#include "sha256.h"

namespace eyes {

void Pairing::load(const uint8_t* data, size_t len) {
  n_ = 0;
  for (size_t off = 0; off + KEY_LEN <= len && n_ < MAX_KEYS; off += KEY_LEN) memcpy(keys_[n_++], data + off, KEY_LEN);
}

size_t Pairing::save(uint8_t* out) const {
  for (int i = 0; i < n_; i++) memcpy(out + i * KEY_LEN, keys_[i], KEY_LEN);
  return (size_t)n_ * KEY_LEN;
}

bool Pairing::add(const uint8_t key[KEY_LEN]) {
  for (int i = 0; i < n_; i++)
    if (memcmp(keys_[i], key, KEY_LEN) == 0) return false;
  if (n_ == MAX_KEYS) {   // forget the oldest laptop
    memmove(keys_[0], keys_[1], (MAX_KEYS - 1) * KEY_LEN);
    n_--;
  }
  memcpy(keys_[n_++], key, KEY_LEN);
  return true;
}

bool Pairing::verify(const char* nonce, const uint8_t mac[KEY_LEN]) const {
  bool ok = false;
  for (int i = 0; i < n_; i++) {
    uint8_t h[32];
    hmacSha256(keys_[i], KEY_LEN, (const uint8_t*)nonce, strlen(nonce), h);
    uint8_t diff = 0;   // constant-time compare
    for (int j = 0; j < KEY_LEN; j++) diff |= h[j] ^ mac[j];
    ok |= diff == 0;
  }
  return ok;
}

}  // namespace eyes
