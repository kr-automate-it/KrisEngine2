#pragma once
#include "bitboard.h"
#include <string>

// TODO: StateInfo, Position class

struct StateInfo {
    // TODO
    StateInfo* previous;
};

class Position {
public:
    void set(const std::string& fen);
    std::string fen() const;
    // TODO
};
