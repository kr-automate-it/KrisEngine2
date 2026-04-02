#include "movegen.h"

// === Pawn move generation ===

static void generate_pawn_moves(const Position& pos, MoveList& list, bool capturesOnly) {
    Color us = pos.side_to_move();
    Color them = ~us;
    int dir = (us == WHITE) ? 8 : -8;
    U64 rank2 = (us == WHITE) ? Rank2_BB : Rank7_BB;  // double push rank
    U64 rank7 = (us == WHITE) ? Rank7_BB : Rank2_BB;  // promotion rank

    U64 pawns = pos.pieces(us, PAWN);
    U64 empty = ~pos.pieces();
    U64 enemies = pos.pieces(them);

    U64 promoters    = pawns & rank7;
    U64 nonPromoters = pawns & ~rank7;

    // === Captures ===
    // Left captures
    U64 leftCaptures = (us == WHITE)
        ? (nonPromoters << 7) & ~FileH_BB & enemies
        : (nonPromoters >> 7) & ~FileA_BB & enemies;
    while (leftCaptures) {
        Square to = pop_lsb(leftCaptures);
        Square from = Square(to - dir + 1);
        if (us == BLACK) from = Square(to - dir - 1);
        list.add(make_move(from, to));
    }

    // Right captures
    U64 rightCaptures = (us == WHITE)
        ? (nonPromoters << 9) & ~FileA_BB & enemies
        : (nonPromoters >> 9) & ~FileH_BB & enemies;
    while (rightCaptures) {
        Square to = pop_lsb(rightCaptures);
        Square from = Square(to - dir - 1);
        if (us == BLACK) from = Square(to - dir + 1);
        list.add(make_move(from, to));
    }

    // === Promotion captures ===
    U64 promoLeftCap = (us == WHITE)
        ? (promoters << 7) & ~FileH_BB & enemies
        : (promoters >> 7) & ~FileA_BB & enemies;
    while (promoLeftCap) {
        Square to = pop_lsb(promoLeftCap);
        Square from = Square(to - dir + 1);
        if (us == BLACK) from = Square(to - dir - 1);
        for (PieceType pt = QUEEN; pt >= KNIGHT; pt = PieceType(pt - 1))
            list.add(make_move(from, to, PROMOTION, pt));
    }

    U64 promoRightCap = (us == WHITE)
        ? (promoters << 9) & ~FileA_BB & enemies
        : (promoters >> 9) & ~FileH_BB & enemies;
    while (promoRightCap) {
        Square to = pop_lsb(promoRightCap);
        Square from = Square(to - dir - 1);
        if (us == BLACK) from = Square(to - dir + 1);
        for (PieceType pt = QUEEN; pt >= KNIGHT; pt = PieceType(pt - 1))
            list.add(make_move(from, to, PROMOTION, pt));
    }

    // === En passant ===
    if (pos.ep_square() != SQ_NONE) {
        U64 epPawns = nonPromoters & PawnAttacks[them][pos.ep_square()];
        while (epPawns) {
            Square from = pop_lsb(epPawns);
            list.add(make_move(from, pos.ep_square(), EN_PASSANT));
        }
    }

    if (capturesOnly) {
        // Still generate promotion pushes (they change material)
        U64 promoPush = (us == WHITE)
            ? (promoters << 8) & empty
            : (promoters >> 8) & empty;
        while (promoPush) {
            Square to = pop_lsb(promoPush);
            Square from = Square(to - dir);
            for (PieceType pt = QUEEN; pt >= KNIGHT; pt = PieceType(pt - 1))
                list.add(make_move(from, to, PROMOTION, pt));
        }
        return;
    }

    // === Quiet moves ===
    // Single push
    U64 singlePush = (us == WHITE)
        ? (nonPromoters << 8) & empty
        : (nonPromoters >> 8) & empty;
    U64 singlePushCopy = singlePush;
    while (singlePushCopy) {
        Square to = pop_lsb(singlePushCopy);
        list.add(make_move(Square(to - dir), to));
    }

    // Double push
    U64 doublePush = (us == WHITE)
        ? ((singlePush & Rank3_BB) << 8) & empty
        : ((singlePush & Rank6_BB) >> 8) & empty;
    while (doublePush) {
        Square to = pop_lsb(doublePush);
        list.add(make_move(Square(to - 2 * dir), to));
    }

    // Promotion push (quiet)
    U64 promoPush = (us == WHITE)
        ? (promoters << 8) & empty
        : (promoters >> 8) & empty;
    while (promoPush) {
        Square to = pop_lsb(promoPush);
        Square from = Square(to - dir);
        for (PieceType pt = QUEEN; pt >= KNIGHT; pt = PieceType(pt - 1))
            list.add(make_move(from, to, PROMOTION, pt));
    }
}

// === Piece move generation ===

static void generate_piece_moves(const Position& pos, MoveList& list, PieceType pt, bool capturesOnly) {
    Color us = pos.side_to_move();
    U64 targets = capturesOnly ? pos.pieces(~us) : ~pos.pieces(us);
    U64 occupied = pos.pieces();

    U64 pieces = pos.pieces(us, pt);
    while (pieces) {
        Square from = pop_lsb(pieces);
        U64 attacks;

        switch (pt) {
            case KNIGHT: attacks = KnightAttacks[from]; break;
            case BISHOP: attacks = bishop_attacks(from, occupied); break;
            case ROOK:   attacks = rook_attacks(from, occupied); break;
            case QUEEN:  attacks = queen_attacks(from, occupied); break;
            case KING:   attacks = KingAttacks[from]; break;
            default: continue;
        }

        attacks &= targets;
        while (attacks) {
            Square to = pop_lsb(attacks);
            list.add(make_move(from, to));
        }
    }
}

// === Castling ===

static void generate_castling(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    U64 occupied = pos.pieces();

    if (us == WHITE) {
        // Kingside O-O
        if ((pos.castling_rights() & WHITE_OO)
            && !(occupied & (square_bb(SQ_F1) | square_bb(SQ_G1)))) {
            list.add(make_move(SQ_E1, SQ_G1, CASTLING));
        }
        // Queenside O-O-O
        if ((pos.castling_rights() & WHITE_OOO)
            && !(occupied & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1)))) {
            list.add(make_move(SQ_E1, SQ_C1, CASTLING));
        }
    } else {
        if ((pos.castling_rights() & BLACK_OO)
            && !(occupied & (square_bb(SQ_F8) | square_bb(SQ_G8)))) {
            list.add(make_move(SQ_E8, SQ_G8, CASTLING));
        }
        if ((pos.castling_rights() & BLACK_OOO)
            && !(occupied & (square_bb(SQ_B8) | square_bb(SQ_C8) | square_bb(SQ_D8)))) {
            list.add(make_move(SQ_E8, SQ_C8, CASTLING));
        }
    }
}

// === Public interface ===

void generate_moves(const Position& pos, MoveList& list) {
    list.count = 0;
    generate_pawn_moves(pos, list, false);
    generate_piece_moves(pos, list, KNIGHT, false);
    generate_piece_moves(pos, list, BISHOP, false);
    generate_piece_moves(pos, list, ROOK, false);
    generate_piece_moves(pos, list, QUEEN, false);
    generate_piece_moves(pos, list, KING, false);
    if (!pos.in_check())
        generate_castling(pos, list);
}

void generate_captures(const Position& pos, MoveList& list) {
    list.count = 0;
    generate_pawn_moves(pos, list, true);
    generate_piece_moves(pos, list, KNIGHT, true);
    generate_piece_moves(pos, list, BISHOP, true);
    generate_piece_moves(pos, list, ROOK, true);
    generate_piece_moves(pos, list, QUEEN, true);
    generate_piece_moves(pos, list, KING, true);
}
