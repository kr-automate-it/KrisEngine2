#include "bitboard.h"
#include "zobrist.h"
#include "uci.h"

int main() {
    init_bitboards();
    Zobrist::init();
    uci_loop();
    return 0;
}
