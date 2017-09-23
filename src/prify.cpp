// Takes a bucketing for the previous street and an IR bucketing for the current
// streeet and creates a new perfect recall bucketing that remembers the
// bucket from the previous street.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <unordered_map>

#include "board_tree.h"
#include "constants.h"
#include "fast_hash.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"
#include "sparse_and_dense.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <prev bucketing> <IR bucketing> "
	  "<new bucketing> <street>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  string prev_bucketing = argv[2];
  string ir_bucketing = argv[3];
  string new_bucketing = argv[4];
  unsigned int st;
  if (sscanf(argv[5], "%u", &st) != 1) Usage(argv[0]);
  unsigned int max_street = Game::MaxStreet();
  if (st < 1 || st > max_street) {
    fprintf(stderr, "Street OOB\n");
    exit(-1);
  }
  unsigned int pst = st - 1;

  char buf[500];
  sprintf(buf, "%s/num_buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, prev_bucketing.c_str(), pst);
  Reader nb_reader(buf);
  unsigned long long int prev_num_buckets = nb_reader.ReadUnsignedIntOrDie();

  BoardTree::Create();
  unsigned int prev_num_boards = BoardTree::NumBoards(pst);
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  unsigned int prev_num_hands = prev_num_boards * prev_num_hole_card_pairs;
  unsigned int *prev_buckets = new unsigned int[prev_num_hands];
  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, prev_bucketing.c_str(), pst);
  Reader prev_reader(buf);
  long long int prev_file_size = prev_reader.FileSize();
  if (prev_file_size == 2 * prev_num_hands) {
    for (unsigned int h = 0; h < prev_num_hands; ++h) {
      prev_buckets[h] = prev_reader.ReadUnsignedShortOrDie();
    }
  } else if (prev_file_size == 4 * prev_num_hands) {
    for (unsigned int h = 0; h < prev_num_hands; ++h) {
      prev_buckets[h] = prev_reader.ReadUnsignedIntOrDie();
    }
  } else {
    fprintf(stderr, "Unexpected file size: %lli\n", prev_file_size);
    exit(-1);
  }

  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int num_hands = num_boards * num_hole_card_pairs;

  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, ir_bucketing.c_str(), st);
  Reader ir_reader(buf);
  long long int ir_file_size = ir_reader.FileSize();
  bool ir_shorts;
  if (ir_file_size == 2 * (long long int)num_hands) {
    ir_shorts = true;
  } else if (ir_file_size == 4 * (long long int)num_hands) {
    ir_shorts = false;
  } else {
    fprintf(stderr, "Unexpected file size B: %lli\n", ir_file_size);
    fprintf(stderr, "Num hands %u\n", num_hands);
    fprintf(stderr, "File: %s\n", buf);
    exit(-1);
  }

  BoardTree::CreateLookup();
  unsigned int *buckets = new unsigned int[num_hands];
  unsigned int num_board_cards = Game::NumBoardCards(st);
  unsigned int max_card = Game::MaxCard();
  Card cards[7];
  SparseAndDenseLong sad;
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    if (bd % 1000 == 0) fprintf(stderr, "bd %u/%u\n", bd, num_boards);
    const Card *board = BoardTree::Board(st, bd);
    unsigned int prev_bd = BoardTree::LookupBoard(board, pst);
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      cards[i + 2] = board[i];
    }
    unsigned int h = bd * num_hole_card_pairs;
    for (unsigned int hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, cards + 2, num_board_cards)) continue;
      cards[0] = hi;
      for (unsigned int lo = 0; lo < hi; ++lo) {
	if (InCards(lo, cards + 2, num_board_cards)) continue;
	cards[1] = lo;
	unsigned long long int ir_b;
	if (ir_shorts) {
	  ir_b = ir_reader.ReadUnsignedShortOrDie();
	} else {
	  ir_b = ir_reader.ReadUnsignedIntOrDie();
	}
	unsigned int prev_hcp = HCPIndex(pst, cards);
	unsigned int prev_h = prev_bd * prev_num_hole_card_pairs + prev_hcp;
	unsigned long long int prev_b = prev_buckets[prev_h];
	unsigned long long int sparse = ir_b * prev_num_buckets + prev_b;
	unsigned int b = sad.SparseToDense(sparse);
	buckets[h] = b;
	++h;
      }
    }
  }

  unsigned int num_buckets = sad.Num();
  bool short_buckets = num_buckets < 65536;

  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u",
	  Files::StaticBase(), Game::GameName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), new_bucketing.c_str(), st);
  Writer writer(buf);
  if (short_buckets) {
    for (unsigned int h = 0; h < num_hands; ++h) {
      writer.WriteUnsignedShort((unsigned short)buckets[h]);
    }
  } else {
    for (unsigned int h = 0; h < num_hands; ++h) {
      writer.WriteUnsignedInt(buckets[h]);
    }
  }

  sprintf(buf, "%s/num_buckets.%s.%u.%u.%u.%s.%u",
	  Files::StaticBase(), Game::GameName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), new_bucketing.c_str(), st);
  Writer writer2(buf);
  writer2.WriteUnsignedInt(num_buckets);
  printf("%u buckets\n", num_buckets);

  delete [] prev_buckets;
  delete [] buckets;
}
