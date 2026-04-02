#!/usr/bin/env python3
"""Self-play test with debug output."""
import chess
import chess.engine
import time
import sys

ENGINE = "./engine.exe"
TC_MS = 5000  # 5s per move
GAMES = 1

def play_game(engine_path, tc_ms):
    board = chess.Board()
    engine = chess.engine.SimpleEngine.popen_uci(engine_path)
    engine.configure({"Hash": 64})

    moves = []
    move_num = 0

    print(f"=== New game, TC={tc_ms}ms ===")
    print(f"Starting FEN: {board.fen()}")

    while not board.is_game_over(claim_draw=True):
        move_num += 1
        if move_num > 200:
            print("Draw by move limit")
            break

        side = "White" if board.turn == chess.WHITE else "Black"
        print(f"\nMove {move_num} ({side}) FEN: {board.fen()}")

        try:
            start = time.perf_counter()
            result = engine.play(
                board,
                chess.engine.Limit(time=tc_ms / 1000.0),
                info=chess.engine.INFO_ALL
            )
            elapsed = time.perf_counter() - start

            if result.move is None:
                print(f"  ERROR: engine returned no move!")
                break

            # Check if move is legal
            if result.move not in board.legal_moves:
                print(f"  ERROR: illegal move {result.move}!")
                print(f"  Legal moves: {[m.uci() for m in board.legal_moves]}")
                break

            print(f"  Move: {result.move.uci()} ({elapsed:.3f}s)")
            if hasattr(result, 'info') and result.info:
                if 'depth' in result.info:
                    print(f"  Depth: {result.info.get('depth', '?')}")
                if 'score' in result.info:
                    print(f"  Score: {result.info.get('score', '?')}")
                if 'nodes' in result.info:
                    print(f"  Nodes: {result.info.get('nodes', '?')}")

            board.push(result.move)
            moves.append(result.move.uci())

            if elapsed > tc_ms / 1000.0 + 1.0:
                print(f"  WARNING: took {elapsed:.1f}s (limit {tc_ms/1000.0}s)")

        except chess.engine.EngineTerminatedError:
            print(f"  CRASH: engine terminated!")
            return None
        except Exception as e:
            print(f"  ERROR: {e}")
            # Try to continue
            try:
                engine.quit()
            except:
                pass
            return None

    result_str = board.result(claim_draw=True)
    print(f"\n=== Game over: {result_str} ===")
    print(f"Moves: {' '.join(moves)}")
    print(f"Final FEN: {board.fen()}")

    try:
        engine.quit()
    except:
        pass

    return result_str


if __name__ == "__main__":
    tc = int(sys.argv[1]) if len(sys.argv) > 1 else TC_MS
    engine_path = sys.argv[2] if len(sys.argv) > 2 else ENGINE

    print(f"Engine: {engine_path}")
    print(f"Time per move: {tc}ms")

    for g in range(GAMES):
        print(f"\n{'='*60}")
        print(f"Game {g+1}/{GAMES}")
        print(f"{'='*60}")
        result = play_game(engine_path, tc)
        if result is None:
            print("Game failed — engine crash or error")
            break
