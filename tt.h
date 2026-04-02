#pragma once
#include "types.h"

enum TTFlag : uint8_t { TT_NONE, TT_EXACT, TT_ALPHA, TT_BETA };

struct TTEntry {
    U64      key;
    int16_t  score;
    int16_t  depth;
    Move     move;
    TTFlag   flag;
    uint8_t  age;
};

class TranspositionTable {
public:
    TranspositionTable(int sizeMB = 64);
    ~TranspositionTable() { delete[] table_; }

    TTEntry* probe(U64 key, bool& found);
    void store(U64 key, int score, int depth, Move move, TTFlag flag, int ply);
    void clear();
    void resize(int sizeMB);
    void new_search() { generation_++; }
    int hashfull() const;

private:
    TTEntry* table_;
    size_t   size_;
    uint8_t  generation_ = 0;
};

extern TranspositionTable TT;
