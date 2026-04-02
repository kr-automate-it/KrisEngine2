#pragma once
#include "types.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

// === Operacje bitowe ===

inline int popcount(U64 b) {
#if defined(__GNUC__)
    return __builtin_popcountll(b);
#elif defined(_MSC_VER)
    return (int)__popcnt64(b);
#else
    // Fallback
    int count = 0;
    while (b) { count++; b &= b - 1; }
    return count;
#endif
}

inline Square lsb(U64 b) {
#if defined(__GNUC__)
    return Square(__builtin_ctzll(b));
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return Square(idx);
#else
    // De Bruijn
    static const int index64[64] = {
        0,1,48,2,57,49,28,3,61,58,50,42,38,29,17,4,
        62,55,59,36,53,51,43,22,45,39,33,30,24,18,12,5,
        63,47,56,27,60,41,37,16,54,35,52,21,44,32,23,11,
        46,26,40,15,34,20,31,10,25,14,19,9,13,8,7,6
    };
    return Square(index64[((b & -b) * 0x03f79d71b4cb0a89ULL) >> 58]);
#endif
}

inline Square pop_lsb(U64& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

// === Stale bitboardowe ===

constexpr U64 FileA_BB = 0x0101010101010101ULL;
constexpr U64 FileB_BB = FileA_BB << 1;
constexpr U64 FileC_BB = FileA_BB << 2;
constexpr U64 FileD_BB = FileA_BB << 3;
constexpr U64 FileE_BB = FileA_BB << 4;
constexpr U64 FileF_BB = FileA_BB << 5;
constexpr U64 FileG_BB = FileA_BB << 6;
constexpr U64 FileH_BB = FileA_BB << 7;

constexpr U64 Rank1_BB = 0xFFULL;
constexpr U64 Rank2_BB = Rank1_BB << 8;
constexpr U64 Rank3_BB = Rank1_BB << 16;
constexpr U64 Rank4_BB = Rank1_BB << 24;
constexpr U64 Rank5_BB = Rank1_BB << 32;
constexpr U64 Rank6_BB = Rank1_BB << 40;
constexpr U64 Rank7_BB = Rank1_BB << 48;
constexpr U64 Rank8_BB = Rank1_BB << 56;

constexpr U64 square_bb(Square s) { return 1ULL << s; }

// === Tablice atakow (inicjalizowane w bitboard.cpp) ===

extern U64 PawnAttacks[COLOR_NB][SQUARE_NB];
extern U64 KnightAttacks[SQUARE_NB];
extern U64 KingAttacks[SQUARE_NB];

// Magic bitboards dla goncow i wiez
struct Magic {
    U64* attacks;
    U64  mask;
    U64  magic;
    int  shift;

    U64 index(U64 occupied) const {
        return ((occupied & mask) * magic) >> shift;
    }
};

extern Magic BishopMagics[SQUARE_NB];
extern Magic RookMagics[SQUARE_NB];

inline U64 bishop_attacks(Square s, U64 occupied) {
    return BishopMagics[s].attacks[BishopMagics[s].index(occupied)];
}

inline U64 rook_attacks(Square s, U64 occupied) {
    return RookMagics[s].attacks[RookMagics[s].index(occupied)];
}

inline U64 queen_attacks(Square s, U64 occupied) {
    return bishop_attacks(s, occupied) | rook_attacks(s, occupied);
}

void init_bitboards();
