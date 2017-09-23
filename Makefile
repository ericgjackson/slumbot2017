# Note that if any header files are missing when you try to build, things fail
# in mysterious ways.  You get told there is "No rule to make target obj/foo.o".
HEADS =	src/constants.h src/rand.h src/split.h src/files.h \
	src/cards.h src/io.h src/params.h src/game_params.h src/game.h \
	src/canonical_cards.h src/board_tree.h src/hand_evaluator.h \
	src/hand_value_tree.h src/hand_tree.h \
	src/card_abstraction_params.h src/card_abstraction.h \
	src/betting_abstraction_params.h src/betting_abstraction.h \
	src/cfr_params.h src/cfr_config.h src/betting_tree.h \
	src/betting_tree_builder.h src/nonterminal_ids.h \
	src/cfr_values.h src/cfr_utils.h \
	src/vcfr.h src/cfrp.h src/rgbr.h src/eg_cfr.h src/endgames.h \
	src/cbr_thread.h src/cbr_builder.h src/path.h src/sorting.h \
	src/rollout.h src/univariate_kmeans.h src/buckets.h src/fast_hash.h \
	src/sparse_and_dense.h src/bcbr_thread.h \
	src/bcfr_thread.h src/bcbr_builder.h src/vcfr_subgame.h src/kmeans.h \
	src/dynamic_cbr.h src/endgame_utils.h \
	src/compression_utils.h src/regret_compression.h src/tcfr.h src/ols.h \
	src/ej_compress.h

# -Wl,--no-as-needed fixes my problem of undefined reference to
# pthread_create (and pthread_join).  Comments I found on the web indicate
# that these flags are a workaround to a gcc bug.
# LIBRARIES = -lcompression -pthread -Wl,--no-as-needed
LIBRARIES = -pthread -Wl,--no-as-needed

LDFLAGS = 

# Causes problems
#  -fipa-pta
CFLAGS = -std=c++11 -Wall -O3 -march=native -ffast-math -flto

obj/%.o:	src/%.cpp $(HEADS)
		gcc $(CFLAGS) -c -o $@ $<

OBJS =	obj/rand.o obj/split.o obj/files.o obj/cards.o obj/io.o \
	obj/params.o obj/game_params.o obj/game.o obj/canonical_cards.o \
	obj/board_tree.o obj/hand_evaluator.o obj/hand_value_tree.o \
	obj/hand_tree.o obj/card_abstraction_params.o obj/card_abstraction.o \
	obj/betting_abstraction_params.o obj/betting_abstraction.o \
	obj/cfr_params.o obj/cfr_config.o obj/betting_tree.o \
	obj/betting_tree_builder.o obj/no_limit_tree.o obj/reentrant_tree.o \
	obj/nonterminal_ids.o \
	obj/cfr_values.o obj/cfr_utils.o obj/cfr.o obj/vcfr.o obj/cfrp.o \
	obj/rgbr.o obj/eg_cfr.o obj/endgames.o obj/cbr_thread.o \
	obj/cbr_builder.o obj/path.o obj/sorting.o obj/rollout.o \
	obj/univariate_kmeans.o obj/buckets.o obj/fast_hash.o \
	obj/sparse_and_dense.o obj/bcbr_thread.o \
	obj/bcfr_thread.o obj/bcbr_builder.o obj/vcfr_subgame.o obj/kmeans.o \
	obj/dynamic_cbr.o obj/endgame_utils.o \
	obj/compression_utils.o obj/regret_compression.o obj/tcfr.o obj/ols.o \
	obj/ej_compress.o

bin/test:	obj/test.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test obj/test.o $(OBJS) \
	$(LIBRARIES)

bin/build_hand_value_tree:	obj/build_hand_value_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_hand_value_tree \
	obj/build_hand_value_tree.o $(OBJS) $(LIBRARIES)

bin/build_betting_tree:	obj/build_betting_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_betting_tree \
	obj/build_betting_tree.o $(OBJS) $(LIBRARIES)

bin/show_betting_tree:	obj/show_betting_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_betting_tree \
	obj/show_betting_tree.o $(OBJS) $(LIBRARIES)

bin/run_cfrp:	obj/run_cfrp.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_cfrp obj/run_cfrp.o \
	$(OBJS) $(LIBRARIES)

bin/run_tcfr:	obj/run_tcfr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_tcfr obj/run_tcfr.o $(OBJS) \
	$(LIBRARIES)

bin/run_rgbr:	obj/run_rgbr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_rgbr obj/run_rgbr.o $(OBJS) \
	$(LIBRARIES)

bin/solve_all_endgames:	obj/solve_all_endgames.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/solve_all_endgames \
	obj/solve_all_endgames.o $(OBJS) $(LIBRARIES)

bin/solve_all_endgames2:	obj/solve_all_endgames2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/solve_all_endgames2 \
	obj/solve_all_endgames2.o $(OBJS) $(LIBRARIES)

bin/solve_all_endgames3:	obj/solve_all_endgames3.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/solve_all_endgames3 \
	obj/solve_all_endgames3.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames:	obj/assemble_endgames.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames \
	obj/assemble_endgames.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames2:	obj/assemble_endgames2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames2 \
	obj/assemble_endgames2.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames3:	obj/assemble_endgames3.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames3 \
	obj/assemble_endgames3.o $(OBJS) $(LIBRARIES)

bin/build_cbrs:	obj/build_cbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_cbrs obj/build_cbrs.o \
	$(OBJS) $(LIBRARIES)

bin/build_cfrs:	obj/build_cfrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_cfrs obj/build_cfrs.o \
	$(OBJS) $(LIBRARIES)

bin/build_bcbrs:	obj/build_bcbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_bcbrs obj/build_bcbrs.o \
	$(OBJS) $(LIBRARIES)

bin/play:	obj/play.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play obj/play.o \
	$(OBJS) $(LIBRARIES)

bin/play2:	obj/play2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play2 obj/play2.o \
	$(OBJS) $(LIBRARIES)

bin/play4:	obj/play4.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play4 obj/play4.o \
	$(OBJS) $(LIBRARIES)

bin/measure_reachability:	obj/measure_reachability.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/measure_reachability \
	obj/measure_reachability.o $(OBJS) $(LIBRARIES)

bin/build_null_buckets:	obj/build_null_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_null_buckets \
	obj/build_null_buckets.o $(OBJS) $(LIBRARIES)

bin/build_unique_buckets:	obj/build_unique_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_unique_buckets \
	obj/build_unique_buckets.o $(OBJS) $(LIBRARIES)

bin/build_kmeans_buckets:	obj/build_kmeans_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_kmeans_buckets \
	obj/build_kmeans_buckets.o $(OBJS) $(LIBRARIES)

bin/show_buckets:	obj/show_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_buckets obj/show_buckets.o \
	$(OBJS) $(LIBRARIES)

bin/show_num_boards:	obj/show_num_boards.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_num_boards \
	obj/show_num_boards.o $(OBJS) $(LIBRARIES)

bin/show_boards:	obj/show_boards.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_boards \
	obj/show_boards.o $(OBJS) $(LIBRARIES)

bin/show_num_hands:	obj/show_num_hands.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_num_hands \
	obj/show_num_hands.o $(OBJS) $(LIBRARIES)

bin/show_num_buckets:	obj/show_num_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_num_buckets \
	obj/show_num_buckets.o $(OBJS) $(LIBRARIES)

bin/build_rollout_features:	obj/build_rollout_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_rollout_features \
	obj/build_rollout_features.o $(OBJS) $(LIBRARIES)

bin/build_hole_card_features:	obj/build_hole_card_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_hole_card_features \
	obj/build_hole_card_features.o $(OBJS) $(LIBRARIES)

bin/build_board_features:	obj/build_board_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_board_features \
	obj/build_board_features.o $(OBJS) $(LIBRARIES)

bin/prify:	obj/prify.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/prify obj/prify.o \
	$(OBJS) $(LIBRARIES)

bin/crossproduct:	obj/crossproduct.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/crossproduct obj/crossproduct.o \
	$(OBJS) $(LIBRARIES)

bin/expand_strategy:	obj/expand_strategy.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/expand_strategy obj/expand_strategy.o \
	$(OBJS) $(LIBRARIES)

bin/test_hand_evaluator:	obj/test_hand_evaluator.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_hand_evaluator \
	obj/test_hand_evaluator.o $(OBJS) $(LIBRARIES)

bin/test_ols:	obj/test_ols.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_ols obj/test_ols.o \
	$(OBJS) $(LIBRARIES)

bin/ols_file:	obj/ols_file.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/ols_file obj/ols_file.o \
	$(OBJS) $(LIBRARIES)

bin/prepare_cbr_ols:	obj/prepare_cbr_ols.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/prepare_cbr_ols obj/prepare_cbr_ols.o \
	$(OBJS) $(LIBRARIES)

bin/test_ej_compression:	obj/test_ej_compression.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_ej_compression \
	obj/test_ej_compression.o $(OBJS) $(LIBRARIES)

bin/show_flop_reach_probs:	obj/show_flop_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_flop_reach_probs \
	obj/show_flop_reach_probs.o $(OBJS) $(LIBRARIES)

bin/show_preflop_strategy:	obj/show_preflop_strategy.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_preflop_strategy \
	obj/show_preflop_strategy.o $(OBJS) $(LIBRARIES)

bin/run_approx_rgbr:	obj/run_approx_rgbr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_approx_rgbr obj/run_approx_rgbr.o \
	$(OBJS) $(LIBRARIES)

bin/compare_cbrs:	obj/compare_cbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/compare_cbrs obj/compare_cbrs.o \
	$(OBJS) $(LIBRARIES)

bin/show_cbrs:	obj/show_cbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_cbrs obj/show_cbrs.o \
	$(OBJS) $(LIBRARIES)

bin/show_reach_probs:	obj/show_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_reach_probs \
	obj/show_reach_probs.o $(OBJS) $(LIBRARIES)

bin/test_shared_ptrs:	obj/test_shared_ptrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_shared_ptrs \
	obj/test_shared_ptrs.o $(OBJS) $(LIBRARIES)

bin/x:	obj/x.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/x obj/x.o \
	$(OBJS) $(LIBRARIES)
