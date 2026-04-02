#include "zobrist.h"
#include <cstdlib>

namespace Zobrist {

U64 psq[PIECE_NB][SQUARE_NB];
U64 enpassant[8];
U64 castling[16];
U64 side;

// Simple PRNG for reproducible keys
static U64 rand64() {
    static U64 s = 1070372ull;
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
}

void init() {
    for (int p = 0; p < PIECE_NB; p++)
        for (int s = 0; s < SQUARE_NB; s++)
            psq[p][s] = rand64();

    for (int f = 0; f < 8; f++)
        enpassant[f] = rand64();

    for (int cr = 0; cr < 16; cr++)
        castling[cr] = rand64();

    side = rand64();
}

} // namespace Zobrist
