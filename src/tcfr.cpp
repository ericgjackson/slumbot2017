// Should initialize buckets outside for either all streets or all streets
// but river if using HVB.
//
// Targeted CFR.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep()

#include <algorithm>
#include <string>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "nonterminal_ids.h"
#include "rand.h"
#include "regret_compression.h"
#include "split.h"
#include "tcfr.h"

using namespace std;

#define T_REGRET unsigned int
#define T_VALUE int
#define T_SUM_PROB unsigned int

static const unsigned int kNumPregenRNGs = 10000000;

class TCFRThread {
public:
  TCFRThread(const BettingAbstraction &ba, const CFRConfig &cc,
	     const Buckets &buckets, unsigned int batch_index,
	     unsigned int num_threads, unsigned char *data,
	     unsigned int target_player, float *rngs, unsigned int *uncompress,
	     unsigned int *short_uncompress, unsigned int *pruning_thresholds,
	     bool *sumprob_streets, unsigned char *hvb_table,
	     unsigned char ***cards_to_indices, unsigned int batch_size);
  ~TCFRThread(void);
  void RunThread(void);
  void Join(void);
  void Run(void);
  unsigned int BatchIndex(void) const {return batch_index_;}
  unsigned long long int ProcessCount(void) const {return process_count_;}
  unsigned long long int FullProcessCount(void) const {
    return full_process_count_;
  }
private:
  static const unsigned int kStackDepth = 50;
  static const unsigned int kMaxSuccs = 50;

  T_VALUE Process(unsigned char *ptr);
  bool HVBDealHand(void);
  bool NoHVBDealHand(void);

  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unsigned int batch_index_;
  unsigned int num_threads_;
  unsigned char *data_;
  bool asymmetric_;
  unsigned int target_player_;
  bool p1_phase_;
  char p1_showdown_;
  unsigned int *canon_bds_;
  unsigned int *p1_buckets_;
  unsigned int *p2_buckets_;
  int showdown_value_;
  pthread_t pthread_id_;
  T_VALUE **succ_value_stack_;
  int **succ_iregret_stack_;
  unsigned int stack_index_;
  double explore_;
  unsigned int *sumprob_ceilings_;
  unsigned long long int it_;
  float *rngs_;
  unsigned int rng_index_;
  bool *quantized_streets_;
  bool *short_quantized_streets_;
  bool *scaled_streets_;
  bool full_only_avg_update_;
  unsigned int *uncompress_;
  unsigned int *short_uncompress_;
  unsigned int *pruning_thresholds_;
  bool *sumprob_streets_;
  unsigned char *hvb_table_;
  unsigned long long int bytes_per_hand_;
  unsigned char ***cards_to_indices_;
  unsigned int max_street_;
  bool all_full_;
  bool *full_;
  unsigned int close_threshold_;
  unsigned long long int process_count_;
  unsigned long long int full_process_count_;
  unsigned int active_mod_;
  unsigned int num_active_conditions_;
  unsigned int *num_active_streets_;
  unsigned int *num_active_rems_;
  unsigned int **active_streets_;
  unsigned int **active_rems_;
  unsigned int batch_size_;
  struct drand48_data rand_buf_;
  int board_count_;
};

TCFRThread::TCFRThread(const BettingAbstraction &ba, const CFRConfig &cc,
		       const Buckets &buckets, unsigned int batch_index,
		       unsigned int num_threads, unsigned char *data,
		       unsigned int target_player, float *rngs,
		       unsigned int *uncompress, unsigned int *short_uncompress,
		       unsigned int *pruning_thresholds, bool *sumprob_streets,
		       unsigned char *hvb_table,
		       unsigned char ***cards_to_indices,
		       unsigned int batch_size) :
  betting_abstraction_(ba), cfr_config_(cc), buckets_(buckets) {
  batch_index_ = batch_index;
  num_threads_ = num_threads;
  data_ = data;
  asymmetric_ = betting_abstraction_.Asymmetric();
  target_player_ = target_player;
  rngs_ = rngs;
  rng_index_ = RandZeroToOne() * kNumPregenRNGs;
  uncompress_ = uncompress;
  short_uncompress_ = short_uncompress;
  pruning_thresholds_ = pruning_thresholds;
  sumprob_streets_ = sumprob_streets;
  hvb_table_ = hvb_table;
  cards_to_indices_ = cards_to_indices;
  batch_size_ = batch_size;
  
  max_street_ = Game::MaxStreet();
  quantized_streets_ = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    quantized_streets_[st] = false;
  }
  const vector<unsigned int> &qsv = cfr_config_.QuantizedStreets();
  unsigned int num_qsv = qsv.size();
  for (unsigned int i = 0; i < num_qsv; ++i) {
    unsigned int st = qsv[i];
    quantized_streets_[st] = true;
  }
  short_quantized_streets_ = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    short_quantized_streets_[st] = false;
  }
  const vector<unsigned int> &sqsv = cfr_config_.ShortQuantizedStreets();
  unsigned int num_sqsv = sqsv.size();
  for (unsigned int i = 0; i < num_sqsv; ++i) {
    unsigned int st = sqsv[i];
    short_quantized_streets_[st] = true;
  }
  scaled_streets_ = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    scaled_streets_[st] = false;
  }
  const vector<unsigned int> &ssv = cfr_config_.ScaledStreets();
  unsigned int num_ssv = ssv.size();
  for (unsigned int i = 0; i < num_ssv; ++i) {
    unsigned int st = ssv[i];
    scaled_streets_[st] = true;
  }
  explore_ = cfr_config_.Explore();
  full_only_avg_update_ = true; // cfr_config_.FullOnlyAvgUpdate();
  canon_bds_ = new unsigned int[max_street_ + 1];
  canon_bds_[0] = 0;
  p1_buckets_ = new unsigned int[max_street_ + 1];
  p2_buckets_ = new unsigned int[max_street_ + 1];
  succ_value_stack_ = new T_VALUE *[kStackDepth];
  succ_iregret_stack_ = new int *[kStackDepth];
  for (unsigned int i = 0; i < kStackDepth; ++i) {
    succ_value_stack_[i] = new T_VALUE[kMaxSuccs];
    succ_iregret_stack_[i] = new int[kMaxSuccs];
  }
  if (cfr_config_.NNR()) {
    fprintf(stderr, "NNR not supported\n");
    exit(-1);
  }
  const vector<int> &fv = cfr_config_.RegretFloors();
  if (fv.size() > 0) {
    fprintf(stderr, "Regret floors not supported\n");
    exit(-1);
  }
  sumprob_ceilings_ = new unsigned int[max_street_ + 1];
  const vector<unsigned int> &cv = cfr_config_.SumprobCeilings();
  if (cv.size() == 0) {
    for (unsigned int s = 0; s <= max_street_; ++s) {
      // Allow a little headroom to avoid overflow
      sumprob_ceilings_[s] = 4000000000U;
    }
  } else {
    if (cv.size() != max_street_ + 1) {
      fprintf(stderr, "Sumprob ceiling vector wrong size\n");
      exit(-1);
    }
    for (unsigned int s = 0; s <= max_street_; ++s) {
      sumprob_ceilings_[s] = cv[s];
    }
  }

  if (hvb_table_) {
    bytes_per_hand_ = 4ULL;
    if (buckets_.NumBuckets(max_street_) <= 65536) bytes_per_hand_ += 2;
    else                                           bytes_per_hand_ += 4;
  } else {
    bytes_per_hand_ = 0ULL;
  }

  full_ = new bool[max_street_ + 1];
  close_threshold_ = cfr_config_.CloseThreshold();

  active_mod_ = cfr_config_.ActiveMod();
  if (active_mod_ == 0) {
    fprintf(stderr, "Must set ActiveMod\n");
    exit(-1);
  }
  num_active_conditions_ = cfr_config_.NumActiveConditions();
  if (num_active_conditions_ > 0) {
    num_active_streets_ = new unsigned int[num_active_conditions_];
    num_active_rems_ = new unsigned int[num_active_conditions_];
    active_streets_ = new unsigned int *[num_active_conditions_];
    active_rems_ = new unsigned int *[num_active_conditions_];
    for (unsigned int c = 0; c < num_active_conditions_; ++c) {
      num_active_streets_[c] = cfr_config_.NumActiveStreets(c);
      num_active_rems_[c] = cfr_config_.NumActiveRems(c);
      active_streets_[c] = new unsigned int[num_active_streets_[c]];
      for (unsigned int i = 0; i < num_active_streets_[c]; ++i) {
	active_streets_[c][i] = cfr_config_.ActiveStreet(c, i);
      }
      active_rems_[c] = new unsigned int[num_active_rems_[c]];
      for (unsigned int i = 0; i < num_active_rems_[c]; ++i) {
	active_rems_[c][i] = cfr_config_.ActiveRem(c, i);
      }
    }
  } else {
    num_active_streets_ = NULL;
    num_active_rems_ = NULL;
    active_streets_ = NULL;
    active_rems_ = NULL;
  }

  srand48_r(batch_index_, &rand_buf_);
}

TCFRThread::~TCFRThread(void) {
  for (unsigned int c = 0; c < num_active_conditions_; ++c) {
    delete [] active_streets_[c];
    delete [] active_rems_[c];
  }
  delete [] num_active_streets_;
  delete [] num_active_rems_;
  delete [] active_streets_;
  delete [] active_rems_;
  delete [] full_;
  delete [] sumprob_ceilings_;
  delete [] quantized_streets_;
  delete [] short_quantized_streets_;
  delete [] scaled_streets_;
  for (unsigned int i = 0; i < kStackDepth; ++i) {
    delete [] succ_value_stack_[i];
    delete [] succ_iregret_stack_[i];
  }
  delete [] succ_value_stack_;
  delete [] succ_iregret_stack_;
  delete [] p1_buckets_;
  delete [] p2_buckets_;
  delete [] canon_bds_;
}

bool TCFRThread::HVBDealHand(void) {
  // For holdem5/mb2b1, 1b its takes about 71m.
  if (it_ == batch_size_ + 1) return false;
  unsigned int num_boards = BoardTree::NumBoards(max_street_);
  double r;
  drand48_r(&rand_buf_, &r);
  unsigned int msbd = r * num_boards;
  canon_bds_[max_street_] = msbd;
  board_count_ = BoardTree::BoardCount(max_street_, msbd);
  for (unsigned int st = 1; st < max_street_; ++st) {
    canon_bds_[st] = BoardTree::PredBoard(msbd, st);
  }
  const Card *board = BoardTree::Board(max_street_, msbd);
  unsigned int num_ms_board_cards = Game::NumBoardCards(max_street_);
  int end_cards = Game::MaxCard() + 1;

  Card c1, c2, c3, c4;
  while (true) {
    drand48_r(&rand_buf_, &r);
    c1 = end_cards * r;
    // c1 = RandBetween(0, max_card);
    if (InCards(c1, board, num_ms_board_cards)) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c2 = end_cards * r;
    // c2 = RandBetween(0, max_card);
    if (InCards(c2, board, num_ms_board_cards)) continue;
    if (c2 == c1) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c3 = end_cards * r;
    // c3 = RandBetween(0, max_card);
    if (InCards(c3, board, num_ms_board_cards)) continue;
    if (c3 == c1 || c3 == c2) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c4 = end_cards * r;
    // c4 = RandBetween(0, max_card);
    if (InCards(c4, board, num_ms_board_cards)) continue;
    if (c4 == c1 || c4 == c2 || c4 == c3) continue;
    break;
  }

  int p1_hic, p1_loc, p2_hic, p2_loc;
  if (c1 > c2) {p1_hic = c1; p1_loc = c2;}
  else         {p1_hic = c2; p1_loc = c1;}
  if (c3 > c4) {p2_hic = c3; p2_loc = c4;}
  else         {p2_hic = c4; p2_loc = c3;}

  unsigned int p1_hv = 0, p2_hv = 0;

  for (unsigned int st = 0; st <= max_street_; ++st) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int bd = canon_bds_[st];
    unsigned char p1_hi = cards_to_indices_[st][bd][p1_hic];
    unsigned char p1_li = cards_to_indices_[st][bd][p1_loc];
    unsigned char p2_hi = cards_to_indices_[st][bd][p2_hic];
    unsigned char p2_li = cards_to_indices_[st][bd][p2_loc];
    unsigned int base = bd * num_hole_card_pairs;
    // The sum from 1... hi_index - 1 is the number of hole card pairs
    // containing a high card less than hi.
    unsigned int p1_hcp = (p1_hi - 1) * p1_hi / 2 + p1_li;
    unsigned int p2_hcp = (p2_hi - 1) * p2_hi / 2 + p2_li;
    unsigned int p1h = base + p1_hcp;
    unsigned int p2h = base + p2_hcp;

    if (st == max_street_) {
      unsigned char *p1_ptr = &hvb_table_[p1h * bytes_per_hand_];
      unsigned char *p2_ptr = &hvb_table_[p2h * bytes_per_hand_];
      if (buckets_.NumBuckets(max_street_) <= 65536) {
	p1_buckets_[max_street_] = *(unsigned short *)p1_ptr;
	p2_buckets_[max_street_] = *(unsigned short *)p2_ptr;
	p1_ptr += 2;
	p2_ptr += 2;
      } else {
	p1_buckets_[max_street_] = *(unsigned int *)p1_ptr;
	p2_buckets_[max_street_] = *(unsigned int *)p2_ptr;
	p1_ptr += 4;
	p2_ptr += 4;
      }
      p1_hv = *(unsigned int *)p1_ptr;
      p2_hv = *(unsigned int *)p2_ptr;
    } else {
      p1_buckets_[st] = buckets_.Bucket(st, p1h);
      p2_buckets_[st] = buckets_.Bucket(st, p2h);
    }
  }

  if (p1_hv > p2_hv)      p1_showdown_ = 1;
  else if (p2_hv > p1_hv) p1_showdown_ = -1;
  else                    p1_showdown_ = 0;

  return true;
}

// Our old implementation which is a bit slower.
bool TCFRThread::NoHVBDealHand(void) {
  // For holdem5/mb2b1, 1b its takes about 71m.
  if (it_ == batch_size_ + 1) return false;
  unsigned int num_boards = BoardTree::NumBoards(max_street_);
  double r;
  drand48_r(&rand_buf_, &r);
  unsigned int msbd = r * num_boards;
  canon_bds_[max_street_] = msbd;
  board_count_ = BoardTree::BoardCount(max_street_, msbd);
  for (unsigned int st = 1; st < max_street_; ++st) {
    canon_bds_[st] = BoardTree::PredBoard(msbd, st);
  }
  const Card *board = BoardTree::Board(max_street_, msbd);
  Card cards[7];
  unsigned int num_board_cards = Game::NumBoardCards(max_street_);
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    cards[i+2] = board[i];
  }
  int end_cards = Game::MaxCard() + 1;
  unsigned int p1_hv = 0, p2_hv = 0;

  Card c1, c2, c3, c4;
  while (true) {
    drand48_r(&rand_buf_, &r);
    c1 = end_cards * r;
    // c1 = RandBetween(0, max_card);
    if (InCards(c1, board, num_board_cards)) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c2 = end_cards * r;
    // c2 = RandBetween(0, max_card);
    if (InCards(c2, board, num_board_cards)) continue;
    if (c2 == c1) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c3 = end_cards * r;
    // c3 = RandBetween(0, max_card);
    if (InCards(c3, board, num_board_cards)) continue;
    if (c3 == c1 || c3 == c2) continue;
    break;
  }
  while (true) {
    drand48_r(&rand_buf_, &r);
    c4 = end_cards * r;
    // c4 = RandBetween(0, max_card);
    if (InCards(c4, board, num_board_cards)) continue;
    if (c4 == c1 || c4 == c2 || c4 == c3) continue;
    break;
  }

  if (c1 > c2) {cards[0] = c1; cards[1] = c2;}
  else         {cards[0] = c2; cards[1] = c1;}

  for (unsigned int s = 0; s <= max_street_; ++s) {
    unsigned int bd = canon_bds_[s];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(s);
    unsigned int hcp = HCPIndex(s, cards);
    unsigned int h = bd * num_hole_card_pairs + hcp;
    p1_buckets_[s] = buckets_.Bucket(s, h);
    if (p1_buckets_[s] > 100000000) {
      fprintf(stderr, "OOB p1 bucket s %u: bd %u h %u b %u\n", s, bd,
	      h, p1_buckets_[s]);
      exit(-1);
    }
    if (s == max_street_) {
      p1_hv = HandValueTree::Val(cards);
    }
  }

  if (c3 > c4) {cards[0] = c3; cards[1] = c4;}
  else         {cards[0] = c4; cards[1] = c3;}

  for (unsigned int s = 0; s <= max_street_; ++s) {
    unsigned int bd = canon_bds_[s];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(s);
    unsigned int hcp = HCPIndex(s, cards);
    unsigned int h = bd * num_hole_card_pairs + hcp;
    p2_buckets_[s] = buckets_.Bucket(s, h);
    if (p2_buckets_[s] > 100000000) {
      fprintf(stderr, "OOB p2 bucket s %u\n", s);
      exit(-1);
    }
    if (s == max_street_) {
      p2_hv = HandValueTree::Val(cards);
    }
  }

  if (p1_hv > p2_hv)      p1_showdown_ = 1;
  else if (p2_hv > p1_hv) p1_showdown_ = -1;
  else                    p1_showdown_ = 0;

  return true;
}

static double *g_preflop_vals = nullptr;
static unsigned long long int *g_preflop_nums = nullptr;

void TCFRThread::Run(void) {
  process_count_ = 0ULL;
  full_process_count_ = 0ULL;
  it_ = 1;
  long long int sum_p1_values = 0LL;
  long long int denom = 0LL;
  
  while (1) {
    bool not_done;
    if (hvb_table_) not_done = HVBDealHand();
    else            not_done = NoHVBDealHand();
    if (! not_done) break;

    if (it_ % 10000000 == 1 && batch_index_ % num_threads_ == 0) {
      fprintf(stderr, "Batch %i it %llu\n", batch_index_, it_);
    }

    all_full_ = false;

    if (! all_full_) {
      for (unsigned int st = 0; st <= max_street_; ++st) full_[st] = false;
      unsigned int rem = it_ % active_mod_;
      unsigned int c;
      for (c = 0; c < num_active_conditions_; ++c) {
	unsigned int num = num_active_rems_[c];
	for (unsigned int i = 0; i < num; ++i) {
	  unsigned int this_rem = active_rems_[c][i];
	  if (rem == this_rem) {
	    goto BREAKOUT;
	  }
	}
      }
    BREAKOUT:
      if (c == num_active_conditions_) {
	all_full_ = true;
      } else {
	unsigned int num = num_active_streets_[c];
	for (unsigned int i = 0; i < num; ++i) {
	  unsigned int st = active_streets_[c][i];
	  full_[st] = true;
	}
      }
    }

    p1_phase_ = true;
    showdown_value_ = p1_showdown_;
    stack_index_ = 0;
    T_VALUE p1_val = Process(data_);
    sum_p1_values += p1_val;
    denom += board_count_;

    // Temporary?
    unsigned int b = p1_buckets_[0];
    g_preflop_vals[b] += p1_val;
    g_preflop_nums[b] += board_count_;
    
    p1_phase_ = false;
    showdown_value_ = -p1_showdown_;
    stack_index_ = 0;
    T_VALUE p2_val = Process(data_);
    sum_p1_values -= p2_val;
    denom += board_count_;

    // Temporary?
    b = p2_buckets_[0];
    g_preflop_vals[b] += p2_val;
    g_preflop_nums[b] += board_count_;

    ++it_;
    if (it_ % 10000000 == 0 && batch_index_ % num_threads_ == 0) {
      fprintf(stderr, "It %llu avg P1 val %f (%lli / %lli)\n", it_,
	      sum_p1_values / (double)denom, sum_p1_values, denom);
    }
  }
  fprintf(stderr, "Batch %i done\n", batch_index_);
  if (batch_index_ % num_threads_ == 0) {
    fprintf(stderr, "Batch %i avg P1 val %f (%lli / %lli)\n", batch_index_,
	    sum_p1_values / (double)denom, sum_p1_values, denom);
  }
}

static void *thread_run(void *v_t) {
  TCFRThread *t = (TCFRThread *)v_t;
  t->Run();
  return NULL;
}

void TCFRThread::RunThread(void) {
  pthread_create(&pthread_id_, NULL, thread_run, this);
}

void TCFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

T_VALUE TCFRThread::Process(unsigned char *ptr) {
#if 0
  fprintf(stderr, "Process depth %u\n", stack_index_);
  fprintf(stderr, "Process offset %llu\n",
	  (unsigned long long int)(ptr - data_));
  if (stack_index_ > 50) exit(0);
#endif
  ++process_count_;
  if (all_full_) {
    ++full_process_count_;
  }
  unsigned char node_type = ptr[0];
  if (node_type == 0) {
    // Showdown
    T_VALUE half_pot_size = *((int *)(ptr + 4));
    return showdown_value_ * board_count_ * half_pot_size;
  } else if (node_type <= 2) {
    // Fold
    bool p1_fold = (ptr[0] == (unsigned char)1);
    bool we_fold = (p1_phase_ && p1_fold) || (! p1_phase_ && ! p1_fold);
    T_VALUE half_pot_size = *((int *)(ptr + 4));
    if (we_fold) {
      return -board_count_ * half_pot_size;
    } else {
      return board_count_ * half_pot_size;
    }
  } else { // Nonterminal node
    unsigned int st = ptr[1] & (unsigned char)3;
    unsigned int num_succs = ptr[2];
    unsigned int default_succ_index = 0;
    unsigned int fold_succ_index = ptr[3];
    bool p1_choice = (node_type == 3);
    if (p1_choice == p1_phase_) {
      unsigned int our_bucket;
      if (p1_phase_) our_bucket = p1_buckets_[st];
      else           our_bucket = p2_buckets_[st];

      unsigned int size_bucket_data;
      if (quantized_streets_[st]) {
	size_bucket_data = num_succs;
      } else if (short_quantized_streets_[st]) {
	size_bucket_data = num_succs * 2;
      } else {
	size_bucket_data = num_succs * sizeof(T_REGRET);
      }
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1_choice) {
	  size_bucket_data += num_succs * sizeof(T_SUM_PROB);
	}
      }
      unsigned char *ptr1 = ptr + 4 + num_succs * 8;
      ptr1 += our_bucket * size_bucket_data;
      // ptr1 has now skipped past prior buckets

      unsigned int min_s = kMaxUInt;
      // min_r2 is second best regret
      unsigned int min_r = kMaxUInt, min_r2 = kMaxUInt;

      if (quantized_streets_[st]) {
	unsigned char *bucket_regrets = ptr1;
	unsigned char min_qr = 255, min_qr2 = 255;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  // There should always be one action with regret 0
	  unsigned char qr = bucket_regrets[s];
	  if (qr < min_qr) {
	    min_s = s;
	    min_qr2 = min_qr;
	    min_qr = qr;
	  } else if (qr < min_qr2) {
	    min_qr2 = qr;
	  }
	}
	min_r = uncompress_[min_qr];
	min_r2 = uncompress_[min_qr2];
      } else if (short_quantized_streets_[st]) {
	unsigned short *bucket_regrets = (unsigned short *)ptr1;
	unsigned short min_qr = 65535, min_qr2 = 65535;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  // There should always be one action with regret 0
	  unsigned short qr = bucket_regrets[s];
	  if (qr < min_qr) {
	    min_s = s;
	    min_qr2 = min_qr;
	    min_qr = qr;
	  } else if (qr < min_qr2) {
	    min_qr2 = qr;
	  }
	}
	min_r = short_uncompress_[min_qr];
	min_r2 = short_uncompress_[min_qr2];
      } else {
	T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  // There should always be one action with regret 0
	  T_REGRET r = bucket_regrets[s];
	  if (r < min_r) {
	    min_s = s;
	    min_r2 = min_r;
	    min_r = r;
	  } else if (r < min_r2) {
	    min_r2 = r;
	  }
	}
      }

      bool recurse_on_all;
      if (all_full_) {
	recurse_on_all = true;
      } else {
	// Could consider only recursing on close children.
	bool close = ((min_r2 - min_r) < close_threshold_);
	recurse_on_all = full_[st] || close;
      }

      T_VALUE *succ_values = succ_value_stack_[stack_index_];
      unsigned int pruning_threshold = pruning_thresholds_[st];
      T_VALUE val;
      if (! recurse_on_all) {
	unsigned int s = min_s;
	if (explore_ > 0) {
	  double thresh = explore_ * num_succs;
	  double rnd = rngs_[rng_index_++];
	  if (rng_index_ == kNumPregenRNGs) rng_index_ = 0;
	  if (rnd < thresh) {
	    s = rnd / explore_;
	  }
	}
	unsigned long long int succ_offset =
	  *((unsigned long long int *)(ptr + 4 + s * 8));
	val = Process(data_ + succ_offset);
      } else { // Recursing on all succs
	for (unsigned int s = 0; s < num_succs; ++s) {
	  unsigned long long int succ_offset =
	    *((unsigned long long int *)(ptr + 4 + s * 8));
	  
	  bool prune = false;
	  if (! quantized_streets_[st] && ! short_quantized_streets_[st]) {
	    T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	    prune = (bucket_regrets[s] >= pruning_threshold);
	  }
	  if (s == fold_succ_index || ! prune) {
	    ++stack_index_;
	    succ_values[s] = Process(data_ + succ_offset);
	    --stack_index_;
	  }
	}
	  
	val = succ_values[min_s];

	int *succ_iregrets = succ_iregret_stack_[stack_index_];
	int min_regret = kMaxInt;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  int ucr;
	  if (quantized_streets_[st]) {
	    unsigned char *bucket_regrets = ptr1;
	    ucr = uncompress_[bucket_regrets[s]];
	  } else if (short_quantized_streets_[st]) {
	    unsigned short *bucket_regrets = (unsigned short *)ptr1;
	    ucr = short_uncompress_[bucket_regrets[s]];
	  } else {
	    T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	    if (s != fold_succ_index &&
		bucket_regrets[s] >= pruning_threshold) {
	      continue;
	    }
	    ucr = bucket_regrets[s];
	  }
	  int i_regret;
	  if (scaled_streets_[st]) {
	    int incr = succ_values[s] - val;
	    double scaled = incr * 0.05;
	    int trunc = scaled;
	    double rnd = rngs_[rng_index_++];
	    if (rng_index_ == kNumPregenRNGs) rng_index_ = 0;
	    if (scaled < 0) {
	      double rem = trunc - scaled;
	      if (rnd < rem) incr = trunc - 1;
	      else           incr = trunc;
	    } else {
	      double rem = scaled - trunc;
	      if (rnd < rem) incr = trunc + 1;
	      else           incr = trunc;
	    }
	    i_regret = ucr - incr;
	  } else {
	    i_regret = ucr - (succ_values[s] - val);
	  }
	  if (s == 0 || i_regret < min_regret) min_regret = i_regret;
	  succ_iregrets[s] = i_regret;
	}
	int offset = -min_regret;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  // Assume no pruning if quantization for now
	  if (! quantized_streets_[st] && ! short_quantized_streets_[st]) {
	    T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	    if (s != fold_succ_index &&
		bucket_regrets[s] >= pruning_threshold) {
	      continue;
	    }
	  }
	  int i_regret = succ_iregrets[s];
	  unsigned int r = (unsigned int)(i_regret + offset);
	  if (quantized_streets_[st]) {
	    unsigned char *bucket_regrets = ptr1;
	    double rnd = rngs_[rng_index_++];
	    if (rng_index_ == kNumPregenRNGs) rng_index_ = 0;
	    bucket_regrets[s] = CompressRegret(r, rnd, uncompress_);
	  } else if (short_quantized_streets_[st]) {
	    unsigned short *bucket_regrets = (unsigned short *)ptr1;
	    double rnd = rngs_[rng_index_++];
	    if (rng_index_ == kNumPregenRNGs) rng_index_ = 0;
	    bucket_regrets[s] = CompressRegretShort(r, rnd, short_uncompress_);
	  } else {
	    T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	    // Try capping instead of dividing by two.  Make sure to apply
	    // cap after adding offset.
	    if (r > 2000000000) r = 2000000000;
	    bucket_regrets[s] = r;
	  }
	}
      }
      return val;
    } else {
      unsigned int opp_bucket;
      if (p1_phase_) opp_bucket = p2_buckets_[st];
      else           opp_bucket = p1_buckets_[st];

      unsigned char *ptr1 = ptr + 4 + num_succs * 8;
      unsigned int size_bucket_data;
      if (quantized_streets_[st]) {
	size_bucket_data = num_succs;
      } else if (short_quantized_streets_[st]) {
	size_bucket_data = num_succs * 2;
      } else {
	size_bucket_data = num_succs * sizeof(T_REGRET);
      }
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1_choice) {
	  size_bucket_data += num_succs * sizeof(T_SUM_PROB);
	}
      }

      ptr1 += opp_bucket * size_bucket_data;
      // ptr1 has now skipped past prior buckets

      unsigned int min_s = default_succ_index;
      // There should always be one action with regret 0
      if (quantized_streets_[st]) {
	unsigned char *bucket_regrets = ptr1;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (bucket_regrets[s] == 0) {
	    min_s = s;
	    break;
	  }
	}
      } else if (short_quantized_streets_[st]) {
	unsigned short *bucket_regrets = (unsigned short *)ptr1;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (bucket_regrets[s] == 0) {
	    min_s = s;
	    break;
	  }
	}
      } else {
	T_REGRET *bucket_regrets = (T_REGRET *)ptr1;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (bucket_regrets[s] == 0) {
	    min_s = s;
	    break;
	  }
	}
      }

      unsigned int ss = min_s;
      if (explore_ > 0) {
	double thresh = explore_ * num_succs;
	double rnd = rngs_[rng_index_++];
	if (rng_index_ == kNumPregenRNGs) rng_index_ = 0;
	if (rnd < thresh) {
	  ss = rnd / explore_;
	}
      }

	// Update sum-probs
      if (sumprob_streets_[st] && (all_full_ || ! full_only_avg_update_)) {
	if (! asymmetric_ || p1_choice == target_player_) {
	  T_SUM_PROB *these_sum_probs;
	  if (quantized_streets_[st]) {
	    these_sum_probs = (T_SUM_PROB *)(ptr1 + num_succs);
	  } else if (short_quantized_streets_[st]) {
	    these_sum_probs = (T_SUM_PROB *)(ptr1 + num_succs * 2);
	  } else {
	    these_sum_probs =
	      (T_SUM_PROB *)(ptr1 + num_succs * sizeof(T_REGRET));
	  }
	  T_SUM_PROB ceiling = sumprob_ceilings_[st];
	  these_sum_probs[ss] += 1;
	  bool sum_prob_too_extreme = false;
	  if (these_sum_probs[ss] > ceiling) {
	    sum_prob_too_extreme = true;
	  }
	  if (sum_prob_too_extreme) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      these_sum_probs[s] /= 2;
	    }
	  }
	}
      }

      unsigned long long int succ_offset =
	*((unsigned long long int *)(ptr + 4 + ss * 8));
      ++stack_index_;
      T_VALUE ret = Process(data_ + succ_offset);
      --stack_index_;
      return ret;
    }
  }
}

void TCFR::ReadRegrets(unsigned char *ptr, Reader ***readers) {
  unsigned char node_type = ptr[0];
  // Terminal node
  if (node_type <= 2 || node_type == 5) return;
  unsigned int num_succs = ptr[2];
  unsigned int p1 = (node_type == 3);
  unsigned int st = ptr[1] & (unsigned char)3;
  Reader *reader = readers[p1][st];
  unsigned int num_buckets = buckets_.NumBuckets(st);
  unsigned char *ptr1 = ptr + 4 + num_succs * 8;
  if (quantized_streets_[st]) {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	ptr1[s] = reader->ReadUnsignedCharOrDie();
      }
      ptr1 += num_succs;
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  } else if (short_quantized_streets_[st]) {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      unsigned short *regrets = (unsigned short *)ptr1;
      for (unsigned int s = 0; s < num_succs; ++s) {
	regrets[s] = reader->ReadUnsignedShortOrDie();
      }
      ptr1 += num_succs * 2;
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  } else {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      T_REGRET *regrets = (T_REGRET *)ptr1;
      for (unsigned int s = 0; s < num_succs; ++s) {
	regrets[s] = reader->ReadUnsignedIntOrDie();
      }
      ptr1 += num_succs * sizeof(T_REGRET);
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    unsigned long long int succ_offset =
      *((unsigned long long int *)(ptr + 4 + s * 8));
    ReadRegrets(data_ + succ_offset, readers);
  }
}

void TCFR::WriteRegrets(unsigned char *ptr, Writer ***writers) {
  unsigned char node_type = ptr[0];
  // Terminal node
  if (node_type <= 2 || node_type == 5) return;
  unsigned int num_succs = ptr[2];
  unsigned int p1 = (node_type == 3);
  unsigned int st = ptr[1] & (unsigned char)3;
  Writer *writer = writers[p1][st];
  unsigned int num_buckets = buckets_.NumBuckets(st);
  unsigned char *ptr1 = ptr + 4 + num_succs * 8;
  if (quantized_streets_[st]) {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	writer->WriteUnsignedChar(ptr1[s]);
      }
      ptr1 += num_succs;
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  } else if (short_quantized_streets_[st]) {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      unsigned short *regrets = (unsigned short *)ptr1;
      for (unsigned int s = 0; s < num_succs; ++s) {
	writer->WriteUnsignedShort(regrets[s]);
      }
      ptr1 += num_succs * 2;
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  } else {
    for (unsigned int b = 0; b < num_buckets; ++b) {
      T_REGRET *regrets = (T_REGRET *)ptr1;
      for (unsigned int s = 0; s < num_succs; ++s) {
	writer->WriteUnsignedInt(regrets[s]);
      }
      ptr1 += num_succs * sizeof(T_REGRET);
      if (sumprob_streets_[st]) {
	if (! asymmetric_ || target_player_ == p1) {
	  ptr1 += num_succs * sizeof(T_SUM_PROB);
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    unsigned long long int succ_offset =
      *((unsigned long long int *)(ptr + 4 + s * 8));
    WriteRegrets(data_ + succ_offset, writers);
  }
}

void TCFR::ReadSumprobs(unsigned char *ptr, Reader ***readers) {
  unsigned char node_type = ptr[0];
  // Terminal node
  if (node_type <= 2 || node_type == 5) return;
  unsigned int num_succs = ptr[2];
  unsigned int p1 = (node_type == 3);
  unsigned int st = ptr[1] & (unsigned char)3;
  unsigned int num_buckets = buckets_.NumBuckets(st);
  unsigned char *ptr1 = ptr + 4 + num_succs * 8;
  if (sumprob_streets_[st]) {
    if (! asymmetric_ || target_player_ == p1) {
      Reader *reader = readers[p1][st];
      if (quantized_streets_[st]) {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs = (T_SUM_PROB *)(ptr1 + num_succs);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    sum_probs[s] = reader->ReadUnsignedIntOrDie();
	  }
	  ptr1 += num_succs * (1 + sizeof(T_SUM_PROB));
	}
      } else if (short_quantized_streets_[st]) {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs = (T_SUM_PROB *)(ptr1 + num_succs * 2);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    sum_probs[s] = reader->ReadUnsignedIntOrDie();
	  }
	  ptr1 += num_succs * (2 + sizeof(T_SUM_PROB));
	}
      } else {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs =
	    (T_SUM_PROB *)(ptr1 + num_succs * sizeof(T_REGRET));
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    sum_probs[s] = reader->ReadUnsignedIntOrDie();
	  }
	  ptr1 += num_succs * (sizeof(T_REGRET) + sizeof(T_SUM_PROB));
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    unsigned long long int succ_offset =
      *((unsigned long long int *)(ptr + 4 + s * 8));
    ReadSumprobs(data_ + succ_offset, readers);
  }
}

void TCFR::WriteSumprobs(unsigned char *ptr, Writer ***writers) {
  unsigned char node_type = ptr[0];
  // Terminal node
  if (node_type <= 2 || node_type == 5) return;
  unsigned int num_succs = ptr[2];
  unsigned int p1 = (node_type == 3);
  unsigned int st = ptr[1] & (unsigned char)3;
  if (sumprob_streets_[st]) {
    if (! asymmetric_ || target_player_ == p1) {
      Writer *writer = writers[p1][st];
      unsigned int num_buckets = buckets_.NumBuckets(st);
      unsigned char *ptr1 = ptr + 4 + num_succs * 8;
      if (quantized_streets_[st]) {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs = (T_SUM_PROB *)(ptr1 + num_succs);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    writer->WriteUnsignedInt(sum_probs[s]);
	  }
	  ptr1 += num_succs * (1 + sizeof(T_SUM_PROB));
	}
      } else if (short_quantized_streets_[st]) {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs = (T_SUM_PROB *)(ptr1 + num_succs * 2);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    writer->WriteUnsignedInt(sum_probs[s]);
	  }
	  ptr1 += num_succs * (2 + sizeof(T_SUM_PROB));
	}
      } else {
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  T_SUM_PROB *sum_probs =
	    (T_SUM_PROB *)(ptr1 + num_succs * sizeof(T_REGRET));
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    writer->WriteUnsignedInt(sum_probs[s]);
	  }
	  ptr1 += num_succs * (sizeof(T_REGRET) + sizeof(T_SUM_PROB));
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    unsigned long long int succ_offset =
      *((unsigned long long int *)(ptr + 4 + s * 8));
    WriteSumprobs(data_ + succ_offset, writers);
  }
}

void TCFR::Read(unsigned int batch_base) {
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), 
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (asymmetric_) {
    char buf2[20];
    sprintf(buf2, ".p%u", target_player_);
    strcat(dir, buf2);
  }
  Reader ***regret_readers = new Reader **[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    regret_readers[p] = new Reader *[max_street_ + 1];
    for (unsigned int st = 0; st <= max_street_; ++st) {
      sprintf(buf, "%s/regrets.0.0.0.0.%u.%u.p%u.i", dir,
	      st, batch_base, p);
      regret_readers[p][st] = new Reader(buf);
    }
  }
  Reader ***sum_prob_readers = new Reader **[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    if (asymmetric_ && target_player_ != p) {
      sum_prob_readers[p] = NULL;
      continue;
    }
    sum_prob_readers[p] = new Reader *[max_street_ + 1];
    for (unsigned int st = 0; st <= max_street_; ++st) {
      if (! sumprob_streets_[st]) {
	sum_prob_readers[p][st] = NULL;
	continue;
      }
      if (! asymmetric_ || target_player_ == p) {
	sprintf(buf, "%s/sumprobs.0.0.0.0.%u.%u.p%u.i", dir,
		st, batch_base, p);
	sum_prob_readers[p][st] = new Reader(buf);
      } else {
	sum_prob_readers[p][st] = nullptr;
      }
    }
  }
  ReadRegrets(data_, regret_readers);
  ReadSumprobs(data_, sum_prob_readers);
  for (unsigned int p = 0; p <= 1; ++p) {
    for (unsigned int st = 0; st <= max_street_; ++st) {
      if (! regret_readers[p][st]->AtEnd()) {
	fprintf(stderr, "Regret reader didn't get to EOF\n");
	exit(-1);
      }
      delete regret_readers[p][st];
    }
    delete [] regret_readers[p];
  }
  delete [] regret_readers;
  for (unsigned int p = 0; p <= 1; ++p) {
    if (asymmetric_ && target_player_ != p) continue;
    for (unsigned int st = 0; st <= max_street_; ++st) {
      if (! sumprob_streets_[st]) continue;
      if (! sum_prob_readers[p][st]->AtEnd()) {
	fprintf(stderr, "Sumprob reader didn't get to EOF\n");
	exit(-1);
      }
      delete sum_prob_readers[p][st];
    }
    delete [] sum_prob_readers[p];
  }
  delete [] sum_prob_readers;
}

void TCFR::Write(unsigned int batch_base) {
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  if (asymmetric_) {
    char buf2[20];
    sprintf(buf2, ".p%u", target_player_);
    strcat(dir, buf2);
  }
  Mkdir(dir);
  Writer ***regret_writers = new Writer **[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    regret_writers[p] = new Writer *[max_street_ + 1];
    for (unsigned int st = 0; st <= max_street_; ++st) {
      sprintf(buf, "%s/regrets.0.0.0.0.%u.%u.p%u.i", dir,
	      st, batch_base, p);
      regret_writers[p][st] = new Writer(buf);
    }
  }
  WriteRegrets(data_, regret_writers);
  for (unsigned int p = 0; p <= 1; ++p) {
    for (unsigned int st = 0; st <= max_street_; ++st) {
      delete regret_writers[p][st];
    }
    delete [] regret_writers[p];
  }
  delete [] regret_writers;

  Writer ***sum_prob_writers = new Writer **[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    // In asymmetric systems, only save sumprobs for target player
    if (asymmetric_ && p != target_player_) {
      sum_prob_writers[p] = NULL;
      continue;
    }
    sum_prob_writers[p] = new Writer *[max_street_ + 1];
    for (unsigned int st = 0; st <= max_street_; ++st) {
      if (! sumprob_streets_[st]) {
	sum_prob_writers[p][st] = NULL;
	continue;
      }
      sprintf(buf, "%s/sumprobs.0.0.0.0.%u.%u.p%u.i", dir,
	      st, batch_base, p);
      sum_prob_writers[p][st] = new Writer(buf);
    }
  }
  WriteSumprobs(data_, sum_prob_writers);
  for (unsigned int p = 0; p <= 1; ++p) {
    if (asymmetric_ && p != target_player_) continue;
    for (unsigned int st = 0; st <= max_street_; ++st) {
      if (! sumprob_streets_[st]) continue;
      delete sum_prob_writers[p][st];
    }
    delete [] sum_prob_writers[p];
  }
  delete [] sum_prob_writers;
}

void TCFR::Run(void) {
  // Temporary?
  unsigned int num_preflop_buckets = buckets_.NumBuckets(0);
  g_preflop_vals = new double[num_preflop_buckets];
  g_preflop_nums = new unsigned long long int[num_preflop_buckets];
  for (unsigned int b = 0; b < num_preflop_buckets; ++b) {
    g_preflop_vals[b] = 0;
    g_preflop_nums[b] = 0ULL;
  }

  for (unsigned int i = 1; i < num_cfr_threads_; ++i) {
    cfr_threads_[i]->RunThread();
  }
  // Execute thread 0 in main execution thread
  fprintf(stderr, "Starting thread 0 in main thread\n");
  cfr_threads_[0]->Run();
  fprintf(stderr, "Finished main thread\n");
  for (unsigned int i = 1; i < num_cfr_threads_; ++i) {
    cfr_threads_[i]->Join();
    fprintf(stderr, "Joined thread %i\n", i);
  }

  // Temporary?
  unsigned int max_card = Game::MaxCard();
  Card hole_cards[2];
  for (Card hi = 1; hi <= max_card; ++hi) {
    hole_cards[0] = hi;
    for (Card lo = 0; lo < hi; ++lo) {
      hole_cards[1] = lo;
      unsigned int hcp = HCPIndex(0, hole_cards);
      unsigned int b = buckets_.Bucket(0, hcp);
      double val = g_preflop_vals[b];
      unsigned long long int num = g_preflop_nums[b];
      if (num > 0) {
	printf("%f ", val / (double)num);
	OutputTwoCards(hi, lo);
	printf(" (%u)\n", b);
	g_preflop_nums[b] = 0;
      }
    }
  }
  fflush(stdout);
  delete [] g_preflop_vals;
  delete [] g_preflop_nums;
}

void TCFR::RunBatch(unsigned int batch_size) {
  SeedRand(batch_base_);
  fprintf(stderr, "Seeding to %i\n", batch_base_);
  for (unsigned int i = 0; i < kNumPregenRNGs; ++i) {
    rngs_[i] = RandZeroToOne();
    // A hack.  We can end up generating 1.0 as an RNG because we are casting
    // doubles to floats.  Code elsewhere assumes RNGs are strictly less than
    // 1.0.
    if (rngs_[i] >= 1.0) rngs_[i] = 0.99999;
  }

  cfr_threads_ = new TCFRThread *[num_cfr_threads_];
  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    TCFRThread *cfr_thread =
      new TCFRThread(betting_abstraction_, cfr_config_, buckets_,
		     batch_base_ + i, num_cfr_threads_, data_, target_player_,
		     rngs_, uncompress_, short_uncompress_,
		     pruning_thresholds_, sumprob_streets_, hvb_table_,
		     cards_to_indices_, batch_size);
    cfr_threads_[i] = cfr_thread;
  }

  fprintf(stderr, "Running batch base %i\n", batch_base_);
  Run();
  fprintf(stderr, "Finished running batch base %i\n", batch_base_);

  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    total_process_count_ += cfr_threads_[i]->ProcessCount();
  }
  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    total_full_process_count_ += cfr_threads_[i]->FullProcessCount();
  }

  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    delete cfr_threads_[i];
  }
  delete [] cfr_threads_;
  cfr_threads_ = NULL;
}

void TCFR::Run(unsigned int start_batch_base, unsigned int batch_size,
	       unsigned int save_interval) {
  if (save_interval < num_cfr_threads_) {
    fprintf(stderr, "Save interval must be at least the number of threads\n");
    exit(-1);
  }

  if (save_interval % num_cfr_threads_ != 0) {
    fprintf(stderr, "Save interval should be multiple of number of threads\n");
    exit(-1);
  }

  if (start_batch_base > 0) {
    // Doesn't allow us to change number of threads
    unsigned int old_batch_base = start_batch_base - num_cfr_threads_;
    Read(old_batch_base);
  }

  total_process_count_ = 0ULL;
  total_full_process_count_ = 0ULL;

  batch_base_ = start_batch_base;
  while (true) {
    RunBatch(batch_size);

    // This is a little ugly.  If our save interval is 800, we want to save
    // at batches 800, 1600, etc., but not at batch 0 even though 0 % 800 == 0.
    // But if our save interval is 8 (and we have eight threads), then we
    // do want to save at batch 0.
    if (batch_base_ % save_interval == 0 &&
	(batch_base_ > 0 || save_interval == num_cfr_threads_)) {
      fprintf(stderr, "Process count: %llu\n", total_process_count_);
      fprintf(stderr, "Full process count: %llu\n", total_full_process_count_);
      time_t start_t = time(NULL);
      Write(batch_base_);
      fprintf(stderr, "Checkpointed batch base %u\n", batch_base_);
      time_t end_t = time(NULL);
      double diff_sec = difftime(end_t, start_t);
      fprintf(stderr, "Writing took %.1f seconds\n", diff_sec);
      total_process_count_ = 0ULL;
      total_full_process_count_ = 0ULL;
    }

    batch_base_ += num_cfr_threads_;
  }
}

void TCFR::Run(unsigned int start_batch_base, unsigned int end_batch_base,
	       unsigned int batch_size, unsigned int save_interval) {
  if ((end_batch_base + num_cfr_threads_ - start_batch_base) %
      num_cfr_threads_ != 0) {
    fprintf(stderr, "Batches to execute should be multiple of number of "
	    "threads\n");
    exit(-1);
  }
  if (save_interval % num_cfr_threads_ != 0) {
    fprintf(stderr, "Save interval should be multiple of number of threads\n");
    exit(-1);
  }
  if ((end_batch_base + num_cfr_threads_ - start_batch_base) %
      save_interval != 0) {
    fprintf(stderr, "Batches to execute should be multiple of save interval\n");
    exit(-1);
  }
  if (start_batch_base > 0) {
    // Doesn't allow us to change number of threads
    unsigned int old_batch_base = start_batch_base - num_cfr_threads_;
    Read(old_batch_base);
  }

  total_process_count_ = 0ULL;
  total_full_process_count_ = 0ULL;

  for (batch_base_ = start_batch_base;
       batch_base_ <= end_batch_base; batch_base_ += num_cfr_threads_) {
    RunBatch(batch_size);
    // This is a little ugly.  If our save interval is 800, we want to save
    // at batches 800, 1600, etc., but not at batch 0 even though 0 % 800 == 0.
    // But if our save interval is 8 (and we have eight threads), then we
    // do want to save at batch 0.
    if (batch_base_ % save_interval == 0 &&
	(batch_base_ > 0 || save_interval == num_cfr_threads_)) {
      fprintf(stderr, "Process count: %llu\n", total_process_count_);
      fprintf(stderr, "Full process count: %llu\n", total_full_process_count_);
      Write(batch_base_);
      fprintf(stderr, "Checkpointed batch base %u\n", batch_base_);
      total_process_count_ = 0ULL;
      total_full_process_count_ = 0ULL;
    }
  }
}

// Returns a pointer to the allocation buffer after this node and all of its
// descendants.
// First byte is node type (0=showdown, 1=P1 fold, 2=P2 fold,
// 3=P1-choice nonterminal,4=P2-choice nonterminal)
// If nonterminal, second byte is street (bottom two bits) and
// granularity (remaining bits).
// If nonterminal, third byte is num succs
// If nonterminal, fourth byte is fold succ index
// If terminal, bytes 4-7 are the pot-size/2
// Remainder is only for nonterminals:
// The next num-succs * 8 bytes are for the succ ptrs
// For each bucket
//   num-succs * sizeof(T_REGRET) for the regrets
//   num-succs * sizeof(T_SUM_PROB) for the sum-probs
// A little padding is added at the end if necessary to make the number of
// bytes for a node be a multiple of 8.
unsigned char *TCFR::Prepare(unsigned char *ptr, Node *node) {
  if (node->Terminal()) {
    if (node->Showdown()) {
      ptr[0] = (unsigned char)0;
    } else if (node->PlayerFolding() == 1) {
      ptr[0] = (unsigned char)1;
    } else if (node->PlayerFolding() == 0) {
      ptr[0] = (unsigned char)2;
    } else {
      fprintf(stderr, "Not folding; player folding no zero or one?!?\n");
      exit(-1);
    }
    *((int *)(ptr + 4)) = node->PotSize() / 2;
    return ptr + 8;
  }
  if (node->PlayerActing() == 1) {
    ptr[0] = (unsigned char)3;
  } else if (node->PlayerActing() == 0) {
    ptr[0] = (unsigned char)4;
  } else {
    fprintf(stderr, "Player to act not zero or one?!?\n");
    exit(-1);
  }
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  ptr[1] = (unsigned char)st & (unsigned char)3;
  ptr[2] = (unsigned char)num_succs;
  // Note: now storing fold-succ-index here.  Used to store default-succ-index.
  // Now assume default-succ-index is always 0.
  ptr[3] = (unsigned char)node->FoldSuccIndex();
  // This is where we will place the succ ptrs
  unsigned char *succ_ptr = ptr + 4;
  unsigned char *ptr1 = succ_ptr + num_succs * 8;
  unsigned int num_buckets = buckets_.NumBuckets(st);
  for (unsigned int b = 0; b < num_buckets; ++b) {
    // Regrets
    if (quantized_streets_[st]) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	*ptr1 = 0;
	++ptr1;
      }
    } else if (short_quantized_streets_[st]) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	*(unsigned short *)ptr1 = 0;
	ptr1 += 2;
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	*((T_REGRET *)ptr1) = 0;
	ptr1 += sizeof(T_REGRET);
      }
    }
    // Sumprobs
    if (sumprob_streets_[st]) {
      if (! asymmetric_ || node->PlayerActing() == target_player_) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  *((T_SUM_PROB *)ptr1) = 0;
	  ptr1 += sizeof(T_SUM_PROB);
	}
      }
    }
  }

  // ptr1 now points to the end of the data for the current node
  unsigned long long int num_bytes = ptr1 - ptr;
  // Round up so that ptr1 - ptr is a multiple of 8
  unsigned long long int rounded_up = ((num_bytes - 1ULL) / 8ULL + 1ULL) * 8ULL;
  ptr1 = ptr + rounded_up;

  for (unsigned int s = 0; s < num_succs; ++s) {
    unsigned long long int ull_offset = ptr1 - data_;
    if (ull_offset % 8 != 0) {
      // Not sure this is necessary, but it should be true, so let's check.
      fprintf(stderr, "Offset is not a multiple of 8?!?\n");
      exit(-1);
    }
    *((unsigned long long int *)(succ_ptr + s * 8)) = ull_offset;
    ptr1 = Prepare(ptr1, node->IthSucc(s));
  }
  return ptr1;
}

void TCFR::MeasureTree(Node *node, unsigned long long int *allocation_size) {
  if (node->Terminal()) {
    // 8 bytes for every terminal node
    *allocation_size += 8ULL;
    return;
  }

  // Four bytes for everything else (e.g., num-succs) to keep things
  // aligned.
  unsigned int this_sz = 4;

  unsigned int num_succs = node->NumSuccs();
  // Eight bytes per succ
  this_sz += num_succs * 8;
  unsigned int st = node->Street();
  // A regret and a sum-prob for each bucket and succ
  unsigned int nb = buckets_.NumBuckets(st);
  if (quantized_streets_[st]) {
    this_sz += nb * num_succs;
  } else if (short_quantized_streets_[st]) {
    this_sz += nb * num_succs * 2;
  } else {
    this_sz += nb * num_succs * sizeof(T_REGRET);
  }
  if (sumprob_streets_[st]) {
    if (! asymmetric_ || node->PlayerActing() == target_player_) {
      this_sz += nb * num_succs * sizeof(T_SUM_PROB);
    }
  }
  
  // Round up to multiple of 8
  this_sz = ((this_sz - 1) / 8 + 1) * 8;
  *allocation_size += this_sz;

  for (unsigned int s = 0; s < num_succs; ++s) {
    MeasureTree(node->IthSucc(s), allocation_size);
  }
}

// Allocate one contiguous block of memory that has successors, street,
// num-succs, regrets, sum-probs, showdown/fold flag, pot-size/2.
void TCFR::Prepare(void) {
  BettingTree *tree;
  if (betting_abstraction_.Asymmetric()) {
    tree = BettingTree::BuildAsymmetricTree(betting_abstraction_,
					    target_player_);
  } else {
    tree = BettingTree::BuildTree(betting_abstraction_);
  }
  // Use an unsigned long long int, but succs are four-byte
  unsigned long long int allocation_size = 0;
  MeasureTree(tree->Root(), &allocation_size);
  // Should get amount of RAM from method in Files class
  if (allocation_size > 2050000000000ULL) {
    fprintf(stderr, "Allocation size %llu too big\n", allocation_size);
    exit(-1);
  }
  fprintf(stderr, "Allocation size: %llu\n", allocation_size);
  data_ = new unsigned char[allocation_size];
  if (data_ == NULL) {
    fprintf(stderr, "Could not allocate\n");
    exit(-1);
  }
  unsigned char *end = Prepare(data_, tree->Root());
  unsigned long long int sz = end - data_;
  if (sz != allocation_size) {
    fprintf(stderr, "Didn't fill expected number of bytes: sz %llu as %llu\n",
	    sz, allocation_size);
    exit(-1);
  }
  delete tree;
}

TCFR::TCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	   const CFRConfig &cc, const Buckets &buckets,
	   unsigned int num_threads, unsigned int target_player) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets) {
  fprintf(stderr, "Full evaluation if regrets close; threshold %u\n",
	  cfr_config_.CloseThreshold());
  time_t start_t = time(NULL);
  asymmetric_ = betting_abstraction_.Asymmetric();
  target_player_ = target_player;
  num_cfr_threads_ = num_threads;
  fprintf(stderr, "Num threads: %i\n", num_cfr_threads_);
  max_street_ = Game::MaxStreet();

  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  BoardTree::BuildPredBoards();
  
  pruning_thresholds_ = new unsigned int[max_street_];
  const vector<unsigned int> &v = cfr_config_.PruningThresholds();
  for (unsigned int st = 0; st <= max_street_; ++st) {
    pruning_thresholds_[st] = v[st];
  }

  sumprob_streets_ = new bool[max_street_ + 1];
  const vector<unsigned int> &ssv = cfr_config_.SumprobStreets();
  unsigned int num_ssv = ssv.size();
  if (num_ssv == 0) {
    for (unsigned int st = 0; st <= max_street_; ++st) {
      sumprob_streets_[st] = true;
    }
  } else {
    for (unsigned int st = 0; st <= max_street_; ++st) {
      sumprob_streets_[st] = false;
    }
    for (unsigned int i = 0; i < num_ssv; ++i) {
      unsigned int st = ssv[i];
      sumprob_streets_[st] = true;
    }
  }

  quantized_streets_ = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    quantized_streets_[st] = false;
  }
  const vector<unsigned int> &qsv = cfr_config_.QuantizedStreets();
  unsigned int num_qsv = qsv.size();
  for (unsigned int i = 0; i < num_qsv; ++i) {
    unsigned int st = qsv[i];
    quantized_streets_[st] = true;
  }
  short_quantized_streets_ = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    short_quantized_streets_[st] = false;
  }
  const vector<unsigned int> &sqsv = cfr_config_.ShortQuantizedStreets();
  unsigned int num_sqsv = sqsv.size();
  for (unsigned int i = 0; i < num_sqsv; ++i) {
    unsigned int st = sqsv[i];
    short_quantized_streets_[st] = true;
  }

  Prepare();

  rngs_ = new float[kNumPregenRNGs];
  uncompress_ = new unsigned int[256];
  for (unsigned int c = 0; c <= 255; ++c) {
    uncompress_[c] = UncompressRegret(c);
  }
  short_uncompress_ = new unsigned int[65536];
  for (unsigned int c = 0; c <= 65535; ++c) {
    short_uncompress_[c] = UncompressRegretShort(c);
  }

  if (cfr_config_.HVBTable()) {
    // How much extra space does this require?  2.5b hands times 4 bytes
    // for hand value is 10 gigs - minus size of HandTree.
    char buf[500];
    string max_street_bucketing = card_abstraction_.Bucketing(max_street_);
    sprintf(buf, "%s/hvb.%s.%u.%u.%u.%s", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	    max_street_, max_street_bucketing.c_str());
    unsigned int num_boards = BoardTree::NumBoards(max_street_);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street_);
    unsigned int num_hands = num_boards * num_hole_card_pairs;
    long long int bytes = 4;
    if (buckets_.NumBuckets(max_street_) <= 65536) bytes += 2;
    else                                           bytes += 4;
    long long int total_bytes = ((long long int)num_hands) * bytes;
    Reader reader(buf);
    if (reader.FileSize() != total_bytes) {
      fprintf(stderr, "File size %lli expected %lli\n", reader.FileSize(),
	      total_bytes);
    }
    hvb_table_ = new unsigned char[total_bytes];
    for (long long int i = 0; i < total_bytes; ++i) {
      if ((i % 1000000000LL) == 0) {
	fprintf(stderr, "i %lli/%lli\n", i, total_bytes);
      }
      hvb_table_[i] = reader.ReadUnsignedCharOrDie();
    }
  } else {
    HandValueTree::Create();
    hvb_table_ = NULL;
  }

  if (cfr_config_.HVBTable()) {
    cards_to_indices_ = new unsigned char **[max_street_ + 1];
    unsigned int max_card = Game::MaxCard();
    for (unsigned int st = 0; st <= max_street_; ++st) {
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int num_board_cards = Game::NumBoardCards(st);
      cards_to_indices_[st] = new unsigned char *[num_boards];
      for (unsigned int bd = 0; bd < num_boards; ++bd) {
	const Card *board = BoardTree::Board(st, bd);
	cards_to_indices_[st][bd] = new unsigned char[max_card + 1];
	unsigned int num_lower = 0;
	for (unsigned int c = 0; c <= max_card; ++c) {
	  // It's OK if we assign a value to a card that is on the board.
	  cards_to_indices_[st][bd][c] = c - num_lower;
	  for (unsigned int i = 0; i < num_board_cards; ++i) {
	    if (c == board[i]) {
	      ++num_lower;
	      break;
	    }
	  }
	}
      }
    }
  } else {
    cards_to_indices_ = NULL;
  }

#if 0
  bool *compressed_streets = new bool[max_street_ + 1];
  for (unsigned int st = 0; st <= max_street_; ++st) {
    compressed_streets[st] = false;
  }

  regrets_.reset(new CFRValues(true, true, false, nullptr, betting_tree_, 0,
			       0, buckets_, compressed_streets));
  // Should check for asymmetric systems
  // Should honor sumprobs_streets_
  sumprobs_.reset(new CFRValues(true, true, true, nullptr, betting_tree_, 0,
				0, buckets_, compressed_streets));
  delete [] compressed_streets;
#endif

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Initialization took %.1f seconds\n", diff_sec);
}

TCFR::~TCFR(void) {
  if (cards_to_indices_) {
    for (unsigned int st = 0; st <= max_street_; ++st) {
      unsigned int num_boards = BoardTree::NumBoards(st);
      for (unsigned int bd = 0; bd < num_boards; ++bd) {
	delete [] cards_to_indices_[st][bd];
      }
      delete [] cards_to_indices_[st];
    }
    delete [] cards_to_indices_;
  }
  delete [] hvb_table_;
  delete [] pruning_thresholds_;
  delete [] uncompress_;
  delete [] short_uncompress_;
  delete [] rngs_;
  delete [] data_;
  delete [] quantized_streets_;
  delete [] short_quantized_streets_;
  delete [] sumprob_streets_;
}
