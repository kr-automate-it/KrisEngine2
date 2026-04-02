#pragma once
#include "types.h"

namespace Zobrist {

extern U64 psq[PIECE_NB][SQUARE_NB];
extern U64 enpassant[8];
extern U64 castling[16];
extern U64 side;

void init();

} // namespace Zobrist
