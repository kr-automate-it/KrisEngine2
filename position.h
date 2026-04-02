#pragma once
#include "bitboard.h"
#include "zobrist.h"
#include <string>
#include <cstring>

struct StateInfo {
    CastlingRight castling;
    Square        epSquare;
    int           halfmoveClock;
    int           fullmoveNumber;
    Piece         captured;
    U64           hash;
    U64           pawnHash;
    int           psqMG;       // incremental: material + PST midgame score (white perspective)
    int           psqEG;       // incremental: material + PST endgame score (white perspective)
    int           gamePhase;   // incremental: game phase counter
    int           staticEval;  // eval zapisany w search (do improving heuristic)
    bool          inCheck;     // cached: czy strona do ruchu jest w szachu
    StateInfo*    previous;
};

class Position {
public:
    Position() = default;

    // Copy: kopiuje cala pozycje bez konwersji do FEN
    Position(const Position& other)
        : sideToMove(other.sideToMove), rootState(*other.st), st(&rootState) {
        std::memcpy(board, other.board, sizeof(board));
        std::memcpy(byColor, other.byColor, sizeof(byColor));
        std::memcpy(byType, other.byType, sizeof(byType));
        rootState.previous = nullptr;
    }

    Position& operator=(const Position& other) {
        if (this != &other) {
            std::memcpy(board, other.board, sizeof(board));
            std::memcpy(byColor, other.byColor, sizeof(byColor));
            std::memcpy(byType, other.byType, sizeof(byType));
            sideToMove = other.sideToMove;
            rootState = *other.st;
            rootState.previous = nullptr;
            st = &rootState;
        }
        return *this;
    }

    void set(const std::string& fen);
    std::string fen() const;

    Piece piece_on(Square s) const { return board[s]; }
    U64 pieces() const { return byColor[WHITE] | byColor[BLACK]; }
    U64 pieces(Color c) const { return byColor[c]; }
    U64 pieces(PieceType pt) const { return byType[pt]; }
    U64 pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    U64 pieces(PieceType pt1, PieceType pt2) const { return byType[pt1] | byType[pt2]; }
    U64 pieces(Color c, PieceType pt1, PieceType pt2) const { return byColor[c] & (byType[pt1] | byType[pt2]); }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    Color side_to_move() const { return sideToMove; }
    CastlingRight castling_rights() const { return st->castling; }
    Square ep_square() const { return st->epSquare; }
    int halfmove_clock() const { return st->halfmoveClock; }
    U64 key() const { return st->hash; }
    U64 pawn_key() const { return st->pawnHash; }
    int psq_mg() const { return st->psqMG; }
    int psq_eg() const { return st->psqEG; }
    int game_phase() const { return st->gamePhase; }

    void do_move(Move m, StateInfo& newSt);
    void undo_move(Move m);

    // Null move — oddanie ruchu przeciwnikowi (do null move pruning)
    void do_null_move(StateInfo& newSt);
    void undo_null_move();

    // Detekcja remisu przez powtorzenie lub regule 50 ruchow
    bool is_draw() const;

    U64 attackers_to(Square s, U64 occupied) const;
    U64 attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool in_check() const { return st->inCheck; }
    bool compute_in_check() const;
    bool is_legal(Move m) const;

    // SEE: wynik wymiany na polu docelowym (>0 = korzystna, <0 = niekorzystna)
    int see(Move m) const;

    Move parse_uci(const std::string& str) const;
    std::string move_to_uci(Move m) const;

private:
    Piece board[SQUARE_NB];
    U64   byColor[COLOR_NB];
    U64   byType[PIECE_TYPE_NB];
    Color sideToMove;

    StateInfo  rootState;
    StateInfo* st;

    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
};
