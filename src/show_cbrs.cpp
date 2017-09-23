#include <stdio.h>
#include <stdlib.h>

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
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

static double *LoadCBRs(unsigned int st, unsigned int pa, unsigned int nt,
			unsigned int bd, unsigned int p,
			const CardAbstraction &card_abstraction,
			const BettingAbstraction &betting_abstraction,
			const CFRConfig &cfr_config, unsigned int it) {
  char dir[500], buf[500];
  // This assumes two players
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s/cbrs.%u.p%u/%u.%u.%u",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(), 
	  cfr_config.CFRConfigName().c_str(), it, p, nt, st, pa);
  sprintf(buf, "%s/vals.%u", dir, bd);
	  
  Reader reader(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double *cvs = new double[num_hole_card_pairs];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    cvs[i] = reader.ReadFloatOrDie();
  }

  return cvs;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <st> <pa> <nt> <bd> <p>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 11) Usage(argv[0]);
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
  unique_ptr<CFRConfig> cfr_config(new CFRConfig(*cfr_params));
  unsigned int it, st, pa, nt, bd, p;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  if (sscanf(argv[6], "%u", &st) != 1) Usage(argv[0]);
  if (sscanf(argv[7], "%u", &pa) != 1) Usage(argv[0]);
  if (sscanf(argv[8], "%u", &nt) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%u", &bd) != 1) Usage(argv[0]);
  if (sscanf(argv[10], "%u", &p) != 1) Usage(argv[0]);

  double *cbrs = LoadCBRs(st, pa, nt, bd, p, *card_abstraction,
			  *betting_abstraction, *cfr_config, it);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    printf("cbrs[%u] %f\n", i, cbrs[i]);
  }
  delete [] cbrs;
}
