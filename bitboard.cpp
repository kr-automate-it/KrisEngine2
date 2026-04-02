#include "bitboard.h"
#include <cstring>

U64 PawnAttacks[COLOR_NB][SQUARE_NB];
U64 KnightAttacks[SQUARE_NB];
U64 KingAttacks[SQUARE_NB];

Magic BishopMagics[SQUARE_NB];
Magic RookMagics[SQUARE_NB];

// Tablice atakow magic bitboard
static U64 BishopTable[0x1480]; // sumaryczny rozmiar
static U64 RookTable[0x19000];

// Kierunki skoczka
static const int KnightDir[8][2] = {
    {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}
};

// Kierunki krola
static const int KingDir[8][2] = {
    {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
};

static U64 sliding_attack(const int deltas[4][2], Square sq, U64 occupied) {
    U64 attack = 0;
    for (int i = 0; i < 4; i++) {
        int r = rank_of(sq) + deltas[i][0];
        int f = file_of(sq) + deltas[i][1];
        while (r >= 0 && r < 8 && f >= 0 && f < 8) {
            Square s = make_square(f, r);
            attack |= square_bb(s);
            if (occupied & square_bb(s)) break;
            r += deltas[i][0];
            f += deltas[i][1];
        }
    }
    return attack;
}

static const int BishopDeltas[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static const int RookDeltas[4][2]   = {{-1,0},{1,0},{0,-1},{0,1}};

// Znane magiczne liczby (sprawdzone, uzywane w wielu silnikach)
static const U64 RookMagicNumbers[64] = {
    0x0080001020400080ULL, 0x0040001000200040ULL, 0x0080081000200080ULL, 0x0080040800100080ULL,
    0x0080020400080080ULL, 0x0080010200040080ULL, 0x0080008001000200ULL, 0x0080002040800100ULL,
    0x0000800020400080ULL, 0x0000400020005000ULL, 0x0000801000200080ULL, 0x0000800800100080ULL,
    0x0000800400080080ULL, 0x0000800200040080ULL, 0x0000800100020080ULL, 0x0000800040800100ULL,
    0x0000208000400080ULL, 0x0000404000201000ULL, 0x0000808010002000ULL, 0x0000808008001000ULL,
    0x0000808004000800ULL, 0x0000808002000400ULL, 0x0000010100020004ULL, 0x0000020000408104ULL,
    0x0000208080004000ULL, 0x0000200040005000ULL, 0x0000100080200080ULL, 0x0000080080100080ULL,
    0x0000040080080080ULL, 0x0000020080040080ULL, 0x0000010080800200ULL, 0x0000800080004100ULL,
    0x0000204000800080ULL, 0x0000200040401000ULL, 0x0000100080802000ULL, 0x0000080080801000ULL,
    0x0000040080800800ULL, 0x0000020080800400ULL, 0x0000020001010004ULL, 0x0000800040800100ULL,
    0x0000204000808000ULL, 0x0000200040008080ULL, 0x0000100020008080ULL, 0x0000080010008080ULL,
    0x0000040008008080ULL, 0x0000020004008080ULL, 0x0000010002008080ULL, 0x0000004081020004ULL,
    0x0000204000800080ULL, 0x0000200040008080ULL, 0x0000100020008080ULL, 0x0000080010008080ULL,
    0x0000040008008080ULL, 0x0000020004008080ULL, 0x0000800100020080ULL, 0x0000800041000080ULL,
    0x00FFFCDDFCED714AULL, 0x007FFCDDFCED714AULL, 0x003FFFCDFFD88096ULL, 0x0000040810002101ULL,
    0x0001000204080011ULL, 0x0001000204000801ULL, 0x0001000082000401ULL, 0x0001FFFAABFAD1A2ULL
};

static const U64 BishopMagicNumbers[64] = {
    0x0002020202020200ULL, 0x0002020202020000ULL, 0x0004010202000000ULL, 0x0004040080000000ULL,
    0x0001104000000000ULL, 0x0000821040000000ULL, 0x0000410410400000ULL, 0x0000104104104000ULL,
    0x0000040404040400ULL, 0x0000020202020200ULL, 0x0000040102020000ULL, 0x0000040400800000ULL,
    0x0000011040000000ULL, 0x0000008210400000ULL, 0x0000004104104000ULL, 0x0000002082082000ULL,
    0x0004000808080800ULL, 0x0002000404040400ULL, 0x0001000202020200ULL, 0x0000800802004000ULL,
    0x0000800400A00000ULL, 0x0000200100884000ULL, 0x0000400082082000ULL, 0x0000200041041000ULL,
    0x0002080010101000ULL, 0x0001040008080800ULL, 0x0000208004010400ULL, 0x0000404004010200ULL,
    0x0000840000802000ULL, 0x0000404002011000ULL, 0x0000808001041000ULL, 0x0000404000820800ULL,
    0x0001041000202000ULL, 0x0000820800101000ULL, 0x0000104400080800ULL, 0x0000020080080080ULL,
    0x0000404040040100ULL, 0x0000808100020100ULL, 0x0001010100020800ULL, 0x0000808080010400ULL,
    0x0000820820004000ULL, 0x0000410410002000ULL, 0x0000082088001000ULL, 0x0000002011000800ULL,
    0x0000080100400400ULL, 0x0001010101000200ULL, 0x0002020202000400ULL, 0x0001010101000200ULL,
    0x0000410410400000ULL, 0x0000208208200000ULL, 0x0000002084100000ULL, 0x0000000020880000ULL,
    0x0000001002020000ULL, 0x0000040408020000ULL, 0x0004040404040000ULL, 0x0002020202020000ULL,
    0x0000104104104000ULL, 0x0000002082082000ULL, 0x0000000020841000ULL, 0x0000000000208800ULL,
    0x0000000010020200ULL, 0x0000000404080200ULL, 0x0000040404040400ULL, 0x0002020202020200ULL
};

static const int RookShifts[64] = {
    52,53,53,53,53,53,53,52,53,54,54,54,54,54,54,53,
    53,54,54,54,54,54,54,53,53,54,54,54,54,54,54,53,
    53,54,54,54,54,54,54,53,53,54,54,54,54,54,54,53,
    53,54,54,54,54,54,54,53,52,53,53,53,53,53,53,52
};

static const int BishopShifts[64] = {
    58,59,59,59,59,59,59,58,59,59,59,59,59,59,59,59,
    59,59,57,57,57,57,59,59,59,59,57,55,55,57,59,59,
    59,59,57,55,55,57,59,59,59,59,57,57,57,57,59,59,
    59,59,59,59,59,59,59,59,58,59,59,59,59,59,59,58
};

// Generowanie maski (krawedzie sa wykluczone)
static U64 rook_mask(Square s) {
    U64 result = 0;
    int r = rank_of(s), f = file_of(s);
    for (int i = r + 1; i < 7; i++) result |= square_bb(make_square(f, i));
    for (int i = r - 1; i > 0; i--) result |= square_bb(make_square(f, i));
    for (int i = f + 1; i < 7; i++) result |= square_bb(make_square(i, r));
    for (int i = f - 1; i > 0; i--) result |= square_bb(make_square(i, r));
    return result;
}

static U64 bishop_mask(Square s) {
    U64 result = 0;
    int r = rank_of(s), f = file_of(s);
    for (int i = 1; r+i < 7 && f+i < 7; i++) result |= square_bb(make_square(f+i, r+i));
    for (int i = 1; r+i < 7 && f-i > 0; i++) result |= square_bb(make_square(f-i, r+i));
    for (int i = 1; r-i > 0 && f+i < 7; i++) result |= square_bb(make_square(f+i, r-i));
    for (int i = 1; r-i > 0 && f-i > 0; i++) result |= square_bb(make_square(f-i, r-i));
    return result;
}

// Enumerate all subsets of a mask
static void init_magic(Magic* magics, U64* table, const U64* magicNumbers,
                        const int* shifts, bool isBishop) {
    U64* ptr = table;
    for (int s = 0; s < 64; s++) {
        Magic& m = magics[s];
        m.mask = isBishop ? bishop_mask(Square(s)) : rook_mask(Square(s));
        m.magic = magicNumbers[s];
        m.shift = shifts[s];
        m.attacks = ptr;

        int bits = popcount(m.mask);
        int size = 1 << bits;

        for (int i = 0; i < size; i++) {
            // Enumerate i-th subset of mask using Carry-Rippler
            U64 occ = 0;
            int idx = i;
            U64 tmp = m.mask;
            for (int j = 0; j < bits; j++) {
                Square bit = pop_lsb(tmp);
                if (idx & (1 << j))
                    occ |= square_bb(bit);
            }

            U64 index = m.index(occ);
            m.attacks[index] = isBishop
                ? sliding_attack(BishopDeltas, Square(s), occ)
                : sliding_attack(RookDeltas, Square(s), occ);
        }

        ptr += 1ULL << (64 - m.shift);
    }
}

void init_bitboards() {
    // Ataki pionkow
    for (int s = 0; s < 64; s++) {
        int r = rank_of(s), f = file_of(s);
        PawnAttacks[WHITE][s] = 0;
        PawnAttacks[BLACK][s] = 0;

        if (r < 7) {
            if (f > 0) PawnAttacks[WHITE][s] |= square_bb(make_square(f-1, r+1));
            if (f < 7) PawnAttacks[WHITE][s] |= square_bb(make_square(f+1, r+1));
        }
        if (r > 0) {
            if (f > 0) PawnAttacks[BLACK][s] |= square_bb(make_square(f-1, r-1));
            if (f < 7) PawnAttacks[BLACK][s] |= square_bb(make_square(f+1, r-1));
        }
    }

    // Ataki skoczka
    for (int s = 0; s < 64; s++) {
        KnightAttacks[s] = 0;
        int r = rank_of(s), f = file_of(s);
        for (auto& d : KnightDir) {
            int nr = r + d[0], nf = f + d[1];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                KnightAttacks[s] |= square_bb(make_square(nf, nr));
        }
    }

    // Ataki krola
    for (int s = 0; s < 64; s++) {
        KingAttacks[s] = 0;
        int r = rank_of(s), f = file_of(s);
        for (auto& d : KingDir) {
            int nr = r + d[0], nf = f + d[1];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                KingAttacks[s] |= square_bb(make_square(nf, nr));
        }
    }

    // Magic bitboards
    init_magic(BishopMagics, BishopTable, BishopMagicNumbers, BishopShifts, true);
    init_magic(RookMagics, RookTable, RookMagicNumbers, RookShifts, false);
}
