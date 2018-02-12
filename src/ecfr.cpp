// External CFR
//
// Assume only two players.
//
// Should I use pure CFR?  Is memory saving the only reason?

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
#include "split.h"
#include "ecfr.h"

using namespace std;

class ECFRThread {
public:
  ECFRThread(const CFRConfig &cc, const Buckets &buckets,
	     const BettingTree *betting_tree, double ****regrets,
	     double ****sumprobs, double ****action_sumprobs,
	     unsigned int num_raw_boards, const unsigned int *board_table,
	     unsigned int **bucket_counts, unsigned int batch_index,
	     unsigned int num_threads, unsigned long long int batch_size,
	     unsigned long long int *total_its);
  ~ECFRThread(void);
  void RunThread(void);
  void Join(void);
  void Run(void);
private:
  void Deal(void);
  double Process(Node *node, bool adjust);

  const Buckets &buckets_;
  const BettingTree *betting_tree_;
  bool boost_;
  // Indexed by st, pa and nt.
  double ****regrets_;
  double ****sumprobs_;
  double ****action_sumprobs_;
  unsigned int num_raw_boards_;
  const unsigned int *board_table_;
  unsigned int **bucket_counts_;
  unsigned int batch_index_;
  unsigned int num_threads_;
  unsigned int num_players_;
  unsigned int max_street_;
  struct drand48_data rand_buf_;
  unsigned int p_;
  // 1 if p_ wins at showdown; -1 if p_ loses at showdown; 0 if chop
  double p1_showdown_mult_;
  double showdown_mult_;
  unsigned int **current_buckets_;
  unique_ptr<unsigned int []> hole_cards_;
  unique_ptr<unsigned int []> hi_cards_;
  unique_ptr<unsigned int []> lo_cards_;
  unique_ptr<unsigned int []> canon_bds_;
  unique_ptr<unsigned int []> hvs_;
  unsigned long long int it_;
  unsigned long long int batch_size_;
  unsigned long long int *total_its_;
  pthread_t pthread_id_;
};

ECFRThread::ECFRThread(const CFRConfig &cc, const Buckets &buckets,
		       const BettingTree *betting_tree, double ****regrets,
		       double ****sumprobs, double ****action_sumprobs,
		       unsigned int num_raw_boards,
		       const unsigned int *board_table,
		       unsigned int **bucket_counts, unsigned int batch_index,
		       unsigned int num_threads,
		       unsigned long long int batch_size,
		       unsigned long long int *total_its) :
  buckets_(buckets) {
  betting_tree_ = betting_tree;
  boost_ = cc.Boost();
  regrets_ = regrets;
  sumprobs_ = sumprobs;
  action_sumprobs_ = action_sumprobs;
  num_raw_boards_ = num_raw_boards;
  board_table_ = board_table;
  bucket_counts_ = bucket_counts;
  batch_index_ = batch_index;
  num_threads_ = num_threads;
  batch_size_ = batch_size;
  total_its_ = total_its;
  max_street_ = Game::MaxStreet();
  num_players_ = Game::NumPlayers();
  canon_bds_.reset(new unsigned int[max_street_ + 1]);
  canon_bds_[0] = 0;
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  hole_cards_.reset(new unsigned int[num_players_ * num_hole_cards]);
  hi_cards_.reset(new unsigned int[num_players_]);
  lo_cards_.reset(new unsigned int[num_players_]);
  hvs_.reset(new unsigned int[num_players_]);
  current_buckets_ = new unsigned int *[num_players_];
  for (unsigned int p = 0; p < num_players_; ++p) {
    current_buckets_[p] = new unsigned int[max_street_ + 1];
  }
  srand48_r(batch_index_, &rand_buf_);
}

ECFRThread::~ECFRThread(void) {
  for (unsigned int p = 0; p < num_players_; ++p) {
    delete [] current_buckets_[p];
  }
  delete [] current_buckets_;
}

void ECFRThread::Deal(void) {
  double r;
  drand48_r(&rand_buf_, &r);
  unsigned int msbd = board_table_[(int)(r * num_raw_boards_)];
  canon_bds_[max_street_] = msbd;
  for (unsigned int st = 1; st < max_street_; ++st) {
    canon_bds_[st] = BoardTree::PredBoard(msbd, st);
  }
  const Card *board = BoardTree::Board(max_street_, msbd);
  Card cards[7];
  unsigned int num_ms_board_cards = Game::NumBoardCards(max_street_);
  for (unsigned int i = 0; i < num_ms_board_cards; ++i) {
    cards[i+2] = board[i];
  }
  int end_cards = Game::MaxCard() + 1;

  for (unsigned int p = 0; p < num_players_; ++p) {
    unsigned int c1, c2;
    while (true) {
      drand48_r(&rand_buf_, &r);
      c1 = end_cards * r;
      if (InCards(c1, board, num_ms_board_cards)) continue;
      if (InCards(c1, hole_cards_.get(), 2 * p)) continue;
      break;
    }
    hole_cards_[2 * p] = c1;
    while (true) {
      drand48_r(&rand_buf_, &r);
      c2 = end_cards * r;
      if (InCards(c2, board, num_ms_board_cards)) continue;
      if (InCards(c2, hole_cards_.get(), 2 * p + 1)) continue;
      break;
    }
    hole_cards_[2 * p + 1] = c2;
    if (c1 > c2) {hi_cards_[p] = c1; lo_cards_[p] = c2;}
    else         {hi_cards_[p] = c2; lo_cards_[p] = c1;}
  }

  for (unsigned int p = 0; p < num_players_; ++p) hvs_[p] = 0;

  for (unsigned int st = 0; st <= max_street_; ++st) {
    unsigned int bd = canon_bds_[st];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int p = 0; p < num_players_; ++p) {
      cards[0] = hi_cards_[p];
      cards[1] = lo_cards_[p];
      unsigned int hcp = HCPIndex(st, cards);
      unsigned int h = bd * num_hole_card_pairs + hcp;
      unsigned int b = buckets_.Bucket(st, h);
      current_buckets_[p][st] = b;
      if (st == max_street_) {
	hvs_[p] = HandValueTree::Val(cards);
      }
    }
  }

  if (hvs_[1] > hvs_[0]) {
    p1_showdown_mult_ = 1;
  } else if (hvs_[0] > hvs_[1]) {
    p1_showdown_mult_ = -1;
  } else {
    p1_showdown_mult_ = 0;
  }
}

double ECFRThread::Process(Node *node, bool adjust) {
  if (node->Terminal()) {
    if (node->Showdown()) {
      return showdown_mult_ * (double)node->LastBetTo();
    } else {
      // Player acting encodes player remaining at fold nodes
      // LastBetTo() doesn't include the last bet
      if (p_ == node->PlayerActing()) {
	return node->LastBetTo();
      } else {
	return -(double)node->LastBetTo();
      }
    }
  }
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  if (pa == p_) {
    unique_ptr<double []> current_probs(new double[num_succs]);
    unique_ptr<double []> succ_values(new double[num_succs]);
    double *my_regrets = regrets_[st][pa][nt];
    unsigned int b = current_buckets_[pa][st];
    double *b_regrets = my_regrets + b * num_succs;
    double sum = 0;
    for (unsigned int s = 0; s < num_succs; ++s) {
      double r = b_regrets[s];
      if (r > 0) sum += r;
    }
    if (sum == 0) {
      unsigned int dsi = node->DefaultSuccIndex();
      for (unsigned int s = 0; s < num_succs; ++s) {
	current_probs[s] = (s == dsi ? 1.0 : 0);
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	double r = b_regrets[s];
	if (r > 0) current_probs[s] = r / sum;
	else       current_probs[s] = 0;
      }
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_values[s] = Process(node->IthSucc(s), adjust);
    }
    double v = 0;
    for (unsigned int s = 0; s < num_succs; ++s) {
      v += current_probs[s] * succ_values[s];
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      b_regrets[s] += (succ_values[s] - v);
    }
    return v;
  } else {
    unique_ptr<double []> current_probs(new double[num_succs]);
    unsigned int b = current_buckets_[pa][st];
    double *b_regrets = regrets_[st][pa][nt] + b * num_succs;
    double sum = 0;
    for (unsigned int s = 0; s < num_succs; ++s) {
      double r = b_regrets[s];
      if (r > 0) sum += r;
    }
    if (sum == 0) {
      unsigned int dsi = node->DefaultSuccIndex();
      for (unsigned int s = 0; s < num_succs; ++s) {
	current_probs[s] = (s == dsi ? 1.0 : 0);
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	double r = b_regrets[s];
	if (r > 0) current_probs[s] = r / sum;
	else       current_probs[s] = 0;
      }
    }
    double *action_sumprobs = nullptr;
    if (boost_) {
      action_sumprobs = action_sumprobs_[st][pa][nt];
    }
    double *b_sumprobs = sumprobs_[st][pa][nt] + b * num_succs;
    for (unsigned int s = 0; s < num_succs; ++s) {
      double cp = current_probs[s];
      b_sumprobs[s] += cp;
      if (boost_) action_sumprobs[s] += cp;
    }
    if (adjust) {
      double sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	sum += action_sumprobs[s];
      }
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (action_sumprobs[s] < 0.01 * sum) {
	  double *regrets = regrets_[st][pa][nt];
	  fprintf(stderr, "Boosting st %u pa %u nt %u s %u\n", st, pa, nt, s);
	  unsigned int num_buckets = buckets_.NumBuckets(st);
	  for (unsigned int b = 0; b < num_buckets; ++b) {
	    double *b_regrets = regrets + b * num_succs;
	    // Hacky to have constant here.  Not sure what it should be.
	    // Maybe should vary by street.
	    b_regrets[s] += 1000;
	  }
	}
      }
    }
    double r;
    drand48_r(&rand_buf_, &r);
    unsigned int s;
    double cum = 0;
    for (s = 0; s < num_succs - 1; ++s) {
      cum += current_probs[s];
      if (r < cum) break;
    }
    return Process(node->IthSucc(s), adjust);
  }
}

#if 0
void ECFRThread::Relevel(Node *node) {
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  unsigned int dsi = node->DefaultSuccIndex();
  double *regrets = regrets_[st][pa][nt];
  unique_ptr<double []> sum_probs(new double[num_succs]);
  for (unsigned int s = 0; s < num_succs; ++s) {
    sum_probs[s] = 0;
  }
  unsigned int num_buckets = buckets_.NumBuckets(st);
  for (unsigned int b = 0; b < num_buckets; ++b) {
    double *b_regrets = regrets + b * num_succs;
    double sum = 0;
    for (unsigned int s = 0; s < num_succs; ++s) {
      double r = b_regrets[s];
      if (r > 0) sum += r;
    }
    if (sum == 0) {
      sum_probs[dsi] += 1.0;
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	double r = b_regrets[s];
	if (r > 0) sum_probs[s] += r / sum;
      }
    }
  }
  double sum_sum_probs = 0;
  for (unsigned int s = 0; s < num_succs; ++s) {
    sum_sum_probs += sum_probs[s];
  }
  unique_ptr<bool []> to_boost(new bool[num_succs]);
  bool boost_needed = false;
  for (unsigned int s = 0; s < num_succs; ++s) {
    // If we take this succ less than 1% of the time, then we will boost it.
    to_boost[s] = (sum_probs[s] < 0.01 * sum_sum_probs);
    if (to_boost[s]) boost_needed = true;
  }
  if (boost_needed) {
    // How do I know how much to boost regrets by?
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (! to_boost[s]) continue;
      double last_boost = 0;
      double boost = 1000;
      double mult = 2.0;
      while (true) {
	double sum_new_probs = 0;
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  double *b_regrets = regrets + b * num_succs;
	  double sum = 0;
	  for (unsigned int s1 = 0; s1 < num_succs; ++s1) {
	    double r;
	    if (s1 == s) r = b_regrets[s] + boost;
	    else         r = b_regrets[s];
	    if (r > 0) sum += r;
	  }
	  double new_prob;
	  if (sum == 0) {
	    new_prob = s == dsi ? 1.0 : 0;
	  } else {
	    double r = b_regrets[s] + boost;
	    if (r > 0) {
	      new_prob = r / sum;
	    }
	  }
	  sum_new_probs += new_prob;
	}
	// Whenever we would flip back to a previously evaluated boost,
	// instead reduce the multiplier.
	if (sum_new_probs > 0.01 * sum_sum_probs) {
	  if (sum_new_probs < 0.02 * sum_sum_probs) {
	    break;
	  } else {
	    double new_boost = boost / mult;
	    if (new_boost == last_boost) {
	      mult /= 2;
	      last_boost = boost;
	      new_boost = boost / mult;
	    } else {
	      last_boost = boost;
	      boost = new_boost;
	    }
	  }
	} else {
	  double new_boost = boost * mult;
	  if (new_boost == last_boost) {
	    mult /= 2;
	    last_boost = boost;
	    new_boost = boost * mult;
	  } else {
	    last_boost = boost;
	    boost = new_boost;
	  }
	}
      }
      for (unsigned int b = 0; b < num_buckets; ++b) {
	regrets[b * num_succs + s] += boost;
      }
    }
  }
  
  for (unsigned int s = 0; s < num_succs; ++s) {
    Relevel(node->IthSucc(s));
  }
}

void ECFRThread::Relevel(void) {
  Relevel(betting_tree_->Root());
}
#endif

void ECFRThread::Run(void) {
  it_ = 1;
  unique_ptr<double []> sum_values(new double[num_players_]);
  unique_ptr<unsigned long long int []> denoms(
			  new unsigned long long int[num_players_]);
  for (unsigned int p = 0; p < num_players_; ++p) {
    sum_values[p] = 0LL;
    denoms[p] = 0ULL;
  }

  while (1) {
    if (*total_its_ >= ((unsigned long long int)batch_size_) * num_threads_) {
      fprintf(stderr, "Thread %u performed %llu iterations\n",
	      batch_index_ % num_threads_, it_);
      break;
    }

    if (it_ % 10000000 == 1 && batch_index_ % num_threads_ == 0) {
      fprintf(stderr, "Batch %i it %llu\n", batch_index_, it_);
    }

    Deal();

    int start, end, incr;
    if (it_ % 2 == 0) {
      start = 0;
      end = num_players_;
      incr = 1;
    } else {
      start = num_players_ - 1;
      end = -1;
      incr = -1;
    }

    // Only start after iteration 1m.  Assume any previous batches would
    // have at least one million iterations.
    bool adjust = boost_ && (batch_index_ % num_threads_ == 0) &&
      (batch_index_ > 0 || it_ > 10000000);
    for (int p = start; p != end; p += incr) {
      p_ = p;
      // 1 if p_ wins at showdown; -1 if p_ loses at showdown; 0 if chop
      if (p_ == 1) showdown_mult_ = p1_showdown_mult_;
      else         showdown_mult_ = -p1_showdown_mult_;
      double val = Process(betting_tree_->Root(), adjust);
      sum_values[p_] += val;
      ++denoms[p_];
    }

    ++it_;
    if (it_ % 10000000 == 0 && batch_index_ % num_threads_ == 0) {
      for (unsigned int p = 0; p < num_players_; ++p) {
	fprintf(stderr, "It %llu avg P%u val %f\n", it_, p,
		sum_values[p] / (double)denoms[p]);
      }
    }
    if (num_threads_ == 1) {
      ++*total_its_;
    } else {
      if (it_ % 1000 == 0) {
	// To reduce the chance of multiple threads trying to update total_its_
	// at the same time, only update every 1000 iterations.
	*total_its_ += 1000;
      }
    }
  }
}

static void *thread_run(void *v_t) {
  ECFRThread *t = (ECFRThread *)v_t;
  t->Run();
  return NULL;
}

void ECFRThread::RunThread(void) {
  pthread_create(&pthread_id_, NULL, thread_run, this);
}

void ECFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

void ECFR::Run(void) {
  total_its_ = 0ULL;

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
}

void ECFR::RunBatch(unsigned int batch_size) {
  cfr_threads_ = new ECFRThread *[num_cfr_threads_];
  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    ECFRThread *cfr_thread =
      new ECFRThread(cfr_config_, buckets_, betting_tree_.get(), regrets_,
		     sumprobs_, action_sumprobs_, num_raw_boards_,
		     board_table_.get(), bucket_counts_, batch_base_ + i,
		     num_cfr_threads_, batch_size, &total_its_);
    cfr_threads_[i] = cfr_thread;
  }

  fprintf(stderr, "Running batch base %i\n", batch_base_);
  Run();
  fprintf(stderr, "Finished running batch base %i\n", batch_base_);

  for (unsigned int i = 0; i < num_cfr_threads_; ++i) {
    delete cfr_threads_[i];
  }
  delete [] cfr_threads_;
  cfr_threads_ = NULL;
}

void ECFR::Run(unsigned int start_batch_base, unsigned int end_batch_base,
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

  for (batch_base_ = start_batch_base;
       batch_base_ <= end_batch_base; batch_base_ += num_cfr_threads_) {
    RunBatch(batch_size);
    // This is a little ugly.  If our save interval is 800, we want to save
    // at batches 800, 1600, etc., but not at batch 0 even though 0 % 800 == 0.
    // But if our save interval is 8 (and we have eight threads), then we
    // do want to save at batch 0.
    if (batch_base_ % save_interval == 0 &&
	(batch_base_ > 0 || save_interval == num_cfr_threads_)) {
      Write(batch_base_);
      fprintf(stderr, "Checkpointed batch base %u\n", batch_base_);
    }
  }
}

void ECFR::ReadRegrets(Node *node, Reader ***readers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Reader *reader = readers[pa][st];
    unsigned int num_buckets = buckets_.NumBuckets(st);
    double *regrets = regrets_[st][pa][nt];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double *my_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	my_regrets[s] = reader->ReadDoubleOrDie();
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    ReadRegrets(node->IthSucc(s), readers);
  }
}

void ECFR::ReadSumprobs(Node *node, Reader ***readers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Reader *reader = readers[pa][st];
    unsigned int num_buckets = buckets_.NumBuckets(st);
    double *sumprobs = sumprobs_[st][pa][nt];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double *my_sumprobs = sumprobs + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	my_sumprobs[s] = reader->ReadDoubleOrDie();
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    ReadSumprobs(node->IthSucc(s), readers);
  }
}

void ECFR::ReadActionSumprobs(Node *node, Reader ***readers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Reader *reader = readers[pa][st];
    double *action_sumprobs = action_sumprobs_[st][pa][nt];
    for (unsigned int s = 0; s < num_succs; ++s) {
      action_sumprobs[s] = reader->ReadDoubleOrDie();
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    ReadActionSumprobs(node->IthSucc(s), readers);
  }
}

void ECFR::Read(unsigned int batch_base) {
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), 
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  Reader ***regret_readers = new Reader **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    regret_readers[p] = new Reader *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.d", dir,
	      st, batch_base, p);
      regret_readers[p][st] = new Reader(buf);
    }
  }
  Reader ***sumprob_readers = new Reader **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    sumprob_readers[p] = new Reader *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.i", dir,
	      st, batch_base, p);
      sumprob_readers[p][st] = new Reader(buf);
    }
  }
  Reader ***action_sumprob_readers = new Reader **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    action_sumprob_readers[p] = new Reader *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/action_sumprobs.x.0.0.%u.%u.p%u.i", dir,
	      st, batch_base, p);
      action_sumprob_readers[p][st] = new Reader(buf);
    }
  }
  ReadRegrets(betting_tree_->Root(), regret_readers);
  ReadSumprobs(betting_tree_->Root(), sumprob_readers);
  ReadActionSumprobs(betting_tree_->Root(), action_sumprob_readers);
  for (unsigned int p = 0; p <= 1; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
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
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (sumprob_readers[p][st] && ! sumprob_readers[p][st]->AtEnd()) {
	fprintf(stderr, "Sumprob reader didn't get to EOF\n");
	exit(-1);
      }
      delete sumprob_readers[p][st];
    }
    delete [] sumprob_readers[p];
  }
  delete [] sumprob_readers;
  for (unsigned int p = 0; p <= 1; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (action_sumprob_readers[p][st] &&
	  ! action_sumprob_readers[p][st]->AtEnd()) {
	fprintf(stderr, "Action sumprob reader didn't get to EOF\n");
	exit(-1);
      }
      delete action_sumprob_readers[p][st];
    }
    delete [] action_sumprob_readers[p];
  }
  delete [] action_sumprob_readers;
}

void ECFR::WriteRegrets(Node *node, Writer ***writers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Writer *writer = writers[pa][st];
    unsigned int num_buckets = buckets_.NumBuckets(st);
    double *regrets = regrets_[st][pa][nt];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double *my_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	writer->WriteDouble(my_regrets[s]);
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    WriteRegrets(node->IthSucc(s), writers);
  }
}

void ECFR::WriteSumprobs(Node *node, Writer ***writers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Writer *writer = writers[pa][st];
    unsigned int num_buckets = buckets_.NumBuckets(st);
    double *sumprobs = sumprobs_[st][pa][nt];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double *my_sumprobs = sumprobs + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	writer->WriteDouble(my_sumprobs[s]);
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    WriteSumprobs(node->IthSucc(s), writers);
  }
}

void ECFR::WriteActionSumprobs(Node *node, Writer ***writers) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int st = node->Street();
    unsigned int pa = node->PlayerActing();
    unsigned int nt = node->NonterminalID();
    Writer *writer = writers[pa][st];
    double *action_sumprobs = action_sumprobs_[st][pa][nt];
    for (unsigned int s = 0; s < num_succs; ++s) {
      writer->WriteDouble(action_sumprobs[s]);
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    WriteActionSumprobs(node->IthSucc(s), writers);
  }
}

void ECFR::Write(unsigned int batch_base) {
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  Mkdir(dir);
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  Writer ***regret_writers = new Writer **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    regret_writers[p] = new Writer *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.d", dir,
	      st, batch_base, p);
      regret_writers[p][st] = new Writer(buf);
    }
  }
  WriteRegrets(betting_tree_->Root(), regret_writers);
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete regret_writers[p][st];
    }
    delete [] regret_writers[p];
  }
  delete [] regret_writers;

  Writer ***sumprob_writers = new Writer **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    sumprob_writers[p] = new Writer *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.d", dir,
	      st, batch_base, p);
      sumprob_writers[p][st] = new Writer(buf);
    }
  }
  WriteSumprobs(betting_tree_->Root(), sumprob_writers);
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete sumprob_writers[p][st];
    }
    delete [] sumprob_writers[p];
  }
  delete [] sumprob_writers;

  Writer ***action_sumprob_writers = new Writer **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    action_sumprob_writers[p] = new Writer *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      sprintf(buf, "%s/action_sumprobs.x.0.0.%u.%u.p%u.d", dir,
	      st, batch_base, p);
      action_sumprob_writers[p][st] = new Writer(buf);
    }
  }
  WriteActionSumprobs(betting_tree_->Root(), action_sumprob_writers);
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete action_sumprob_writers[p][st];
    }
    delete [] action_sumprob_writers[p];
  }
  delete [] action_sumprob_writers;
}

void ECFR::Initialize(Node *node) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_buckets = buckets_.NumBuckets(st);
  unsigned int num = num_buckets * num_succs;
  regrets_[st][pa][nt] = new double[num];
  sumprobs_[st][pa][nt] = new double[num];
  for (unsigned int i = 0; i < num; ++i) {
    regrets_[st][pa][nt][i] = 0;
    sumprobs_[st][pa][nt][i] = 0;
  }
  action_sumprobs_[st][pa][nt] = new double[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    action_sumprobs_[st][pa][nt][s] = 0;
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Initialize(node->IthSucc(s));
  }
}

static unsigned int Factorial(unsigned int n) {
  if (n == 0) return 1;
  if (n == 1) return 1;
  return n * Factorial(n - 1);
}

ECFR::ECFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	   const CFRConfig &cc, const Buckets &buckets,
	   unsigned int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets) {
  num_cfr_threads_ = num_threads;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (buckets_.None(st)) {
      fprintf(stderr, "ECFR expects buckets on all streets\n");
      exit(-1);
    }
  }
  BoardTree::Create();
  BoardTree::BuildPredBoards();
  betting_tree_.reset(BettingTree::BuildTree(betting_abstraction_));

  BoardTree::BuildBoardCounts();
  num_raw_boards_ = 1;
  unsigned int num_remaining = Game::NumCardsInDeck();
  for (unsigned int st = 1; st <= max_street; ++st) {
    unsigned int num_street_cards = Game::NumCardsForStreet(st);
    unsigned int multiplier = 1;
    for (unsigned int n = (num_remaining - num_street_cards) + 1;
	 n <= num_remaining; ++n) {
      multiplier *= n;
    }
    num_raw_boards_ *= multiplier / Factorial(num_street_cards);
    num_remaining -= num_street_cards;
  }
  board_table_.reset(new unsigned int[num_raw_boards_]);
  unsigned int num_boards = BoardTree::NumBoards(max_street);
  unsigned int i = 0;
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    unsigned int ct = BoardTree::BoardCount(max_street, bd);
    for (unsigned int j = 0; j < ct; ++j) {
      board_table_[i++] = bd;
    }
  }
  if (i != num_raw_boards_) {
    fprintf(stderr, "Num raw board mismatch: %u, %u\n", i, num_raw_boards_);
    exit(-1);
  }

#if 0
  bucket_counts_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_buckets = buckets_.NumBuckets(st);
    bucket_counts_[st] = new unsigned int[num_buckets];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      bucket_counts_[st][b] = 0;
    }
  }
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      unsigned int ct = BoardTree::BoardCount(st, bd);
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int h = bd * num_hole_card_pairs + i;
	unsigned int b = buckets_.Bucket(st, h);
	bucket_counts_[st][b] += ct;
      }
    }
  }
#endif
  bucket_counts_ = nullptr;
  BoardTree::DeleteBoardCounts();

  regrets_ = new double ***[max_street];
  sumprobs_ = new double ***[max_street];
  action_sumprobs_ = new double ***[max_street];
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int st = 0; st <= max_street; ++st) {
    regrets_[st] = new double **[num_players];
    sumprobs_[st] = new double **[num_players];
    action_sumprobs_[st] = new double **[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p, st);
      regrets_[st][p] = new double *[num_nt];
      sumprobs_[st][p] = new double *[num_nt];
      action_sumprobs_[st][p] = new double *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	regrets_[st][p][i] = nullptr;
	sumprobs_[st][p][i] = nullptr;
	action_sumprobs_[st][p][i] = nullptr;
      }
    }
  }
  Initialize(betting_tree_->Root());

  HandValueTree::Create();
}

ECFR::~ECFR(void) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  if (bucket_counts_) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete [] bucket_counts_[st];
    }
    delete [] bucket_counts_;
  }
  for (unsigned int st = 0; st <= max_street; ++st) {
    for (unsigned int p = 0; p < num_players; ++p) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p, st);
      for (unsigned int i = 0; i < num_nt; ++i) {
	delete [] regrets_[st][p][i];
	delete [] sumprobs_[st][p][i];
	delete [] action_sumprobs_[st][p][i];
      }
      delete [] regrets_[st][p];
      delete [] sumprobs_[st][p];
      delete [] action_sumprobs_[st][p];
    }
    delete [] regrets_[st];
    delete [] sumprobs_[st];
    delete [] action_sumprobs_[st];
  }
  delete [] regrets_;
  delete [] sumprobs_;
  delete [] action_sumprobs_;
}
