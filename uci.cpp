#include "uci.h"
#include "search.h"
#include "movegen.h"
#include "tt.h"
#include "eval.h"
#include "bitboard.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

static Position pos;
static SearchInfo searchInfo;
static std::thread searchThread;

// === Parse "go" command ===

static void parse_go(std::istringstream& is) {
    std::string token;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    int depth = 64;
    int movetime = 0;
    bool infinite = false;

    while (is >> token) {
        if (token == "wtime")          is >> wtime;
        else if (token == "btime")     is >> btime;
        else if (token == "winc")      is >> winc;
        else if (token == "binc")      is >> binc;
        else if (token == "movestogo") is >> movestogo;
        else if (token == "depth")     is >> depth;
        else if (token == "movetime")  is >> movetime;
        else if (token == "infinite")  infinite = true;
    }

    searchInfo.maxDepth = depth;

    if (movetime > 0) {
        searchInfo.timeLimit = movetime;
    } else if (!infinite && (wtime > 0 || btime > 0)) {
        int timeLeft = (pos.side_to_move() == WHITE) ? wtime : btime;
        int inc      = (pos.side_to_move() == WHITE) ? winc  : binc;

        if (movestogo > 0) {
            searchInfo.timeLimit = timeLeft / (movestogo + 1) + inc * 3 / 4;
        } else {
            int movesLeft = 25;
            searchInfo.timeLimit = timeLeft / movesLeft + inc * 3 / 4;
        }

        // Safety
        if (searchInfo.timeLimit < 10) searchInfo.timeLimit = 10;
        int64_t maxTime = (int64_t)timeLeft / 2;
        if (searchInfo.timeLimit > maxTime)
            searchInfo.timeLimit = std::max((int64_t)10, maxTime);
        if (searchInfo.timeLimit > timeLeft - 50)
            searchInfo.timeLimit = std::max((int64_t)10, (int64_t)(timeLeft - 50));
    } else {
        searchInfo.timeLimit = 0;
    }

    // Stop previous search
    searchInfo.stopped.store(true);
    if (searchThread.joinable()) searchThread.join();

    searchThread = std::thread([&pos, &searchInfo]() {
        SearchResult result = search(pos, searchInfo);
        if (result.bestMove != MOVE_NONE)
            std::cout << "bestmove " << pos.move_to_uci(result.bestMove) << std::endl;
        else
            std::cout << "bestmove 0000" << std::endl;
    });
}

// === Parse "position" command ===

static void parse_position(std::istringstream& is) {
    searchInfo.stopped.store(true);
    if (searchThread.joinable()) searchThread.join();

    std::string token;
    is >> token;

    if (token == "startpos") {
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        is >> token; // consume "moves" if present
    } else if (token == "fen") {
        std::string fen;
        while (is >> token && token != "moves")
            fen += token + " ";
        pos.set(fen);
    }

    // Apply moves
    static StateInfo states[1024];
    int stateIdx = 0;
    while (is >> token) {
        Move m = pos.parse_uci(token);
        if (m != MOVE_NONE) {
            pos.do_move(m, states[stateIdx++]);
            if (stateIdx >= 1020) stateIdx = 0;
        }
    }
}

// === Main UCI loop ===

void uci_loop() {
    pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    std::string line, token;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        is >> token;

        if (token == "uci") {
            std::cout << "id name StockfishLike" << std::endl;
            std::cout << "id author rajda" << std::endl;
            std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
            std::cout << "option name Threads type spin default 1 min 1 max 1" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (token == "setoption") {
            std::string name, value;
            is >> token; // "name"
            is >> name;
            is >> token; // "value"
            is >> value;
            if (name == "Hash") {
                TT.resize(std::stoi(value));
            }
        }
        else if (token == "ucinewgame") {
            TT.clear();
            clear_search_tables();
            pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        }
        else if (token == "position") {
            parse_position(is);
        }
        else if (token == "go") {
            parse_go(is);
        }
        else if (token == "stop") {
            searchInfo.stopped.store(true);
            if (searchThread.joinable()) searchThread.join();
        }
        else if (token == "quit") {
            searchInfo.stopped.store(true);
            if (searchThread.joinable()) searchThread.join();
            return;
        }
        else if (token == "d") {
            std::cout << pos.fen() << std::endl;
        }
    }
    // Cleanup: stop search and wait for thread
    searchInfo.stopped.store(true);
    if (searchThread.joinable()) searchThread.join();
}
