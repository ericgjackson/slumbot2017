// Currently assume that we sample on only at most a single street.  Well,
// not sure about this.
//
// I'm keeping denoms for each succ separately although that's a bit of a
// waste.  Without sampling, they would be the same for every succ.

#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "rand.h"
#include "sampled_bcfr_builder.h"
#include "vcfr_state.h"

using namespace std;

SampledBCFRBuilder::SampledBCFRBuilder(const CardAbstraction &ca,
				       const BettingAbstraction &ba,
				       const CFRConfig &cc,
				       const Buckets &buckets,
				       const BettingTree *betting_tree,
				       unsigned int p,
				       unsigned int it, unsigned int sample_st,
				       unsigned int num_to_sample,
				       unsigned int num_threads) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  p_ = p;
  it_ = it;
  sample_st_ = sample_st;
  num_to_sample_ = num_to_sample;
  unsigned int max_street = Game::MaxStreet();
  if (sample_st_ + 1 <= max_street) {
    fprintf(stderr, "sample_st must be max street or larger\n");
    exit(-1);
  }
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  br_current_ = false;
  value_calculation_ = true;
  prune_ = false;

  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  BoardTree::CreateLookup();
  BoardTree::BuildPredBoards();

  HandValueTree::Create();
  if (sample_st > max_street) {
    trunk_hand_tree_ = new HandTree(0, 0, max_street);
  } else {
    trunk_hand_tree_ = new HandTree(0, 0, sample_st - 1);
  }

  // Should handle asymmetric systems
  // Should honor sumprobs_streets_
  sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_, 0, 0,
				card_abstraction_, buckets_, nullptr));

  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
#if 0
  if (betting_abstraction_.Asymmetric()) {
    if (target_p_) strcat(dir, ".p1");
    else           strcat(dir, ".p2");
  }
#endif
  sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);

  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  bucketed_ = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed_ = true;
  }
#if 0
  if (bucketed_) {
    // Current strategy always uses doubles
    // This doesn't generalize to multiplayer
    current_strategy_.reset(new CFRValues(nullptr, false,
					  bucketed_streets.get(),
					  betting_tree_, 0, 0,
					  card_abstraction_, buckets_,
					  compressed_streets_));
    current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
					       kMaxUInt);
    SetCurrentStrategy(betting_tree_->Root());
  } else {
    current_strategy_.reset(nullptr);
  }
#endif

  unsigned int num_players = Game::NumPlayers();
  bucket_sum_vals_ = new float ***[max_street + 1];
  bucket_denoms_ = new float ***[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucket_sum_vals_[st] = new float **[num_players];
    bucket_denoms_[st] = new float **[num_players];
    for (unsigned int pa = 0; pa < num_players; ++pa) {
      unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
      bucket_sum_vals_[st][pa] = new float *[num_nt];
      bucket_denoms_[st][pa] = new float *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	bucket_sum_vals_[st][pa][i] = nullptr;
	bucket_denoms_[st][pa][i] = nullptr;
      }
    }
  }

  mutexes_ = new pthread_mutex_t **[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    mutexes_[st] = new pthread_mutex_t *[num_players];
    for (unsigned int pa = 0; pa < num_players; ++pa) {
      unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
      mutexes_[st][pa] = new pthread_mutex_t[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	pthread_mutex_init(&mutexes_[st][pa][i], NULL);
      }
    }
  }
}

SampledBCFRBuilder::~SampledBCFRBuilder(void) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int st = 0; st <= max_street; ++st) {
    for (unsigned int pa = 0; pa < num_players; ++pa) {
      unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
      for (unsigned int i = 0; i < num_nt; ++i) {
	pthread_mutex_destroy(&mutexes_[st][pa][i]);
      }
      delete [] mutexes_[st][pa];
    }
    delete [] mutexes_[st];
  }
  delete [] mutexes_;
  for (unsigned int st = 0; st <= max_street; ++st) {
    for (unsigned int pa = 0; pa < num_players; ++pa) {
      unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
      for (unsigned int i = 0; i < num_nt; ++i) {
	delete [] bucket_sum_vals_[st][pa][i];
	delete [] bucket_denoms_[st][pa][i];
      }
      delete [] bucket_sum_vals_[st][pa];
      delete [] bucket_denoms_[st][pa];
    }
    delete [] bucket_sum_vals_[st];
    delete [] bucket_denoms_[st];
  }
  delete [] bucket_sum_vals_;
  delete [] bucket_denoms_;
  delete trunk_hand_tree_;
}

void SampledBCFRBuilder::Write(Node *node, string *action_sequences) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int pa = node->PlayerActing();
  if (pa == p_) {
    unsigned int nt = node->NonterminalID();
    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s/sbcfrs.%u.p%u/%u",
	    Files::NewCFRBase(), Game::GameName().c_str(), Game::NumPlayers(),
	    card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	    Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction_.BettingAbstractionName().c_str(), 
	    cfr_config_.CFRConfigName().c_str(), it_, p_, st);
    string path = dir;
    for (unsigned int st1 = 0; st1 <= st; ++st1) {
      path += "/";
      path += action_sequences[st1];
      if (st1 < st) {
	Mkdir(path.c_str());
      }
    }
    Writer writer(path.c_str());

    unsigned int num_buckets = buckets_.NumBuckets(st);
    if (bucket_sum_vals_[st][pa][nt] == nullptr) {
      // No data on this bucket.  Just write out zeroes.
      for (unsigned int b = 0; b < num_buckets; ++b) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  writer.WriteFloat(0);
	}
      }
    } else {
      for (unsigned int b = 0; b < num_buckets; ++b) {
	if (st == 0 && pa == 1 && nt == 0) {
	  printf("Root b %u:", b);
	}
	for (unsigned int s = 0; s < num_succs; ++s) {
	  double val = bucket_sum_vals_[st][pa][nt][b * num_succs + s];
	  double denom = bucket_denoms_[st][pa][nt][b * num_succs + s];
	  float avg_val = val / denom;
	  if (st == 0 && pa == 1 && nt == 0) {
	    printf(" %f", avg_val);
	  }
	  writer.WriteFloat(avg_val);
	}
	if (st == 0 && pa == 1 && nt == 0) {
	  printf("\n");
	  fflush(stdout);
	}
      }
    }
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    string old = action_sequences[st];
    action_sequences[st] += action;
    Write(node->IthSucc(s), action_sequences);
    action_sequences[st] = old;
  }
}

double *SampledBCFRBuilder::Showdown(Node *node, const CanonicalCards *hands,
				     double *opp_probs, double sum_opp_probs,
				     double *total_card_probs,
				     double **ret_norms) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  double cum_prob = 0;
  double cum_card_probs[52];
  for (Card c = 0; c < max_card1; ++c) cum_card_probs[c] = 0;
  unsigned int num_hole_card_pairs = hands->NumRaw();
  double *win_probs = new double[num_hole_card_pairs];
  double half_pot = node->LastBetTo();
  double *vals = new double[num_hole_card_pairs];
  double *norms = new double[num_hole_card_pairs];

  unsigned int j = 0;
  while (j < num_hole_card_pairs) {
    unsigned int last_hand_val = hands->HandValue(j);
    unsigned int begin_range = j;
    // Make three passes through the range of equally strong hands
    // First pass computes win counts for each hand and finds end of range
    // Second pass updates cumulative counters
    // Third pass computes lose counts for each hand
    while (j < num_hole_card_pairs) {
      unsigned int hand_val = hands->HandValue(j);
      if (hand_val != last_hand_val) break;
      const Card *cards = hands->Cards(j);
      Card hi = cards[0];
      Card lo = cards[1];
      win_probs[j] = cum_prob - cum_card_probs[hi] - cum_card_probs[lo];
      ++j;
    }
    // Positions begin_range...j-1 (inclusive) all have the same hand value
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double prob = opp_probs[enc];
      cum_card_probs[hi] += prob;
      cum_card_probs[lo] += prob;
      cum_prob += prob;
    }
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      double better_hi_prob = total_card_probs[hi] - cum_card_probs[hi];
      double better_lo_prob = total_card_probs[lo] - cum_card_probs[lo];
      double lose_prob = (sum_opp_probs - cum_prob) -
	better_hi_prob - better_lo_prob;
      vals[k] = (win_probs[k] - lose_prob) * half_pot;
      unsigned int enc = hi * max_card1 + lo;
      norms[k] =
	(sum_opp_probs - total_card_probs[hi] - total_card_probs[lo]) +
	opp_probs[enc];
    }
  }

  delete [] win_probs;

  *ret_norms = norms;
  return vals;
}

double *SampledBCFRBuilder::Fold(Node *node, unsigned int p,
				 const CanonicalCards *hands,
				 double *opp_probs, double sum_opp_probs,
				 double *total_card_probs,
				 double **ret_norms) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  // Sign of half_pot reflects who wins the pot
  double half_pot;
  // Player acting encodes player remaining at fold nodes
  // LastBetTo() doesn't include the last called bet
  if (p == node->PlayerActing()) {
    half_pot = node->LastBetTo();
  } else {
    half_pot = -(double)node->LastBetTo();
  }
  unsigned int num_hole_card_pairs = hands->NumRaw();
  double *vals = new double[num_hole_card_pairs];
  double *norms = new double[num_hole_card_pairs];

  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double opp_prob = opp_probs[enc];
    norms[i] =
      (sum_opp_probs - total_card_probs[hi] - total_card_probs[lo]) +
      opp_prob;
    vals[i] = half_pot * norms[i];
  }

  *ret_norms = norms;
  return vals;
}

double *SampledBCFRBuilder::OurChoice(Node *node, unsigned int lbd,
				      const VCFRState &state,
				      double **ret_norms) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(),
					    st, lbd);
  double **succ_vals = new double *[num_succs];
  double **succ_norms = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    VCFRState succ_state(state, node, s);
    succ_vals[s] = Process(node->IthSucc(s), lbd, succ_state, st,
			   &succ_norms[s]);
  }

  unsigned int board_count;
  if (st < sample_st_) {
    board_count = BoardTree::BoardCount(st, gbd);
  } else {
    // This assumes st == max_street
    unsigned int pgbd = BoardTree::PredBoard(gbd, sample_st_ - 1);
    board_count = BoardTree::BoardCount(sample_st_ - 1, pgbd);
  }
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);

  // Mutex protect the updating of bucket_sum_vals_ and bucket_denoms_.
  pthread_mutex_lock(&mutexes_[st][pa][nt]);
  if (bucket_sum_vals_[st][pa][nt] == nullptr) {
    unsigned int num_buckets = buckets_.NumBuckets(st);
    unsigned int num = num_buckets * num_succs;
    bucket_sum_vals_[st][pa][nt] = new float[num];
    bucket_denoms_[st][pa][nt] = new float[num];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	bucket_sum_vals_[st][pa][nt][b * num_succs + s] = 0;
	bucket_denoms_[st][pa][nt][b * num_succs + s] = 0;
      }
    }
  }
  unsigned int **street_buckets = state.StreetBuckets();
  for (unsigned int s = 0; s < num_succs; ++s) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      // Need to scale by board count, right?
      bucket_sum_vals_[st][pa][nt][b * num_succs + s] +=
	succ_vals[s][i] * board_count;
      bucket_denoms_[st][pa][nt][b * num_succs + s] +=
	board_count * succ_norms[s][i];
    }
  }
  pthread_mutex_unlock(&mutexes_[st][pa][nt]);

  *ret_norms = succ_norms[0];
  succ_norms[0] = nullptr;
  double *vals;
  if (num_succs == 1) {
    vals = succ_vals[0];
    succ_vals[0] = nullptr;
  } else {
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;

    double *current_probs = new double[num_succs];
    unsigned int default_succ_index = node->DefaultSuccIndex();
    double *d_all_cs_vals = nullptr;
    int *i_all_cs_vals = nullptr;
    if (sumprobs_->Ints(p_, st)) {
      sumprobs_->Values(p_, st, nt, &i_all_cs_vals);
    } else {
      sumprobs_->Values(p_, st, nt, &d_all_cs_vals);
    }
    bool nonneg = true;
    // Don't want to impose exploration when working off of sumprobs.
    double explore = 0;
    unsigned int num_nonterminal_succs = 0;
    bool *nonterminal_succs = new bool[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (node->IthSucc(s)->Terminal()) {
	nonterminal_succs[s] = false;
      } else {
	nonterminal_succs[s] = true;
	++num_nonterminal_succs;
      }
    }
    if (i_all_cs_vals) {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int b = street_buckets[st][i];
	int *my_cs_vals = i_all_cs_vals + b * num_succs;
	RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
		       default_succ_index, explore,
		       num_nonterminal_succs, nonterminal_succs,
		       current_probs);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  vals[i] += succ_vals[s][i] * current_probs[s];
	}
      }
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int b = street_buckets[st][i];
	double *my_cs_vals = d_all_cs_vals + b * num_succs;
	RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
		       default_succ_index, explore,
		       num_nonterminal_succs, nonterminal_succs,
		       current_probs);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  vals[i] += succ_vals[s][i] * current_probs[s];
	}
      }
    }
    delete [] current_probs;
    delete [] nonterminal_succs;
  }
  
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_vals[s];
    delete [] succ_norms[s];
  }
  delete [] succ_vals;
  delete [] succ_norms;

  return vals;
}

double *SampledBCFRBuilder::OppChoice(Node *node, unsigned int lbd,
				      const VCFRState &state,
				      double **ret_norms) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;

  double *opp_probs = state.OppProbs();
  double **succ_opp_probs = new double *[num_succs];
  if (num_succs == 1) {
    succ_opp_probs[0] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) {
      succ_opp_probs[0][i] = opp_probs[i];
    }
  } else {
    unsigned int **street_buckets = state.StreetBuckets();
    unsigned int nt = node->NonterminalID();
    unsigned int opp = p_^1;
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_opp_probs[s] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) succ_opp_probs[s][i] = 0;
    }

    // The "all" values point to the values for all hands.
    double *d_all_current_probs = nullptr;
    double *d_all_cs_vals = nullptr;
    int *i_all_cs_vals = nullptr;

    double explore;
    if (value_calculation_ && ! br_current_) explore = 0;
    else                                     explore = explore_;

    bool bucketed = ! buckets_.None(st) &&
      node->LastBetTo() < card_abstraction_.BucketThreshold(st);

    if (bucketed && ! value_calculation_) {
      current_strategy_->Values(opp, st, nt, &d_all_current_probs);
    } else {
      // cs_vals are the current strategy values; i.e., the values we pass into
      // RegretsToProbs() in order to get the current strategy.  In VCFR, these
      // are regrets; in a best-response calculation, they are (normally)
      // sumprobs.

      if (value_calculation_ && ! br_current_) {
	if (sumprobs_->Ints(opp, st)) {
	  sumprobs_->Values(opp, st, nt, &i_all_cs_vals);
	} else {
	  sumprobs_->Values(opp, st, nt, &d_all_cs_vals);
	}
      } else {
	if (regrets_->Ints(opp, st)) {
	  regrets_->Values(opp, st, nt, &i_all_cs_vals);
	} else {
	  regrets_->Values(opp, st, nt, &d_all_cs_vals);
	}
      }
    }

    // The "all" values point to the values for all hands.
    double *d_all_sumprobs = nullptr;
    int *i_all_sumprobs = nullptr;
    // sumprobs_->Players(opp) check is there because in asymmetric systems
    // (e.g., endgame solving with CFR-D method) we are only saving probs for
    // one player.
    // Don't update sumprobs during pre phase
    if (! pre_phase_ && ! value_calculation_ && sumprob_streets_[st] &&
	sumprobs_->Players(opp)) {
      if (sumprobs_->Ints(opp, st)) {
	sumprobs_->Values(opp, st, nt, &i_all_sumprobs);
      } else {
	sumprobs_->Values(opp, st, nt, &d_all_sumprobs);
      }
    }

    // These values will point to the values for the current board
    double *d_cs_vals = nullptr, *d_sumprobs = nullptr;
    int *i_cs_vals = nullptr, *i_sumprobs = nullptr;

    if (bucketed) {
      i_cs_vals = i_all_cs_vals;
      d_cs_vals = d_all_cs_vals;
      i_sumprobs = i_all_sumprobs;
      d_sumprobs = d_all_sumprobs;
    } else {
      if (i_all_cs_vals) {
	i_cs_vals = i_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
      } else {
	d_cs_vals = d_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
      }
      if (i_all_sumprobs) {
	i_sumprobs = i_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
      if (d_all_sumprobs) {
	d_sumprobs = d_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
    }

    bool nonneg;
    if (value_calculation_ && ! br_current_) {
      nonneg = true;
    } else {
      nonneg = nn_regrets_ && regret_floors_[st] >= 0;
    }
    // No sumprob update if a) doing value calculation (e.g., RGBR), b)
    // we have no sumprobs (e.g., in asymmetric CFR), or c) we are during
    // the hard warmup period
    if (bucketed && ! value_calculation_) {
      if (d_sumprobs) {
	// Double sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || d_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				opp_probs, succ_opp_probs,
				d_all_current_probs, d_sumprobs);
      } else {
	// Int sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || i_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				sumprob_scaling_, opp_probs, succ_opp_probs,
				d_all_current_probs, i_sumprobs);
      }
    } else {
      if (i_cs_vals) {
	if (d_sumprobs) {
	  // Int regrets, double sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || d_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			  uniform_, explore, it_, soft_warmup_, hard_warmup_,
			  update_sumprobs, opp_probs, succ_opp_probs,
			  i_cs_vals, d_sumprobs);
	} else {
	  // Int regrets and sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || i_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			  uniform_, explore, prob_method_, it_, soft_warmup_,
			  hard_warmup_, update_sumprobs, sumprob_scaling_,
			  opp_probs, succ_opp_probs, i_cs_vals, i_sumprobs);
	}
      } else {
	// Double regrets and sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || d_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			uniform_, explore, it_, soft_warmup_, hard_warmup_,
			update_sumprobs, opp_probs, succ_opp_probs,
			d_cs_vals, d_sumprobs);
      }
    }
  }

  double *vals = nullptr;
  double *norms = nullptr;
  double succ_sum_opp_probs;
  for (unsigned int s = 0; s < num_succs; ++s) {
    double *succ_total_card_probs = new double[max_card1];
    CommonBetResponseCalcs(st, hands, succ_opp_probs[s], &succ_sum_opp_probs,
			   succ_total_card_probs);
    if (prune_ && succ_sum_opp_probs == 0) {
      delete [] succ_total_card_probs;
      continue;
    }
    VCFRState succ_state(state, node, s, succ_opp_probs[s], succ_sum_opp_probs,
			 succ_total_card_probs);
    double *succ_norms;
    double *succ_vals = Process(node->IthSucc(s), lbd, succ_state, st,
				&succ_norms);
    if (vals == nullptr) {
      vals = succ_vals;
      norms = succ_norms;
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	vals[i] += succ_vals[i];
	norms[i] += succ_norms[i];
      }
      delete [] succ_vals;
      delete [] succ_norms;
    }
    delete [] succ_total_card_probs;
  }
  if (vals == nullptr) {
    // This can happen if there were non-zero opp probs on the prior street,
    // but the board cards just dealt blocked all the opponent hands with
    // non-zero probability.
    vals = new double[num_hole_card_pairs];
    norms = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      vals[i] = 0;
      norms[i] = 0;
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_opp_probs[s];
  }
  delete [] succ_opp_probs;

  *ret_norms = norms;
  return vals;
}

class SBCFRThread {
public:
  SBCFRThread(SampledBCFRBuilder *builder, unsigned int thread_index,
	      unsigned int num_threads, Node *node,
	      const string &action_sequence, double *opp_probs,
	      const HandTree *hand_tree, unsigned int p,
	      unsigned int *prev_canons);
  ~SBCFRThread(void);
  void Run(void);
  void Join(void);
  void Go(void);
  double *RetVals(void) const {return ret_vals_;}
  double *RetNorms(void) const {return ret_norms_;}
private:
  SampledBCFRBuilder *builder_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  Node *node_;
  const string &action_sequence_;
  double *opp_probs_;
  const HandTree *hand_tree_;
  unsigned int p_;
  unsigned int *prev_canons_;
  double *ret_vals_;
  double *ret_norms_;
  pthread_t pthread_id_;
};

SBCFRThread::SBCFRThread(SampledBCFRBuilder *builder,
			 unsigned int thread_index, unsigned int num_threads,
			 Node *node, const string &action_sequence,
			 double *opp_probs, const HandTree *hand_tree,
			 unsigned int p, unsigned int *prev_canons) :
  action_sequence_(action_sequence) {
  builder_ = builder;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
  node_ = node;
  opp_probs_ = opp_probs;
  hand_tree_ = hand_tree;
  p_ = p;
  prev_canons_ = prev_canons;
}

SBCFRThread::~SBCFRThread(void) {
  delete [] ret_vals_;
  delete [] ret_norms_;
}

static void *sbcfr_thread_run(void *v_t) {
  SBCFRThread *t = (SBCFRThread *)v_t;
  t->Go();
  return NULL;
}

void SBCFRThread::Run(void) {
  pthread_create(&pthread_id_, NULL, sbcfr_thread_run, this);
}

void SBCFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

void SBCFRThread::Go(void) {
  unsigned int st = node_->Street();
  unsigned int pst = node_->Street() - 1;
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_prev_hole_card_pairs = Game::NumHoleCardPairs(pst);
  Card max_card1 = Game::MaxCard() + 1;
  ret_vals_ = new double[num_prev_hole_card_pairs];
  ret_norms_ = new double[num_prev_hole_card_pairs];
  for (unsigned int i = 0; i < num_prev_hole_card_pairs; ++i) {
    ret_vals_[i] = 0;
    ret_norms_[i] = 0;
  }
  for (unsigned int bd = thread_index_; bd < num_boards; bd += num_threads_) {
    unsigned int **street_buckets = AllocateStreetBuckets();
    VCFRState state(opp_probs_, hand_tree_, bd, action_sequence_, 0, 0,
		    street_buckets, p_);
    // Initialize buckets for this street
    builder_->SetStreetBuckets(st, bd, state);
    double *bd_norms;
    double *bd_vals = builder_->Process(node_, bd, state, st, &bd_norms);
    const CanonicalCards *hands = hand_tree_->Hands(st, bd);
    unsigned int board_variants = BoardTree::NumVariants(st, bd);
    unsigned int num_hands = hands->NumRaw();
    for (unsigned int h = 0; h < num_hands; ++h) {
      const Card *cards = hands->Cards(h);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      unsigned int prev_canon = prev_canons_[enc];
      ret_vals_[prev_canon] += board_variants * bd_vals[h];
      ret_norms_[prev_canon] += board_variants * bd_norms[h];
    }
    delete [] bd_vals;
    delete [] bd_norms;
    DeleteStreetBuckets(street_buckets);
  }
}

// Divide work at a street-initial node between multiple threads.  Spawns
// the threads, joins them, aggregates the resulting CVs.
// Only support splitting on the flop for now.
// Ugly that we pass prev_canons in.
void SampledBCFRBuilder::Split(Node *node, double *opp_probs,
			       const HandTree *hand_tree,
			       const string &action_sequence,
			       unsigned int *prev_canons,
			       double *vals, double *norms) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) {
    vals[i] = 0;
    norms[i] = 0;
  }
  unique_ptr<SBCFRThread * []> threads(new SBCFRThread *[num_threads_]);
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads[t] = new SBCFRThread(this, t, num_threads_, node, action_sequence,
				 opp_probs, hand_tree, p_, prev_canons);
  }
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Run();
  }
  // Do first thread in main thread
  threads[0]->Go();
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Join();
  }
  for (unsigned int t = 0; t < num_threads_; ++t) {
    double *t_vals = threads[t]->RetVals();
    double *t_norms = threads[t]->RetNorms();
    for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) {
      vals[i] += t_vals[i];
      norms[i] += t_norms[i];
    }
    delete threads[t];
  }
}

double *SampledBCFRBuilder::StreetInitial(Node *node, unsigned int plbd,
					  const VCFRState &state,
					  double **ret_norms) {
  unsigned int nst = node->Street();
  if (nst == 1) {
    fprintf(stderr, "%s\n", state.ActionSequence().c_str());
  }
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  const CanonicalCards *pred_hands = state.GetHandTree()->Hands(pst, plbd);
  Card max_card = Game::MaxCard();
  unsigned int num_enc = (max_card + 1) * (max_card + 1);
  unsigned int *prev_canons = new unsigned int[num_enc];
  double *vals = new double[prev_num_hole_card_pairs];
  double *norms = new double[prev_num_hole_card_pairs];
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) {
    vals[i] = 0;
    norms[i] = 0;
  }
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_enc = prev_cards[0] * (max_card + 1) + prev_cards[1];
      prev_canons[prev_enc] = ph;
    }
  }
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_enc = prev_cards[0] * (max_card + 1) + prev_cards[1];
      unsigned int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_enc] = pc;
    }
  }

  if (nst == 1 && subgame_street_ == kMaxUInt && num_threads_ > 1) {
    // Currently only flop supported
    Split(node, state.OppProbs(), state.GetHandTree(), state.ActionSequence(),
	  prev_canons, vals, norms);
  } else if (nst == sample_st_) {
    if (Game::NumCardsForStreet(nst) != 1) {
      fprintf(stderr, "Expect to sample only on street with one card\n");
      exit(-1);
    }
    // Sample just one board
    // First, select a river card at random
    unsigned int pgbd = BoardTree::GlobalIndex(state.RootBdSt(),
					       state.RootBd(), pst, plbd);
    const Card *prev_board = BoardTree::Board(pst, pgbd);
    unsigned int num_prev_board_cards = Game::NumBoardCards(pst);
    // I used to seed the RNG to the previous board.  This guarantees we will
    // always sample the same river card for a given turn board (no matter the
    // betting state).  The alternative, which we do now, is to sample
    // independently at each betting state.  The danger of this is that we
    // might sample a favorable river card after one turn action, but a less
    // favorable river card after a different turn action.  So the first
    // turn action looks better, but it is really just sampling variance.
    //
    // Having said that, sampling independently seems to lead to lower variance
    // for preflop CV estimates.
    struct drand48_data rand_buf;
    srand48_r(pgbd, &rand_buf);
    // SeedRand(pgbd);

    // I sample N *distinct* river cards.
    unique_ptr<Card []> rivers(new Card[num_to_sample_]);
    for (unsigned int i = 0; i < num_to_sample_; ++i) {
      unsigned int num_remaining =
	Game::NumCardsInDeck() - num_prev_board_cards - i;
      // unsigned int r = RandBetween(0, num_remaining - 1);
      double rnd;
      drand48_r(&rand_buf, &rnd);
      unsigned int r = rnd * num_remaining;
      unsigned int num = 0;
      Card river = 0;
      Card max_card = Game::MaxCard();
      for (river = 0; river <= max_card; ++river) {
	if (! InCards(river, prev_board, num_prev_board_cards) &&
	    ! InCards(river, rivers.get(), i)) {
	  if (num == r) {
	    rivers[i] = river;
	    break;
	  }
	  ++num;
	}
      }
      if (river > max_card) {
	fprintf(stderr, "Didn't find river?!?\n");
	exit(-1);
      }
    }
    Card board[5], canon_board[5];
    for (unsigned int i = 0; i < num_prev_board_cards; ++i) {
      board[i] = prev_board[i];
    }
    for (unsigned int i = 0; i < num_to_sample_; ++i) {
      Card river = rivers[i];
      board[num_prev_board_cards] = river;
      CanonicalizeCards(board, nullptr, nst, canon_board, nullptr);
      unsigned int ngbd = BoardTree::LookupBoard(canon_board, nst);
      HandTree hand_tree(nst, ngbd, Game::MaxStreet());
      unsigned int **street_buckets = AllocateStreetBuckets();
      VCFRState new_state(state.OppProbs(), &hand_tree, 0,
			  state.ActionSequence(), ngbd, nst, street_buckets,
			  p_);
      SetStreetBuckets(nst, ngbd, new_state);
      double *next_norms;
      double *next_vals = Process(node, 0, new_state, nst, &next_norms);
      DeleteStreetBuckets(street_buckets);

      const CanonicalCards *hands = hand_tree.Hands(nst, 0);
      unsigned int num_next_hands = hands->NumRaw();
      for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * (max_card + 1) + lo;
	unsigned int prev_canon = prev_canons[enc];
	// We do not scale by the number of board variants (as we do below).
	// Note that we sample each raw river card with equal probability.
	vals[prev_canon] += next_vals[nh];
	norms[prev_canon] += next_norms[nh];
      }
      delete [] next_vals;
      delete [] next_norms;
    }
  } else {
    unsigned int pgbd = BoardTree::GlobalIndex(state.RootBdSt(),
					       state.RootBd(), pst, plbd);
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      unsigned int nlbd = BoardTree::LocalIndex(state.RootBdSt(),
						state.RootBd(), nst, ngbd);
      SetStreetBuckets(nst, ngbd, state);
      // I can pass unset values for sum_opp_probs and total_card_probs.  I
      // know I will come across an opp choice node before getting to a terminal
      // node.
      double *next_norms;
      double *next_vals = Process(node, nlbd, state, nst, &next_norms);

      unsigned int board_variants = BoardTree::NumVariants(nst, ngbd);
      const CanonicalCards *hands = state.GetHandTree()->Hands(nst, nlbd);
      unsigned int num_next_hands = hands->NumRaw();
      for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * (max_card + 1) + lo;
	unsigned int prev_canon = prev_canons[enc];
	vals[prev_canon] += board_variants * next_vals[nh];
	norms[prev_canon] += board_variants * next_norms[nh];
      }
      delete [] next_vals;
      delete [] next_norms;
    }
  }
  // Scale down the values of the previous-street canonical hands
  double scale_up;
  if (nst >= sample_st_) {
    unsigned int num_remaining =
      Game::NumCardsInDeck() - Game::NumBoardCards(pst);
    scale_up = ((double)num_remaining) / (double)num_to_sample_;
  } else {
    scale_up = 1.0;
  }
  double scale_down = Game::StreetPermutations(nst);
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    unsigned int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      vals[ph] = vals[ph] * scale_up / (scale_down * prev_hand_variants);
      norms[ph] = norms[ph] * scale_up / (scale_down * prev_hand_variants);
    }
  }
  // Copy the canonical hand values to the non-canonical
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      vals[ph] = vals[prev_canons[pred_hands->Canon(ph)]];
      norms[ph] = norms[prev_canons[pred_hands->Canon(ph)]];
    }
  }

  delete [] prev_canons;
  *ret_norms = norms;
  return vals;
}

double *SampledBCFRBuilder::Process(Node *node, unsigned int lbd,
				    const VCFRState &state,
				    unsigned int last_st, double **norms) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    if (node->NumRemaining() == 1) {
      return Fold(node, p_, state.GetHandTree()->Hands(st, lbd),
		  state.OppProbs(), state.SumOppProbs(),
		  state.TotalCardProbs(), norms);
    } else {
      return Showdown(node, state.GetHandTree()->Hands(st, lbd),
		      state.OppProbs(), state.SumOppProbs(),
		      state.TotalCardProbs(), norms);
    }
  }
  if (st > last_st) {
    return StreetInitial(node, lbd, state, norms);
  }
  if (node->PlayerActing() == p_) {
    return OurChoice(node, lbd, state, norms);
  } else {
    return OppChoice(node, lbd, state, norms);
  }
}

void SampledBCFRBuilder::Go(void) {
  time_t start_t = time(NULL);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);

  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, trunk_hand_tree_, p_);
  SetStreetBuckets(0, 0, state);
  double *norms;
  double *vals = Process(betting_tree_->Root(), 0, state, 0, &norms);
  delete [] norms;
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;

  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_cards_in_deck = Game::NumCardsInDeck();
  unsigned int num_remaining = num_cards_in_deck - num_hole_cards;
  unsigned int num_opp_hole_card_pairs;
  if (num_hole_cards == 1) {
    num_opp_hole_card_pairs = num_remaining;
  } else {
    num_opp_hole_card_pairs = num_remaining * (num_remaining - 1) / 2;
  }
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i] / num_opp_hole_card_pairs;
  }
  double ev = sum / num_hole_card_pairs;
  fprintf(stderr, "P%u EV: %f\n", p_, ev);

  delete [] vals;

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Processing took %.1f seconds\n", diff_sec);

  start_t = time(NULL);
  unsigned int max_street = Game::MaxStreet();
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s/sbcfrs.%u.p%u",
	  Files::NewCFRBase(), Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str(), it_, p_);
  Mkdir(dir);
  char buf[500];
  for (unsigned int st = 0; st <= max_street; ++st) {
    sprintf(buf, "%s/%u", dir, st);
    Mkdir(buf);
  }
  unique_ptr<string []> action_sequences(new string[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    action_sequences[st] = "x";
  }
  Write(betting_tree_->Root(), action_sequences.get());
  end_t = time(NULL);
  diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Writing took %.1f seconds\n", diff_sec);
}
