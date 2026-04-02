#include "eval.h"

namespace Eval {

// === Helper functions ===

// Pinned direction: detects if a piece on 'sq' is a blocker for a king.
// Returns direction encoding:
//   0 = not pinned
//   1 = horizontal, 2 = topleft-bottomright diagonal, 3 = vertical,
//   4 = topright-bottomleft diagonal
// Positive = our piece pinned to our king (blocker for our king)
// Negative = enemy piece blocking (blocker for enemy king)
//
// Algorithm: for each of 8 directions from the square:
//   - Look outward: if we find our king, this direction leads to king
//   - Then look opposite direction: if we find enemy slider that matches
//     (queen, or bishop on diagonal, or rook on straight) → pinned
static int pinned_direction(const Position& pos, Square sq) {
    Piece pc = pos.piece_on(sq);
    if (pc == NO_PIECE) return 0;
    PieceType pt = type_of(pc);
    if (pt < PAWN || pt > KING) return 0;

    int color = (color_of(pc) == WHITE) ? 1 : -1;

    // 8 directions: dx,dy pairs
    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    int sx = file_of(sq), sy = rank_of(sq);

    for (int i = 0; i < 8; i++) {
        int ix = dx[i], iy = dy[i];

        // Look outward from sq: do we hit our (white) king?
        bool king = false;
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * ix, ny = sy + d * iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece b = pos.piece_on(make_square(nx, ny));
            if (b == W_KING) { king = true; break; }
            if (b != NO_PIECE) break;
        }

        if (king) {
            // Look opposite direction: enemy slider?
            for (int d = 1; d < 8; d++) {
                int nx = sx - d * ix, ny = sy - d * iy;
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
                Piece b = pos.piece_on(make_square(nx, ny));
                PieceType bpt = type_of(b);
                if (b != NO_PIECE && color_of(b) == BLACK) {
                    bool isDiag = (ix * iy != 0);
                    if (bpt == QUEEN
                        || (bpt == BISHOP && isDiag)
                        || (bpt == ROOK && !isDiag)) {
                        // Pinned! Encode direction: |ix + iy * 3|
                        return std::abs(ix + iy * 3) * color;
                    }
                }
                if (b != NO_PIECE) break; // blocked by another piece
            }
        }
    }
    return 0;
}

// Pinned: returns true if piece on square is pinned to our king
static bool pinned(const Position& pos, Square sq) {
    return pinned_direction(pos, sq) > 0;
}

// === Attack helpers ===

// Knight attack: count attacks on 'sq' by white (unpinned) knights
// If s2 is given, only count attack from that specific knight square
static int knight_attack(const Position& pos, Square sq, Square s2 = SQ_NONE) {
    if (sq == SQ_NONE) return 0; // sum mode handled by caller
    int v = 0;
    // 8 knight move offsets: (dx, dy)
    static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
    static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
    int sx = file_of(sq), sy = rank_of(sq);

    for (int i = 0; i < 8; i++) {
        int nx = sx + kdx[i], ny = sy + kdy[i];
        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
        Square from = make_square(nx, ny);
        if (s2 != SQ_NONE && from != s2) continue;
        if (pos.piece_on(from) == W_KNIGHT && !pinned(pos, from))
            v++;
    }
    return v;
}

// Sum knight attacks over all squares (total attack count for white)
static int knight_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += knight_attack(pos, Square(sq));
    return v;
}

// Bishop xray attack: count attacks on 'sq' by white bishops
// Includes x-ray through queens (both colors) — bishop sees through queens
// Respects pin direction: pinned bishop can still attack along pin line
static int bishop_xray_attack(const Position& pos, Square sq, Square s2 = SQ_NONE) {
    if (sq == SQ_NONE) return 0;
    int v = 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // 4 diagonal directions
    static const int dx[] = {-1, 1, -1, 1};
    static const int dy[] = {-1, -1, 1, 1};

    for (int i = 0; i < 4; i++) {
        int ix = dx[i], iy = dy[i];
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * ix, ny = sy + d * iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Square from = make_square(nx, ny);
            Piece b = pos.piece_on(from);

            if (b == W_BISHOP && (s2 == SQ_NONE || from == s2)) {
                // Check pin: pinned bishop can attack along its pin direction
                int dir = pinned_direction(pos, from);
                if (dir == 0 || std::abs(ix + iy * 3) == dir)
                    v++;
            }
            // X-ray: see through queens (both colors), stop on anything else
            if (b != NO_PIECE && type_of(b) != QUEEN) break;
        }
    }
    return v;
}

static int bishop_xray_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += bishop_xray_attack(pos, Square(sq));
    return v;
}

// Rook xray attack: count attacks on 'sq' by white rooks
// X-ray through queens (both colors) AND our own rooks — rook sees through R/Q
// Respects pin direction
static int rook_xray_attack(const Position& pos, Square sq, Square s2 = SQ_NONE) {
    if (sq == SQ_NONE) return 0;
    int v = 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // 4 straight directions: left, right, down, up
    static const int dx[] = {-1, 1, 0, 0};
    static const int dy[] = {0, 0, -1, 1};

    for (int i = 0; i < 4; i++) {
        int ix = dx[i], iy = dy[i];
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * ix, ny = sy + d * iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Square from = make_square(nx, ny);
            Piece b = pos.piece_on(from);

            if (b == W_ROOK && (s2 == SQ_NONE || from == s2)) {
                int dir = pinned_direction(pos, from);
                if (dir == 0 || std::abs(ix + iy * 3) == dir)
                    v++;
            }
            // X-ray: see through our rooks (R), and queens of both colors (Q/q)
            PieceType bpt = type_of(b);
            if (b != NO_PIECE && bpt != QUEEN && !(b == W_ROOK)) break;
        }
    }
    return v;
}

static int rook_xray_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += rook_xray_attack(pos, Square(sq));
    return v;
}

// Queen attack: count attacks on 'sq' by white queens
// No x-ray — queen stops at first piece in each direction
// Respects pin direction
static int queen_attack(const Position& pos, Square sq, Square s2 = SQ_NONE) {
    if (sq == SQ_NONE) return 0;
    int v = 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // 8 directions (4 straight + 4 diagonal)
    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (int i = 0; i < 8; i++) {
        int ix = dx[i], iy = dy[i];
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * ix, ny = sy + d * iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Square from = make_square(nx, ny);
            Piece b = pos.piece_on(from);

            if (b == W_QUEEN && (s2 == SQ_NONE || from == s2)) {
                int dir = pinned_direction(pos, from);
                if (dir == 0 || std::abs(ix + iy * 3) == dir)
                    v++;
            }
            if (b != NO_PIECE) break; // no x-ray for queen
        }
    }
    return v;
}

static int queen_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += queen_attack(pos, Square(sq));
    return v;
}

// Pawn attack: count attacks on 'sq' by white pawns
// Simple: check if white pawn on (sq.x-1, sq.y+1) or (sq.x+1, sq.y+1)
// No pin consideration, no en passant
static int pawn_attack(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int v = 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // White pawn attacks from below-left and below-right
    if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == W_PAWN) v++;
    if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == W_PAWN) v++;
    return v;
}

static int pawn_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += pawn_attack(pos, Square(sq));
    return v;
}

// King attack: count attacks on 'sq' by white king (adjacent squares)
static int king_attack(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    int v = 0;
    for (int i = 0; i < 8; i++) {
        int nx = sx + dx[i], ny = sy + dy[i];
        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
        if (pos.piece_on(make_square(nx, ny)) == W_KING) v++;
    }
    return v;
}

// Attack: total attacks on 'sq' by all white pieces
// Bishop/rook use x-ray, pawn ignores pins, queen no x-ray
static int attack(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    return pawn_attack(pos, sq)
         + king_attack(pos, sq)
         + knight_attack(pos, sq)
         + bishop_xray_attack(pos, sq)
         + rook_xray_attack(pos, sq)
         + queen_attack(pos, sq);
}

static int attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += attack(pos, Square(sq));
    return v;
}

// Queen attack diagonal: count attacks on 'sq' by white queen using diagonal directions only
// Same as queen_attack but only diagonal (no straight lines)
static int queen_attack_diagonal(const Position& pos, Square sq, Square s2 = SQ_NONE) {
    if (sq == SQ_NONE) return 0;
    int v = 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // 4 diagonal directions only
    static const int dx[] = {-1, 1, -1, 1};
    static const int dy[] = {-1, -1, 1, 1};

    for (int i = 0; i < 4; i++) {
        int ix = dx[i], iy = dy[i];
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * ix, ny = sy + d * iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Square from = make_square(nx, ny);
            Piece b = pos.piece_on(from);

            if (b == W_QUEEN && (s2 == SQ_NONE || from == s2)) {
                int dir = pinned_direction(pos, from);
                if (dir == 0 || std::abs(ix + iy * 3) == dir)
                    v++;
            }
            if (b != NO_PIECE) break;
        }
    }
    return v;
}

// === Sub-terms (TODO: implement each) ===

// === Imbalance ===
// Evaluates material imbalance using Stockfish's piece interaction tables.
// qo = bonus for OUR piece interactions, qt = bonus for THEIR piece interactions
static int imbalance(const Position& pos, Color us) {
    static const int qo[6][6] = {
        {0},
        {40, 38},
        {32, 255, -62},
        {0, 104, 4, 0},
        {-26, -2, 47, 105, -208},
        {-189, 24, 117, 133, -134, -6}
    };
    static const int qt[6][6] = {
        {0},
        {36, 0},
        {9, 63, 0},
        {59, 65, 42, 0},
        {46, 39, 24, -24, 0},
        {97, 100, -42, 137, 268, 0}
    };

    Color them = ~us;
    // Count: 0=bishopPair, 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen
    int ours[6] = {0}, theirs[6] = {0};
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < PAWN || pt > QUEEN) continue;
        if (color_of(p) == us) ours[pt]++;
        else                   theirs[pt]++;
    }
    int ourBP  = (ours[BISHOP] >= 2) ? 1 : 0;
    int theirBP = (theirs[BISHOP] >= 2) ? 1 : 0;

    int v = 0;
    for (int j = 0; j <= 5; j++) {
        int cj = (j == 0) ? ourBP : ours[j];
        if (!cj) continue;
        for (int i = 0; i <= j; i++) {
            v += qo[j][i] * ((i == 0) ? ourBP : ours[i]) * cj;
            v += qt[j][i] * ((i == 0) ? theirBP : theirs[i]) * cj;
        }
    }
    return v;
}

static int imbalance_total(const Position& pos) {
    return (imbalance(pos, WHITE) - imbalance(pos, BLACK)) / 16;
}

// Pawnless flank: returns 1 if enemy king is on a flank with no pawns (any color)
// Checks the 3-4 files around the enemy king for any pawns
static int pawnless_flank(const Position& pos) {
    // Find enemy (black) king file
    int kx = -1;
    for (int sq = 0; sq < 64; sq++) {
        if (pos.piece_on(Square(sq)) == B_KING) {
            kx = file_of(Square(sq));
            break;
        }
    }
    if (kx < 0) return 0;

    // Count ALL pawns (both colors) on relevant files
    int pawns[8] = {0};
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (type_of(p) == PAWN)
            pawns[file_of(Square(sq))]++;
    }

    int sum;
    if (kx == 0)      sum = pawns[0] + pawns[1] + pawns[2];
    else if (kx < 3)  sum = pawns[0] + pawns[1] + pawns[2] + pawns[3];
    else if (kx < 5)  sum = pawns[2] + pawns[3] + pawns[4] + pawns[5];
    else if (kx < 7)  sum = pawns[4] + pawns[5] + pawns[6] + pawns[7];
    else               sum = pawns[5] + pawns[6] + pawns[7];

    return (sum == 0) ? 1 : 0;
}

// Strength square: king shelter strength for a given square
// Evaluates pawn shield quality around a potential king position
// Uses weakness table indexed by [mirrored file][pawn rank]
static int strength_square(const Position& pos, Square square) {
    static const int weakness[4][7] = {
        {-6, 81, 93, 58, 39, 18, 25},
        {-43, 61, 35, -49, -29, -11, -63},
        {-10, 75, 23, -2, 32, 3, -45},
        {-39, -13, -29, -52, -48, -67, -166}
    };

    int v = 5;
    int kx = std::clamp(file_of(square), 1, 6);
    int ky = rank_of(square);

    // Check 3 files around king position
    for (int x = kx - 1; x <= kx + 1; x++) {
        // Find our (white) pawn rank on this file that is NOT defended by adjacent pawns
        // "us" = rank of undefended enemy (black) pawn, 0 if none
        int us = 0;
        for (int y = 7; y >= ky; y--) {
            Square sq = make_square(x, y);
            if (pos.piece_on(sq) == B_PAWN) {
                // Check if this black pawn is defended by white pawns
                bool defended = false;
                if (x > 0 && y < 7 && pos.piece_on(make_square(x - 1, y + 1)) == W_PAWN)
                    defended = true;
                if (x < 7 && y < 7 && pos.piece_on(make_square(x + 1, y + 1)) == W_PAWN)
                    defended = true;
                if (!defended) us = y;
            }
        }
        int f = std::min(x, 7 - x); // mirror file to 0-3
        if (f < 4 && us < 7)
            v += weakness[f][us];
    }

    return v;
}

static int strength_square_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += strength_square(pos, Square(sq));
    return v;
}

// Storm square: enemy pawn storm evaluation for a given square
// Returns MG value by default, EG value if eg=true
// Checks 3 files around king: finds our undefended pawn (us) and their pawn (them)
// If blocked (them directly in front of us): use blocked table
// Otherwise: use unblocked table
static int storm_square(const Position& pos, Square square, bool eg = false) {
    static const int unblockedstorm[4][7] = {
        {85, -289, -166, 97, 50, 45, 50},
        {46, -25, 122, 45, 37, -10, 20},
        {-6, 51, 168, 34, -2, -22, -14},
        {-15, -11, 101, 4, 11, -15, -29}
    };
    static const int blockedstorm[2][7] = {
        {0, 0, 76, -10, -7, -4, -1},
        {0, 0, 78, 15, 10, 6, 2}
    };

    int v = 0, ev = 5;
    int kx = std::clamp(file_of(square), 1, 6);
    int ky = rank_of(square);

    for (int x = kx - 1; x <= kx + 1; x++) {
        int us = 0, them = 0;
        for (int y = 7; y >= ky; y--) {
            Square sq = make_square(x, y);
            Piece p = pos.piece_on(sq);
            // Our undefended black pawn
            if (p == B_PAWN) {
                bool defended = false;
                if (x > 0 && y < 7 && pos.piece_on(make_square(x - 1, y + 1)) == W_PAWN)
                    defended = true;
                if (x < 7 && y < 7 && pos.piece_on(make_square(x + 1, y + 1)) == W_PAWN)
                    defended = true;
                if (!defended) us = y;
            }
            // Their (white) pawn
            if (p == W_PAWN) them = y;
        }

        int f = std::min(x, 7 - x);
        if (us > 0 && them == us + 1) {
            // Blocked: their pawn directly in front of our pawn
            if (them < 7) {
                v += blockedstorm[0][them];
                ev += blockedstorm[1][them];
            }
        } else {
            // Unblocked storm
            if (f < 4 && them < 7)
                v += unblockedstorm[f][them];
        }
    }

    return eg ? ev : v;
}

static int storm_square_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += storm_square(pos, Square(sq));
    return v;
}

// Shelter strength: king shelter bonus for best king position
// Considers current king position AND castling destinations
// Picks the position where (storm - strength) is minimized
// Returns the strength value for that best position
static int shelter_strength(const Position& pos) {
    int bestW = 0, bestS = 1024;

    // Check all candidate king squares: current king + castling targets
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        int x = file_of(Square(sq)), y = rank_of(Square(sq));

        bool isCandidate = false;
        // Current black king position
        if (p == B_KING) isCandidate = true;
        // Kingside castling target (g1 for black = g8 in our coords = (6,7)...
        // but this is from white POV looking at black king)
        // In Stockfish guide: pos.c[2]=black kingside, pos.c[3]=black queenside
        // Castling squares for black king: g8 (6,7) and c8 (2,7)
        // TODO: check actual castling rights from Position
        // For now, check standard castling squares
        if (x == 6 && y == 7) isCandidate = true; // black kingside castle
        if (x == 2 && y == 7) isCandidate = true; // black queenside castle

        if (isCandidate) {
            int w1 = strength_square(pos, Square(sq));
            int s1 = storm_square(pos, Square(sq));
            if (s1 - w1 < bestS - bestW) {
                bestW = w1;
                bestS = s1;
            }
        }
    }

    return bestW;
}

// Shelter storm: storm penalty for best king position
// Same selection logic as shelter_strength, but returns storm value
static int shelter_storm(const Position& pos) {
    int bestW = 0, bestS = 1024;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        int x = file_of(Square(sq)), y = rank_of(Square(sq));

        bool isCandidate = false;
        if (p == B_KING) isCandidate = true;
        if (x == 6 && y == 7) isCandidate = true;
        if (x == 2 && y == 7) isCandidate = true;

        if (isCandidate) {
            int w1 = strength_square(pos, Square(sq));
            int s1 = storm_square(pos, Square(sq));
            if (s1 - w1 < bestS - bestW) {
                bestW = w1;
                bestS = s1;
            }
        }
    }

    return bestS;
}

// King pawn distance: minimum Chebyshev distance from our king to our nearest pawn
// Returns 6 if no pawns
static int king_pawn_distance(const Position& pos) {
    int kx = -1, ky = -1;
    for (int sq = 0; sq < 64; sq++) {
        if (pos.piece_on(Square(sq)) == W_KING) {
            kx = file_of(Square(sq));
            ky = rank_of(Square(sq));
            break;
        }
    }

    int v = 6;
    for (int sq = 0; sq < 64; sq++) {
        if (pos.piece_on(Square(sq)) == W_PAWN) {
            int dist = std::max(std::abs(file_of(Square(sq)) - kx),
                                std::abs(rank_of(Square(sq)) - ky));
            v = std::min(v, dist);
        }
    }
    return v;
}

// Check: can a white piece give check from 'sq'?
// Returns 1 if sq is attacked by our piece AND has line to enemy king
// type filter: 0=knight, 1=bishop, 2=rook, 3=queen, 4=any, -1(default)=any
// Queen is NOT considered a blocker (x-ray through queens for check lines)
static int check(const Position& pos, Square sq, int type = -1) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Find black king
    int kx = -1, ky = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == B_KING) {
            kx = file_of(Square(s));
            ky = rank_of(Square(s));
            break;
        }
    }
    if (kx < 0) return 0;

    // Rook/Queen straight-line check
    if ((rook_xray_attack(pos, sq) && (type < 0 || type == 2 || type == 4))
     || (queen_attack(pos, sq) && (type < 0 || type == 3))) {
        static const int rdx[] = {-1, 1, 0, 0};
        static const int rdy[] = {0, 0, -1, 1};
        for (int i = 0; i < 4; i++) {
            for (int d = 1; d < 8; d++) {
                int nx = sx + d * rdx[i], ny = sy + d * rdy[i];
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
                if (nx == kx && ny == ky) return 1;
                Piece b = pos.piece_on(make_square(nx, ny));
                if (b != NO_PIECE && type_of(b) != QUEEN) break; // queen not a blocker
            }
        }
    }

    // Bishop/Queen diagonal check
    if ((bishop_xray_attack(pos, sq) && (type < 0 || type == 1 || type == 4))
     || (queen_attack(pos, sq) && (type < 0 || type == 3))) {
        static const int bdx[] = {-1, 1, -1, 1};
        static const int bdy[] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; i++) {
            for (int d = 1; d < 8; d++) {
                int nx = sx + d * bdx[i], ny = sy + d * bdy[i];
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
                if (nx == kx && ny == ky) return 1;
                Piece b = pos.piece_on(make_square(nx, ny));
                if (b != NO_PIECE && type_of(b) != QUEEN) break;
            }
        }
    }

    // Knight check
    if (knight_attack(pos, sq) && (type < 0 || type == 0 || type == 4)) {
        static const int ndx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
        static const int ndy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
        for (int i = 0; i < 8; i++) {
            if (sx + ndx[i] == kx && sy + ndy[i] == ky) return 1;
        }
    }

    return 0;
}

static int check_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += check(pos, Square(sq));
    return v;
}

// Forward declarations for mutual recursion
// Weak squares: squares attacked by us where enemy defense is at most queen or king only
// Returns 1 if: we attack it AND (enemy doesn't defend OR defends only with queen/king)
static int weak_squares(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;

    // Must be attacked by white
    if (!attack(pos, sq)) return 0;

    // Count black (enemy) defense on this square
    // We need to compute black's attacks on sq
    // Black pawn attacks
    int sx = file_of(sq), sy = rank_of(sq);
    int enemyAtt = 0;

    // Black pawn attacks
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == B_PAWN) enemyAtt++;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == B_PAWN) enemyAtt++;

    // Black knight attacks
    static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
    static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
    for (int i = 0; i < 8; i++) {
        int nx = sx + kdx[i], ny = sy + kdy[i];
        if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7)
            if (pos.piece_on(make_square(nx, ny)) == B_KNIGHT) enemyAtt++;
    }

    // Black bishop attacks (diagonal slider)
    static const int bdx[] = {-1, 1, -1, 1};
    static const int bdy[] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; i++) {
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * bdx[i], ny = sy + d * bdy[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece p = pos.piece_on(make_square(nx, ny));
            if (p == B_BISHOP) { enemyAtt++; break; }
            if (p != NO_PIECE) break;
        }
    }

    // Black rook attacks (straight slider)
    static const int rdx[] = {-1, 1, 0, 0};
    static const int rdy[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; i++) {
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * rdx[i], ny = sy + d * rdy[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece p = pos.piece_on(make_square(nx, ny));
            if (p == B_ROOK) { enemyAtt++; break; }
            if (p != NO_PIECE) break;
        }
    }

    // If enemy defends with 2+ non-queen/non-king pieces: not weak
    if (enemyAtt >= 2) return 0;
    if (enemyAtt == 0) return 1;

    // Defended by exactly 1 piece — weak only if that piece is queen or king
    // Check if the single defender is king or queen
    // Black king attack
    bool kingDef = false;
    for (int i = 0; i < 8; i++) {
        int nx = sx + bdx[i % 4], ny = sy + bdy[i % 4]; // reuse — not right
        // Simpler: check king adjacency
    }
    // Simplified: check black king and queen
    for (int i = 0; i < 8; i++) {
        static const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        int nx = sx + dx8[i], ny = sy + dy8[i];
        if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7) {
            if (pos.piece_on(make_square(nx, ny)) == B_KING) return 1;
        }
    }
    // Black queen on straight/diagonal
    for (int i = 0; i < 8; i++) {
        static const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * dx8[i], ny = sy + d * dy8[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece p = pos.piece_on(make_square(nx, ny));
            if (p == B_QUEEN) return 1;
            if (p != NO_PIECE) break;
        }
    }

    return 0;
}

// Safe check: can we give a SAFE check from 'sq' with piece type 'type'?
// Safe = square not defended by enemy, OR weak square with multiple attackers
// Queen checks excluded if rook check possible from same square (rook more valuable)
// Bishop checks excluded if queen check possible from same square
// type: 0=knight, 1=bishop, 2=rook, 3=queen
static int safe_check(const Position& pos, Square sq, int type) {
    if (sq == SQ_NONE) return 0;

    // Must be empty square (can't check from occupied square)
    Piece p = pos.piece_on(sq);
    if (p != NO_PIECE && color_of(p) == WHITE && type_of(p) != NO_PIECE_TYPE) return 0;
    // More precisely: no white piece on the square
    if (p != NO_PIECE && "PNBRQK"[type_of(p)] && color_of(p) == WHITE) return 0;

    // Must be able to give check
    if (!check(pos, sq, type)) return 0;

    // Queen check: skip if rook check is also possible (rook check more valuable)
    if (type == 3 && safe_check(pos, sq, 2)) return 0;
    // Bishop check: skip if queen check is also possible
    if (type == 1 && safe_check(pos, sq, 3)) return 0;

    // Check if square is safe:
    // Colorflip to get black's attacks on the mirrored square
    // Simplified: check if enemy (black) attacks this square
    // "Safe" means: enemy doesn't attack it, OR (it's a weak square AND we attack it >1 time)
    Square flipped = make_square(file_of(sq), 7 - rank_of(sq));

    // Count black attacks on this square (enemy defending)
    // TODO: use colorflipped attack when Position supports it
    // For now approximate: count black pieces attacking sq
    int enemyAtt = 0; // TODO: attack(colorflip(pos), flipped)
    int ourAtt = attack(pos, sq);

    bool safe = (enemyAtt == 0) || (weak_squares(pos, sq) && ourAtt > 1);

    // Queen check: also not safe if enemy queen defends
    // TODO: queen_attack(colorflip(pos), flipped)
    if (type == 3) {
        // int enemyQueenAtt = 0; // TODO
        // if (enemyQueenAtt) safe = false;
    }

    return safe ? 1 : 0;
}

static int safe_check_total(const Position& pos, int type) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += safe_check(pos, Square(sq), type);
    return v;
}

// King ring: squares around enemy (black) king + king square itself
// Squares defended by two enemy pawns are excluded (unless full=true)
// Extended: if king is on edge file (a or h), ring extends one extra file inward
static int king_ring(const Position& pos, Square sq, bool full = false) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Exclude if defended by two black pawns (unless full mode)
    if (!full) {
        if (sx > 0 && sy > 0 && sx < 7
            && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN
            && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN)
            return 0;
    }

    // Check if sq is in king ring: within 1 square of black king
    // Extended to 2 squares on edge files
    for (int ix = -2; ix <= 2; ix++) {
        for (int iy = -2; iy <= 2; iy++) {
            int nx = sx + ix, ny = sy + iy;
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
            if (pos.piece_on(make_square(nx, ny)) != B_KING) continue;
            // Must be within 1 square, or edge extension
            bool inRange = (ix >= -1 && ix <= 1) || (sx + ix == 0) || (sx + ix == 7);
            bool inRangeY = (iy >= -1 && iy <= 1) || (sy + iy == 0) || (sy + iy == 7);
            if (inRange && inRangeY) return 1;
        }
    }
    return 0;
}

// King attackers count: number of white pieces attacking king ring
// Pawns: count attacked squares in ring (half weight if adjacent pawn on same rank)
// Pieces (N/B/R/Q): 1 if any square in king ring is attacked by this piece
static int king_attackers_count(const Position& pos, Square square) {
    if (square == SQ_NONE) return 0;
    Piece p = pos.piece_on(square);
    if (p == NO_PIECE || color_of(p) != WHITE) return 0;
    PieceType pt = type_of(p);
    if (pt < PAWN || pt > QUEEN) return 0;

    int sx = file_of(square), sy = rank_of(square);

    if (pt == PAWN) {
        // For pawns: count attacked king ring squares
        float v = 0;
        for (int dir = -1; dir <= 1; dir += 2) {
            int ax = sx + dir, ay = sy - 1; // attack square
            if (ax < 0 || ax > 7 || ay < 0) continue;
            if (king_ring(pos, make_square(ax, ay), true)) {
                // Half weight if friendly pawn 2 files away on same rank
                bool fr = (sx + dir * 2 >= 0 && sx + dir * 2 <= 7
                           && pos.piece_on(make_square(sx + dir * 2, sy)) == W_PAWN);
                v += fr ? 0.5f : 1.0f;
            }
        }
        return (int)v;
    }

    // For pieces: check if any king ring square is attacked by this piece
    for (int sq = 0; sq < 64; sq++) {
        if (!king_ring(pos, Square(sq))) continue;
        if (knight_attack(pos, Square(sq), square)
         || bishop_xray_attack(pos, Square(sq), square)
         || rook_xray_attack(pos, Square(sq), square)
         || queen_attack(pos, Square(sq), square))
            return 1;
    }
    return 0;
}

// King attackers weight: weighted sum of attacking pieces
// Weights: P=0, N=81, B=52, R=44, Q=10
static int king_attackers_weight(const Position& pos, Square square) {
    static const int weights[] = {0, 81, 52, 44, 10}; // P, N, B, R, Q
    if (king_attackers_count(pos, square) == 0) return 0;
    Piece p = pos.piece_on(square);
    if (p == NO_PIECE || color_of(p) != WHITE) return 0;
    PieceType pt = type_of(p);
    if (pt < PAWN || pt > QUEEN) return 0;
    return weights[pt - 1]; // PAWN=1 -> index 0
}

// King attacks: count attacks by a specific piece on squares adjacent to enemy king
// Multiply counted (e.g. knight attacking 2 king-adjacent squares = 2)
static int king_attacks(const Position& pos, Square square) {
    if (square == SQ_NONE) return 0;
    Piece p = pos.piece_on(square);
    if (p == NO_PIECE || color_of(p) != WHITE) return 0;
    PieceType pt = type_of(p);
    if (pt < KNIGHT || pt > QUEEN) return 0;
    if (king_attackers_count(pos, square) == 0) return 0;

    // Find black king
    int kx = -1, ky = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == B_KING) {
            kx = file_of(Square(s));
            ky = rank_of(Square(s));
            break;
        }
    }
    if (kx < 0) return 0;

    int v = 0;
    for (int x = kx - 1; x <= kx + 1; x++) {
        for (int y = ky - 1; y <= ky + 1; y++) {
            if (x < 0 || x > 7 || y < 0 || y > 7) continue;
            if (x == kx && y == ky) continue;
            Square s2 = make_square(x, y);
            v += knight_attack(pos, s2, square);
            v += bishop_xray_attack(pos, s2, square);
            v += rook_xray_attack(pos, s2, square);
            v += queen_attack(pos, s2, square);
        }
    }
    return v;
}

// Sum helpers
static int king_attackers_count_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) v += king_attackers_count(pos, Square(sq));
    return v;
}
static int king_attackers_weight_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) v += king_attackers_weight(pos, Square(sq));
    return v;
}
static int king_attacks_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) v += king_attacks(pos, Square(sq));
    return v;
}

// Weak bonus: count weak squares in king ring
// Weak AND in king ring = exploitable weakness near king
static int weak_bonus(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!weak_squares(pos, sq)) return 0;
    if (!king_ring(pos, sq)) return 0;
    return 1;
}

static int weak_bonus_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += weak_bonus(pos, Square(sq));
    return v;
}

// Unsafe checks: squares where we can give check but NOT safely
// Only counts if there's NO safe check of that type anywhere on the board
// Excludes queen checks (only knight=0, bishop=1, rook=2)
static int unsafe_checks(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (check(pos, sq, 0) && safe_check_total(pos, 0) == 0) return 1;
    if (check(pos, sq, 1) && safe_check_total(pos, 1) == 0) return 1;
    if (check(pos, sq, 2) && safe_check_total(pos, 2) == 0) return 1;
    return 0;
}

static int unsafe_checks_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += unsafe_checks(pos, Square(sq));
    return v;
}

// Knight defender: squares defended by our knight that are also adjacent to our king
static int knight_defender(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (knight_attack(pos, sq) && king_attack(pos, sq)) return 1;
    return 0;
}

static int knight_defender_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += knight_defender(pos, Square(sq));
    return v;
}

// Endgame shelter: EG component of blocked storm penalty
// Same best-shelter selection as shelter_strength/storm, but returns storm EG value
static int endgame_shelter(const Position& pos) {
    int bestW = 0, bestS = 1024, bestE = 5;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        int x = file_of(Square(sq)), y = rank_of(Square(sq));

        bool isCandidate = false;
        if (p == B_KING) isCandidate = true;
        if (x == 6 && y == 7) isCandidate = true; // black kingside castle
        if (x == 2 && y == 7) isCandidate = true; // black queenside castle

        if (isCandidate) {
            int w1 = strength_square(pos, Square(sq));
            int s1 = storm_square(pos, Square(sq), false);
            int e1 = storm_square(pos, Square(sq), true);  // EG value
            if (s1 - w1 < bestS - bestW) {
                bestW = w1;
                bestS = s1;
                bestE = e1;
            }
        }
    }

    return bestE;
}

// Blockers for king: pieces that block attacks on enemy king
// Uses pinned_direction from enemy perspective — if a piece is pinned
// to the enemy king, it's a "blocker" (potential discovered attack)
static int blockers_for_king(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    // Check from black's perspective: is this piece pinned to black king?
    // pinned_direction gives negative for enemy blockers
    // We check colorflipped: piece on (x, 7-y) from black's view
    Square flipped = make_square(file_of(sq), 7 - rank_of(sq));
    // TODO: needs colorflip support — for now check if piece blocks line to black king
    int dir = pinned_direction(pos, sq);
    return (dir < 0) ? 1 : 0; // negative = blocker for enemy king
}

static int blockers_for_king_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += blockers_for_king(pos, Square(sq));
    return v;
}

// Flank attack: count our attacks on enemy king's flank
// Returns 0 if square not on enemy king's flank or rank > 4
// Returns 1 if attacked once, 2 if attacked twice+
static int flank_attack(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Only consider ranks 1-4 (from white's perspective: y <= 4, i.e. rank 1-5)
    if (sy > 4) return 0;

    // Find black king file
    int kx = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == B_KING) {
            kx = file_of(Square(s));
            break;
        }
    }
    if (kx < 0) return 0;

    // Check if square is on king's flank
    if (kx == 0 && sx > 2) return 0;
    if (kx < 3 && sx > 3) return 0;
    if (kx >= 3 && kx < 5 && (sx < 2 || sx > 5)) return 0;
    if (kx >= 5 && sx < 4) return 0;
    if (kx == 7 && sx < 5) return 0;

    int a = attack(pos, sq);
    if (!a) return 0;
    return a > 1 ? 2 : 1;
}

static int flank_attack_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += flank_attack(pos, Square(sq));
    return v;
}

// Flank defense: count enemy defenses on their king flank
// Same flank determination as flank_attack, but checks enemy (black) attacks
static int flank_defense(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    if (sy > 4) return 0;

    // Find black king file (same logic as flank_attack)
    int kx = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == B_KING) {
            kx = file_of(Square(s));
            break;
        }
    }
    if (kx < 0) return 0;

    if (kx == 0 && sx > 2) return 0;
    if (kx < 3 && sx > 3) return 0;
    if (kx >= 3 && kx < 5 && (sx < 2 || sx > 5)) return 0;
    if (kx >= 5 && sx < 4) return 0;
    if (kx == 7 && sx < 5) return 0;

    // Check if enemy (black) defends this square
    // TODO: use colorflip attack properly when Position supports it
    // For now: manually count black attacks on sq
    int blackAtt = 0;
    // Black pawn attacks
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == B_PAWN) blackAtt++;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == B_PAWN) blackAtt++;
    // Black knight
    static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
    static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
    for (int i = 0; i < 8; i++) {
        int nx = sx + kdx[i], ny = sy + kdy[i];
        if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7)
            if (pos.piece_on(make_square(nx, ny)) == B_KNIGHT) blackAtt++;
    }
    // Black king
    static const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int i = 0; i < 8; i++) {
        int nx = sx + dx8[i], ny = sy + dy8[i];
        if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7)
            if (pos.piece_on(make_square(nx, ny)) == B_KING) blackAtt++;
    }
    // Black sliders (simplified — just bishops/rooks/queens)
    for (int i = 0; i < 8; i++) {
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * dx8[i], ny = sy + d * dy8[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece p = pos.piece_on(make_square(nx, ny));
            if (p == NO_PIECE) continue;
            bool isDiag = (dx8[i] != 0 && dy8[i] != 0);
            if ((p == B_QUEEN) || (p == B_BISHOP && isDiag) || (p == B_ROOK && !isDiag))
                blackAtt++;
            break;
        }
    }

    return blackAtt > 0 ? 1 : 0;
}

static int flank_defense_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += flank_defense(pos, Square(sq));
    return v;
}

// King danger: main king safety score combining all factors
// Returns 0 if danger <= 100 (not enough threat)
static int king_danger(const Position& pos) {
    int count   = king_attackers_count_total(pos);
    int weight  = king_attackers_weight_total(pos);
    int kAttacks = king_attacks_total(pos);
    int weak    = weak_bonus_total(pos);
    int unsafeCk = unsafe_checks_total(pos);
    int blockers = blockers_for_king_total(pos);
    int flankAtt = flank_attack_total(pos);
    int flankDef = flank_defense_total(pos);

    // No queen penalty
    int noQueen = (queen_count(pos, WHITE) > 0) ? 0 : 1;

    // Knight defender for enemy (black has knight defending king)
    int knightDef = 0;  // TODO: knight_defender from black perspective
    // Simplified: check if black has knight near black king
    int bkx = -1, bky = -1;
    for (int sq = 0; sq < 64; sq++)
        if (pos.piece_on(Square(sq)) == B_KING) {
            bkx = file_of(Square(sq)); bky = rank_of(Square(sq)); break;
        }
    if (bkx >= 0) {
        static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
        static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
        for (int sq = 0; sq < 64; sq++) {
            if (pos.piece_on(Square(sq)) != B_KNIGHT) continue;
            int nx = file_of(Square(sq)), ny = rank_of(Square(sq));
            // Check if knight is adjacent to king
            for (int i = 0; i < 8; i++) {
                int dx = bkx + ((i > 3) + 1) * (((i % 4) > 1) * 2 - 1);
                int dy = bky + (2 - (i > 3)) * ((i % 2 == 0) * 2 - 1);
                // Simpler: knight defends sq if king attacks sq and knight attacks sq
            }
        }
        // Simplified for now
        knightDef = knight_defender_total(pos); // This is for white — TODO fix for black
    }

    int shelter = shelter_strength(pos) - shelter_storm(pos);
    int mobDiff = mobility_mg(pos); // TODO: - mobility_mg(colorflip(pos))

    // Safe checks with diminishing returns (min caps)
    int safeQ = safe_check_total(pos, 3);
    int safeR = safe_check_total(pos, 2);
    int safeB = safe_check_total(pos, 1);
    int safeN = safe_check_total(pos, 0);

    int v = count * weight
          +  69 * kAttacks
          + 185 * weak
          - 100 * (knightDef > 0 ? 1 : 0)
          + 148 * unsafeCk
          +  98 * blockers
          -   4 * flankDef
          + 3 * flankAtt * flankAtt / 8
          - 873 * noQueen
          - 6 * shelter / 8
          + mobDiff
          + 37
          + (int)(772 * std::min(safeQ, 1))    // simplified: cap at 1
          + (int)(1084 * std::min(safeR, 1))
          + (int)(645 * std::min(safeB, 1))
          + (int)(792 * std::min(safeN, 1));

    return v > 100 ? v : 0;
}

// Mobility area: squares included in mobility calculation
// Excludes: our king/queen, squares attacked by enemy pawns,
// our blocked pawns or pawns on ranks 2-3, blockers for our king
static int mobility_area(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    Piece p = pos.piece_on(sq);

    // Exclude our king and queen
    if (p == W_KING || p == W_QUEEN) return 0;

    // Exclude squares attacked by enemy pawns
    if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) return 0;
    if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) return 0;

    // Exclude our blocked pawns or pawns on ranks 2-3
    if (p == W_PAWN) {
        int r = rank_of(sq) + 1; // 1-indexed rank
        if (r < 4) return 0;     // rank 1-3
        // Blocked: piece directly in front
        if (sy < 7 && pos.piece_on(make_square(sx, sy + 1)) != NO_PIECE) return 0;
    }

    // Exclude blockers for our king (pieces pinned to white king)
    // These are our pieces that are pinned — can't move freely
    if (p != NO_PIECE && color_of(p) == WHITE) {
        int dir = pinned_direction(pos, sq);
        if (dir > 0) return 0; // pinned to our king
    }

    return 1;
}

// Mobility: count attacked squares in mobility area for each piece
// For queen: ignore squares defended by enemy N/B/R
// For minors: ignore squares occupied by our queen
static int mobility(const Position& pos, Square square) {
    if (square == SQ_NONE) return 0;
    Piece p = pos.piece_on(square);
    if (p == NO_PIECE || color_of(p) != WHITE) return 0;
    PieceType pt = type_of(p);
    if (pt < KNIGHT || pt > QUEEN) return 0;

    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s2 = Square(sq);
        if (!mobility_area(pos, s2)) continue;
        Piece target = pos.piece_on(s2);

        if (pt == KNIGHT && knight_attack(pos, s2, square) && target != W_QUEEN) v++;
        if (pt == BISHOP && bishop_xray_attack(pos, s2, square) && target != W_QUEEN) v++;
        if (pt == ROOK && rook_xray_attack(pos, s2, square)) v++;
        if (pt == QUEEN && queen_attack(pos, s2, square)) v++;
    }
    return v;
}

static int mobility_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += mobility(pos, Square(sq));
    return v;
}

// Supported: count pawns supporting this pawn (diagonally behind)
static int supported(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    int v = 0;
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == W_PAWN) v++;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == W_PAWN) v++;
    return v;
}

// Candidate passed: is this pawn passed or a candidate to become passed?
// Checks: no stoppers, or can lever through, or outnumbers lever pushes
static int candidate_passed(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Check for own pawn ahead on same file — not passed
    int ty1 = 8, ty2 = 8; // ty1=enemy pawn same file, ty2=enemy pawn adjacent file
    for (int y = sy - 1; y >= 0; y--) {
        if (pos.piece_on(make_square(sx, y)) == W_PAWN) return 0;
        if (pos.piece_on(make_square(sx, y)) == B_PAWN) ty1 = y;
        if (sx > 0 && pos.piece_on(make_square(sx - 1, y)) == B_PAWN) ty2 = std::min(ty2, y);
        if (sx < 7 && pos.piece_on(make_square(sx + 1, y)) == B_PAWN) ty2 = std::min(ty2, y);
    }

    // (a) no stoppers except some levers
    if (ty1 == 8 && ty2 >= sy - 1) return 1;

    // Quick reject
    if (ty2 < sy - 2 || ty1 < sy - 1) return 0;

    // (c) one front stopper which can be levered
    if (ty2 >= sy && ty1 == sy - 1 && sy < 4) {
        // Left lever
        if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == W_PAWN
            && (sx < 1 || pos.piece_on(make_square(sx - 1, sy)) != B_PAWN)
            && (sx < 2 || pos.piece_on(make_square(sx - 2, sy - 1)) != B_PAWN)) return 1;
        // Right lever
        if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == W_PAWN
            && (sx > 6 || pos.piece_on(make_square(sx + 1, sy)) != B_PAWN)
            && (sx > 5 || pos.piece_on(make_square(sx + 2, sy - 1)) != B_PAWN)) return 1;
    }

    // Blocked by enemy pawn directly ahead
    if (sy > 0 && pos.piece_on(make_square(sx, sy - 1)) == B_PAWN) return 0;

    // Count levers, lever pushes, phalanx
    int lever = 0, leverpush = 0, phalanx = 0;
    if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) lever++;
    if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) lever++;
    if (sx > 0 && sy > 1 && pos.piece_on(make_square(sx - 1, sy - 2)) == B_PAWN) leverpush++;
    if (sx < 7 && sy > 1 && pos.piece_on(make_square(sx + 1, sy - 2)) == B_PAWN) leverpush++;
    if (sx > 0 && pos.piece_on(make_square(sx - 1, sy)) == W_PAWN) phalanx++;
    if (sx < 7 && pos.piece_on(make_square(sx + 1, sy)) == W_PAWN) phalanx++;

    if (lever - supported(pos, sq) > 1) return 0;
    if (leverpush - phalanx > 0) return 0;
    if (lever > 0 && leverpush > 0) return 0;

    return 1;
}

static int candidate_passed_total(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++)
        v += candidate_passed(pos, Square(sq));
    return v;
}

// Passed leverable: candidate passer that can actually advance
// If blocked by enemy pawn, needs a feasible lever (adjacent friendly pawn)
static int passed_leverable(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!candidate_passed(pos, sq)) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Not blocked by enemy pawn — always leverable
    if (sy == 0 || pos.piece_on(make_square(sx, sy - 1)) != B_PAWN) return 1;

    // Blocked — check for feasible lever
    for (int dir = -1; dir <= 1; dir += 2) {
        int lx = sx + dir;
        if (lx < 0 || lx > 7) continue;
        // Friendly pawn behind to lever with
        if (sy < 7 && pos.piece_on(make_square(lx, sy + 1)) != W_PAWN) continue;
        // Lever square not blocked by enemy piece
        Piece onLever = pos.piece_on(make_square(lx, sy));
        if (onLever != NO_PIECE && color_of(onLever) == BLACK) continue;
        // Either we attack the lever square or enemy doesn't defend it strongly
        if (attack(pos, make_square(lx, sy)) > 0) return 1;
        // TODO: check enemy attack count <= 1 (needs colorflip)
        return 1; // simplified
    }
    return 0;
}

// King proximity: EG bonus based on king distances to passed pawn
// Bonus for enemy king being far, penalty for our king being far
static int king_proximity(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!passed_leverable(pos, sq)) return 0;
    int r = rank_of(sq);
    int rr = 7 - r; // rank from white's perspective (rank 8 = 0 internal)
    // Actually in Stockfish guide, rank(pos, square)-1 where rank=8-y
    // Our rank_of gives 0=rank1, 7=rank8. For white pawn, relative rank = rank_of
    // White pawn on rank_of=6 means 7th rank. r = rank_of(sq) for white perspective
    int relRank = r; // 0=rank1, 7=rank8
    int w = relRank > 2 ? 5 * relRank - 13 : 0;
    if (w <= 0) return 0;

    int v = 0;
    int px = file_of(sq), py = rank_of(sq);

    // Find both kings
    for (int s = 0; s < 64; s++) {
        Piece p = pos.piece_on(Square(s));
        int kx = file_of(Square(s)), ky = rank_of(Square(s));

        if (p == B_KING) {
            // Enemy king far from block square = bonus
            int dist = std::min(std::max(std::abs(ky - py + 1), std::abs(kx - px)), 5);
            v += (dist * 19 / 4) * w;
        }
        if (p == W_KING) {
            // Our king far from block square = penalty
            int dist = std::min(std::max(std::abs(ky - py + 1), std::abs(kx - px)), 5);
            v -= dist * 2 * w;
            // Second push consideration
            if (py > 1) {
                int dist2 = std::min(std::max(std::abs(ky - py + 2), std::abs(kx - px)), 5);
                v -= dist2 * w;
            }
        }
    }
    return v;
}

// Passed block: bonus if passed pawn is free to advance
// Adjusted by attack/defense of block square and path
static int passed_block(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!passed_leverable(pos, sq)) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    int relRank = sy; // white pawn rank

    if (relRank < 3) return 0; // rank < 4 (0-indexed: < 3)
    // Block square must be empty
    if (sy > 0 && pos.piece_on(make_square(sx, sy - 1)) != NO_PIECE) return 0;

    int r = relRank;
    int w = r > 2 ? 5 * r - 13 : 0;

    // Count defended/unsafe squares on path to promotion
    int defended = 0, unsafe = 0, wunsafe = 0;
    int defended1 = 0, unsafe1 = 0;

    for (int y = sy - 1; y >= 0; y--) {
        if (attack(pos, make_square(sx, y))) defended++;
        // TODO: enemy attack (needs colorflip) — approximate with 0
        if (y == sy - 1) {
            defended1 = defended;
            unsafe1 = unsafe;
        }
    }

    // Rook/Queen behind pawn boost defense/unsafe
    for (int y = sy + 1; y < 8; y++) {
        Piece p = pos.piece_on(make_square(sx, y));
        if (p == W_ROOK || p == W_QUEEN) defended1 = defended = sy;
        if (p == B_ROOK || p == B_QUEEN) unsafe1 = unsafe = sy;
    }

    int k = (unsafe == 0 && wunsafe == 0 ? 35
           : unsafe == 0 ? 20
           : unsafe1 == 0 ? 9 : 0)
          + (defended1 != 0 ? 5 : 0);

    return k * w;
}

// Passed file: bonus based on file of passed pawn (center files = higher)
// Returns min(file-1, 8-file) = 0 for a/h, 1 for b/g, 2 for c/f, 3 for d/e
static int passed_file(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!passed_leverable(pos, sq)) return 0;
    int f = file_of(sq) + 1; // 1-indexed file
    return std::min(f - 1, 8 - f);
}

// Passed rank: rank of passed pawn (0-6, higher = closer to promotion)
// 73 ELO worth — one of the most important terms!
static int passed_rank(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!passed_leverable(pos, sq)) return 0;
    return rank_of(sq); // 0=rank1, 6=rank7 for white pawn
}

// Isolated: pawn with no friendly pawn on adjacent files (14.8 ELO)
static int isolated(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq);
    for (int y = 0; y < 8; y++) {
        if (sx > 0 && pos.piece_on(make_square(sx - 1, y)) == W_PAWN) return 0;
        if (sx < 7 && pos.piece_on(make_square(sx + 1, y)) == W_PAWN) return 0;
    }
    return 1;
}

// Opposed: enemy pawn on same file ahead (prevents advancing)
static int opposed(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    for (int y = sy - 1; y >= 0; y--) {
        if (pos.piece_on(make_square(sx, y)) == B_PAWN) return 1;
    }
    return 0;
}

// Phalanx: friendly pawn on adjacent file, same rank
static int phalanx(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    if (sx > 0 && pos.piece_on(make_square(sx - 1, sy)) == W_PAWN) return 1;
    if (sx < 7 && pos.piece_on(make_square(sx + 1, sy)) == W_PAWN) return 1;
    return 0;
}

// Backward: pawn behind all friendly pawns on adjacent files AND cannot safely advance
// (enemy pawn attacks the stop square or stop+1 square)
static int backward(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    // Check if any friendly pawn on adjacent files at same rank or behind
    for (int y = sy; y < 8; y++) {
        if (sx > 0 && pos.piece_on(make_square(sx - 1, y)) == W_PAWN) return 0;
        if (sx < 7 && pos.piece_on(make_square(sx + 1, y)) == W_PAWN) return 0;
    }

    // Check if enemy pawn prevents advance
    if ((sx > 0 && sy >= 2 && pos.piece_on(make_square(sx - 1, sy - 2)) == B_PAWN)
     || (sx < 7 && sy >= 2 && pos.piece_on(make_square(sx + 1, sy - 2)) == B_PAWN)
     || (sy >= 1 && pos.piece_on(make_square(sx, sy - 1)) == B_PAWN))
        return 1;

    return 0;
}

// Doubled: friendly pawn directly behind AND not supported
// Only the front pawn of a doubled pair gets this penalty
static int doubled(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    // Pawn directly behind
    if (sy >= 7 || pos.piece_on(make_square(sx, sy + 1)) != W_PAWN) return 0;
    // Not supported (no friendly pawn diagonally behind)
    if (sx > 0 && pos.piece_on(make_square(sx - 1, sy + 1)) == W_PAWN) return 0;
    if (sx < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == W_PAWN) return 0;
    return 1;
}

// Connected: pawn is supported or phalanx (29 ELO)
static int connected(const Position& pos, Square sq) {
    if (supported(pos, sq) || phalanx(pos, sq)) return 1;
    return 0;
}

// Connected bonus: bonus for connected pawns based on rank, phalanx, supported, opposed
static int connected_bonus(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!connected(pos, sq)) return 0;
    static const int seed[] = {0, 7, 8, 12, 29, 48, 86};
    int r = rank_of(sq); // 0-indexed, white pawn: rank 1=0, rank 7=6
    if (r < 1 || r > 6) return 0;
    int op = opposed(pos, sq);
    int ph = phalanx(pos, sq);
    int su = supported(pos, sq);
    return seed[r] * (2 + ph - op) + 21 * su;
}

// Weak unopposed pawn: isolated or backward AND not opposed
static int weak_unopposed_pawn(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (opposed(pos, sq)) return 0;
    if (isolated(pos, sq) || backward(pos, sq)) return 1;
    return 0;
}

// Weak lever: unsupported pawn attacked by TWO enemy pawns
static int weak_lever(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    // Attacked by both enemy pawns diagonally
    if (!(sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN)) return 0;
    if (!(sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN)) return 0;
    // Not supported by our pawns
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == W_PAWN) return 0;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == W_PAWN) return 0;
    return 1;
}

// Blocked: bonus for pawn blocked by enemy pawn on 5th or 6th rank
// Returns 2 for 5th rank, 1 for 6th rank (from white perspective)
static int blocked_pawn(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    int sy = rank_of(sq);
    // 5th rank = index 4, 6th rank = index 5 (from white's view)
    // In JS code: y==2 (rank 6) or y==3 (rank 5), returns 4-y
    // Our 0-indexed: rank 4 (5th) or rank 5 (6th)
    if (sy != 4 && sy != 5) return 0;
    int sx = file_of(sq);
    if (sy > 0 && pos.piece_on(make_square(sx, sy - 1)) != B_PAWN) return 0;
    // Higher rank = lower bonus (5th=2, 6th=1)
    return (sy == 4) ? 2 : 1;
}

// Doubled isolated: isolated pawn with another friendly pawn behind,
// stopped by enemy pawn on same file, no enemy pawns on adjacent files
static int doubled_isolated(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    if (!isolated(pos, sq)) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    int obe = 0, eop = 0, ene = 0;
    for (int y = 0; y < 8; y++) {
        if (y > sy && pos.piece_on(make_square(sx, y)) == W_PAWN) obe++;   // our pawn behind
        if (y < sy && pos.piece_on(make_square(sx, y)) == B_PAWN) eop++;   // enemy pawn ahead
        if (sx > 0 && pos.piece_on(make_square(sx - 1, y)) == B_PAWN) ene++;
        if (sx < 7 && pos.piece_on(make_square(sx + 1, y)) == B_PAWN) ene++;
    }
    if (obe > 0 && ene == 0 && eop > 0) return 1;
    return 0;
}

// Pawn attacks span: can an enemy pawn on adjacent file attack this square?
// Checks for enemy pawns ahead that are not backward and not blocked
static int pawn_attacks_span(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    for (int y = 0; y < sy; y++) {
        for (int dx = -1; dx <= 1; dx += 2) {
            int ax = sx + dx;
            if (ax < 0 || ax > 7) continue;
            if (pos.piece_on(make_square(ax, y)) != B_PAWN) continue;
            if (y == sy - 1) return 1; // directly adjacent
            // Check if this enemy pawn can advance (not blocked by our pawn, not backward)
            if (pos.piece_on(make_square(ax, y + 1)) == W_PAWN) continue;
            // TODO: backward check from black perspective (simplified: assume can advance)
            return 1;
        }
    }
    return 0;
}

// Outpost square: rank 4-6, supported by our pawn, no enemy pawn attacks span
static int outpost_square(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int r = rank_of(sq);
    if (r < 3 || r > 5) return 0; // ranks 4-6 (0-indexed: 3-5)
    int sx = file_of(sq), sy = rank_of(sq);
    // Must be supported by our pawn
    bool sup = false;
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == W_PAWN) sup = true;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == W_PAWN) sup = true;
    if (!sup) return 0;
    // No enemy pawn can attack this square
    if (pawn_attacks_span(pos, sq)) return 0;
    return 1;
}

// Outpost: knight or bishop on an outpost square
static int outpost(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p != W_KNIGHT && p != W_BISHOP) return 0;
    if (!outpost_square(pos, sq)) return 0;
    return 1;
}

// Reachable outpost: knight/bishop that can reach an outpost square in one move
// Returns 2 if outpost is pawn-supported, 1 otherwise, 0 if none reachable
static int reachable_outpost(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p != W_KNIGHT && p != W_BISHOP) return 0;

    int v = 0;
    for (int y = 2; y < 5; y++) {       // ranks 3-5 (0-indexed) = outpost ranks
        for (int x = 0; x < 8; x++) {
            Square target = make_square(x, y);
            // Target must be empty or enemy
            Piece tp = pos.piece_on(target);
            if (tp != NO_PIECE && color_of(tp) == WHITE) continue;
            if (!outpost_square(pos, target)) continue;

            bool canReach = false;
            if (p == W_KNIGHT && knight_attack(pos, target, sq)) canReach = true;
            if (p == W_BISHOP && bishop_xray_attack(pos, target, sq)) canReach = true;

            if (canReach) {
                bool pawnSup = (x > 0 && y < 7 && pos.piece_on(make_square(x - 1, y + 1)) == W_PAWN)
                            || (x < 7 && y < 7 && pos.piece_on(make_square(x + 1, y + 1)) == W_PAWN);
                v = std::max(v, pawnSup ? 2 : 1);
            }
        }
    }
    return v;
}

// Minor behind pawn: knight/bishop with any pawn directly in front
static int minor_behind_pawn(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p != W_KNIGHT && p != W_BISHOP) return 0;
    int sy = rank_of(sq);
    if (sy == 0) return 0;
    Piece ahead = pos.piece_on(make_square(file_of(sq), sy - 1));
    if (type_of(ahead) == PAWN) return 1; // any color pawn
    return 0;
}

// Bishop pawns: bad bishop penalty — pawns on same color × (1 + blocked center pawns)
// Also penalized more if bishop is not on a pawn-attacked square (10.57 ELO)
static int bishop_pawns(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_BISHOP) return 0;
    int color = (file_of(sq) + rank_of(sq)) % 2;
    int sameColor = 0, blocked = 0;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) != W_PAWN) continue;
        int fx = file_of(Square(s)), fy = rank_of(Square(s));
        if ((fx + fy) % 2 == color) sameColor++;
        // Blocked center pawn (files C-F = 2-5)
        if (fx >= 2 && fx <= 5 && fy > 0
            && pos.piece_on(make_square(fx, fy - 1)) != NO_PIECE)
            blocked++;
    }
    int notAttacked = (pawn_attack(pos, sq) > 0) ? 0 : 1;
    return sameColor * (blocked + notAttacked);
}

// Rook on file: 2 = open file (no pawns), 1 = semi-open (no our pawns), 0 = closed
static int rook_on_file(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_ROOK) return 0;
    int sx = file_of(sq);
    int open = 1;
    for (int y = 0; y < 8; y++) {
        if (pos.piece_on(make_square(sx, y)) == W_PAWN) return 0; // closed
        if (pos.piece_on(make_square(sx, y)) == B_PAWN) open = 0; // semi-open
    }
    return open + 1; // 2=open, 1=semi-open
}

// Trapped rook: rook on closed file, low mobility, trapped by own king
static int trapped_rook(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_ROOK) return 0;
    if (rook_on_file(pos, sq)) return 0;
    if (mobility(pos, sq) > 3) return 0;
    // Find our king
    int kx = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == W_KING) {
            kx = file_of(Square(s));
            break;
        }
    }
    if (kx < 0) return 0;
    int rx = file_of(sq);
    // King on same side as rook, rook on wrong side of king
    if ((kx < 4) != (rx < kx)) return 0;
    return 1;
}

// Weak queen: queen exposed to relative pin or discovered attack
// Checks if enemy rook (on straight) or bishop (on diagonal) is behind exactly 1 piece
static int weak_queen(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_QUEEN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);

    static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (int i = 0; i < 8; i++) {
        int count = 0;
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * dx[i], ny = sy + d * dy[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            Piece p = pos.piece_on(make_square(nx, ny));
            bool isDiag = (dx[i] != 0 && dy[i] != 0);
            if (p == B_ROOK && !isDiag && count == 1) return 1;
            if (p == B_BISHOP && isDiag && count == 1) return 1;
            if (p != NO_PIECE) count++;
        }
    }
    return 0;
}

// King distance: Chebyshev distance from square to our (white) king
static int king_distance(const Position& pos, Square sq) {
    int kx = -1, ky = -1;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == W_KING) {
            kx = file_of(Square(s)); ky = rank_of(Square(s)); break;
        }
    }
    if (kx < 0) return 0;
    return std::max(std::abs(file_of(sq) - kx), std::abs(rank_of(sq) - ky));
}

// King protector: minor piece distance to own king (penalty for being far)
static int king_protector(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p != W_KNIGHT && p != W_BISHOP) return 0;
    return king_distance(pos, sq);
}

// Long diagonal bishop: bishop on long diagonal (a1-h8 or h1-a8) that can see center
// Must be on outer 3 squares of diagonal, no pawns blocking view to center
static int long_diagonal_bishop(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_BISHOP) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    // Must be on a1-h8 diagonal (x==y) or h1-a8 diagonal (x==7-y)
    if (sx - sy != 0 && sx - (7 - sy) != 0) return 0;
    // Must be on outer part (min(x, 7-x) <= 2)
    if (std::min(sx, 7 - sx) > 2) return 0;
    // Walk toward center — no pawns blocking
    int x1 = sx, y1 = sy;
    for (int i = std::min(sx, 7 - sx); i < 4; i++) {
        Piece p = pos.piece_on(make_square(x1, y1));
        if (p == W_PAWN || p == B_PAWN) return 0;
        x1 += (x1 < 4) ? 1 : -1;
        y1 += (y1 < 4) ? 1 : -1;
    }
    return 1;
}

// Outpost total: combined outpost bonus for knights/bishops (12.05 ELO)
// Knight on outpost = 4, bishop = 3. Edge knight with no targets = 2.
// Reachable outpost for knight only = 1.
static int outpost_total(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p != W_KNIGHT && p != W_BISHOP) return 0;
    bool isKnight = (p == W_KNIGHT);

    if (!outpost(pos, sq)) {
        if (!isKnight) return 0;
        if (!reachable_outpost(pos, sq)) return 0;
        return 1; // knight can reach outpost
    }

    // Knight on edge file (a,b or g,h) with few targets
    if (isKnight && (file_of(sq) < 2 || file_of(sq) > 5)) {
        int sx = file_of(sq), sy = rank_of(sq);
        bool enemyAttacked = false;
        int sameSideEnemies = 0;
        static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
        static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
        for (int i = 0; i < 8; i++) {
            int nx = sx + kdx[i], ny = sy + kdy[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
            Piece tp = pos.piece_on(make_square(nx, ny));
            if (tp != NO_PIECE && color_of(tp) == BLACK && type_of(tp) >= KNIGHT)
                enemyAttacked = true;
        }
        for (int s = 0; s < 64; s++) {
            Piece ep = pos.piece_on(Square(s));
            if (ep == NO_PIECE || color_of(ep) != BLACK) continue;
            if (type_of(ep) < KNIGHT) continue;
            int ex = file_of(Square(s));
            if ((ex < 4 && sx < 4) || (ex >= 4 && sx >= 4))
                sameSideEnemies++;
        }
        if (!enemyAttacked && sameSideEnemies <= 1) return 2;
    }

    return isKnight ? 4 : 3;
}

// Rook on queen file: rook on same file as any queen
static int rook_on_queen_file(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_ROOK) return 0;
    int sx = file_of(sq);
    for (int y = 0; y < 8; y++) {
        Piece p = pos.piece_on(make_square(sx, y));
        if (type_of(p) == QUEEN) return 1;
    }
    return 0;
}

// Bishop xray pawns: count enemy pawns on same diagonal as our bishop
static int bishop_xray_pawns(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_BISHOP) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    int count = 0;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) != B_PAWN) continue;
        if (std::abs(file_of(Square(s)) - sx) == std::abs(rank_of(Square(s)) - sy))
            count++;
    }
    return count;
}

// Rook on king ring: rook on same file as enemy king ring (but not already counted as attacker)
static int rook_on_king_ring(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_ROOK) return 0;
    if (king_attackers_count(pos, sq) > 0) return 0;
    int sx = file_of(sq);
    for (int y = 0; y < 8; y++) {
        if (king_ring(pos, make_square(sx, y))) return 1;
    }
    return 0;
}

// Queen infiltration: queen on weak square in enemy camp (rank 5+)
// Can't be kicked by pawns now or later
static int queen_infiltration(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_QUEEN) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    if (sy > 3) return 0; // must be in enemy half (rank 5+ from white = y <= 3)
    // Not attacked by adjacent enemy pawns
    if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) return 0;
    if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) return 0;
    // No enemy pawn can attack this square in future
    if (pawn_attacks_span(pos, sq)) return 0;
    return 1;
}

// Bishop on king ring: bishop on diagonal of enemy king ring (not already attacker)
static int bishop_on_king_ring(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (pos.piece_on(sq) != W_BISHOP) return 0;
    if (king_attackers_count(pos, sq) > 0) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    // Check diagonals for king ring squares
    static const int dx[] = {-1, 1, -1, 1};
    static const int dy[] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; i++) {
        for (int d = 1; d < 8; d++) {
            int nx = sx + d * dx[i], ny = sy + d * dy[i];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
            if (king_ring(pos, make_square(nx, ny))) return 1;
            if (pos.piece_on(make_square(nx, ny)) != NO_PIECE) break;
        }
    }
    return 0;
}

// === Sub-components (TODO: implement each) ===

// Piece value bonus: material values (Stockfish classical)
// MG: P=124, N=781, B=825, R=1276, Q=2538
// EG: P=206, N=854, B=915, R=1380, Q=2682
static const int PieceValueMG[] = {0, 124, 781, 825, 1276, 2538, 0}; // NO_PT, P, N, B, R, Q, K
static const int PieceValueEG[] = {0, 206, 854, 915, 1380, 2682, 0};

// Piece value MG: sum of white material - black material (MG values)
static int piece_value_mg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < PAWN || pt > QUEEN) continue;
        if (color_of(p) == WHITE) v += PieceValueMG[pt];
        else                      v -= PieceValueMG[pt];
    }
    return v;
}
// PSQT bonus tables (Stockfish classical)
// Pieces indexed: [rank from white 8th][min(file, 7-file)] — horizontally mirrored
// Pawns indexed: [rank from white 8th][file]

static const int PieceBonusMG[5][8][4] = {
    // Knight
    {{-175,-92,-74,-73},{-77,-41,-27,-15},{-61,-17,6,12},{-35,8,40,49},{-34,13,44,51},{-9,22,58,53},{-67,-27,4,37},{-201,-83,-56,-26}},
    // Bishop
    {{-53,-5,-8,-23},{-15,8,19,4},{-7,21,-5,17},{-5,11,25,39},{-12,29,22,31},{-16,6,1,11},{-17,-14,5,0},{-48,1,-14,-23}},
    // Rook
    {{-31,-20,-14,-5},{-21,-13,-8,6},{-25,-11,-1,3},{-13,-5,-4,-6},{-27,-15,-4,3},{-22,-2,6,12},{-2,12,16,18},{-17,-19,-1,9}},
    // Queen
    {{3,-5,-5,4},{-3,5,8,12},{-3,6,13,7},{4,5,9,8},{0,14,12,5},{-4,10,6,8},{-5,6,10,8},{-2,-2,1,-2}},
    // King
    {{271,327,271,198},{278,303,234,179},{195,258,169,120},{164,190,138,98},{154,179,105,70},{123,145,81,31},{88,120,65,33},{59,89,45,-1}}
};

static const int PieceBonusEG[5][8][4] = {
    // Knight
    {{-96,-65,-49,-21},{-67,-54,-18,8},{-40,-27,-8,29},{-35,-2,13,28},{-45,-16,9,39},{-51,-44,-16,17},{-69,-50,-51,12},{-100,-88,-56,-17}},
    // Bishop
    {{-57,-30,-37,-12},{-37,-13,-17,1},{-16,-1,-2,10},{-20,-6,0,17},{-17,-1,-14,15},{-30,6,4,6},{-31,-20,-1,1},{-46,-42,-37,-24}},
    // Rook
    {{-9,-13,-10,-9},{-12,-9,-1,-2},{6,-8,-2,-6},{-6,1,-9,7},{-5,8,7,-6},{6,1,-7,10},{4,5,20,-5},{18,0,19,13}},
    // Queen
    {{-69,-57,-47,-26},{-55,-31,-22,-4},{-39,-18,-9,3},{-23,-3,13,24},{-29,-6,9,21},{-38,-18,-12,1},{-50,-27,-24,-8},{-75,-52,-43,-36}},
    // King
    {{1,45,85,76},{53,100,133,135},{88,130,169,175},{103,156,172,172},{96,166,199,199},{92,172,184,191},{47,121,116,131},{11,59,73,78}}
};

static const int PawnBonusMG[8][8] = {
    {0,0,0,0,0,0,0,0},
    {3,3,10,19,16,19,7,-5},
    {-9,-15,11,15,32,22,5,-22},
    {-4,-23,6,20,40,17,4,-8},
    {13,0,-13,1,11,-2,-13,5},
    {5,-12,-7,22,-8,-5,-15,-8},
    {-7,7,-3,-13,5,-16,10,-8},
    {0,0,0,0,0,0,0,0}
};

static const int PawnBonusEG[8][8] = {
    {0,0,0,0,0,0,0,0},
    {-10,-6,10,0,14,7,-5,-19},
    {-10,-10,-10,4,4,3,-6,-4},
    {6,-2,-8,-4,-13,-12,-10,-9},
    {10,5,4,-5,-5,-5,14,9},
    {28,20,21,28,30,7,6,13},
    {0,-11,12,21,25,19,4,7},
    {0,0,0,0,0,0,0,0}
};

// PSQT MG: sum of PST bonuses for white - black
static int psqt_mg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < PAWN || pt > KING) continue;
        int x = file_of(Square(sq));
        int y = rank_of(Square(sq));
        // From white's perspective: rank index = 7-y for white, y for black
        int ry = (color_of(p) == WHITE) ? 7 - y : y;
        int bonus;
        if (pt == PAWN) {
            int fx = (color_of(p) == WHITE) ? x : 7 - x;
            bonus = PawnBonusMG[ry][fx];
        } else {
            int fx = std::min(x, 7 - x);
            bonus = PieceBonusMG[pt - KNIGHT][ry][fx]; // KNIGHT=2 -> index 0
        }
        if (color_of(p) == WHITE) v += bonus;
        else                      v -= bonus;
    }
    return v;
}
// imbalance_total — defined above
// Pawns MG: pawn structure evaluation for middlegame
static int pawns_mg(const Position& pos) {
    static const int blockedBonus[] = {0, -11, -3};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        if (pos.piece_on(s) != W_PAWN) continue;

        if (doubled_isolated(pos, s))       v -= 11;
        else if (isolated(pos, s))           v -= 5;
        else if (backward(pos, s))           v -= 9;

        v -= doubled(pos, s) * 11;
        if (connected(pos, s))
            v += connected_bonus(pos, s);
        v -= 13 * weak_unopposed_pawn(pos, s);
        v += blockedBonus[blocked_pawn(pos, s)];
    }
    return v;
}
// Pieces MG: bonuses/penalties for N/B/R/Q placement
static int pieces_mg(const Position& pos) {
    static const int outpostBonus[] = {0, 31, -7, 30, 56};
    static const int rookFileBonus[] = {0, 19, 48};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        Piece p = pos.piece_on(s);
        if (p == NO_PIECE || color_of(p) != WHITE) continue;
        PieceType pt = type_of(p);
        if (pt < KNIGHT || pt > QUEEN) continue;

        int ot = outpost_total(pos, s);
        if (ot >= 0 && ot <= 4) v += outpostBonus[ot];
        v += 18 * minor_behind_pawn(pos, s);
        v -= 3 * bishop_pawns(pos, s);
        v -= 4 * bishop_xray_pawns(pos, s);
        v += 6 * rook_on_queen_file(pos, s);
        v += 16 * rook_on_king_ring(pos, s);
        v += 24 * bishop_on_king_ring(pos, s);
        int rf = rook_on_file(pos, s);
        if (rf >= 0 && rf <= 2) v += rookFileBonus[rf];
        // Trapped rook: worse if can't castle
        // TODO: check castling rights properly
        v -= trapped_rook(pos, s) * 55; // simplified: assume can castle
        v -= 56 * weak_queen(pos, s);
        v -= 2 * queen_infiltration(pos, s);
        v -= (pt == KNIGHT ? 8 : 6) * king_protector(pos, s);
        v += 45 * long_diagonal_bishop(pos, s);
    }
    return v;
}
// Mobility bonus tables indexed by [piece_type][mobility_count]
static const int MobilityBonusMG[4][28] = {
    // Knight (0-8)
    {-62,-53,-12,-4,3,13,22,28,33},
    // Bishop (0-13)
    {-48,-20,16,26,38,51,55,63,63,68,81,81,91,98},
    // Rook (0-14)
    {-60,-20,2,3,3,11,22,31,40,40,41,48,57,57,62},
    // Queen (0-27)
    {-30,-12,-8,-9,20,23,23,35,38,53,64,65,65,66,67,67,72,72,77,79,93,108,108,108,110,114,114,116}
};
static const int MobilityBonusEG[4][28] = {
    {-81,-56,-31,-16,5,11,17,20,25},
    {-59,-23,-3,13,24,42,54,57,65,73,78,86,88,97},
    {-78,-17,23,39,70,99,103,121,134,139,158,164,168,169,172},
    {-48,-30,-7,19,40,55,59,75,78,96,96,100,121,127,131,133,136,141,147,150,151,168,168,171,182,182,192,219}
};
static const int MobilityMaxCount[] = {8, 13, 14, 27}; // max index per piece type

// Mobility bonus for a single piece
static int mobility_bonus(const Position& pos, Square sq, bool mg) {
    Piece p = pos.piece_on(sq);
    if (p == NO_PIECE || color_of(p) != WHITE) return 0;
    PieceType pt = type_of(p);
    int idx = pt - KNIGHT; // N=0, B=1, R=2, Q=3
    if (idx < 0 || idx > 3) return 0;
    int mob = std::min(mobility(pos, sq), MobilityMaxCount[idx]);
    return mg ? MobilityBonusMG[idx][mob] : MobilityBonusEG[idx][mob];
}

// Mobility MG: sum of mobility bonuses for all white pieces - black
static int mobility_mg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < KNIGHT || pt > QUEEN) continue;
        // For black pieces, we'd need colorflip — simplified:
        // compute white mobility only, black handled by caller (white - colorflip)
        if (color_of(p) == WHITE)
            v += mobility_bonus(pos, Square(sq), true);
    }
    return v;
}
// === Threat sub-terms ===

// Safe pawn: our pawn that is defended or not attacked
static int safe_pawn(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    if (sx < 0 || sx > 7 || sy < 0 || sy > 7) return 0;
    if (pos.piece_on(sq) != W_PAWN) return 0;
    if (attack(pos, sq)) return 1;       // defended by our pieces
    // TODO: check if not attacked by enemy (needs colorflip)
    // Simplified: assume safe if defended
    return 0;
}

// Threat safe pawn: enemy non-pawn pieces attacked by our safe pawn (14.95 ELO)
static int threat_safe_pawn(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p == NO_PIECE || color_of(p) != BLACK) return 0;
    PieceType pt = type_of(p);
    if (pt < KNIGHT || pt > QUEEN) return 0;
    // Must be attacked by our pawn
    if (!pawn_attack(pos, sq)) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    // At least one attacking pawn must be safe
    if (sx > 0 && sy < 7 && safe_pawn(pos, make_square(sx - 1, sy + 1))) return 1;
    if (sx < 7 && sy < 7 && safe_pawn(pos, make_square(sx + 1, sy + 1))) return 1;
    return 0;
}

// Weak enemies: enemy pieces not defended by pawn or defended only weakly
static int weak_enemies(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    Piece p = pos.piece_on(sq);
    if (p == NO_PIECE || color_of(p) != BLACK) return 0;
    if (!attack(pos, sq)) return 0; // must be attacked by us
    // Check if defended by enemy pawn
    int sx = file_of(sq), sy = rank_of(sq);
    if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == B_PAWN) return 0;
    if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == B_PAWN) return 0;
    return 1;
}

// Minor threat: enemy piece attacked by our minor (N/B). Returns piece type index.
static int minor_threat(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!weak_enemies(pos, sq)) return 0;
    Piece p = pos.piece_on(sq);
    PieceType pt = type_of(p);
    // Must be attacked by our knight or bishop
    if (!knight_attack(pos, sq) && !bishop_xray_attack(pos, sq)) return 0;
    return pt; // PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5
}

// Rook threat: enemy piece attacked by our rook. Returns piece type index.
static int rook_threat(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    if (!weak_enemies(pos, sq)) return 0;
    Piece p = pos.piece_on(sq);
    PieceType pt = type_of(p);
    if (!rook_xray_attack(pos, sq)) return 0;
    return pt;
}

// Hanging: enemy pieces attacked by us and not defended by enemy
static int hanging_threat(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE || color_of(p) != BLACK) continue;
        if (!attack(pos, Square(sq))) continue;
        // Check if hanging (not defended) — simplified
        if (weak_enemies(pos, Square(sq)) && type_of(p) >= KNIGHT) v++;
    }
    return v;
}

// King threat: weak enemy pieces attacked by our king (4.69 ELO)
static int king_threat(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        Piece p = pos.piece_on(s);
        if (p == NO_PIECE || color_of(p) != BLACK) continue;
        if (type_of(p) < PAWN || type_of(p) > QUEEN) continue;
        if (!weak_enemies(pos, s)) continue;
        if (!king_attack(pos, s)) continue;
        v++;
    }
    return v;
}

// Pawn push threat: enemy piece that can be attacked by a pawn push (7.89 ELO)
// Checks single push and double push from rank 2
static int pawn_push_threat(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE || color_of(p) != BLACK) continue;
        if (type_of(p) < PAWN) continue;
        int sx = file_of(Square(sq)), sy = rank_of(Square(sq));

        for (int ix = -1; ix <= 1; ix += 2) {
            int px = sx + ix;
            if (px < 0 || px > 7) continue;

            // Single push: pawn 2 ranks behind, 1 rank behind empty, not attacked by enemy pawns
            if (sy + 2 < 8 && pos.piece_on(make_square(px, sy + 2)) == W_PAWN
                && pos.piece_on(make_square(px, sy + 1)) == NO_PIECE
                && (px == 0 || pos.piece_on(make_square(px - 1, sy)) != B_PAWN)
                && (px == 7 || pos.piece_on(make_square(px + 1, sy)) != B_PAWN)
                && (attack(pos, make_square(px, sy + 1)) > 0 /* || !enemy attack */)) {
                v++;
                continue;
            }

            // Double push: pawn 3 ranks behind (on rank 2), path clear
            if (sy == 3 && sy + 3 < 8
                && pos.piece_on(make_square(px, sy + 3)) == W_PAWN
                && pos.piece_on(make_square(px, sy + 2)) == NO_PIECE
                && pos.piece_on(make_square(px, sy + 1)) == NO_PIECE
                && (px == 0 || pos.piece_on(make_square(px - 1, sy)) != B_PAWN)
                && (px == 7 || pos.piece_on(make_square(px + 1, sy)) != B_PAWN)
                && (attack(pos, make_square(px, sy + 1)) > 0)) {
                v++;
            }
        }
    }
    return v;
}

// Slider on queen: safe slider threats on enemy queen
// Only when enemy has exactly 1 queen. Bonus x2 if we have no queen.
static int slider_on_queen(const Position& pos) {
    // Count enemy queens
    int enemyQueens = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == B_QUEEN) enemyQueens++;
    if (enemyQueens != 1) return 0;

    int ourQueens = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == W_QUEEN) ourQueens++;

    int bonus = (ourQueens == 0) ? 2 : 1;
    int v = 0;

    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        // Must be empty, not attacked by enemy pawn, attacked by us >1, in mobility area
        if (pos.piece_on(s) == W_PAWN) continue;
        int sx = file_of(s), sy = rank_of(s);
        if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) continue;
        if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) continue;
        if (attack(pos, s) <= 1) continue;
        if (!mobility_area(pos, s)) continue;

        // Check if our bishop xrays queen diagonally, or rook xrays queen on straight
        // TODO: full queen_attack_diagonal from black perspective
        if (bishop_xray_attack(pos, s)) { v += bonus; continue; }
        if (rook_xray_attack(pos, s)) { v += bonus; continue; }
    }
    return v;
}

// Knight on queen: safe knight attack on squares that are knight-move from enemy queen
// Only when enemy has exactly 1 queen
static int knight_on_queen(const Position& pos) {
    // Find single enemy queen
    int qx = -1, qy = -1, qcount = 0;
    for (int s = 0; s < 64; s++) {
        if (pos.piece_on(Square(s)) == B_QUEEN) {
            qx = file_of(Square(s)); qy = rank_of(Square(s)); qcount++;
        }
    }
    if (qcount != 1) return 0;

    int ourQueens = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == W_QUEEN) ourQueens++;
    int bonus = (ourQueens == 0) ? 2 : 1;

    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        int sx = file_of(s), sy = rank_of(s);
        if (pos.piece_on(s) == W_PAWN) continue;
        if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) continue;
        if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) continue;
        if (!mobility_area(pos, s)) continue;
        if (!knight_attack(pos, s)) continue;
        // Square must be knight-move from queen
        if ((std::abs(qx - sx) == 2 && std::abs(qy - sy) == 1)
         || (std::abs(qx - sx) == 1 && std::abs(qy - sy) == 2))
            v += bonus;
    }
    return v;
}

// Restricted: squares we attack AND enemy attacks, but enemy can't use pawn defense
// and we don't only have 1 attacker vs 2+ enemy defenders
static int restricted(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        int ourAtt = attack(pos, s);
        if (ourAtt == 0) continue;

        // Count enemy attacks/pawn attacks on this square (simplified)
        int sx = file_of(s), sy = rank_of(s);
        int enemyAtt = 0, enemyPawnAtt = 0;
        // Black pawn attacks
        if (sx > 0 && sy < 7 && pos.piece_on(make_square(sx - 1, sy + 1)) == B_PAWN) { enemyAtt++; enemyPawnAtt++; }
        if (sx < 7 && sy < 7 && pos.piece_on(make_square(sx + 1, sy + 1)) == B_PAWN) { enemyAtt++; enemyPawnAtt++; }
        // Black pieces (simplified count)
        static const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int i = 0; i < 8; i++) {
            int nx = sx + dx8[i], ny = sy + dy8[i];
            if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7)
                if (pos.piece_on(make_square(nx, ny)) == B_KING) enemyAtt++;
        }
        static const int kdx[] = {-2, -1, 1, 2, 2, 1, -1, -2};
        static const int kdy[] = {-1, -2, -2, -1, 1, 2, 2, 1};
        for (int i = 0; i < 8; i++) {
            int nx = sx + kdx[i], ny = sy + kdy[i];
            if (nx >= 0 && nx <= 7 && ny >= 0 && ny <= 7)
                if (pos.piece_on(make_square(nx, ny)) == B_KNIGHT) enemyAtt++;
        }
        for (int i = 0; i < 8; i++) {
            for (int d = 1; d < 8; d++) {
                int nx = sx + d * dx8[i], ny = sy + d * dy8[i];
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
                Piece p = pos.piece_on(make_square(nx, ny));
                if (p == NO_PIECE) continue;
                bool isDiag = (dx8[i] != 0 && dy8[i] != 0);
                if ((p == B_QUEEN) || (p == B_BISHOP && isDiag) || (p == B_ROOK && !isDiag))
                    enemyAtt++;
                break;
            }
        }

        if (enemyAtt == 0) continue;           // enemy doesn't attack — not restricted
        if (enemyPawnAtt > 0) continue;         // enemy pawn defends — not restricted
        if (enemyAtt > 1 && ourAtt == 1) continue; // we're outnumbered
        v++;
    }
    return v;
}

// Weak queen protection: weak enemy piece defended by their queen
static int weak_queen_protection(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        if (!weak_enemies(pos, s)) continue;
        // Check if black queen defends this square
        int sx = file_of(s), sy = rank_of(s);
        static const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int i = 0; i < 8; i++) {
            for (int d = 1; d < 8; d++) {
                int nx = sx + d * dx[i], ny = sy + d * dy[i];
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7) break;
                Piece p = pos.piece_on(make_square(nx, ny));
                if (p == B_QUEEN) { v++; goto next_sq; }
                if (p != NO_PIECE) break;
            }
        }
        next_sq:;
    }
    return v;
}

// Threats MG: combine all threat terms
static int threats_mg(const Position& pos) {
    static const int minorThreatBonus[] = {0, 5, 57, 77, 88, 79, 0};
    static const int rookThreatBonus[]  = {0, 3, 37, 42, 0, 58, 0};
    int v = 0;
    v += 69 * hanging_threat(pos);
    v += (king_threat(pos) > 0 ? 24 : 0);
    v += 48 * pawn_push_threat(pos);
    v += 173 * threat_safe_pawn(pos);
    v += 60 * slider_on_queen(pos);
    v += 16 * knight_on_queen(pos);
    v += 7 * restricted(pos);
    v += 14 * weak_queen_protection(pos);
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        int mt = minor_threat(pos, s);
        if (mt >= 0 && mt <= 6) v += minorThreatBonus[mt];
        int rt = rook_threat(pos, s);
        if (rt >= 0 && rt <= 6) v += rookThreatBonus[rt];
    }
    return v;
}
// Passed MG: rank bonus + block bonus - file penalty
static int passed_mg(const Position& pos) {
    static const int RankBonus[] = {0, 10, 17, 15, 62, 168, 276};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        if (!passed_leverable(pos, s)) continue;
        int r = passed_rank(pos, s);
        if (r >= 0 && r < 7) v += RankBonus[r];
        v += passed_block(pos, s);
        v -= 11 * passed_file(pos, s);
    }
    return v;
}
// Space area: safe central squares for minor pieces (ranks 2-4, files c-f)
// Counted twice if behind our pawn and not attacked by enemy
static int space_area(const Position& pos, Square sq) {
    if (sq == SQ_NONE) return 0;
    int r = rank_of(sq) + 1; // 1-indexed rank
    int f = file_of(sq) + 1; // 1-indexed file
    if (r < 2 || r > 4 || f < 3 || f > 6) return 0;
    int sx = file_of(sq), sy = rank_of(sq);
    if (pos.piece_on(sq) == W_PAWN) return 0;
    // Not attacked by enemy pawns
    if (sx > 0 && sy > 0 && pos.piece_on(make_square(sx - 1, sy - 1)) == B_PAWN) return 0;
    if (sx < 7 && sy > 0 && pos.piece_on(make_square(sx + 1, sy - 1)) == B_PAWN) return 0;

    int v = 1;
    // Doubled if behind our pawn (1-3 squares) and not attacked by enemy
    bool behindPawn = false;
    for (int d = 1; d <= 3; d++) {
        if (sy - d >= 0 && pos.piece_on(make_square(sx, sy - d)) == W_PAWN) {
            behindPawn = true;
            break;
        }
    }
    if (behindPawn) {
        // TODO: check enemy attack on this square (needs colorflip)
        // Simplified: assume not attacked
        v++;
    }
    return v;
}

// Space: space evaluation weighted by piece count and blocked pawns
// weight = pieceCount - 3 + min(blockedCount, 9)
// Only when total NPM >= 12222
static int space(const Position& pos) {
    if (non_pawn_material(pos, WHITE) + non_pawn_material(pos, BLACK) < 12222) return 0;

    int pieceCount = 0, blockedCount = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p != NO_PIECE && color_of(p) == WHITE) pieceCount++;
        int sx = file_of(Square(sq)), sy = rank_of(Square(sq));
        // Blocked white pawn: enemy pawn directly ahead, or two enemy pawns diag-2 ahead
        if (p == W_PAWN && sy > 0) {
            if (pos.piece_on(make_square(sx, sy - 1)) == B_PAWN) blockedCount++;
            else if (sy > 1
                     && sx > 0 && pos.piece_on(make_square(sx - 1, sy - 2)) == B_PAWN
                     && sx < 7 && pos.piece_on(make_square(sx + 1, sy - 2)) == B_PAWN)
                blockedCount++;
        }
        // Blocked black pawn (symmetric)
        if (p == B_PAWN && sy < 7) {
            if (pos.piece_on(make_square(sx, sy + 1)) == W_PAWN) blockedCount++;
            else if (sy < 6
                     && sx > 0 && pos.piece_on(make_square(sx - 1, sy + 2)) == W_PAWN
                     && sx < 7 && pos.piece_on(make_square(sx + 1, sy + 2)) == W_PAWN)
                blockedCount++;
        }
    }

    int area = 0;
    for (int sq = 0; sq < 64; sq++)
        area += space_area(pos, Square(sq));

    int weight = pieceCount - 3 + std::min(blockedCount, 9);
    return area * weight * weight / 16;
}
// King MG: shelter, storm, danger², flank attack, pawnless flank
static int king_mg(const Position& pos) {
    int v = 0;
    int kd = king_danger(pos);
    v -= shelter_strength(pos);
    v += shelter_storm(pos);
    v += kd * kd / 4096;    // quadratic danger — big penalty when danger is high
    v += 8 * flank_attack_total(pos);
    v += 17 * pawnless_flank(pos);
    return v;
}
// Winnable: second-order correction based on position characteristics
static int winnable(const Position& pos) {
    int pawns = 0;
    int kx[2] = {0, 0}, ky[2] = {0, 0};
    int flanks[2] = {0, 0};

    for (int x = 0; x < 8; x++) {
        int open[2] = {0, 0};
        for (int y = 0; y < 8; y++) {
            Piece p = pos.piece_on(make_square(x, y));
            if (type_of(p) == PAWN) {
                open[color_of(p) == WHITE ? 0 : 1] = 1;
                pawns++;
            }
            if (type_of(p) == KING) {
                int ci = (color_of(p) == WHITE) ? 0 : 1;
                kx[ci] = x;
                ky[ci] = y;
            }
        }
        if (open[0] + open[1] > 0)
            flanks[x < 4 ? 0 : 1] = 1;
    }

    // TODO: candidate_passed for black (needs colorflip)
    int passedCount = candidate_passed_total(pos); // white only, simplified
    int bothFlanks = (flanks[0] && flanks[1]) ? 1 : 0;
    int outflanking = std::abs(kx[0] - kx[1]) - std::abs(ky[0] - ky[1]);
    int purePawn = (non_pawn_material(pos, WHITE) + non_pawn_material(pos, BLACK) == 0) ? 1 : 0;
    int almostUnwinnable = (outflanking < 0 && !bothFlanks) ? 1 : 0;
    int infiltration = (ky[0] < 4 || ky[1] > 3) ? 1 : 0;

    return 9 * passedCount
         + 12 * pawns
         + 9 * outflanking
         + 21 * bothFlanks
         + 24 * infiltration
         + 51 * purePawn
         - 43 * almostUnwinnable
         - 110;
}

// Winnable total MG: reduces eval toward 0 when position is hard to win
// sign(v) * max(min(winnable+50, 0), -abs(v))
// If winnable+50 >= 0: no adjustment (position complex enough)
// If winnable+50 < 0: reduce eval, capped at -abs(v) (can't flip sign)
static int winnable_total_mg(const Position& pos, int v) {
    int sign = (v > 0) ? 1 : (v < 0) ? -1 : 0;
    int w = winnable(pos);
    return sign * std::max(std::min(w + 50, 0), -std::abs(v));
}

// Middle game evaluation — all MG terms combined
// Each term computed for white, then subtracted for black (colorflip).
// Stockfish uses colorflip(pos) to reuse same function for both sides.
// We compute white - black for each term.
static int middle_game_evaluation(const Position& pos, bool nowinnable = false) {
    int v = 0;

    // Material: piece values (no PST)
    v += piece_value_mg(pos);   // white - black built into function

    // Piece-square tables
    v += psqt_mg(pos);

    // Material imbalance (bishop pair, redundancy, etc.)
    v += imbalance_total(pos);

    // Pawn structure: isolated, doubled, backward, connected, etc.
    v += pawns_mg(pos);

    // Piece evaluation: outposts, rook on open file, bishop mobility, etc.
    v += pieces_mg(pos);

    // Mobility: safe squares each piece can reach
    v += mobility_mg(pos);

    // Threats: pieces under attack by lesser pieces
    v += threats_mg(pos);

    // Passed pawns
    v += passed_mg(pos);

    // Space control behind own pawn chain
    v += space(pos);

    // King safety: attack units, pawn shelter, etc.
    v += king_mg(pos);

    // Winnable: complexity adjustment (avoid draws when ahead)
    if (!nowinnable)
        v += winnable_total_mg(pos, v);

    return v;
}

// === EG Sub-components (TODO: implement each) ===

// Piece value EG
static int piece_value_eg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < PAWN || pt > QUEEN) continue;
        if (color_of(p) == WHITE) v += PieceValueEG[pt];
        else                      v -= PieceValueEG[pt];
    }
    return v;
}
// PSQT EG
static int psqt_eg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        if (pt < PAWN || pt > KING) continue;
        int x = file_of(Square(sq));
        int y = rank_of(Square(sq));
        int ry = (color_of(p) == WHITE) ? 7 - y : y;
        int bonus;
        if (pt == PAWN) {
            int fx = (color_of(p) == WHITE) ? x : 7 - x;
            bonus = PawnBonusEG[ry][fx];
        } else {
            int fx = std::min(x, 7 - x);
            bonus = PieceBonusEG[pt - KNIGHT][ry][fx];
        }
        if (color_of(p) == WHITE) v += bonus;
        else                      v -= bonus;
    }
    return v;
}
// Pawns EG: pawn structure evaluation for endgame
// Heavier penalties, connected bonus scaled by rank, weak lever penalty
static int pawns_eg(const Position& pos) {
    static const int blockedBonus[] = {0, -4, 4};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        if (pos.piece_on(s) != W_PAWN) continue;

        if (doubled_isolated(pos, s))       v -= 56;
        else if (isolated(pos, s))           v -= 15;
        else if (backward(pos, s))           v -= 24;

        v -= doubled(pos, s) * 56;
        if (connected(pos, s)) {
            int r = rank_of(s); // 0-indexed
            v += connected_bonus(pos, s) * (r - 2) / 4; // scale by rank
        }
        v -= 27 * weak_unopposed_pawn(pos, s);
        v -= 56 * weak_lever(pos, s);
        v += blockedBonus[blocked_pawn(pos, s)];
    }
    return v;
}
// Pieces EG: endgame bonuses/penalties for N/B/R/Q
static int pieces_eg(const Position& pos) {
    static const int outpostBonus[] = {0, 22, 36, 23, 36};
    static const int rookFileBonus[] = {0, 7, 29};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        Piece p = pos.piece_on(s);
        if (p == NO_PIECE || color_of(p) != WHITE) continue;
        PieceType pt = type_of(p);
        if (pt < KNIGHT || pt > QUEEN) continue;

        int ot = outpost_total(pos, s);
        if (ot >= 0 && ot <= 4) v += outpostBonus[ot];
        v += 3 * minor_behind_pawn(pos, s);
        v -= 7 * bishop_pawns(pos, s);
        v -= 5 * bishop_xray_pawns(pos, s);
        v += 11 * rook_on_queen_file(pos, s);
        int rf = rook_on_file(pos, s);
        if (rf >= 0 && rf <= 2) v += rookFileBonus[rf];
        v -= trapped_rook(pos, s) * 13; // simplified
        v -= 15 * weak_queen(pos, s);
        v += 14 * queen_infiltration(pos, s);
        v -= 9 * king_protector(pos, s);
    }
    return v;
}
// Mobility EG
static int mobility_eg(const Position& pos) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE || color_of(p) != WHITE) continue;
        PieceType pt = type_of(p);
        if (pt < KNIGHT || pt > QUEEN) continue;
        v += mobility_bonus(pos, Square(sq), false);
    }
    return v;
}
// Threats EG
static int threats_eg(const Position& pos) {
    static const int minorThreatBonus[] = {0, 32, 41, 56, 119, 161, 0};
    static const int rookThreatBonus[]  = {0, 46, 68, 60, 38, 41, 0};
    int v = 0;
    v += 36 * hanging_threat(pos);
    v += (king_threat(pos) > 0 ? 89 : 0);
    v += 39 * pawn_push_threat(pos);
    v += 94 * threat_safe_pawn(pos);
    v += 18 * slider_on_queen(pos);
    v += 11 * knight_on_queen(pos);
    v += 7 * restricted(pos);
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        int mt = minor_threat(pos, s);
        if (mt >= 0 && mt <= 6) v += minorThreatBonus[mt];
        int rt = rook_threat(pos, s);
        if (rt >= 0 && rt <= 6) v += rookThreatBonus[rt];
    }
    return v;
}
// Passed EG: king proximity + rank bonus + block bonus - file penalty
static int passed_eg(const Position& pos) {
    static const int RankBonus[] = {0, 28, 33, 41, 72, 177, 260};
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Square s = Square(sq);
        if (!passed_leverable(pos, s)) continue;
        v += king_proximity(pos, s);
        int r = passed_rank(pos, s);
        if (r >= 0 && r < 7) v += RankBonus[r];
        v += passed_block(pos, s);
        v -= 8 * passed_file(pos, s);
    }
    return v;
}
// King EG: king-pawn distance, endgame shelter, pawnless flank, danger/16
static int king_eg(const Position& pos) {
    int v = 0;
    v -= 16 * king_pawn_distance(pos);  // king must be near pawns in EG
    v += endgame_shelter(pos);            // blocked storm EG component
    v += 95 * pawnless_flank(pos);        // bigger penalty in EG
    v += king_danger(pos) / 16;           // linear (not quadratic) in EG
    return v;
}
// Winnable total EG: sign(v) * max(winnable, -abs(v))
// No +50 bias — EG is always adjusted (more aggressive than MG)
static int winnable_total_eg(const Position& pos, int v) {
    int sign = (v > 0) ? 1 : (v < 0) ? -1 : 0;
    int w = winnable(pos);
    return sign * std::max(w, -std::abs(v));
}

// End game evaluation — all EG terms combined
// Same structure as MG but no space term, different weights
static int end_game_evaluation(const Position& pos, bool nowinnable = false) {
    int v = 0;

    // Material: piece values (EG weights)
    v += piece_value_eg(pos);

    // Piece-square tables (EG)
    v += psqt_eg(pos);

    // Material imbalance (same for MG/EG)
    v += imbalance_total(pos);

    // Pawn structure (EG weights)
    v += pawns_eg(pos);

    // Piece evaluation (EG)
    v += pieces_eg(pos);

    // Mobility (EG weights)
    v += mobility_eg(pos);

    // Threats (EG weights)
    v += threats_eg(pos);

    // Passed pawns (EG — more important than MG)
    v += passed_eg(pos);

    // King (EG — centralization, not shelter)
    v += king_eg(pos);

    // Note: no space() in EG (irrelevant in endgame)

    // Winnable: complexity adjustment (EG)
    if (!nowinnable)
        v += winnable_total_eg(pos, v);

    return v;
}

// Phase: 128 = full middlegame, 0 = full endgame
// Based on total non-pawn material on the board
// Stockfish limits: midgameLimit=15258, endgameLimit=3915
static int phase(const Position& pos) {
    constexpr int MidgameLimit = 15258;
    constexpr int EndgameLimit = 3915;

    int npm = non_pawn_material(pos, WHITE) + non_pawn_material(pos, BLACK);
    npm = std::clamp(npm, EndgameLimit, MidgameLimit);

    return ((npm - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit);
}

// === Helpers for scale factor (TODO: implement with real Position methods) ===

static int pawn_count(const Position& pos, Color c) {
    int v = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == make_piece(c, PAWN)) v++;
    return v;
}
static int queen_count(const Position& pos, Color c) {
    int v = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == make_piece(c, QUEEN)) v++;
    return v;
}
static int bishop_count(const Position& pos, Color c) {
    int v = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == make_piece(c, BISHOP)) v++;
    return v;
}
static int knight_count(const Position& pos, Color c) {
    int v = 0;
    for (int s = 0; s < 64; s++)
        if (pos.piece_on(Square(s)) == make_piece(c, KNIGHT)) v++;
    return v;
}
static int piece_count(const Position& pos, Color c) {
    int v = 0;
    for (int s = 0; s < 64; s++) {
        Piece p = pos.piece_on(Square(s));
        if (p != NO_PIECE && color_of(p) == c) v++;
    }
    return v;
}
// Non pawn material: sum of MG values of N+B+R+Q for given color
static int non_pawn_material(const Position& pos, Color c) {
    int v = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE || color_of(p) != c) continue;
        PieceType pt = type_of(p);
        if (pt >= KNIGHT && pt <= QUEEN)
            v += PieceValueMG[pt];
    }
    return v;
}
static bool opposite_bishops(const Position& pos) {
    int wb = 0, bb = 0;
    int wcolor = -1, bcolor = -1;
    for (int s = 0; s < 64; s++) {
        Piece p = pos.piece_on(Square(s));
        if (p == W_BISHOP) { wb++; wcolor = (file_of(Square(s)) + rank_of(Square(s))) % 2; }
        if (p == B_BISHOP) { bb++; bcolor = (file_of(Square(s)) + rank_of(Square(s))) % 2; }
    }
    return wb == 1 && bb == 1 && wcolor != bcolor;
}
// candidate_passed for specific color — simplified: count white passers only
// TODO: needs colorflip for black
static int candidate_passed(const Position& pos, Color c) {
    if (c == WHITE) return candidate_passed_total(pos);
    return 0; // TODO: colorflip
}

// Stockfish material values used in scale factor
constexpr int BishopValueMg = 825;
constexpr int BishopValueEg = 915;
constexpr int RookValueMg   = 1276;

// Scale factor: 64 = normal, <64 = drawish
// Reduces EG eval in drawn endgames
// Stockfish logic: handles no-pawn, opposite bishops, rook endings, queen vs pieces
static int scale_factor(const Position& pos, int eg) {
    // Determine strong/weak side based on EG eval sign
    Color strong = (eg > 0) ? WHITE : BLACK;
    Color weak   = ~strong;

    int sf = 64;

    int pc_w = pawn_count(pos, strong);
    int pc_b = pawn_count(pos, weak);
    int qc_w = queen_count(pos, strong);
    int qc_b = queen_count(pos, weak);
    int bc_w = bishop_count(pos, strong);
    int bc_b = bishop_count(pos, weak);
    int nc_w = knight_count(pos, strong);
    int nc_b = knight_count(pos, weak);
    int npm_w = non_pawn_material(pos, strong);
    int npm_b = non_pawn_material(pos, weak);

    // Strong side has no pawns and small material advantage
    if (pc_w == 0 && npm_w - npm_b <= BishopValueMg) {
        if (npm_w < RookValueMg)
            sf = 0;                         // Insufficient material to win
        else if (npm_b <= BishopValueMg)
            sf = 4;                         // Rook vs minor — very drawish
        else
            sf = 14;                        // Rook+ vs rook — drawish
    }

    if (sf == 64) {
        bool ob = opposite_bishops(pos);

        if (ob && npm_w == BishopValueMg && npm_b == BishopValueMg) {
            // Pure opposite color bishops — very drawish
            // More candidate passers = slightly less drawish
            sf = 22 + 4 * candidate_passed(pos, strong);
        }
        else if (ob) {
            // Opposite bishops + other pieces
            sf = 22 + 3 * piece_count(pos, strong);
        }
        else {
            // Rook endgame: R vs R with similar pawns
            if (npm_w == RookValueMg && npm_b == RookValueMg && pc_w - pc_b <= 1) {
                // TODO: check pawn flanks and king proximity
                // Simplified: if single flank pawns with king near — drawish
                // sf = 36 in specific cases
            }

            // Queen vs minor pieces
            if (qc_w + qc_b == 1) {
                sf = 37 + 3 * (qc_w == 1 ? bc_b + nc_b : bc_w + nc_w);
            } else {
                sf = std::min(sf, 36 + 7 * pc_w);
            }
        }
    }

    return sf;
}

// Tempo: small bonus (28cp) for having the right to move
static int tempo(const Position& pos) {
    return 28 * (pos.side_to_move() == WHITE ? 1 : -1);
}

// Rule50: halfmove clock (0-100)
static int rule50(const Position& pos) {
    return pos.halfmove_clock();
}

// === Main evaluation ===
// Stockfish classical eval structure:
// 1. Compute mg and eg scores
// 2. Scale eg by scale_factor / 64
// 3. Tapered eval: blend mg and eg by phase
// 4. Round to granularity of 16
// 5. Add tempo
// 6. Scale by rule50

int evaluate(const Position& pos) {
    int mg = middle_game_evaluation(pos);
    int eg = end_game_evaluation(pos);

    int p = phase(pos);
    int r50 = rule50(pos);

    // Scale endgame by scale factor (64 = 100%, 32 = 50%, etc.)
    eg = eg * scale_factor(pos, eg) / 64;

    // Tapered eval: phase 128 = pure MG, phase 0 = pure EG
    int v = (mg * p + eg * (128 - p)) / 128;

    // Granularity: round to nearest multiple of 16
    v = (v / GRAIN) * GRAIN;

    // Add tempo bonus
    v += tempo(pos);

    // Rule50 scaling: eval decays toward 0 as 50-move rule approaches
    v = v * (100 - r50) / 100;

    return v;
}

} // namespace Eval
