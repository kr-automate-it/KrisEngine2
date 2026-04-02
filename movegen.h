#pragma once
#include "position.h"

// Generate all pseudo-legal moves
void generate_moves(const Position& pos, MoveList& list);

// Generate only capture moves (for quiescence search)
void generate_captures(const Position& pos, MoveList& list);
