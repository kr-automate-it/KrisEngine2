#include "eval.h"
#include <algorithm>
#include <cmath>

namespace Eval {

// === Material values (Stockfish classical) ===
static const int PieceValueMG[] = {0, 124, 781, 825, 1276, 2538, 0};
static const int PieceValueEG[] = {0, 206, 854, 915, 1380, 2682, 0};

// === PSQT (Stockfish classical, horizontally mirrored for pieces) ===
static const int PiecePSQT_MG[5][8][4] = {
    {{-175,-92,-74,-73},{-77,-41,-27,-15},{-61,-17,6,12},{-35,8,40,49},{-34,13,44,51},{-9,22,58,53},{-67,-27,4,37},{-201,-83,-56,-26}},
    {{-53,-5,-8,-23},{-15,8,19,4},{-7,21,-5,17},{-5,11,25,39},{-12,29,22,31},{-16,6,1,11},{-17,-14,5,0},{-48,1,-14,-23}},
    {{-31,-20,-14,-5},{-21,-13,-8,6},{-25,-11,-1,3},{-13,-5,-4,-6},{-27,-15,-4,3},{-22,-2,6,12},{-2,12,16,18},{-17,-19,-1,9}},
    {{3,-5,-5,4},{-3,5,8,12},{-3,6,13,7},{4,5,9,8},{0,14,12,5},{-4,10,6,8},{-5,6,10,8},{-2,-2,1,-2}},
    {{271,327,271,198},{278,303,234,179},{195,258,169,120},{164,190,138,98},{154,179,105,70},{123,145,81,31},{88,120,65,33},{59,89,45,-1}}
};
static const int PiecePSQT_EG[5][8][4] = {
    {{-96,-65,-49,-21},{-67,-54,-18,8},{-40,-27,-8,29},{-35,-2,13,28},{-45,-16,9,39},{-51,-44,-16,17},{-69,-50,-51,12},{-100,-88,-56,-17}},
    {{-57,-30,-37,-12},{-37,-13,-17,1},{-16,-1,-2,10},{-20,-6,0,17},{-17,-1,-14,15},{-30,6,4,6},{-31,-20,-1,1},{-46,-42,-37,-24}},
    {{-9,-13,-10,-9},{-12,-9,-1,-2},{6,-8,-2,-6},{-6,1,-9,7},{-5,8,7,-6},{6,1,-7,10},{4,5,20,-5},{18,0,19,13}},
    {{-69,-57,-47,-26},{-55,-31,-22,-4},{-39,-18,-9,3},{-23,-3,13,24},{-29,-6,9,21},{-38,-18,-12,1},{-50,-27,-24,-8},{-75,-52,-43,-36}},
    {{1,45,85,76},{53,100,133,135},{88,130,169,175},{103,156,172,172},{96,166,199,199},{92,172,184,191},{47,121,116,131},{11,59,73,78}}
};
static const int PawnPSQT_MG[8][8] = {
    {0,0,0,0,0,0,0,0},{3,3,10,19,16,19,7,-5},{-9,-15,11,15,32,22,5,-22},{-4,-23,6,20,40,17,4,-8},
    {13,0,-13,1,11,-2,-13,5},{5,-12,-7,22,-8,-5,-15,-8},{-7,7,-3,-13,5,-16,10,-8},{0,0,0,0,0,0,0,0}
};
static const int PawnPSQT_EG[8][8] = {
    {0,0,0,0,0,0,0,0},{-10,-6,10,0,14,7,-5,-19},{-10,-10,-10,4,4,3,-6,-4},{6,-2,-8,-4,-13,-12,-10,-9},
    {10,5,4,-5,-5,-5,14,9},{28,20,21,28,30,7,6,13},{0,-11,12,21,25,19,4,7},{0,0,0,0,0,0,0,0}
};

// === Mobility bonus tables ===
static const int MobBonusMG[4][28] = {
    {-62,-53,-12,-4,3,13,22,28,33},
    {-48,-20,16,26,38,51,55,63,63,68,81,81,91,98},
    {-60,-20,2,3,3,11,22,31,40,40,41,48,57,57,62},
    {-30,-12,-8,-9,20,23,23,35,38,53,64,65,65,66,67,67,72,72,77,79,93,108,108,108,110,114,114,116}
};
static const int MobBonusEG[4][28] = {
    {-81,-56,-31,-16,5,11,17,20,25},
    {-59,-23,-3,13,24,42,54,57,65,73,78,86,88,97},
    {-78,-17,23,39,70,99,103,121,134,139,158,164,168,169,172},
    {-48,-30,-7,19,40,55,59,75,78,96,96,100,121,127,131,133,136,141,147,150,151,168,168,171,182,182,192,219}
};
static const int MobMax[] = {8, 13, 14, 27};

// === Main evaluation — bitboard based ===

int evaluate(const Position& pos) {
    int mg = 0, eg = 0;

    // Cache bitboards
    U64 occ = pos.pieces();
    U64 wPieces = pos.pieces(WHITE);
    U64 bPieces = pos.pieces(BLACK);

    Square wKing = pos.king_square(WHITE);
    Square bKing = pos.king_square(BLACK);

    // === Material + PSQT (incremental would be better, but correct first) ===
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;
        PieceType pt = type_of(p);
        Color c = color_of(p);
        int sign = (c == WHITE) ? 1 : -1;
        int x = file_of(Square(sq));
        int y = rank_of(Square(sq));
        int ry = (c == WHITE) ? 7 - y : y;

        if (pt >= PAWN && pt <= QUEEN) {
            mg += sign * PieceValueMG[pt];
            eg += sign * PieceValueEG[pt];
        }

        if (pt == PAWN) {
            int fx = (c == WHITE) ? x : 7 - x;
            mg += sign * PawnPSQT_MG[ry][fx];
            eg += sign * PawnPSQT_EG[ry][fx];
        } else if (pt >= KNIGHT && pt <= KING) {
            int fx = std::min(x, 7 - x);
            mg += sign * PiecePSQT_MG[pt - KNIGHT][ry][fx];
            eg += sign * PiecePSQT_EG[pt - KNIGHT][ry][fx];
        }
    }

    // === Attack bitboards (computed ONCE) ===
    U64 wPawnAtt = ((pos.pieces(WHITE, PAWN) << 7) & ~FileH_BB)
                 | ((pos.pieces(WHITE, PAWN) << 9) & ~FileA_BB);
    U64 bPawnAtt = ((pos.pieces(BLACK, PAWN) >> 9) & ~FileH_BB)
                 | ((pos.pieces(BLACK, PAWN) >> 7) & ~FileA_BB);

    U64 wAttAll = wPawnAtt | KingAttacks[wKing];
    U64 bAttAll = bPawnAtt | KingAttacks[bKing];

    // Mobility area: exclude king, queen, enemy pawn attacks, blocked pawns rank 2-3
    U64 wMobArea = ~(pos.pieces(WHITE, KING) | pos.pieces(WHITE, QUEEN) | bPawnAtt);
    U64 bMobArea = ~(pos.pieces(BLACK, KING) | pos.pieces(BLACK, QUEEN) | wPawnAtt);
    // Exclude blocked pawns on rank 2-3
    U64 wBlockedPawns = pos.pieces(WHITE, PAWN) & (Rank2_BB | Rank3_BB);
    wBlockedPawns |= pos.pieces(WHITE, PAWN) & (occ >> 8); // pawn with piece in front
    wMobArea &= ~wBlockedPawns;
    U64 bBlockedPawns = pos.pieces(BLACK, PAWN) & (Rank6_BB | Rank7_BB);
    bBlockedPawns |= pos.pieces(BLACK, PAWN) & (occ << 8);
    bMobArea &= ~bBlockedPawns;

    // Knight attacks + mobility
    U64 knights = pos.pieces(WHITE, KNIGHT);
    while (knights) {
        Square s = pop_lsb(knights);
        U64 att = KnightAttacks[s];
        wAttAll |= att;
        int mob = std::min(popcount(att & wMobArea & ~pos.pieces(WHITE, QUEEN)), MobMax[0]);
        mg += MobBonusMG[0][mob];
        eg += MobBonusEG[0][mob];
    }
    knights = pos.pieces(BLACK, KNIGHT);
    while (knights) {
        Square s = pop_lsb(knights);
        U64 att = KnightAttacks[s];
        bAttAll |= att;
        int mob = std::min(popcount(att & bMobArea & ~pos.pieces(BLACK, QUEEN)), MobMax[0]);
        mg -= MobBonusMG[0][mob];
        eg -= MobBonusEG[0][mob];
    }

    // Bishop attacks + mobility
    U64 bishops = pos.pieces(WHITE, BISHOP);
    while (bishops) {
        Square s = pop_lsb(bishops);
        U64 att = bishop_attacks(s, occ ^ pos.pieces(WHITE, QUEEN)); // x-ray through queen
        wAttAll |= att;
        int mob = std::min(popcount(att & wMobArea & ~pos.pieces(WHITE, QUEEN)), MobMax[1]);
        mg += MobBonusMG[1][mob];
        eg += MobBonusEG[1][mob];
    }
    bishops = pos.pieces(BLACK, BISHOP);
    while (bishops) {
        Square s = pop_lsb(bishops);
        U64 att = bishop_attacks(s, occ ^ pos.pieces(BLACK, QUEEN));
        bAttAll |= att;
        int mob = std::min(popcount(att & bMobArea & ~pos.pieces(BLACK, QUEEN)), MobMax[1]);
        mg -= MobBonusMG[1][mob];
        eg -= MobBonusEG[1][mob];
    }

    // Rook attacks + mobility
    U64 rooks = pos.pieces(WHITE, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        U64 att = rook_attacks(s, occ ^ pos.pieces(WHITE, QUEEN) ^ pos.pieces(WHITE, ROOK));
        wAttAll |= att;
        int mob = std::min(popcount(att & wMobArea), MobMax[2]);
        mg += MobBonusMG[2][mob];
        eg += MobBonusEG[2][mob];

        // Rook on open/semi-open file
        U64 fileBB = FileA_BB << file_of(s);
        if (!(pos.pieces(PAWN) & fileBB))              { mg += 48; eg += 29; }
        else if (!(pos.pieces(WHITE, PAWN) & fileBB))  { mg += 19; eg += 7; }
    }
    rooks = pos.pieces(BLACK, ROOK);
    while (rooks) {
        Square s = pop_lsb(rooks);
        U64 att = rook_attacks(s, occ ^ pos.pieces(BLACK, QUEEN) ^ pos.pieces(BLACK, ROOK));
        bAttAll |= att;
        int mob = std::min(popcount(att & bMobArea), MobMax[2]);
        mg -= MobBonusMG[2][mob];
        eg -= MobBonusEG[2][mob];

        U64 fileBB = FileA_BB << file_of(s);
        if (!(pos.pieces(PAWN) & fileBB))              { mg -= 48; eg -= 29; }
        else if (!(pos.pieces(BLACK, PAWN) & fileBB))  { mg -= 19; eg -= 7; }
    }

    // Queen attacks + mobility
    U64 queens = pos.pieces(WHITE, QUEEN);
    while (queens) {
        Square s = pop_lsb(queens);
        U64 att = queen_attacks(s, occ);
        wAttAll |= att;
        int mob = std::min(popcount(att & wMobArea), MobMax[3]);
        mg += MobBonusMG[3][mob];
        eg += MobBonusEG[3][mob];
    }
    queens = pos.pieces(BLACK, QUEEN);
    while (queens) {
        Square s = pop_lsb(queens);
        U64 att = queen_attacks(s, occ);
        bAttAll |= att;
        int mob = std::min(popcount(att & bMobArea), MobMax[3]);
        mg -= MobBonusMG[3][mob];
        eg -= MobBonusEG[3][mob];
    }

    // === Pawn structure ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        U64 ourPawns = pos.pieces(c, PAWN);
        U64 theirPawns = pos.pieces(them, PAWN);

        U64 pawns = ourPawns;
        while (pawns) {
            Square s = pop_lsb(pawns);
            int f = file_of(s);
            U64 fileMask = FileA_BB << f;
            U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);

            // Isolated
            if (!(ourPawns & adjFiles)) {
                mg -= sign * 5;
                eg -= sign * 15;
            }

            // Doubled
            if (popcount(ourPawns & fileMask) > 1) {
                mg -= sign * 11;
                eg -= sign * 56;
            }

            // Connected bonus (simplified)
            bool phalanx = (f > 0 && (ourPawns & square_bb(make_square(f-1, rank_of(s)))))
                        || (f < 7 && (ourPawns & square_bb(make_square(f+1, rank_of(s)))));
            bool supported = (c == WHITE)
                ? ((f > 0 && rank_of(s) < 7 && (ourPawns & square_bb(make_square(f-1, rank_of(s)+1))))
                || (f < 7 && rank_of(s) < 7 && (ourPawns & square_bb(make_square(f+1, rank_of(s)+1)))))
                : ((f > 0 && rank_of(s) > 0 && (ourPawns & square_bb(make_square(f-1, rank_of(s)-1))))
                || (f < 7 && rank_of(s) > 0 && (ourPawns & square_bb(make_square(f+1, rank_of(s)-1)))));
            if (phalanx || supported) {
                static const int seed[] = {0, 7, 8, 12, 29, 48, 86};
                int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
                if (r >= 1 && r <= 6) {
                    int bonus = seed[r] * (2 + (phalanx?1:0)) + 21 * (supported?1:0);
                    mg += sign * bonus;
                    eg += sign * bonus * (r - 2) / 4;
                }
            }
        }
    }

    // === Bishop pair ===
    if (popcount(pos.pieces(WHITE, BISHOP)) >= 2) { mg += 30; eg += 50; }
    if (popcount(pos.pieces(BLACK, BISHOP)) >= 2) { mg -= 30; eg -= 50; }

    // === Threats (simplified bitboard) ===
    // Minor pieces attacked by enemy pawns
    mg -= popcount(pos.pieces(WHITE, KNIGHT, BISHOP) & bPawnAtt) * 25;
    eg -= popcount(pos.pieces(WHITE, KNIGHT, BISHOP) & bPawnAtt) * 15;
    mg += popcount(pos.pieces(BLACK, KNIGHT, BISHOP) & wPawnAtt) * 25;
    eg += popcount(pos.pieces(BLACK, KNIGHT, BISHOP) & wPawnAtt) * 15;

    // Hanging pieces
    U64 wHanging = (wPieces & bAttAll) & ~(wPieces & wAttAll) & ~pos.pieces(WHITE, PAWN);
    U64 bHanging = (bPieces & wAttAll) & ~(bPieces & bAttAll) & ~pos.pieces(BLACK, PAWN);
    mg -= popcount(wHanging) * 69;
    eg -= popcount(wHanging) * 36;
    mg += popcount(bHanging) * 69;
    eg += popcount(bHanging) * 36;

    // === King safety (simplified) ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Square ksq = (c == WHITE) ? bKing : wKing; // enemy king
        U64 ourAtt = (c == WHITE) ? wAttAll : bAttAll;
        U64 kingZone = KingAttacks[ksq] | square_bb(ksq);

        int attackers = 0, attackWeight = 0, attackCount = 0;
        static const int weights[] = {0, 0, 81, 52, 44, 10, 0};

        // Count attackers on king zone
        U64 pieces = pos.pieces(c);
        while (pieces) {
            Square s = pop_lsb(pieces);
            PieceType pt = type_of(pos.piece_on(s));
            if (pt < KNIGHT || pt > QUEEN) continue;

            U64 att;
            switch (pt) {
                case KNIGHT: att = KnightAttacks[s]; break;
                case BISHOP: att = bishop_attacks(s, occ); break;
                case ROOK:   att = rook_attacks(s, occ); break;
                case QUEEN:  att = queen_attacks(s, occ); break;
                default: continue;
            }
            if (att & kingZone) {
                attackers++;
                attackWeight += weights[pt];
                attackCount += popcount(att & kingZone);
            }
        }

        if (attackers >= 2) {
            int danger = attackWeight + 69 * attackCount + 37;
            if (danger > 100) {
                mg += sign * danger * danger / 4096;
                eg += sign * danger / 16;
            }
        }
    }

    // === Passed pawns (simplified) ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        U64 pawns = pos.pieces(c, PAWN);
        U64 theirPawns = pos.pieces(~c, PAWN);
        while (pawns) {
            Square s = pop_lsb(pawns);
            int f = file_of(s);
            int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            U64 fileMask = FileA_BB << f;
            U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);

            // Front mask
            U64 frontMask;
            if (c == WHITE)
                frontMask = (r < 7) ? ~((1ULL << ((rank_of(s) + 1) * 8)) - 1) : 0;
            else
                frontMask = (rank_of(s) > 0) ? (1ULL << (rank_of(s) * 8)) - 1 : 0;

            if (!(theirPawns & (fileMask | adjFiles) & frontMask)) {
                // Passed pawn!
                static const int mgBonus[] = {0, 10, 17, 15, 62, 168, 276};
                static const int egBonus[] = {0, 28, 33, 41, 72, 177, 260};
                if (r >= 1 && r <= 6) {
                    mg += sign * mgBonus[r];
                    eg += sign * egBonus[r];
                }
            }
        }
    }

    // === Space (simplified) ===
    {
        int npm = 0;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = pos.piece_on(Square(sq));
            if (p != NO_PIECE && type_of(p) >= KNIGHT && type_of(p) <= QUEEN)
                npm += PieceValueMG[type_of(p)];
        }
        if (npm >= 12222) {
            U64 center = FileC_BB | FileD_BB | FileE_BB | FileF_BB;
            U64 wSpace = pos.pieces(WHITE, PAWN) & center & (Rank2_BB | Rank3_BB | Rank4_BB);
            U64 bSpace = pos.pieces(BLACK, PAWN) & center & (Rank5_BB | Rank6_BB | Rank7_BB);
            int space = popcount(wSpace) - popcount(bSpace);
            mg += space * 2;
        }
    }

    // === Tempo ===
    mg += (pos.side_to_move() == WHITE) ? 28 : -28;
    eg += (pos.side_to_move() == WHITE) ? 28 : -28;

    // === Tapered eval ===
    constexpr int MidgameLimit = 15258;
    constexpr int EndgameLimit = 3915;
    int npm = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p != NO_PIECE && type_of(p) >= KNIGHT && type_of(p) <= QUEEN)
            npm += PieceValueMG[type_of(p)];
    }
    npm = std::clamp(npm, EndgameLimit, MidgameLimit);
    int phase = ((npm - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit);

    int score = (mg * phase + eg * (128 - phase)) / 128;

    // Rule50
    int r50 = pos.halfmove_clock();
    if (r50 > 0) score = score * (100 - r50) / 100;

    return (pos.side_to_move() == WHITE) ? score : -score;
}

} // namespace Eval
