#pragma once
#include "types.h"

// === Bitboard constants and utilities ===
// TODO: magic bitboards, attack tables, init

constexpr U64 FileA_BB = 0x0101010101010101ULL;
constexpr U64 FileH_BB = 0x8080808080808080ULL;
constexpr U64 Rank1_BB = 0xFFULL;
constexpr U64 Rank8_BB = 0xFF00000000000000ULL;

inline U64 square_bb(Square s) { return 1ULL << s; }
inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline Square lsb(U64 b) { return Square(__builtin_ctzll(b)); }
inline Square pop_lsb(U64& b) { Square s = lsb(b); b &= b - 1; return s; }

void bitboard_init();
