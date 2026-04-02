#pragma once
#include "position.h"
#include <chrono>
#include <atomic>

struct SearchInfo {
    int maxDepth = 64;
    int depth = 0;
    std::atomic<int64_t> nodes{0};
    std::atomic<bool> stopped{false};
    int64_t timeLimit = 0;
    std::chrono::steady_clock::time_point startTime;
};

struct SearchResult {
    Move bestMove = MOVE_NONE;
    Move ponderMove = MOVE_NONE;
    int score = -VALUE_INFINITE;
};

SearchResult search(Position& pos, SearchInfo& info);
void clear_search_tables();
