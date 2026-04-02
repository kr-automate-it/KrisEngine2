#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>

// === LMR Table ===
static int lmrTable[64][64];

static void init_lmr() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++) {
            if (d == 0 || m == 0) lmrTable[d][m] = 0;
            else lmrTable[d][m] = (int)(0.5 + std::log(d) * std::log(m) / 2.25);
        }
}

// === Killer moves ===
static Move killers[128][2];

static void store_killer(int ply, Move m) {
    if (ply >= 128) return;
    if (m != killers[ply][0]) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
}

// === History heuristic ===
static int history[COLOR_NB][SQUARE_NB][SQUARE_NB];

static void update_history(Color c, Move m, int depth) {
    history[c][move_from(m)][move_to(m)] += depth * depth;
    if (history[c][move_from(m)][move_to(m)] > 1000000) {
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                history[c][f][t] /= 2;
    }
}

// === Countermove ===
static Move countermoves[PIECE_NB][SQUARE_NB];

// === Time management ===
static bool time_up(SearchInfo& info) {
    // Timer thread sets info.stopped — check it frequently
    if (info.stopped.load(std::memory_order_relaxed)) return true;

    static thread_local int checkCounter = 0;
    if (++checkCounter < 2048) return false;
    checkCounter = 0;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - info.startTime).count();
    if (info.timeLimit > 0 && elapsed >= info.timeLimit) return true;
    if (info.timeLimit <= 0 && elapsed > 30000) return true;
    return false;
}

// === Move scoring ===
static const int SCORE_TT_MOVE      = 10000000;
static const int SCORE_GOOD_CAPTURE = 5000000;
static const int SCORE_KILLER1      = 900000;
static const int SCORE_KILLER2      = 800000;
static const int SCORE_COUNTERMOVE  = 700000;

constexpr int PieceValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 20000};

static int move_score(const Position& pos, Move m, Move ttMove, int ply, Move counterMove) {
    if (m == ttMove) return SCORE_TT_MOVE;

    Piece captured = pos.piece_on(move_to(m));
    Piece mover    = pos.piece_on(move_from(m));

    if (captured != NO_PIECE)
        return SCORE_GOOD_CAPTURE + PieceValue[type_of(captured)] * 10 - PieceValue[type_of(mover)];

    if (move_type(m) == PROMOTION)
        return SCORE_GOOD_CAPTURE + PieceValue[promo_type(m)];

    if (ply < 128) {
        if (m == killers[ply][0]) return SCORE_KILLER1;
        if (m == killers[ply][1]) return SCORE_KILLER2;
    }

    if (m == counterMove) return SCORE_COUNTERMOVE;

    return history[pos.side_to_move()][move_from(m)][move_to(m)];
}

// === Quiescence Search ===

static int quiescence(Position& pos, int alpha, int beta, SearchInfo& info, int ply) {
    if (info.stopped.load(std::memory_order_relaxed)) return 0;

    info.nodes.fetch_add(1, std::memory_order_relaxed);

    bool inCheck = pos.in_check();
    if (ply > 64) return Eval::evaluate(pos);

    // TT probe
    Move ttMove = MOVE_NONE;
    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    if (ttHit) {
        ttMove = tte->move;
        if (tte->flag == TT_EXACT) return tte->score;
        if (tte->flag == TT_BETA && tte->score >= beta) return tte->score;
        if (tte->flag == TT_ALPHA && tte->score <= alpha) return tte->score;
    }

    int stand_pat = Eval::evaluate(pos);
    if (!inCheck) {
        if (stand_pat >= beta) return stand_pat;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    int bestScore = inCheck ? -VALUE_INFINITE : stand_pat;
    Move bestMove = MOVE_NONE;

    MoveList list;
    if (inCheck) generate_moves(pos, list);
    else         generate_captures(pos, list);

    // Score and sort
    int scores[256];
    for (int i = 0; i < list.count; i++)
        scores[i] = move_score(pos, list.moves[i], ttMove, ply, MOVE_NONE);

    for (int i = 0; i < list.count; i++) {
        // Selection sort
        int best = i;
        for (int j = i + 1; j < list.count; j++)
            if (scores[j] > scores[best]) best = j;
        if (best != i) {
            std::swap(list.moves[i], list.moves[best]);
            std::swap(scores[i], scores[best]);
        }

        Move m = list.moves[i];
        if (!pos.is_legal(m)) continue;

        if (!inCheck) {
            // Delta pruning
            Piece cap = pos.piece_on(move_to(m));
            if (cap != NO_PIECE && stand_pat + PieceValue[type_of(cap)] + 250 < alpha
                && move_type(m) != PROMOTION)
                continue;
            // SEE pruning
            if (pos.see(m) < 0) continue;
        }

        StateInfo newSt;
        pos.do_move(m, newSt);
        int score = -quiescence(pos, -beta, -alpha, info, ply + 1);
        pos.undo_move(m);
        if (info.stopped.load(std::memory_order_relaxed)) return bestScore;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (score >= beta) return score;
        if (score > alpha) alpha = score;
    }

    if (inCheck && bestScore == -VALUE_INFINITE)
        return -VALUE_MATE + ply;

    return bestScore;
}

// === Alpha-Beta Search ===

static int alpha_beta(Position& pos, int depth, int alpha, int beta,
                       SearchInfo& info, int ply, bool doNull = true, Move prevMove = MOVE_NONE) {
    // Check stopped FIRST (set by timer or UCI stop command)
    if (info.stopped.load(std::memory_order_relaxed)) return 0;
    // Periodic time check
    {
        static thread_local int tc = 0;
        if (++tc >= 2048) {
            tc = 0;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - info.startTime).count();
            if ((info.timeLimit > 0 && elapsed >= info.timeLimit)
                || (info.timeLimit <= 0 && elapsed > 30000)) {
                info.stopped.store(true);
                return 0;
            }
        }
    }

    bool pvNode = (beta - alpha > 1);
    bool inCheck = pos.in_check();

    // Hard ply limit — prevent stack overflow from extensions
    if (ply >= 127) return Eval::evaluate(pos);

    // Qsearch at leaf
    if (depth <= 0) return quiescence(pos, alpha, beta, info, ply);

    info.nodes.fetch_add(1, std::memory_order_relaxed);

    // Draw detection
    if (ply > 0 && pos.is_draw()) return 0;

    // Mate distance pruning
    alpha = std::max(alpha, -VALUE_MATE + ply);
    beta  = std::min(beta,  VALUE_MATE - ply - 1);
    if (alpha >= beta) return alpha;

    // TT probe
    Move ttMove = MOVE_NONE;
    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    if (ttHit) {
        ttMove = tte->move;
        if (!pvNode && tte->depth >= depth) {
            int ttScore = tte->score;
            if (ttScore > 30000) ttScore -= ply;
            if (ttScore < -30000) ttScore += ply;
            if (tte->flag == TT_EXACT) return ttScore;
            if (tte->flag == TT_BETA && ttScore >= beta) return ttScore;
            if (tte->flag == TT_ALPHA && ttScore <= alpha) return ttScore;
        }
    }

    // Static eval
    int eval = inCheck ? -VALUE_INFINITE : Eval::evaluate(pos);

    // Improving heuristic
    static thread_local int evalHistory[128];
    if (ply < 128 && !inCheck) evalHistory[ply] = eval;
    bool improving = (!inCheck && ply >= 2 && ply < 128 && eval > evalHistory[ply - 2]);

    // === Pruning (non-PV, non-check) ===
    if (!inCheck && !pvNode) {
        // Reverse Futility Pruning
        if (depth <= 3 && eval - 110 * depth >= beta)
            return eval;

        // Razoring
        if (depth <= 2 && eval + 300 + 200 * depth < alpha) {
            int qScore = quiescence(pos, alpha - 1, alpha, info, ply);
            if (qScore < alpha) return qScore;
        }

        // Null Move Pruning
        if (doNull && depth >= 3 && eval >= beta) {
            U64 npm = pos.pieces(pos.side_to_move()) & ~pos.pieces(PAWN) & ~pos.pieces(KING);
            if (popcount(npm) >= 2) {
                int R = 3 + depth / 4 + std::min((eval - beta) / 200, 3);
                StateInfo newSt;
                pos.do_null_move(newSt);
                int score = -alpha_beta(pos, depth - R - 1, -beta, -beta + 1, info, ply + 1, false);
                pos.undo_null_move();
                if (info.stopped.load(std::memory_order_relaxed)) return 0;
                if (score >= beta) return beta;
            }
        }
    }

    // IID
    if (ttMove == MOVE_NONE && depth >= 6) {
        alpha_beta(pos, depth / 3, alpha, beta, info, ply, false, prevMove);
        if (info.stopped.load(std::memory_order_relaxed)) return 0;
        bool iidHit;
        TTEntry* iidEntry = TT.probe(pos.key(), iidHit);
        if (iidHit) ttMove = iidEntry->move;
    }

    // IIR
    if (ttMove == MOVE_NONE && depth >= 4 && !pvNode)
        depth--;

    // === Generate and search moves ===
    MoveList list;
    generate_moves(pos, list);

    // Countermove
    Move counterMove = MOVE_NONE;
    if (prevMove != MOVE_NONE) {
        Piece prevPc = pos.piece_on(move_to(prevMove));
        if (prevPc != NO_PIECE)
            counterMove = countermoves[prevPc][move_to(prevMove)];
    }

    // Score moves lazily: TT move first
    int moveScores[256];
    bool movesScored = false;
    if (ttMove != MOVE_NONE) {
        for (int j = 0; j < list.count; j++) {
            if (list.moves[j] == ttMove) {
                if (j > 0) std::swap(list.moves[0], list.moves[j]);
                break;
            }
        }
    }

    int legalMoves = 0;
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INFINITE;
    TTFlag ttFlag = TT_ALPHA;

    for (int i = 0; i < list.count; i++) {
        // Lazy scoring: score rest after TT move
        if (i == 1 && !movesScored) {
            for (int j = 1; j < list.count; j++)
                moveScores[j] = move_score(pos, list.moves[j], MOVE_NONE, ply, counterMove);
            movesScored = true;
        }
        if (i >= 1 && movesScored) {
            int best = i;
            for (int j = i + 1; j < list.count; j++)
                if (moveScores[j] > moveScores[best]) best = j;
            if (best != i) {
                std::swap(list.moves[i], list.moves[best]);
                std::swap(moveScores[i], moveScores[best]);
            }
        }

        Move m = list.moves[i];
        if (!pos.is_legal(m)) continue;

        legalMoves++;
        bool isCapture = pos.piece_on(move_to(m)) != NO_PIECE || move_type(m) == EN_PASSANT;
        bool isQuiet   = !isCapture && move_type(m) != PROMOTION;

        // LMP
        int lmpThreshold = 3 + depth * depth + (improving ? depth : 0);
        if (!pvNode && !inCheck && isQuiet && depth <= 7 && legalMoves > lmpThreshold)
            continue;

        // Futility pruning
        if (!pvNode && !inCheck && isQuiet && depth <= 3 && legalMoves > 1
            && eval + 200 * depth < alpha)
            continue;

        StateInfo newSt;
        pos.do_move(m, newSt);

        bool givesCheck = pos.in_check();
        int score;

        // Check extension
        int ext = 0;
        if (givesCheck && (legalMoves == 1 || (m == ttMove && depth >= 6)))
            ext = 1;

        // === PVS + LMR ===
        if (legalMoves == 1) {
            score = -alpha_beta(pos, depth - 1 + ext, -beta, -alpha, info, ply + 1, true, m);
        } else {
            int reduction = 0;
            if (legalMoves > 2 && depth >= 2 && !inCheck && !givesCheck) {
                if (isQuiet) {
                    reduction = lmrTable[std::min(depth, 63)][std::min(legalMoves, 63)];
                    if (!pvNode) reduction++;
                    if (!improving) reduction++;
                    if (legalMoves > 10) reduction++;
                    int hist = history[pos.side_to_move()][move_from(m)][move_to(m)];
                    reduction -= std::max(-2, std::min(2, hist / 5000));
                } else if (isCapture && legalMoves > 5) {
                    reduction = 1;
                }
                reduction = std::max(0, std::min(reduction, depth - 1));
            }

            // Zero-window
            score = -alpha_beta(pos, depth - 1 + ext - reduction, -alpha - 1, -alpha,
                                 info, ply + 1, true, m);

            // Re-search if reduced and failed high
            if (score > alpha && reduction > 0)
                score = -alpha_beta(pos, depth - 1 + ext, -alpha - 1, -alpha, info, ply + 1, true, m);

            // Full window re-search
            if (score > alpha && score < beta)
                score = -alpha_beta(pos, depth - 1 + ext, -beta, -alpha, info, ply + 1, true, m);
        }

        pos.undo_move(m);

        if (info.stopped.load(std::memory_order_relaxed)) return 0;
        // Inline time check in move loop
        if (info.timeLimit > 0) {
            static thread_local int mtc = 0;
            if (++mtc >= 512) {
                mtc = 0;
                auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - info.startTime).count();
                if (el >= info.timeLimit) { info.stopped.store(true); return 0; }
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }

        if (score >= beta) {
            ttFlag = TT_BETA;
            if (isQuiet) {
                store_killer(ply, m);
                update_history(pos.side_to_move(), m, depth);
                Piece prevPc = (prevMove != MOVE_NONE) ? pos.piece_on(move_to(prevMove)) : NO_PIECE;
                if (prevPc != NO_PIECE)
                    countermoves[prevPc][move_to(prevMove)] = m;
            }
            break;
        }

        if (score > alpha) {
            alpha = score;
            ttFlag = TT_EXACT;
        }
    }

    // Mate / stalemate
    if (legalMoves == 0) {
        if (inCheck) return -VALUE_MATE + ply;
        return 0;
    }

    // TT store
    TT.store(pos.key(), bestScore, depth, bestMove, ttFlag, ply);

    return bestScore;
}

// === Clear tables ===

void clear_search_tables() {
    std::memset(history, 0, sizeof(history));
    std::memset(killers, 0, sizeof(killers));
    std::memset(countermoves, 0, sizeof(countermoves));
}

// === Iterative Deepening ===

SearchResult search(Position& pos, SearchInfo& info) {
    SearchResult result;
    result.bestMove = MOVE_NONE;
    result.score = -VALUE_INFINITE;

    info.startTime = std::chrono::steady_clock::now();
    info.nodes.store(0);
    info.stopped.store(false);

    static bool lmrInit = false;
    if (!lmrInit) { init_lmr(); lmrInit = true; }

    std::memset(killers, 0, sizeof(killers));
    TT.new_search();

    // Hard timer: stop search after timeLimit (separate check)
    std::thread timer;
    if (info.timeLimit > 0) {
        timer = std::thread([&info]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(info.timeLimit));
            info.stopped.store(true);
        });
        timer.detach();
    }

    for (int depth = 1; depth <= info.maxDepth; depth++) {
        info.depth = depth;

        // Time guard: don't start new depth unless safe
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - info.startTime).count();

            // Hard rule: if we've used ANY measurable time and remaining < 10x elapsed, stop
            if (info.timeLimit > 0 && elapsed > 0 && depth > 6) {
                if (elapsed * 100 > info.timeLimit) break;
            }
            // No time limit: max 5 seconds total
            if (info.timeLimit <= 0 && elapsed > 5000) break;
        }

        // Aspiration windows
        int alpha, beta;
        if (depth >= 4 && std::abs(result.score) < VALUE_MATE - 100) {
            alpha = result.score - 25;
            beta  = result.score + 25;
        } else {
            alpha = -VALUE_INFINITE;
            beta  =  VALUE_INFINITE;
        }

        int aspDelta = 25;
        int score;
        while (true) {
            score = alpha_beta(pos, depth, alpha, beta, info, 0, true);
            if (info.stopped.load(std::memory_order_relaxed)) break;

            if (score <= alpha) {
                // Fail-low: widen downward only, don't touch beta
                alpha = std::max(-VALUE_INFINITE, score - aspDelta);
                aspDelta *= 2;
                if (aspDelta > 500) alpha = -VALUE_INFINITE;
                continue;
            }
            if (score >= beta) {
                beta = std::min(VALUE_INFINITE, score + aspDelta);
                aspDelta *= 2;
                if (aspDelta > 500) beta = VALUE_INFINITE;
                continue;
            }
            break;
        }

        // Always update best move from TT (even if stopped mid-search)
        {
            bool ttHit;
            TTEntry* tte = TT.probe(pos.key(), ttHit);
            if (ttHit && tte->move != MOVE_NONE)
                result.bestMove = tte->move;
        }
        if (!info.stopped.load(std::memory_order_relaxed))
            result.score = score;
        else
            break;

        // UCI output
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - info.startTime).count();
        int64_t nodeCount = info.nodes.load();
        int64_t nps = (elapsed > 0) ? (nodeCount * 1000 / elapsed) : nodeCount;

        // Extract PV from TT — use copy to avoid corrupting main position
        std::string pvStr;
        {
            Position pvPos(pos); // work on copy
            StateInfo pvStates[64];
            U64 seen[64]; int seenCount = 0;
            for (int i = 0; i < depth && i < 40; i++) {
                bool hit;
                TTEntry* e = TT.probe(pvPos.key(), hit);
                if (!hit || e->move == MOVE_NONE) break;
                Move pvMove = e->move;
                if (pvPos.piece_on(move_from(pvMove)) == NO_PIECE) break;
                if (!pvPos.is_legal(pvMove)) break;
                // Cycle detection
                bool cycle = false;
                for (int j = 0; j < seenCount; j++)
                    if (seen[j] == pvPos.key()) { cycle = true; break; }
                if (cycle) break;
                seen[seenCount++] = pvPos.key();
                pvStr += pvPos.move_to_uci(pvMove) + " ";
                pvPos.do_move(pvMove, pvStates[i]);
            }
        }

        std::cout << "info depth " << depth
                  << " score cp " << score
                  << " nodes " << nodeCount
                  << " nps " << nps
                  << " time " << elapsed
                  << " pv " << pvStr
                  << std::endl;
        std::cout.flush();

        if (std::abs(score) > VALUE_MATE - 100) break;

        // Time management
        if (info.timeLimit > 0 && elapsed > info.timeLimit / 2) break;

        // Safety: if no time limit set and depth took > 10s, stop
        // (prevents hanging on "go depth 64" from startpos)
        if (info.timeLimit == 0 && elapsed > 10000 && depth >= 6) break;
    }

    // Fallback
    if (result.bestMove == MOVE_NONE) {
        MoveList fallback;
        generate_moves(pos, fallback);
        for (int i = 0; i < fallback.count; i++) {
            if (pos.is_legal(fallback.moves[i])) {
                result.bestMove = fallback.moves[i];
                break;
            }
        }
    }

    return result;
}
