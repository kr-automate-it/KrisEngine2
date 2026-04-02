#include "bitboard.h"
#include "uci.h"

int main() {
    bitboard_init();
    uci_loop();
    return 0;
}
