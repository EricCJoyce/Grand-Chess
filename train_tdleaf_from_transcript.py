#!/usr/bin/env python3
'''
Transcript-guided TDLeaf(lambda) pretraining for linear evaluators.

Expected input: one game per line, with two tab-separated fields:

    12-28 50-34 6-21\t1-0

Promotions use a third field, for example 50-58-Q. Chess and Grand Chess transcripts are supported directly.
Other profiles can use --match-via-print-move when their binary move layout is not the standard three-byte from/to/promotion layout.
'''

from __future__ import annotations

import argparse
import random
from pathlib import Path

import numpy as np

from linear_tdleaf_common import (AlphaBetaSearcher, CachedCLI, EvalConfig, GAME_PROFILES, LinearEvaluator, LinearTDLeaf, SearchConfig, checkpoint_name, current_pov_target_from_successor, find_transcript_move, labeled_successor_value, load_scales, load_weights, print_board_payload, save_weights, searched_value_and_grad, terminal_value)

def train_from_transcripts(cli: CachedCLI, evaluator: LinearEvaluator, learner: LinearTDLeaf, searcher: AlphaBetaSearcher, *, profile_key: str, transcript_path: str, weights_out: str, starting_game: int, checkpoint_every: int, direct_encoding: bool, draw: bool, verbose: bool) -> None:
	profile = GAME_PROFILES[profile_key]
	lines = Path(transcript_path).read_text(encoding='utf-8').splitlines()
	completed_game = starting_game

	for game_index, raw_line in enumerate(lines, start=1):
		if game_index <= starting_game or not raw_line.strip():
			continue
		fields = raw_line.strip().split('\t')
		if len(fields) != 2:
			raise ValueError(f'Game {game_index}: expected moves<TAB>result, got {raw_line!r}')
		move_tokens = fields[0].split()
		result_label = fields[1].strip()

		state = cli.startpos()
		learner.reset_traces()
		plies_played = 0

		for ply_index, token in enumerate(move_tokens):
			is_terminal, _ = cli.terminal(state)
			if is_terminal:
				break

			current_search = searcher.best_move_and_leaf(state)
			value_t, grad_t = searched_value_and_grad(evaluator, current_search)

			move_hex = find_transcript_move(cli, state, token, profile, direct_three_byte_encoding=direct_encoding)
			next_state = cli.apply_move(state, move_hex)
			plies_played += 1

			if draw:
				print(token)
				print_board_payload(cli.draw(next_state))
				print()

			next_terminal, next_result = cli.terminal(next_state)
			last_transcript_ply = ply_index == len(move_tokens) - 1
			if last_transcript_ply:
																	#  The first player moves on even-numbered plies from the initial state.
				next_first_player_to_move = ((ply_index + 1) % 2 == 0)
				successor_value = labeled_successor_value(result_label, next_first_player_to_move)
			elif next_terminal:
				successor_value = terminal_value(str(next_result))
			else:
				next_search = searcher.best_move_and_leaf(next_state)
				successor_value, _ = searched_value_and_grad(evaluator, next_search)

			target = current_pov_target_from_successor(successor_value)
			delta = target - value_t
			learner.update(delta, grad_t)

			if verbose:
				print(f'game={game_index} ply={ply_index + 1} token={token} v={value_t:+.5f} target={target:+.5f} delta={delta:+.5f}')
			state = next_state

		completed_game = game_index
		if game_index % checkpoint_every == 0:
			save_weights(checkpoint_name(weights_out, game_index), evaluator.W, game_index, {'kind': 'transcript', 'game': profile.name, 'plies': plies_played, 'result': result_label, 'cli': cli.stats()})
		norm = float(np.linalg.norm(evaluator.W))
		stats = cli.stats()
		print(f'game={game_index:6d} plies={plies_played:4d} result={result_label:9s} ||W||={norm:.4f} calls={stats["cli_calls"]} hits={stats["cache_hits"]} starts={stats["process_starts"]}')

	save_weights(weights_out, evaluator.W, completed_game, {'kind': 'transcript', 'game': profile.name, 'cli': cli.stats()})
	return

def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument('--game', required=True, choices=sorted(GAME_PROFILES))
	parser.add_argument('--cli', required=True)
	parser.add_argument('--script', required=True)
	parser.add_argument('--weights-in', required=True)
	parser.add_argument('--weights-out', required=True)
	parser.add_argument('--scales', default='')
	parser.add_argument('--depth', type=int, default=4)
	parser.add_argument('--lr', type=float, default=1e-3)
	parser.add_argument('--lam', type=float, default=0.7)
	parser.add_argument('--tau', type=float, default=3.0)
	parser.add_argument('--weight-decay', type=float, default=1e-5)
	parser.add_argument('--trace-clip', type=float, default=10.0)
	parser.add_argument('--grad-clip', type=float, default=10.0)
	parser.add_argument('--weight-clip', type=float, default=1e6)
	parser.add_argument('--checkpoint-every', type=int, default=1)
	parser.add_argument('--seed', type=int, default=1)
	parser.add_argument('--fixed-phase', type=float, default=None)
	parser.add_argument('--cache-entries', type=int, default=250_000)
	parser.add_argument('--one-shot-cli', action='store_true')
	parser.add_argument('--match-via-print-move', action='store_true')
	parser.add_argument('--no-shuffle', action='store_true')
	parser.add_argument('--no-tt', action='store_true')
	parser.add_argument('--draw', action='store_true')
	parser.add_argument('--verbose', action='store_true')
	args = parser.parse_args()

	random.seed(args.seed)
	np.random.seed(args.seed)
	weights, starting_game, _ = load_weights(args.weights_in)
	feature_count = weights.shape[-1]
	scales = load_scales(args.scales, feature_count) if args.scales else None

	with CachedCLI(args.cli, prefer_persistent=not args.one_shot_cli, cache_entries=args.cache_entries) as cli:
		evaluator = LinearEvaluator(cli, weights, EvalConfig(tau=args.tau, feature_scales=scales, fixed_phase=args.fixed_phase))
		learner = LinearTDLeaf(evaluator, learning_rate=args.lr, lam=args.lam, weight_decay=args.weight_decay, trace_clip=args.trace_clip, grad_clip=args.grad_clip, weight_clip=args.weight_clip)
		searcher = AlphaBetaSearcher(cli, evaluator, SearchConfig(depth=args.depth, shuffle_moves=not args.no_shuffle, use_transposition_table=not args.no_tt))
		train_from_transcripts(cli, evaluator, learner, searcher, profile_key=args.game, transcript_path=args.script, weights_out=args.weights_out, starting_game=starting_game, checkpoint_every=args.checkpoint_every, direct_encoding=not args.match_via_print_move, draw=args.draw, verbose=args.verbose)

	return

if __name__ == '__main__':
	main()