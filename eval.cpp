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

// === Sub-components (TODO: implement each) ===

static int piece_value_mg(const Position& pos) { return 0; } // TODO
static int psqt_mg(const Position& pos) { return 0; }        // TODO
// imbalance_total — defined above
static int pawns_mg(const Position& pos) { return 0; }       // TODO
static int pieces_mg(const Position& pos) { return 0; }      // TODO
static int mobility_mg(const Position& pos) { return 0; }    // TODO
static int threats_mg(const Position& pos) { return 0; }     // TODO
static int passed_mg(const Position& pos) { return 0; }      // TODO
static int space(const Position& pos) { return 0; }          // TODO
static int king_mg(const Position& pos) { return 0; }        // TODO
static int winnable_total_mg(const Position& pos, int v) { return 0; } // TODO

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

static int piece_value_eg(const Position& pos) { return 0; } // TODO
static int psqt_eg(const Position& pos) { return 0; }        // TODO
static int pawns_eg(const Position& pos) { return 0; }       // TODO
static int pieces_eg(const Position& pos) { return 0; }      // TODO
static int mobility_eg(const Position& pos) { return 0; }    // TODO
static int threats_eg(const Position& pos) { return 0; }     // TODO
static int passed_eg(const Position& pos) { return 0; }      // TODO
static int king_eg(const Position& pos) { return 0; }        // TODO
static int winnable_total_eg(const Position& pos, int v) { return 0; } // TODO

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

static int pawn_count(const Position& pos, Color c) { return 0; }       // TODO
static int queen_count(const Position& pos, Color c) { return 0; }      // TODO
static int bishop_count(const Position& pos, Color c) { return 0; }     // TODO
static int knight_count(const Position& pos, Color c) { return 0; }     // TODO
static int piece_count(const Position& pos, Color c) { return 0; }      // TODO: all pieces
static int non_pawn_material(const Position& pos, Color c) { return 0; }// TODO
static bool opposite_bishops(const Position& pos) { return false; }     // TODO
static int candidate_passed(const Position& pos, Color c) { return 0; } // TODO

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
