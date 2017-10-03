// Combine feature values from multiple streets

#include <stdio.h>
#include <stdlib.h>

#include "board_tree.h"
#include "cards.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

using namespace std;

static unsigned int **BuildPredBoards(unsigned int st) {
  BoardTree::BuildPredBoards();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int **pred_boards = new unsigned int *[num_boards];
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    pred_boards[bd] = new unsigned int[st];
    for (unsigned int pst = 0; pst < st; ++pst) {
      pred_boards[bd][pst] = kMaxUInt;
    }
  }
  unsigned int num_ms_boards = BoardTree::NumBoards(max_street);
  for (unsigned int msbd = 0; msbd < num_ms_boards; ++msbd) {
    if (st == max_street) {
      for (unsigned int pst = 0; pst < st; ++pst) {
	pred_boards[msbd][pst] = BoardTree::PredBoard(msbd, pst);
      }
    } else {
      unsigned int bd = BoardTree::PredBoard(msbd, st);
      for (unsigned int pst = 0; pst < st; ++pst) {
	pred_boards[bd][pst] = BoardTree::PredBoard(msbd, pst);
      }
    }
  }
  BoardTree::DeletePredBoards();
  return pred_boards;
}

static void Go(unsigned int st, short **vals, unsigned int *num_features,
	       Writer *writer, double multiplier) {
  unsigned int **pred_boards = BuildPredBoards(st);
  unsigned int max_street = Game::MaxStreet();
  Card cards[7];
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int num_hands = num_boards * num_hole_card_pairs;
  unsigned int num_board_cards = Game::NumBoardCards(st);
  unsigned int max_card = Game::MaxCard();
  unsigned int h = 0;
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    if (bd % 1000 == 0) fprintf(stderr, "bd %u/%u\n", bd, num_boards);
    const Card *board = BoardTree::Board(st, bd);
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      cards[i + 2] = board[i];
    }
    for (unsigned int hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, cards + 2, num_board_cards)) continue;
      cards[0] = hi;
      for (unsigned int lo = 0; lo < hi; ++lo) {
	if (InCards(lo, cards + 2, num_board_cards)) continue;
	cards[1] = lo;
	for (unsigned int pst = 0; pst <= st; ++pst) {
	  if (vals[pst] == NULL) continue;
	  unsigned int ph;
	  if (pst < st) {
	    unsigned int pbd = pred_boards[bd][pst];
	    unsigned int phcp = HCPIndex(pst, cards);
	    unsigned int num_pred_hole_card_pairs = Game::NumHoleCardPairs(pst);
	    ph = pbd * num_pred_hole_card_pairs + phcp;
	  } else {
	    ph = h;
	  }
	  unsigned int num_f = num_features[pst];
	  for (unsigned int f = 0; f < num_f; ++f) {
	    int v = vals[pst][ph * num_f + f];
	    if (pst == max_street) {
	      v *= multiplier;
	      if (v > kMaxShort || v < kMinShort) {
		fprintf(stderr, "v %i too extreme\n", v);
		exit(-1);
	      }
	    }
	    writer->WriteShort((short)v);
	  }
	}
	++h;
      }
    }
  }
  if (h != num_hands) {
    fprintf(stderr, "Saw %u hands expected %u\n", h, num_hands);
    exit(-1);
  }
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    delete [] pred_boards[bd];
  }
  delete [] pred_boards;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <street> <preflop features> "
	  "<flop features> <turn features> <river features> "
	  "<combined features> <last multiplier>\n", prog_name);
  fprintf(stderr, "Use \"null\" for unused streets\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 9) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unsigned int street;
  if (sscanf(argv[2], "%u", &street) != 1) Usage(argv[0]);
  string preflop_features = argv[3];
  string flop_features = argv[4];
  string turn_features = argv[5];
  string river_features = argv[6];
  string new_features = argv[7];
  double multiplier;
  if (sscanf(argv[8], "%lf", &multiplier) != 1) Usage(argv[0]);

  BoardTree::Create();

  unsigned int num_combined_features = 0;
  unsigned int *num_features = new unsigned int[street + 1];
  short **vals = new short *[street + 1];
  for (unsigned int st = 0; st <= street; ++st) {
    string features;
    if (st == 0)      features = preflop_features;
    else if (st == 1) features = flop_features;
    else if (st == 2) features = turn_features;
    else              features = river_features;
    if (features == "null") {
      num_features[st] = 0;
      vals[st] = NULL;
      continue;
    }
    char buf[500];
    sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), features.c_str(), st);
    Reader reader(buf);
    unsigned int num_f = reader.ReadUnsignedIntOrDie();
    num_features[st] = num_f;
    num_combined_features += num_f;
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int num_hands = num_boards * num_hole_card_pairs;
    unsigned int num_vals = num_hands * num_f;
    vals[st] = new short[num_vals];
    for (unsigned int i = 0; i < num_vals; ++i) {
      vals[st][i] = reader.ReadShortOrDie();
    }
  }
  char buf[500];
  sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), new_features.c_str(),
	  street);
  Writer writer(buf);
  writer.WriteUnsignedInt(num_combined_features);

  Go(street, vals, num_features, &writer, multiplier);

  for (unsigned int st = 0; st <= street; ++st) {
    delete [] vals[st];
  }
  delete [] vals;
}
