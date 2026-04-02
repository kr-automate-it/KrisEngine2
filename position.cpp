#include "position.h"
#include "zobrist.h"
#include <sstream>
#include <cstring>

// Stub PSQ tables — eval handles this separately
static const int PSQ_MG_stub[16][64] = {};
static const int PSQ_EG_stub[16][64] = {};
static const int (*PSQ_MG)[64] = PSQ_MG_stub;
static const int (*PSQ_EG)[64] = PSQ_EG_stub;
static const int PhaseValue[8] = {0, 0, 1, 1, 2, 4, 0, 0};

static Square str_to_square(const std::string& s) {
    return make_square(s[0] - 'a', s[1] - '1');
}

static std::string square_to_str(Square s) {
    return std::string(1, 'a' + file_of(s)) + std::string(1, '1' + rank_of(s));
}

void Position::put_piece(Piece p, Square s) {
    board[s] = p;
    U64 bb = square_bb(s);
    byColor[color_of(p)] |= bb;
    byType[type_of(p)]   |= bb;
}

void Position::remove_piece(Square s) {
    Piece p = board[s];
    U64 bb = square_bb(s);
    board[s] = NO_PIECE;
    byColor[color_of(p)] ^= bb;
    byType[type_of(p)]   ^= bb;
}

void Position::move_piece(Square from, Square to) {
    Piece p = board[from];
    U64 fromTo = square_bb(from) | square_bb(to);
    board[from] = NO_PIECE;
    board[to] = p;
    byColor[color_of(p)] ^= fromTo;
    byType[type_of(p)]   ^= fromTo;
}

void Position::set(const std::string& fen) {
    std::memset(board, 0, sizeof(board));
    std::memset(byColor, 0, sizeof(byColor));
    std::memset(byType, 0, sizeof(byType));

    std::istringstream ss(fen);
    std::string token;

    // 1. Pozycja figur
    ss >> token;
    int sq = 56; // zaczynamy od A8
    for (char c : token) {
        if (c == '/') {
            sq -= 16; // cofnij o rzad + 8 ktore dodalismy
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

    // 2. Strona do ruchu
    ss >> token;
    sideToMove = (token == "w") ? WHITE : BLACK;

    // 3. Roszada
    st = &rootState;
    st->previous = nullptr;
    st->castling = NO_CASTLING;
    ss >> token;
    for (char c : token) {
        switch (c) {
            case 'K': st->castling |= WHITE_OO;  break;
            case 'Q': st->castling |= WHITE_OOO; break;
            case 'k': st->castling |= BLACK_OO;  break;
            case 'q': st->castling |= BLACK_OOO; break;
        }
    }

    // 4. En passant
    ss >> token;
    st->epSquare = (token == "-") ? SQ_NONE : str_to_square(token);

    // 5. Zegar polruchow
    if (ss >> token)
        st->halfmoveClock = std::stoi(token);
    else
        st->halfmoveClock = 0;

    // 6. Numer pelnego ruchu
    if (ss >> token)
        st->fullmoveNumber = std::stoi(token);
    else
        st->fullmoveNumber = 1;

    st->captured = NO_PIECE;

    // Oblicz hash Zobrist od zera
    U64 h = 0;
    for (int s = 0; s < 64; s++) {
        if (board[s] != NO_PIECE)
            h ^= Zobrist::psq[board[s]][s];
    }
    if (sideToMove == BLACK)
        h ^= Zobrist::side;
    h ^= Zobrist::castling[st->castling];
    if (st->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st->epSquare)];
    st->hash = h;

    // Pawn hash — tylko pionki
    st->pawnHash = 0;
    for (int s = 0; s < 64; s++) {
        if (board[s] != NO_PIECE && type_of(board[s]) == PAWN)
            st->pawnHash ^= Zobrist::psq[board[s]][s];
    }

    // Incremental PSQ: material + PST od zera
    st->psqMG = 0;
    st->psqEG = 0;
    st->gamePhase = 0;
    for (int s = 0; s < 64; s++) {
        Piece p = board[s];
        if (p == NO_PIECE) continue;
        int sign = (color_of(p) == WHITE) ? 1 : -1;
        st->psqMG += sign * PSQ_MG[p][s];
        st->psqEG += sign * PSQ_EG[p][s];
        st->gamePhase += PhaseValue[type_of(p)];
    }

    // Cache in_check
    st->inCheck = compute_in_check();
}

std::string Position::fen() const {
    std::string result;

    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            Piece p = board[make_square(f, r)];
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

    result += (sideToMove == WHITE) ? " w " : " b ";

    std::string castling;
    if (st->castling & WHITE_OO)  castling += 'K';
    if (st->castling & WHITE_OOO) castling += 'Q';
    if (st->castling & BLACK_OO)  castling += 'k';
    if (st->castling & BLACK_OOO) castling += 'q';
    if (castling.empty()) castling = "-";
    result += castling;

    result += ' ';
    result += (st->epSquare == SQ_NONE) ? "-" : square_to_str(st->epSquare);

    result += ' ' + std::to_string(st->halfmoveClock);
    result += ' ' + std::to_string(st->fullmoveNumber);

    return result;
}

U64 Position::attackers_to(Square s, U64 occupied) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
         | (KnightAttacks[s]     & pieces(KNIGHT))
         | (bishop_attacks(s, occupied) & pieces(BISHOP, QUEEN))
         | (rook_attacks(s, occupied)   & pieces(ROOK, QUEEN))
         | (KingAttacks[s]       & pieces(KING));
}

bool Position::compute_in_check() const {
    return attackers_to(king_square(sideToMove)) & pieces(~sideToMove);
}

// Tablica utraty praw do roszady
static CastlingRight castling_loss[SQUARE_NB];
static bool castling_loss_init = []() {
    for (int i = 0; i < SQUARE_NB; i++)
        castling_loss[i] = ALL_CASTLING;
    castling_loss[SQ_A1] = CastlingRight(ALL_CASTLING & ~WHITE_OOO);
    castling_loss[SQ_E1] = CastlingRight(ALL_CASTLING & ~(WHITE_OO | WHITE_OOO));
    castling_loss[SQ_H1] = CastlingRight(ALL_CASTLING & ~WHITE_OO);
    castling_loss[SQ_A8] = CastlingRight(ALL_CASTLING & ~BLACK_OOO);
    castling_loss[SQ_E8] = CastlingRight(ALL_CASTLING & ~(BLACK_OO | BLACK_OOO));
    castling_loss[SQ_H8] = CastlingRight(ALL_CASTLING & ~BLACK_OO);
    return true;
}();

void Position::do_move(Move m, StateInfo& newSt) {
    Square from = move_from(m);
    Square to   = move_to(m);
    MoveType mt = move_type(m);
    Piece   pc  = board[from];
    Piece   cap = (mt == EN_PASSANT) ? make_piece(~sideToMove, PAWN) : board[to];

    // Kopiuj stan
    newSt.castling      = st->castling;
    newSt.epSquare      = SQ_NONE;
    newSt.halfmoveClock = st->halfmoveClock + 1;
    newSt.fullmoveNumber = st->fullmoveNumber + (sideToMove == BLACK);
    newSt.captured      = cap;

    newSt.previous = st;
    newSt.pawnHash = st->pawnHash;
    newSt.psqMG    = st->psqMG;
    newSt.psqEG    = st->psqEG;
    newSt.gamePhase = st->gamePhase;

    // Inkrementalny Zobrist hash
    U64 h = st->hash;
    h ^= Zobrist::side; // zmiana strony
    // Usun stary en passant i roszade z hasha
    if (st->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st->epSquare)];
    h ^= Zobrist::castling[st->castling];

    // Bicie
    if (cap != NO_PIECE) {
        Square capSq = to;
        if (mt == EN_PASSANT)
            capSq = make_square(file_of(to), rank_of(from));
        h ^= Zobrist::psq[cap][capSq];
        if (type_of(cap) == PAWN)
            newSt.pawnHash ^= Zobrist::psq[cap][capSq];
        // Incremental PSQ: usun bita figure
        int capSign = (color_of(cap) == WHITE) ? 1 : -1;
        newSt.psqMG -= capSign * PSQ_MG[cap][capSq];
        newSt.psqEG -= capSign * PSQ_EG[cap][capSq];
        newSt.gamePhase -= PhaseValue[type_of(cap)];
        remove_piece(capSq);
        newSt.halfmoveClock = 0;
    }

    // Ruch pionka resetuje zegar
    if (type_of(pc) == PAWN)
        newSt.halfmoveClock = 0;

    // Roszada
    if (mt == CASTLING) {
        Square rookFrom, rookTo;
        if (to > from) { // Krotka
            rookFrom = make_square(7, rank_of(from));
            rookTo   = make_square(5, rank_of(from));
        } else { // Dluga
            rookFrom = make_square(0, rank_of(from));
            rookTo   = make_square(3, rank_of(from));
        }
        Piece rook = make_piece(sideToMove, ROOK);
        h ^= Zobrist::psq[rook][rookFrom] ^ Zobrist::psq[rook][rookTo];
        // Incremental PSQ: wieza zmienia pole
        int rookSign = (sideToMove == WHITE) ? 1 : -1;
        newSt.psqMG += rookSign * (PSQ_MG[rook][rookTo] - PSQ_MG[rook][rookFrom]);
        newSt.psqEG += rookSign * (PSQ_EG[rook][rookTo] - PSQ_EG[rook][rookFrom]);
        move_piece(rookFrom, rookTo);
    }

    // Przesun figure (hash: usun ze starego pola, dodaj na nowe)
    h ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (type_of(pc) == PAWN)
        newSt.pawnHash ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    // Incremental PSQ: figura zmienia pole
    int pcSign = (sideToMove == WHITE) ? 1 : -1;
    newSt.psqMG += pcSign * (PSQ_MG[pc][to] - PSQ_MG[pc][from]);
    newSt.psqEG += pcSign * (PSQ_EG[pc][to] - PSQ_EG[pc][from]);
    move_piece(from, to);

    // Promocja
    if (mt == PROMOTION) {
        Piece promoPc = make_piece(sideToMove, promo_type(m));
        h ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promoPc][to];
        newSt.pawnHash ^= Zobrist::psq[pc][to]; // promocja: pionek znika z pawn hasha
        // Incremental PSQ: pionek -> figura promowana
        newSt.psqMG += pcSign * (PSQ_MG[promoPc][to] - PSQ_MG[pc][to]);
        newSt.psqEG += pcSign * (PSQ_EG[promoPc][to] - PSQ_EG[pc][to]);
        newSt.gamePhase += PhaseValue[type_of(promoPc)]; // pionek ma phase=0, wiec dodajemy nowa
        remove_piece(to);
        put_piece(promoPc, to);
    }

    // En passant square
    if (type_of(pc) == PAWN && std::abs(rank_of(to) - rank_of(from)) == 2) {
        newSt.epSquare = make_square(file_of(from), (rank_of(from) + rank_of(to)) / 2);
    }

    // Aktualizuj prawa roszady
    newSt.castling &= castling_loss[from];
    newSt.castling &= castling_loss[to];

    // Dodaj nowy castling i en passant do hasha
    h ^= Zobrist::castling[newSt.castling];
    if (newSt.epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(newSt.epSquare)];
    newSt.hash = h;

    st = &newSt;
    sideToMove = ~sideToMove;
    st->inCheck = compute_in_check();
}

void Position::undo_move(Move m) {
    sideToMove = ~sideToMove;

    Square from = move_from(m);
    Square to   = move_to(m);
    MoveType mt = move_type(m);

    // Cofnij promocje
    if (mt == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(sideToMove, PAWN), to);
    }

    move_piece(to, from);

    // Cofnij roszade
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

    // Przywroc zbita figure
    if (st->captured != NO_PIECE) {
        Square capSq = to;
        if (mt == EN_PASSANT)
            capSq = make_square(file_of(to), rank_of(from));
        put_piece(st->captured, capSq);
    }

    st = st->previous;
}

bool Position::is_legal(Move m) const {
    Square from = move_from(m);
    Square to   = move_to(m);
    Square ksq  = king_square(sideToMove);
    U64    occ  = pieces();

    if (move_type(m) == EN_PASSANT) {
        Square capSq = make_square(file_of(to), rank_of(from));
        U64 after = (occ ^ square_bb(from) ^ square_bb(capSq)) | square_bb(to);
        U64 enemies_after = pieces(~sideToMove) ^ square_bb(capSq);
        return !(rook_attacks(ksq, after)   & enemies_after & pieces(ROOK, QUEEN))
            && !(bishop_attacks(ksq, after) & enemies_after & pieces(BISHOP, QUEEN))
            && !(KnightAttacks[ksq]         & enemies_after & pieces(KNIGHT))
            && !(PawnAttacks[sideToMove][ksq] & enemies_after & pieces(PAWN));
    }

    if (move_type(m) == CASTLING) {
        // Sprawdz czy krol nie przechodzi przez szachowane pola
        int step = (to > from) ? 1 : -1;
        for (Square s = from; s != to; s = Square(s + step)) {
            if (attackers_to(s, occ) & pieces(~sideToMove))
                return false;
        }
        // Sprawdz pole docelowe krola
        if (attackers_to(to, occ) & pieces(~sideToMove))
            return false;
        return true;
    }

    if (type_of(board[from]) == KING)
        return !(attackers_to(to, occ ^ square_bb(from)) & pieces(~sideToMove));

    // Sprawdz czy krol nie jest atakowany po ruchu
    // Dla sliding pieces: przelicz z nowym occupied
    U64 after = (occ ^ square_bb(from)) | square_bb(to);
    // Sliding attacks
    if (rook_attacks(ksq, after) & pieces(~sideToMove, ROOK, QUEEN) & ~square_bb(to))
        return false;
    if (bishop_attacks(ksq, after) & pieces(~sideToMove, BISHOP, QUEEN) & ~square_bb(to))
        return false;
    // Non-sliding attacks (skoczek, pionek) - nie zaleza od occupied
    // ale figura na 'to' mogla byc ta ktora dawala szacha
    if (KnightAttacks[ksq] & pieces(~sideToMove, KNIGHT) & ~square_bb(to))
        return false;
    if (PawnAttacks[sideToMove][ksq] & pieces(~sideToMove, PAWN) & ~square_bb(to))
        return false;
    return true;
}

Move Position::parse_uci(const std::string& str) const {
    Square from = str_to_square(str.substr(0, 2));
    Square to   = str_to_square(str.substr(2, 2));

    MoveType mt = NORMAL;
    PieceType promo = KNIGHT;

    if (str.size() > 4) {
        mt = PROMOTION;
        switch (str[4]) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK;   break;
            case 'q': promo = QUEEN;  break;
        }
    }

    // Wykryj roszade
    if (type_of(board[from]) == KING && std::abs(file_of(to) - file_of(from)) > 1) {
        mt = CASTLING;
    }

    // Wykryj en passant
    if (type_of(board[from]) == PAWN && to == ep_square()) {
        mt = EN_PASSANT;
    }

    if (mt == PROMOTION)
        return make_move(from, to, mt, promo);
    if (mt != NORMAL)
        return make_move(from, to, mt);
    return make_move(from, to);
}

std::string Position::move_to_uci(Move m) const {
    std::string result = square_to_str(move_from(m)) + square_to_str(move_to(m));
    if (move_type(m) == PROMOTION) {
        const char promo[] = "nbrq";
        result += promo[promo_type(m) - KNIGHT];
    }
    return result;
}

// Null move — oddaj ruch przeciwnikowi bez wykonywania ruchu
// Uzywane w null move pruning: "jezeli nawet bez ruchu mam duza przewage,
// to ta galaz jest pewnie dobra i moge ja przycinac"
void Position::do_null_move(StateInfo& newSt) {
    newSt.castling      = st->castling;
    newSt.halfmoveClock = st->halfmoveClock + 1;
    newSt.fullmoveNumber = st->fullmoveNumber + (sideToMove == BLACK);
    newSt.captured      = NO_PIECE;
    newSt.previous      = st;

    U64 h = st->hash ^ Zobrist::side;
    if (st->epSquare != SQ_NONE)
        h ^= Zobrist::enpassant[file_of(st->epSquare)];
    newSt.epSquare = SQ_NONE;
    newSt.hash = h;
    newSt.pawnHash = st->pawnHash;
    newSt.psqMG = st->psqMG;
    newSt.psqEG = st->psqEG;
    newSt.gamePhase = st->gamePhase;

    st = &newSt;
    sideToMove = ~sideToMove;
    st->inCheck = false; // null move nigdy nie daje szacha
}

void Position::undo_null_move() {
    sideToMove = ~sideToMove;
    st = st->previous;
}

// === SEE (Static Exchange Evaluation) ===
// Symuluje pelna wymiane figur na jednym polu.
// Przyklad: Nc3xd5, c6xd5, Bf1xd5, Qd8xd5 — kto wygrywa?
// Zwraca wartosc z perspektywy strony wykonujacej pierwszy ruch.
static const int SeeValue[PIECE_TYPE_NB] = { 0, 100, 320, 330, 500, 900, 20000 };

// Znajdz najtansza figure atakujaca 'sq' z koloru 'c'
static Square least_valuable_attacker(const Position& pos, Square sq, U64 occupied, Color c, PieceType& pt) {
    // Pionki
    U64 att = PawnAttacks[~c][sq] & pos.pieces(c, PAWN) & occupied;
    if (att) { pt = PAWN; return lsb(att); }
    // Skoczki
    att = KnightAttacks[sq] & pos.pieces(c, KNIGHT) & occupied;
    if (att) { pt = KNIGHT; return lsb(att); }
    // Gonce
    att = bishop_attacks(sq, occupied) & pos.pieces(c, BISHOP) & occupied;
    if (att) { pt = BISHOP; return lsb(att); }
    // Wieze
    att = rook_attacks(sq, occupied) & pos.pieces(c, ROOK) & occupied;
    if (att) { pt = ROOK; return lsb(att); }
    // Hetman
    att = queen_attacks(sq, occupied) & pos.pieces(c, QUEEN) & occupied;
    if (att) { pt = QUEEN; return lsb(att); }
    // Krol
    att = KingAttacks[sq] & pos.pieces(c, KING) & occupied;
    if (att) { pt = KING; return lsb(att); }
    return SQ_NONE;
}

int Position::see(Move m) const {
    Square from = move_from(m);
    Square to   = move_to(m);

    // Wartosc pierwszego bicia
    int gain[32];
    int d = 0;

    PieceType attacker = type_of(board[from]);
    Color stm = sideToMove; // strona do ruchu

    // Specjalne przypadki
    if (move_type(m) == EN_PASSANT) {
        gain[0] = SeeValue[PAWN];
    } else if (move_type(m) == PROMOTION) {
        gain[0] = SeeValue[type_of(board[to])] + SeeValue[promo_type(m)] - SeeValue[PAWN];
    } else {
        gain[0] = SeeValue[type_of(board[to])];
    }

    U64 occupied = pieces() ^ square_bb(from);

    // En passant: usun zbity pionek z occupied
    if (move_type(m) == EN_PASSANT)
        occupied ^= square_bb(make_square(file_of(to), rank_of(from)));

    stm = ~stm;

    while (true) {
        d++;
        gain[d] = SeeValue[attacker] - gain[d - 1]; // wartość jeśli strona bierze

        // Pruning: jeśli nawet po wzięciu wynik jest negatywny, nie opłaca się kontynuować
        if (std::max(-gain[d - 1], gain[d]) < 0)
            break;

        PieceType nextPt;
        Square attSq = least_valuable_attacker(*this, to, occupied, stm, nextPt);
        if (attSq == SQ_NONE)
            break;

        occupied ^= square_bb(attSq);
        attacker = nextPt;
        stm = ~stm;

        if (d >= 31) break;
    }

    // Propaguj wstecz: strona moze wybrac czy brac czy nie
    while (--d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    }

    return gain[0];
}

// Detekcja remisu: regula 50 ruchow lub 3-krotne powtorzenie pozycji
bool Position::is_draw() const {
    // Regula 50 ruchow
    if (st->halfmoveClock >= 100)
        return true;

    // Powtorzenie: szukamy wstecz po lancuchu StateInfo
    // Pozycja moze sie powtorzyc tylko co 2 polruchy (ta sama strona)
    // i tylko w obrebie halfmoveClock (od ostatniego bicia/ruchu pionka)
    int count = 0;
    StateInfo* s = st;
    int limit = st->halfmoveClock;
    for (int i = 2; i <= limit; i += 2) {
        s = s->previous;
        if (!s) break;
        s = s->previous;
        if (!s) break;
        if (s->hash == st->hash) {
            count++;
            if (count >= 1)
                return true; // 2-fold w search wystarczy (unika petli)
        }
    }
    return false;
}
