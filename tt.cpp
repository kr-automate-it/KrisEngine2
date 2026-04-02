#include "tt.h"
#include <cstring>
#include <algorithm>

TranspositionTable TT(64);

TranspositionTable::TranspositionTable(int sizeMB) {
    size_ = (size_t(sizeMB) * 1024 * 1024) / sizeof(TTEntry);
    table_ = new TTEntry[size_];
    clear();
}

void TranspositionTable::clear() {
    std::memset(table_, 0, size_ * sizeof(TTEntry));
    generation_ = 0;
}

void TranspositionTable::resize(int sizeMB) {
    delete[] table_;
    size_ = (size_t(sizeMB) * 1024 * 1024) / sizeof(TTEntry);
    table_ = new TTEntry[size_];
    clear();
}

TTEntry* TranspositionTable::probe(U64 key, bool& found) {
    TTEntry* entry = &table_[key % size_];
    found = (entry->key == key && entry->flag != TT_NONE);
    return entry;
}

void TranspositionTable::store(U64 key, int score, int depth, Move move, TTFlag flag, int ply) {
    TTEntry* entry = &table_[key % size_];

    // Adjust mate scores for storage
    if (score > 30000) score += ply;
    if (score < -30000) score -= ply;

    // Replace if: new entry, deeper search, or old generation
    if (entry->flag == TT_NONE
        || entry->age != generation_
        || depth >= entry->depth) {
        entry->key   = key;
        entry->score = (int16_t)score;
        entry->depth = (int16_t)depth;
        entry->move  = move;
        entry->flag  = flag;
        entry->age   = generation_;
    }
}

int TranspositionTable::hashfull() const {
    int count = 0;
    for (int i = 0; i < 1000 && i < (int)size_; i++)
        if (table_[i].flag != TT_NONE && table_[i].age == generation_)
            count++;
    return count;
}
