// The laptops this board trusts over Bluetooth: up to 8 random 16-byte keys, kept in flash (NVS).
// A laptop proves it has a key by answering the board's one-time challenge (nonce) with
// HMAC-SHA256(key, nonce), so the key itself never travels over the air after pairing.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace eyes {

class Pairing {
 public:
  static constexpr int MAX_KEYS = 8, KEY_LEN = 16;

  void load(const uint8_t* data, size_t len);   // as written by save()
  size_t save(uint8_t* out) const;              // out must hold MAX_KEYS * KEY_LEN bytes
  int count() const { return n_; }
  bool add(const uint8_t key[KEY_LEN]);         // false if already known; drops the oldest when full
  void clear() { n_ = 0; }
  bool verify(const char* nonce, const uint8_t mac[KEY_LEN]) const;   // mac = first 16 bytes of the HMAC

 private:
  uint8_t keys_[MAX_KEYS][KEY_LEN] = {};
  int n_ = 0;
};

}  // namespace eyes
