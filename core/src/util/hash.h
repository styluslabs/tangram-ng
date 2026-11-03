#pragma once

#include <cstdint>
#include <functional> // for hash function

// The generic hash_combine used in Boost
// http://www.boost.org/doc/libs/1_35_0/doc/html/boost/hash_combine_id241013.html
template <class T>
inline void hash_combine(std::size_t & seed, const T & v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// FNV-1a hash of a null-terminated string's characters (std::hash<const char*> hashes the pointer, and
//  std::hash<std::string> needs a copy); the same on every platform and standard library
inline std::size_t hash_cstr(const char* s) {
    uint64_t h = 14695981039346656037ull;
    for (; *s; ++s) { h = (h ^ uint8_t(*s)) * 1099511628211ull; }
    return std::size_t(h);
}
