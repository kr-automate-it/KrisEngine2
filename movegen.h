#pragma once
#include "position.h"

// TODO: move generation
void generate_moves(const Position& pos, MoveList& list);
void generate_captures(const Position& pos, MoveList& list);
