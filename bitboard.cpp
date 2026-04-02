#include "bitboard.h"

// === Attack tables ===
U64 PawnAttacks[COLOR_NB][SQUARE_NB];
U64 KnightAttacks[SQUARE_NB];
U64 KingAttacks[SQUARE_NB];

// === Magic bitboard storage ===
Magic BishopMagics[SQUARE_NB];
Magic RookMagics[SQUARE_NB];

static U64 BishopTable[0x1480]; // 5248 entries
static U64 RookTable[0x19000];  // 102400 entries

// === Slider attack computation (used for magic init) ===

static U64 sliding_attack(Square sq, U64 occupied, const int dx[], const int dy[], int dirs) {
    U64 attacks = 0;
    for (int d = 0; d < dirs; d++) {
        int x = file_of(sq) + dx[d];
        int y = rank_of(sq) + dy[d];
        while (x >= 0 && x <= 7 && y >= 0 && y <= 7) {
            Square s = make_square(x, y);
            attacks |= square_bb(s);
            if (occupied & square_bb(s)) break;
            x += dx[d];
            y += dy[d];
        }
    }
    return attacks;
}

static U64 bishop_attack_slow(Square sq, U64 occupied) {
    static const int dx[] = {-1, 1, -1, 1};
    static const int dy[] = {-1, -1, 1, 1};
    return sliding_attack(sq, occupied, dx, dy, 4);
}

static U64 rook_attack_slow(Square sq, U64 occupied) {
    static const int dx[] = {-1, 1, 0, 0};
    static const int dy[] = {0, 0, -1, 1};
    return sliding_attack(sq, occupied, dx, dy, 4);
}

// === Magic number tables (precomputed) ===
// These are known-good magic numbers for each square

static const U64 BishopMagicNumbers[64] = {
    0x89a1121896040240ULL, 0x2004844802002010ULL, 0x2068080051921000ULL,
    0x62880a0220200808ULL, 0x0004042004000000ULL, 0x0100822020200011ULL,
    0xc00444222012000aULL, 0x0028808801216001ULL, 0x0400492088408100ULL,
    0x0201c401040c0084ULL, 0x00840800910a0010ULL, 0x0000082080240060ULL,
    0x2000840504006000ULL, 0x30010c4108405004ULL, 0x1008005410080802ULL,
    0x8144042209100900ULL, 0x0208081020014400ULL, 0x004800201208ca00ULL,
    0x0f18140408012008ULL, 0x1004002802102001ULL, 0x0841000820080811ULL,
    0x0040200200a42008ULL, 0x0000800054042000ULL, 0x88010400410c9000ULL,
    0x0520040470104290ULL, 0x1004040051500081ULL, 0x2002081833080021ULL,
    0x000400c00c010142ULL, 0x941408200c002000ULL, 0x0658810000806011ULL,
    0x0188071040440a00ULL, 0x4800404002011c00ULL, 0x0104442040404200ULL,
    0x0511080200222004ULL, 0x0000801220a2000cULL, 0x0000215040029000ULL,
    0x0000100400c00240ULL, 0x0021041001a41308ULL, 0x0803000804000800ULL,
    0x0c08220028800c00ULL, 0x0000820040082200ULL, 0x000400a080880100ULL,
    0x0800010c42004400ULL, 0x4410a10000000008ULL, 0x0001000204080022ULL,
    0x0001001012200900ULL, 0x0001004108020000ULL, 0x0040200a22080000ULL,
    0x0000000200010042ULL, 0x2000000000208010ULL, 0x0000000000880020ULL,
    0x0004040002000081ULL, 0x0200104001000080ULL, 0x0018008008000400ULL,
    0x000000100800a001ULL, 0x0000080200110040ULL, 0x0001810080480080ULL,
    0x0028000404040020ULL, 0x0000000000040100ULL, 0x0200004420400048ULL,
    0x0400020000280080ULL, 0x0020100080080080ULL, 0x0001100480000100ULL,
    0x0101008044002200ULL,
};

static const U64 RookMagicNumbers[64] = {
    0x0080001020400080ULL, 0x0040001000200040ULL, 0x0080081000200080ULL,
    0x0080040800100080ULL, 0x0080020400080080ULL, 0x0080010200040080ULL,
    0x0080008001000200ULL, 0x0080002040800100ULL, 0x0000800020400080ULL,
    0x0000400020005000ULL, 0x0000801000200080ULL, 0x0000800800100080ULL,
    0x0000800400080080ULL, 0x0000800200040080ULL, 0x0000800100020080ULL,
    0x0000800040800100ULL, 0x0000208000400080ULL, 0x0000404000201000ULL,
    0x0000808010002000ULL, 0x0000808008001000ULL, 0x0000808004000800ULL,
    0x0000808002000400ULL, 0x0000010100020004ULL, 0x0000020000408104ULL,
    0x0000208080004000ULL, 0x0000200040005000ULL, 0x0000100080200080ULL,
    0x0000080080100080ULL, 0x0000040080080080ULL, 0x0000020080040080ULL,
    0x0000010080800200ULL, 0x0000800080004100ULL, 0x0000204000800080ULL,
    0x0000200040401000ULL, 0x0000100080802000ULL, 0x0000080080801000ULL,
    0x0000040080800800ULL, 0x0000020080800400ULL, 0x0000020001010004ULL,
    0x0000800040800100ULL, 0x0000204000808000ULL, 0x0000200040008080ULL,
    0x0000100020008080ULL, 0x0000080010008080ULL, 0x0000040008008080ULL,
    0x0000020004008080ULL, 0x0000010002008080ULL, 0x0000004081020004ULL,
    0x0000204000800080ULL, 0x0000200040008080ULL, 0x0000100020008080ULL,
    0x0000080010008080ULL, 0x0000040008008080ULL, 0x0000020004008080ULL,
    0x0000800100020080ULL, 0x0000800041000080ULL, 0x00FFFCDDFCED714AULL,
    0x007FFCDDFCED714AULL, 0x003FFFCDFFD88096ULL, 0x0000040810002101ULL,
    0x0001000204080011ULL, 0x0001000204000801ULL, 0x0001000082000401ULL,
    0x0001FFFAABFAD1A2ULL,
};

// === Bishop/Rook relevant occupancy masks ===

static U64 bishop_mask(Square sq) {
    U64 mask = 0;
    int f = file_of(sq), r = rank_of(sq);
    static const int dx[] = {-1, 1, -1, 1};
    static const int dy[] = {-1, -1, 1, 1};
    for (int d = 0; d < 4; d++) {
        int x = f + dx[d], y = r + dy[d];
        while (x > 0 && x < 7 && y > 0 && y < 7) {
            mask |= square_bb(make_square(x, y));
            x += dx[d]; y += dy[d];
        }
    }
    return mask;
}

static U64 rook_mask(Square sq) {
    U64 mask = 0;
    int f = file_of(sq), r = rank_of(sq);
    for (int x = f + 1; x < 7; x++) mask |= square_bb(make_square(x, r));
    for (int x = f - 1; x > 0; x--) mask |= square_bb(make_square(x, r));
    for (int y = r + 1; y < 7; y++) mask |= square_bb(make_square(f, y));
    for (int y = r - 1; y > 0; y--) mask |= square_bb(make_square(f, y));
    return mask;
}

// === Enumerate subsets of a mask (Carry-Rippler) ===

static void init_magics(Magic magics[], U64 table[], const U64 magicNumbers[],
                         U64 (*mask_fn)(Square), U64 (*attack_fn)(Square, U64)) {
    U64* ptr = table;

    for (int sq = 0; sq < 64; sq++) {
        Magic& m = magics[sq];
        m.mask  = mask_fn(Square(sq));
        m.magic = magicNumbers[sq];
        m.shift = 64 - popcount(m.mask);
        m.attacks = ptr;

        // Enumerate all subsets of mask using Carry-Rippler
        U64 occ = 0;
        int size = 0;
        do {
            U64 idx = m.index(occ);
            m.attacks[idx] = attack_fn(Square(sq), occ);
            size++;
            occ = (occ - m.mask) & m.mask;
        } while (occ);

        ptr += (1ULL << popcount(m.mask));
    }
}

// === Non-slider attack init ===

static void init_pawn_attacks() {
    for (int sq = 0; sq < 64; sq++) {
        int f = file_of(Square(sq)), r = rank_of(Square(sq));
        U64 w = 0, b = 0;
        if (f > 0 && r < 7) w |= square_bb(make_square(f - 1, r + 1));
        if (f < 7 && r < 7) w |= square_bb(make_square(f + 1, r + 1));
        if (f > 0 && r > 0) b |= square_bb(make_square(f - 1, r - 1));
        if (f < 7 && r > 0) b |= square_bb(make_square(f + 1, r - 1));
        PawnAttacks[WHITE][sq] = w;
        PawnAttacks[BLACK][sq] = b;
    }
}

static void init_knight_attacks() {
    static const int dx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
    static const int dy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
    for (int sq = 0; sq < 64; sq++) {
        U64 att = 0;
        int f = file_of(Square(sq)), r = rank_of(Square(sq));
        for (int i = 0; i < 8; i++) {
            int x = f + dx[i], y = r + dy[i];
            if (x >= 0 && x <= 7 && y >= 0 && y <= 7)
                att |= square_bb(make_square(x, y));
        }
        KnightAttacks[sq] = att;
    }
}

static void init_king_attacks() {
    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int sq = 0; sq < 64; sq++) {
        U64 att = 0;
        int f = file_of(Square(sq)), r = rank_of(Square(sq));
        for (int i = 0; i < 8; i++) {
            int x = f + dx[i], y = r + dy[i];
            if (x >= 0 && x <= 7 && y >= 0 && y <= 7)
                att |= square_bb(make_square(x, y));
        }
        KingAttacks[sq] = att;
    }
}

// === Main init ===

void bitboard_init() {
    init_pawn_attacks();
    init_knight_attacks();
    init_king_attacks();
    init_magics(BishopMagics, BishopTable, BishopMagicNumbers, bishop_mask, bishop_attack_slow);
    init_magics(RookMagics, RookTable, RookMagicNumbers, rook_mask, rook_attack_slow);
}
