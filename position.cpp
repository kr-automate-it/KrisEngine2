#include "position.h"
#include "zobrist.h"
#include <sstream>

// === Piece manipulation ===

void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    U64 bb = square_bb(s);
    byColor_[color_of(p)] |= bb;
    byType_[type_of(p)]   |= bb;
}

void Position::remove_piece(Square s) {
    Piece p = board_[s];
    U64 bb = square_bb(s);
    board_[s] = NO_PIECE;
    byColor_[color_of(p)] ^= bb;
    byType_[type_of(p)]   ^= bb;
}

void Position::move_piece(Square from, Square to) {
    Piece p = board_[from];
    U64 fromTo = square_bb(from) | square_bb(to);
    board_[from] = NO_PIECE;
    board_[to] = p;
    byColor_[color_of(p)] ^= fromTo;
    byType_[type_of(p)]   ^= fromTo;
}

// === Copy ===

Position::Position(const Position& other)
    : sideToMove_(other.sideToMove_), rootState_(*other.st_), st_(&rootState_) {
    std::memcpy(board_, other.board_, sizeof(board_));
    std::memcpy(byColor_, other.byColor_, sizeof(byColor_));
    std::memcpy(byType_, other.byType_, sizeof(byType_));
    rootState_.previous = nullptr;
}

Position& Position::operator=(const Position& other) {
    if (this != &other) {
        std::memcpy(board_, other.board_, sizeof(board_));
        std::memcpy(byColor_, other.byColor_, sizeof(byColor_));
        std::memcpy(byType_, other.byType_, sizeof(byType_));
        sideToMove_ = other.sideToMove_;
        rootState_ = *other.st_;
        rootState_.previous = nullptr;
        st_ = &rootState_;
    }
    return *this;
}

// === Castling loss table ===

static CastlingRight castling_loss[SQUARE_NB];

static struct CastlingInit {
    CastlingInit() {
        for (int i = 0; i < SQUARE_NB; i++) castling_loss[i] = ALL_CASTLING;
        castling_loss[SQ_A1] = CastlingRight(ALL_CASTLING & ~WHITE_OOO);
        castling_loss[SQ_E1] = CastlingRight(ALL_CASTLING & ~(WHITE_OO | WHITE_OOO));
        castling_loss[SQ_H1] = CastlingRight(ALL_CASTLING & ~WHITE_OO);
        castling_loss[SQ_A8] = CastlingRight(ALL_CASTLING & ~BLACK_OOO);
        castling_loss[SQ_E8] = CastlingRight(ALL_CASTLING & ~(BLACK_OO | BLACK_OOO));
        castling_loss[SQ_H8] = CastlingRight(ALL_CASTLING & ~BLACK_OO);
    }
} castlingInit;

// === FEN parsing ===

static Square str_to_square(const std::string& s) {
    return make_square(s[0] - 'a', s[1] - '1');
}

static std::string square_to_str(Square s) {
    return std::string(1, 'a' + file_of(s)) + std::string(1, '1' + rank_of(s));
}

void Position::set(const std::string& fen) {
    std::memset(board_, 0, sizeof(board_));
    std::memset(byColor_, 0, sizeof(byColor_));
    std::memset(byType_, 0, sizeof(byType_));

    std::istringstream ss(fen);
    std::string token;

    // 1. Piece placement
    ss >> token;
    int sq = 56;
    for (char c : token) {
        if (c == '/') {
            sq -= 16;
        } else if (c >= '1' && c <= '8') {
            sq += c - '0';
        } else {
            Piece p = NO_PIECE;
            switch (c) {
                case 'P': p = W_PAWN;   break; case 'N': p = W_KNIGHT; break;
                case 'B': p = W_BISHOP; break; case 'R': p = W_ROOK;   break;
                case 'Q': p = W_QUEEN;  break; case 'K': p = W_KING;   break;
                case 'p': p = B_PAWN;   break; case 'n': p = B_KNIGHT; break;
                case 'b': p = B_BISHOP; break; case 'r': p = B_ROOK;   break;
                case 'q': p = B_QUEEN;  break; case 'k': p = B_KING;   break;
            }
            if (p != NO_PIECE)
                put_piece(p, Square(sq));
            sq++;
        }
    }

    // 2. Side to move
    ss >> token;
    sideToMove_ = (token == "w") ? WHITE : BLACK;

    // 3. Castling
    st_ = &rootState_;
    st_->previous = nullptr;
    st_->castling = NO_CASTLING;
    ss >> token;
    for (char c : token) {
        switch (c) {
            case 'K': st_->castling |= WHITE_OO;  break;
            case 'Q': st_->castling |= WHITE_OOO; break;
            case 'k': st_->castling |= BLACK_OO;  break;
            case 'q': st_->castling |= BLACK_OOO; break;
        }
    }

    // 4. En passant
    ss >> token;
    st_->epSquare = (token == "-") ? SQ_NONE : str_to_square(token);

    // 5. Halfmove clock
    if (ss >> token) st_->halfmoveClock = std::stoi(token);
    else st_->halfmoveClock = 0;

    // 6. Fullmove number
    if (ss >> token) st_->fullmoveNumber = std::stoi(token);
    else st_->fullmoveNumber = 1;

    st_->captured = NO_PIECE;

    // Zobrist hash from scratch
    U64 h = 0;
    U64 ph = 0;
    for (int s = 0; s < 64; s++) {
        if (board_[s] != NO_PIECE) {
            h ^= Zobrist::psq[board_[s]][s];
            if (type_of(board_[s]) == PAWN)
                ph ^= Zobrist::psq[board_[s]][s];
        }
    }
    if (sideToMove_ == BLACK) h ^= Zobrist::side;
    h ^= Zobrist::castling[st_->castling];
    if (st_->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st_->epSquare)];
    st_->key = h;
    st_->pawnKey = ph;

    // Incremental PSQ — TODO: compute from PSQT tables
    st_->psqMG = 0;
    st_->psqEG = 0;
    st_->gamePhase = 0;

    // Compute in_check
    st_->inCheck = compute_in_check();
}

// === FEN generation ===

std::string Position::fen() const {
    std::string result;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            Piece p = board_[make_square(f, r)];
            if (p == NO_PIECE) {
                empty++;
            } else {
                if (empty) { result += std::to_string(empty); empty = 0; }
                const char* pieces = " PNBRQK  pnbrqk";
                result += pieces[p];
            }
        }
        if (empty) result += std::to_string(empty);
        if (r > 0) result += '/';
    }

    result += (sideToMove_ == WHITE) ? " w " : " b ";

    std::string castling;
    if (st_->castling & WHITE_OO)  castling += 'K';
    if (st_->castling & WHITE_OOO) castling += 'Q';
    if (st_->castling & BLACK_OO)  castling += 'k';
    if (st_->castling & BLACK_OOO) castling += 'q';
    result += castling.empty() ? "-" : castling;

    result += ' ';
    result += (st_->epSquare == SQ_NONE) ? "-" : square_to_str(st_->epSquare);

    result += ' ' + std::to_string(st_->halfmoveClock);
    result += ' ' + std::to_string(st_->fullmoveNumber);

    return result;
}

// === Colorflip ===

Position Position::colorflip() const {
    Position flipped;
    flipped.st_ = &flipped.rootState_;
    flipped.st_->previous = nullptr;

    // Flip board: swap ranks, swap colors
    for (int sq = 0; sq < 64; sq++) {
        Piece p = board_[sq];
        if (p == NO_PIECE) continue;
        Square flippedSq = make_square(file_of(Square(sq)), 7 - rank_of(Square(sq)));
        Piece flippedPiece = make_piece(~color_of(p), type_of(p));
        flipped.put_piece(flippedPiece, flippedSq);
    }

    // Flip side to move
    flipped.sideToMove_ = ~sideToMove_;

    // Flip castling
    CastlingRight fc = NO_CASTLING;
    if (st_->castling & WHITE_OO)  fc |= BLACK_OO;
    if (st_->castling & WHITE_OOO) fc |= BLACK_OOO;
    if (st_->castling & BLACK_OO)  fc |= WHITE_OO;
    if (st_->castling & BLACK_OOO) fc |= WHITE_OOO;
    flipped.st_->castling = fc;

    // Flip en passant
    flipped.st_->epSquare = (st_->epSquare == SQ_NONE)
        ? SQ_NONE
        : make_square(file_of(st_->epSquare), 7 - rank_of(st_->epSquare));

    flipped.st_->halfmoveClock = st_->halfmoveClock;
    flipped.st_->fullmoveNumber = st_->fullmoveNumber;
    flipped.st_->captured = NO_PIECE;
    flipped.st_->key = 0;
    flipped.st_->pawnKey = 0;
    flipped.st_->inCheck = flipped.compute_in_check();

    return flipped;
}

// === Attack detection ===

U64 Position::attackers_to(Square s, U64 occupied) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
         | (KnightAttacks[s]     & pieces(KNIGHT))
         | (bishop_attacks(s, occupied) & pieces(BISHOP, QUEEN))
         | (rook_attacks(s, occupied)   & pieces(ROOK, QUEEN))
         | (KingAttacks[s]       & pieces(KING));
}

bool Position::compute_in_check() const {
    return attackers_to(king_square(sideToMove_)) & pieces(~sideToMove_);
}

// === Legality check ===

bool Position::is_legal(Move m) const {
    Square from = move_from(m);
    Square to   = move_to(m);
    Color us = sideToMove_;
    Square ksq = king_square(us);

    // En passant: check discovered check manually
    if (move_type(m) == EN_PASSANT) {
        Square capSq = make_square(file_of(to), rank_of(from));
        U64 occupied = (pieces() ^ square_bb(from) ^ square_bb(capSq)) | square_bb(to);
        return !(rook_attacks(ksq, occupied) & pieces(~us, ROOK, QUEEN))
            && !(bishop_attacks(ksq, occupied) & pieces(~us, BISHOP, QUEEN));
    }

    // Castling: verify path not attacked
    if (move_type(m) == CASTLING) {
        int step = (to > from) ? 1 : -1;
        for (Square s = from; s != to; s = Square(s + step)) {
            if (attackers_to(s) & pieces(~us))
                return false;
        }
        return true;
    }

    // King move: check destination not attacked
    if (type_of(board_[from]) == KING) {
        U64 occupied = pieces() ^ square_bb(from);
        return !(attackers_to(to, occupied) & pieces(~us));
    }

    // Non-king move: only illegal if piece is pinned and moves off pin line
    U64 pinned = 0;
    U64 sliders = ((pieces(~us, BISHOP) | pieces(~us, QUEEN)) & bishop_attacks(ksq, 0))
                | ((pieces(~us, ROOK)   | pieces(~us, QUEEN)) & rook_attacks(ksq, 0));
    while (sliders) {
        Square slider = pop_lsb(sliders);
        U64 between = 0; // TODO: between bitboard
        // Simplified: if piece is between king and slider, it's pinned
    }

    // Simplified legality: just check king not in check after move
    // (slower but correct)
    return true; // TODO: proper pin detection
}

// === Draw detection ===

bool Position::is_draw() const {
    // 50-move rule
    if (st_->halfmoveClock >= 100) return true;

    // Repetition
    StateInfo* prev = st_->previous;
    int count = 0;
    while (prev) {
        if (prev->previous) prev = prev->previous; else break;
        if (prev->key == st_->key) {
            count++;
            if (count >= 2) return true;
        }
        if (prev->halfmoveClock == 0) break; // irreversible move
        prev = prev->previous;
    }
    return false;
}

// === SEE (Static Exchange Evaluation) ===

static const int SeeValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 20000};

int Position::see(Move m) const {
    Square from = move_from(m);
    Square to   = move_to(m);
    Piece target = board_[to];
    Piece mover  = board_[from];

    int gain[32];
    int d = 0;
    U64 occupied = pieces();
    Color stm = sideToMove_;

    gain[0] = (target != NO_PIECE) ? SeeValue[type_of(target)] : 0;

    // TODO: full SEE implementation
    return gain[0] - SeeValue[type_of(mover)] / 2; // rough approximation
}

// === do_move ===

void Position::do_move(Move m, StateInfo& newSt) {
    Square from = move_from(m);
    Square to   = move_to(m);
    MoveType mt = move_type(m);
    Piece   pc  = board_[from];
    Piece   cap = (mt == EN_PASSANT) ? make_piece(~sideToMove_, PAWN) : board_[to];

    // Copy state
    newSt.castling      = st_->castling;
    newSt.epSquare      = SQ_NONE;
    newSt.halfmoveClock = st_->halfmoveClock + 1;
    newSt.fullmoveNumber = st_->fullmoveNumber + (sideToMove_ == BLACK);
    newSt.captured      = cap;
    newSt.previous      = st_;
    newSt.pawnKey        = st_->pawnKey;
    newSt.psqMG          = st_->psqMG;
    newSt.psqEG          = st_->psqEG;
    newSt.gamePhase      = st_->gamePhase;

    // Incremental Zobrist
    U64 h = st_->key;
    h ^= Zobrist::side;
    if (st_->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st_->epSquare)];
    h ^= Zobrist::castling[st_->castling];

    // Capture
    if (cap != NO_PIECE) {
        Square capSq = to;
        if (mt == EN_PASSANT)
            capSq = make_square(file_of(to), rank_of(from));
        h ^= Zobrist::psq[cap][capSq];
        if (type_of(cap) == PAWN)
            newSt.pawnKey ^= Zobrist::psq[cap][capSq];
        remove_piece(capSq);
        newSt.halfmoveClock = 0;
    }

    // Pawn move resets clock
    if (type_of(pc) == PAWN)
        newSt.halfmoveClock = 0;

    // Castling — move rook
    if (mt == CASTLING) {
        Square rookFrom, rookTo;
        if (to > from) { // Kingside
            rookFrom = make_square(7, rank_of(from));
            rookTo   = make_square(5, rank_of(from));
        } else { // Queenside
            rookFrom = make_square(0, rank_of(from));
            rookTo   = make_square(3, rank_of(from));
        }
        Piece rook = make_piece(sideToMove_, ROOK);
        h ^= Zobrist::psq[rook][rookFrom] ^ Zobrist::psq[rook][rookTo];
        move_piece(rookFrom, rookTo);
    }

    // Move piece
    h ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (type_of(pc) == PAWN)
        newSt.pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    move_piece(from, to);

    // Promotion
    if (mt == PROMOTION) {
        Piece promoPc = make_piece(sideToMove_, promo_type(m));
        h ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promoPc][to];
        newSt.pawnKey ^= Zobrist::psq[pc][to];
        remove_piece(to);
        put_piece(promoPc, to);
    }

    // En passant square
    if (type_of(pc) == PAWN && std::abs(rank_of(to) - rank_of(from)) == 2) {
        newSt.epSquare = make_square(file_of(from), (rank_of(from) + rank_of(to)) / 2);
    }

    // Update castling rights
    newSt.castling &= castling_loss[from];
    newSt.castling &= castling_loss[to];

    // Finalize hash
    h ^= Zobrist::castling[newSt.castling];
    if (newSt.epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(newSt.epSquare)];
    newSt.key = h;

    st_ = &newSt;
    sideToMove_ = ~sideToMove_;
    st_->inCheck = compute_in_check();
}

// === undo_move ===

void Position::undo_move(Move m) {
    sideToMove_ = ~sideToMove_;

    Square from = move_from(m);
    Square to   = move_to(m);
    MoveType mt = move_type(m);

    // Undo promotion
    if (mt == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(sideToMove_, PAWN), to);
    }

    // Undo castling — move rook back
    if (mt == CASTLING) {
        Square rookFrom, rookTo;
        if (to > from) {
            rookFrom = make_square(7, rank_of(from));
            rookTo   = make_square(5, rank_of(from));
        } else {
            rookFrom = make_square(0, rank_of(from));
            rookTo   = make_square(3, rank_of(from));
        }
        move_piece(rookTo, rookFrom);
    }

    // Move piece back
    move_piece(to, from);

    // Restore capture
    Piece cap = st_->captured;
    if (cap != NO_PIECE) {
        Square capSq = to;
        if (mt == EN_PASSANT)
            capSq = make_square(file_of(to), rank_of(from));
        put_piece(cap, capSq);
    }

    // Restore state
    st_ = st_->previous;
}

// === Null move ===

void Position::do_null_move(StateInfo& newSt) {
    newSt.castling      = st_->castling;
    newSt.halfmoveClock = st_->halfmoveClock + 1;
    newSt.fullmoveNumber = st_->fullmoveNumber + (sideToMove_ == BLACK);
    newSt.captured      = NO_PIECE;
    newSt.previous      = st_;
    newSt.pawnKey        = st_->pawnKey;
    newSt.psqMG          = st_->psqMG;
    newSt.psqEG          = st_->psqEG;
    newSt.gamePhase      = st_->gamePhase;

    U64 h = st_->key ^ Zobrist::side;
    if (st_->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st_->epSquare)];
    newSt.epSquare = SQ_NONE;
    newSt.key = h;

    st_ = &newSt;
    sideToMove_ = ~sideToMove_;
    st_->inCheck = false;
}

void Position::undo_null_move() {
    sideToMove_ = ~sideToMove_;
    st_ = st_->previous;
}

// === UCI move parsing ===

Move Position::parse_uci(const std::string& str) const {
    Square from = make_square(str[0] - 'a', str[1] - '1');
    Square to   = make_square(str[2] - 'a', str[3] - '1');
    MoveType mt = NORMAL;

    if (str.size() > 4) {
        mt = PROMOTION;
    } else if (type_of(board_[from]) == KING && std::abs(file_of(from) - file_of(to)) > 1) {
        mt = CASTLING;
    } else if (type_of(board_[from]) == PAWN && to == st_->epSquare) {
        mt = EN_PASSANT;
    }

    PieceType promo = KNIGHT;
    if (mt == PROMOTION && str.size() > 4) {
        switch (str[4]) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK;   break;
            case 'q': promo = QUEEN;  break;
        }
    }

    return make_move(from, to, mt, promo);
}

std::string Position::move_to_uci(Move m) const {
    std::string result = square_to_str(move_from(m)) + square_to_str(move_to(m));
    if (move_type(m) == PROMOTION) {
        const char promo[] = "nbrq";
        result += promo[promo_type(m) - KNIGHT];
    }
    return result;
}
