#include "bitboard.h"
#include "zobrist.h"
#include "uci.h"

int main() {
    bitboard_init();
    Zobrist::init();
    uci_loop();
    return 0;
}
