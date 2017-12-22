// Copyright (c) 2017, The Regents of the University of California.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_UTILS_HASH_H_
#define BESS_UTILS_HASH_H_

#include <x86intrin.h>

namespace bess {
namespace utils {

// Returns hash values of 'len' bytes from 'buf'
inline uint32_t Hash(const void *buf, size_t len) {
  const uint64_t *buf64 = reinterpret_cast<const uint64_t *>(buf);
  uint64_t hash64 = 0;

  if (len >= sizeof(uint64_t)) {
    hash64 = _mm_crc32_u64(hash64, buf64[0]);
    len -= sizeof(uint64_t);
    buf64++;
  }

  uint32_t hash32 = _mm_crc32_u32(hash64 << 32, hash64 & 0xFFFFFFFF);
  return hash32;
}

template <typename T, int length>
struct StaticHasher {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    const uint64_t *u64 = reinterpret_cast<const uint64_t *>(&t);
    uint32_t ret = init;
    for (int i = 0; i < length; i += 8) {
      ret = _mm_crc32_u64(*u64++, static_cast<uint64_t>(ret));
    }
    return ret;
  }
};

template <typename T>
struct StaticHasher<T, 1> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u8(*reinterpret_cast<const uint8_t *>(&t), 0);
  }
};

template <typename T>
struct StaticHasher<T, 2> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u16(*reinterpret_cast<const uint16_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 3> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u16(*reinterpret_cast<const uint16_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 4> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u32(*reinterpret_cast<const uint32_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 5> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u32(*reinterpret_cast<const uint32_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 6> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u32(*reinterpret_cast<const uint32_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 7> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u32(*reinterpret_cast<const uint32_t *>(&t), init);
  }
};

template <typename T>
struct StaticHasher<T, 8> {
  uint32_t operator()(const T &t, uint32_t init = 0) const noexcept {
    return _mm_crc32_u64(*(reinterpret_cast<const uint64_t *>(&t)),
                         static_cast<uint64_t>(init));
  }
};

template <typename T>
struct Hasher {
  uint32_t operator()(const T &t) const noexcept { return Hash(&t, sizeof(T)); }

  uint32_t operator()(const T &t, size_t len) const noexcept {
    return Hash(&t, len);
  }
};

template <typename T1, typename T2>
struct PairHasher {
  uint32_t operator()(const std::pair<T1, T2> &p) const noexcept {
    const uint64_t kMul = 0x9ddfea08eb382d69ULL;
    uint32_t x = Hash(&p.first, sizeof(T1));
    uint32_t y = Hash(&p.second, sizeof(T2));
    uint64_t a = (x ^ y) * kMul;
    a ^= (a >> 47);
    uint64_t b = (x ^ y) * kMul;
    b ^= (b >> 47);
    b *= kMul;
    return static_cast<uint32_t>(b);
  }
};

}  // namespace utils
}  // namespace bess

#endif
