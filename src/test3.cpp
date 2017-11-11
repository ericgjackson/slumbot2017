#include <stdio.h>
#include <stdlib.h>

#include <unordered_set>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

using namespace std;

void Show(Node *node, const string &action_sequence,
	  const string &target_action_sequence,
	  const Buckets &buckets, const BettingTree *betting_tree,
	  const CardAbstraction &card_abstraction,
	  const BettingAbstraction &betting_abstraction,
	  const CFRConfig &cfr_config, unsigned int it) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  fprintf(stderr, "%s\n", action_sequence.c_str());
  if (action_sequence == target_action_sequence) {
    unsigned int nt = node->NonterminalID();
    unsigned int pa = node->PlayerActing();
    unsigned int st = node->Street();

    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      players[p] = (p == pa);
    }
    unsigned int max_street = Game::MaxStreet();
    unique_ptr<bool []> streets(new bool[max_street + 1]);
    for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
      streets[st1] = (st1 == st);
    }
    CFRValues sumprobs(players.get(), true, streets.get(), betting_tree,
		       0, 0, card_abstraction, buckets, nullptr);
    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
	    card_abstraction.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction.BettingAbstractionName().c_str(),
	    cfr_config.CFRConfigName().c_str());
    sumprobs.Read(dir, it, betting_tree->Root(), "x", kMaxUInt);
    int *i_values = nullptr;
    if (sumprobs.Ints(pa, st)) {
      sumprobs.Values(pa, st, nt, &i_values);
    } else {
      fprintf(stderr, "Doubles?!?\n");
      exit(-1);
    }
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unordered_set<unsigned int> ak_buckets;
    Card cards[7];
    long long int overall_sum = 0LL;
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      const Card *board = BoardTree::Board(st, bd);
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	cards[i + 2] = board[i];
      }
      for (unsigned int s1 = 0; s1 <= 3; ++s1) {
	Card ace = MakeCard(12, s1);
	if (InCards(ace, board, num_board_cards)) continue;
	cards[0] = ace;
	for (unsigned int s2 = 0; s2 <= 3; ++s2) {
	  if (s1 == s2) continue;
	  Card king = MakeCard(11, s2);
	  if (InCards(king, board, num_board_cards)) continue;
	  cards[1] = king;
	  unsigned int hcp = HCPIndex(st, cards);
	  unsigned int h = bd * num_hole_card_pairs + hcp;
	  unsigned int b = buckets.Bucket(st, h);
	  ak_buckets.insert(b);
	}
      }
    }
    unordered_set<unsigned int>::iterator it;
    for (it = ak_buckets.begin(); it != ak_buckets.end(); ++it) {
      unsigned int b = *it;
      unsigned int offset = b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	overall_sum += i_values[offset + s];
	if (st == 0) {
	  fprintf(stderr, "s %u sp %u\n", s, i_values[offset + s]);
	}
      }
    }
    fprintf(stderr, "Overall sum: %lli\n", overall_sum);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Show(node->IthSucc(s), action_sequence + action, target_action_sequence,
	 buckets, betting_tree, card_abstraction, betting_abstraction,
	 cfr_config, it);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <action>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 7) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params = CreateCardAbstractionParams();
  card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction(new CardAbstraction(*card_params));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params = CreateCFRParams();
  cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig>
    cfr_config(new CFRConfig(*cfr_params));
  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  string target_action_sequence = argv[6];

  BoardTree::Create();
  // Excessive to load all buckets.
  Buckets buckets(*card_abstraction, false);
  unique_ptr<BettingTree>
    betting_tree(BettingTree::BuildTree(*betting_abstraction));

  Show(betting_tree->Root(), "x", target_action_sequence, buckets, 
       betting_tree.get(), *card_abstraction, *betting_abstraction,
       *cfr_config, it);
}
