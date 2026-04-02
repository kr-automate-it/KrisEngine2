#pragma once
#include "position.h"

// Stockfish-like evaluation
// Main evaluation = tapered(mg, eg * scaleFactor/64) + tempo, scaled by rule50

namespace Eval {

// Granularity — eval rounded to multiples of 16 (Stockfish convention)
constexpr int GRAIN = 16;

int evaluate(const Position& pos);

} // namespace Eval
