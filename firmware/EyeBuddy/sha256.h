// SHA-256 and HMAC-SHA256 in plain C++ (no ESP-IDF), so the pairing code runs and is tested on the Mac too.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace eyes {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]);

// hex helpers (lowercase out; either case in). fromHex returns false on bad length/characters.
void toHex(const uint8_t* in, size_t n, char* out);   // writes 2n chars + '\0'
bool fromHex(const char* hex, uint8_t* out, size_t n);

}  // namespace eyes
