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
	src/cfr_value_type.h src/cfr_values.h src/prob_method.h \
	src/cfr_utils.h src/vcfr_state.h src/vcfr.h src/cfrp.h src/rgbr.h \
	src/resolving_method.h src/eg_cfr.h src/endgames.h src/cbr_thread.h \
	src/cbr_builder.h src/path.h src/sorting.h src/rollout.h \
	src/univariate_kmeans.h src/buckets.h src/fast_hash.h \
	src/sparse_and_dense.h src/bcbr_thread.h src/bcfr_thread.h \
	src/bcbr_builder.h src/vcfr_subgame.h src/kmeans.h src/pkmeans.h \
	src/endgame_utils.h src/compression_utils.h \
	src/regret_compression.h src/tcfr.h src/ols.h src/ej_compress.h \
	src/pcs_cfr.h src/canonical.h src/mp_vcfr.h src/mp_rgbr.h \
	src/sampled_bcfr_builder.h src/runtime_params.h src/runtime_config.h \
	src/acpc_protocol.h src/agent.h src/nearest_neighbors.h \
	src/nl_agent.h src/dynamic_cbr2.h src/cfr_values_file.h src/bot.h \
	src/cv_calc_thread.h src/joint_reach_probs.h src/ecfr.h

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
	obj/betting_tree_builder.o obj/limit_tree.o obj/no_limit_tree.o \
	obj/reentrant_tree.o obj/mp_betting_tree.o obj/nonterminal_ids.o \
	obj/cfr_value_type.o obj/cfr_values.o obj/cfr_utils.o \
	obj/vcfr_state.o obj/vcfr.o obj/cfrp.o obj/rgbr.o \
	obj/resolving_method.o obj/eg_cfr.o obj/endgames.o obj/cbr_thread.o \
	obj/cbr_builder.o obj/path.o obj/sorting.o obj/rollout.o \
	obj/univariate_kmeans.o obj/buckets.o obj/fast_hash.o \
	obj/sparse_and_dense.o obj/bcbr_thread.o obj/bcfr_thread.o \
	obj/bcbr_builder.o obj/vcfr_subgame.o obj/kmeans.o obj/pkmeans.o \
	obj/endgame_utils.o obj/compression_utils.o obj/regret_compression.o \
	obj/tcfr.o obj/ols.o obj/ej_compress.o obj/pcs_cfr.o obj/canonical.o \
	obj/mp_vcfr.o obj/mp_rgbr.o obj/sampled_bcfr_builder.o \
	obj/runtime_params.o obj/runtime_config.o \
	obj/acpc_protocol.o obj/nearest_neighbors.o obj/nl_agent.o \
	obj/dynamic_cbr2.o obj/cfr_values_file.o obj/bot.o \
	obj/cv_calc_thread.o obj/joint_reach_probs.o obj/ecfr.o

bin/test:	obj/test.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test obj/test.o $(OBJS) \
	$(LIBRARIES)

bin/test2:	obj/test2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test2 obj/test2.o $(OBJS) \
	$(LIBRARIES)

bin/test3:	obj/test3.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test3 obj/test3.o $(OBJS) \
	$(LIBRARIES)

bin/test4:	obj/test4.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test4 obj/test4.o $(OBJS) \
	$(LIBRARIES)

bin/test_canonicalization:	obj/test_canonicalization.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_canonicalization \
	obj/test_canonicalization.o $(OBJS) $(LIBRARIES)

bin/build_hand_value_tree:	obj/build_hand_value_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_hand_value_tree \
	obj/build_hand_value_tree.o $(OBJS) $(LIBRARIES)

bin/build_betting_tree:	obj/build_betting_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_betting_tree \
	obj/build_betting_tree.o $(OBJS) $(LIBRARIES)

bin/show_betting_tree:	obj/show_betting_tree.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_betting_tree \
	obj/show_betting_tree.o $(OBJS) $(LIBRARIES)

bin/show_node:	obj/show_node.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_node \
	obj/show_node.o $(OBJS) $(LIBRARIES)

bin/run_cfrp:	obj/run_cfrp.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_cfrp obj/run_cfrp.o \
	$(OBJS) $(LIBRARIES)

bin/run_tcfr:	obj/run_tcfr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_tcfr obj/run_tcfr.o $(OBJS) \
	$(LIBRARIES)

bin/run_ecfr:	obj/run_ecfr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_ecfr obj/run_ecfr.o $(OBJS) \
	$(LIBRARIES)

bin/run_pcs:	obj/run_pcs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_pcs obj/run_pcs.o $(OBJS) \
	$(LIBRARIES)

bin/run_rgbr:	obj/run_rgbr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_rgbr obj/run_rgbr.o $(OBJS) \
	$(LIBRARIES)

bin/run_rgbr2:	obj/run_rgbr2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_rgbr2 obj/run_rgbr2.o $(OBJS) \
	$(LIBRARIES)

bin/run_mp_rgbr:	obj/run_mp_rgbr.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_mp_rgbr obj/run_mp_rgbr.o $(OBJS) \
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

bin/solve_all_endgames4:	obj/solve_all_endgames4.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/solve_all_endgames4 \
	obj/solve_all_endgames4.o $(OBJS) $(LIBRARIES)

bin/solve_all_endgames5:	obj/solve_all_endgames5.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/solve_all_endgames5 \
	obj/solve_all_endgames5.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames:	obj/assemble_endgames.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames \
	obj/assemble_endgames.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames2:	obj/assemble_endgames2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames2 \
	obj/assemble_endgames2.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames3:	obj/assemble_endgames3.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames3 \
	obj/assemble_endgames3.o $(OBJS) $(LIBRARIES)

bin/assemble_endgames4:	obj/assemble_endgames4.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/assemble_endgames4 \
	obj/assemble_endgames4.o $(OBJS) $(LIBRARIES)

bin/build_cbrs:	obj/build_cbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_cbrs obj/build_cbrs.o \
	$(OBJS) $(LIBRARIES)

bin/build_cfrs:	obj/build_cfrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_cfrs obj/build_cfrs.o \
	$(OBJS) $(LIBRARIES)

bin/build_bcbrs:	obj/build_bcbrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_bcbrs obj/build_bcbrs.o \
	$(OBJS) $(LIBRARIES)

bin/build_sampled_bcfrs:	obj/build_sampled_bcfrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_sampled_bcfrs \
	obj/build_sampled_bcfrs.o $(OBJS) $(LIBRARIES)

bin/play:	obj/play.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play obj/play.o \
	$(OBJS) $(LIBRARIES)

bin/play2:	obj/play2.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play2 obj/play2.o \
	$(OBJS) $(LIBRARIES)

bin/play3:	obj/play3.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play3 obj/play3.o \
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

bin/build_pkmeans_buckets:	obj/build_pkmeans_buckets.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_pkmeans_buckets \
	obj/build_pkmeans_buckets.o $(OBJS) $(LIBRARIES)

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

bin/build_suit_features:	obj/build_suit_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/build_suit_features \
	obj/build_suit_features.o $(OBJS) $(LIBRARIES)

bin/combine_features:	obj/combine_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/combine_features \
	obj/combine_features.o $(OBJS) $(LIBRARIES)

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

bin/show_strategy:	obj/show_strategy.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_strategy obj/show_strategy.o \
	$(OBJS) $(LIBRARIES)

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

bin/show_joint_reach_probs:	obj/show_joint_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_joint_reach_probs \
	obj/show_joint_reach_probs.o $(OBJS) $(LIBRARIES)

bin/test_shared_ptrs:	obj/test_shared_ptrs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_shared_ptrs \
	obj/test_shared_ptrs.o $(OBJS) $(LIBRARIES)

bin/compare_boards:	obj/compare_boards.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/compare_boards obj/compare_boards.o \
	$(OBJS) $(LIBRARIES)

bin/show_features:	obj/show_features.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_features obj/show_features.o \
	$(OBJS) $(LIBRARIES)

bin/show_nuts:	obj/show_nuts.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/show_nuts obj/show_nuts.o \
	$(OBJS) $(LIBRARIES)

bin/check_the_nuts:	obj/check_the_nuts.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/check_the_nuts obj/check_the_nuts.o \
	$(OBJS) $(LIBRARIES)

bin/test_agent:	obj/test_agent.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_agent obj/test_agent.o \
	$(OBJS) $(LIBRARIES)

bin/test_ms1f1_agent:	obj/test_ms1f1_agent.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_ms1f1_agent \
	obj/test_ms1f1_agent.o $(OBJS) $(LIBRARIES)

bin/test_mp_agent:	obj/test_mp_agent.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_mp_agent obj/test_mp_agent.o \
	$(OBJS) $(LIBRARIES)

bin/run_bot:	obj/run_bot.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/run_bot obj/run_bot.o \
	$(OBJS) $(LIBRARIES)

bin/restructure:	obj/restructure.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/restructure obj/restructure.o \
	$(OBJS) $(LIBRARIES)

bin/test_cfr_values_file:	obj/test_cfr_values_file.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/test_cfr_values_file \
	obj/test_cfr_values_file.o $(OBJS) $(LIBRARIES)

bin/play_agents:	obj/play_agents.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/play_agents obj/play_agents.o \
	$(OBJS) $(LIBRARIES)

bin/sampled_play_agents:	obj/sampled_play_agents.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/sampled_play_agents \
	obj/sampled_play_agents.o $(OBJS) $(LIBRARIES)

bin/calc_cv:	obj/calc_cv.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/calc_cv obj/calc_cv.o \
	$(OBJS) $(LIBRARIES)

bin/measure_consistency:	obj/measure_consistency.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/measure_consistency \
	obj/measure_consistency.o $(OBJS) $(LIBRARIES)

bin/compute_joint_reach_probs:	obj/compute_joint_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/compute_joint_reach_probs \
	obj/compute_joint_reach_probs.o $(OBJS) $(LIBRARIES)

bin/compute_sampled_joint_reach_probs:	\
	obj/compute_sampled_joint_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/compute_sampled_joint_reach_probs \
	obj/compute_sampled_joint_reach_probs.o $(OBJS) $(LIBRARIES)

bin/compare_joint_reach_probs:	obj/compare_joint_reach_probs.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/compare_joint_reach_probs \
	obj/compare_joint_reach_probs.o $(OBJS) $(LIBRARIES)

bin/x:	obj/x.o $(OBJS) $(HEADS)
	g++ $(LDFLAGS) $(CFLAGS) -o bin/x obj/x.o \
	$(OBJS) $(LIBRARIES)
