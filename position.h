#pragma once
#include "bitboard.h"
#include <string>
#include <cstring>

struct StateInfo {
    // Copied incrementally in do_move
    U64           key;
    U64           pawnKey;
    CastlingRight castling;
    Square        epSquare;
    int           halfmoveClock;
    int           fullmoveNumber;
    Piece         captured;

    // Incremental eval
    int           psqMG;
    int           psqEG;
    int           gamePhase;
    bool          inCheck;

    StateInfo*    previous;
};

class Position {
public:
    // Construction
    Position() { std::memset(this, 0, sizeof(*this)); }
    Position(const Position& other);
    Position& operator=(const Position& other);

    // Setup
    void set(const std::string& fen);
    std::string fen() const;

    // Board access
    Piece piece_on(Square s) const { return board_[s]; }
    Color side_to_move() const { return sideToMove_; }
    U64 pieces() const { return byColor_[WHITE] | byColor_[BLACK]; }
    U64 pieces(Color c) const { return byColor_[c]; }
    U64 pieces(PieceType pt) const { return byType_[pt]; }
    U64 pieces(Color c, PieceType pt) const { return byColor_[c] & byType_[pt]; }
    U64 pieces(PieceType a, PieceType b) const { return byType_[a] | byType_[b]; }
    U64 pieces(Color c, PieceType a, PieceType b) const { return byColor_[c] & (byType_[a] | byType_[b]); }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    // State access
    CastlingRight castling_rights() const { return st_->castling; }
    Square ep_square() const { return st_->epSquare; }
    int halfmove_clock() const { return st_->halfmoveClock; }
    U64 key() const { return st_->key; }
    U64 pawn_key() const { return st_->pawnKey; }
    bool in_check() const { return st_->inCheck; }
    int psq_mg() const { return st_->psqMG; }
    int psq_eg() const { return st_->psqEG; }
    int game_phase() const { return st_->gamePhase; }

    // Move execution
    void do_move(Move m, StateInfo& newSt);
    void undo_move(Move m);
    void do_null_move(StateInfo& newSt);
    void undo_null_move();

    // Attack detection
    U64 attackers_to(Square s, U64 occupied) const;
    U64 attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool is_legal(Move m) const;
    bool compute_in_check() const;
    int see(Move m) const;

    // Draw detection
    bool is_draw() const;

    // UCI
    Move parse_uci(const std::string& str) const;
    std::string move_to_uci(Move m) const;

    // Colorflip: returns a new position with colors swapped (for eval)
    Position colorflip() const;

private:
    // Board representation
    Piece board_[SQUARE_NB];
    U64   byColor_[COLOR_NB];
    U64   byType_[PIECE_TYPE_NB];
    Color sideToMove_;

    // State
    StateInfo  rootState_;
    StateInfo* st_;

    // Piece manipulation
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
};
