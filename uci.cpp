#include "uci.h"
#include <iostream>
#include <string>

// TODO: full UCI protocol
void uci_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name StockfishLike" << std::endl;
            std::cout << "id author rajda" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (line == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (line == "quit") {
            break;
        }
    }
}
