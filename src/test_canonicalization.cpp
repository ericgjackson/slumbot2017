// 7s7h2s->7d7c2c.
// 7s7h2h->7d7c2c.

#include <stdio.h>
#include <stdlib.h>

#include "canonical_cards.h"
#include "canonical.h"
#include "cards.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  // CanonicalCards cards(1, nullptr, 0, 0, true);
  Card cards[3], canon_cards[3], canon_cards2[3];
  cards[0] = MakeCard(5, 3);
  cards[1] = MakeCard(5, 2);
  cards[2] = MakeCard(0, 3);
  printf("Raw cards: ");
  OutputThreeCards(cards);
  printf("\n");
  unsigned int suit_groups = 0;
  CanonicalCards::ToCanon(cards, 3, suit_groups, canon_cards);
  printf("Canon cards: ");
  OutputThreeCards(canon_cards);
  printf("\n");
  CanonicalizeCards(cards, 1, 1, canon_cards2);
  printf("Canon cards2: ");
  OutputThreeCards(canon_cards2);
  printf("\n");
  exit(0);
  
  // 2c2h2d
  cards[0] = MakeCard(0, 0);
  cards[1] = MakeCard(0, 2);
  cards[2] = MakeCard(0, 2);
  printf("Raw cards: ");
  OutputThreeCards(cards);
  printf("\n");
  suit_groups = 0;

  CanonicalCards::ToCanon(cards, 1, suit_groups, canon_cards);
  unsigned int new_suit_groups;
  UpdateSuitGroups(cards, 1, suit_groups, &new_suit_groups);
  suit_groups = new_suit_groups;
  printf("First canon card: ");
  OutputCard(canon_cards[0]);
  printf("\n");
  printf("Suit groups: %i %i %i %i\n",
	 (int)((unsigned char *)&suit_groups)[0],
	 (int)((unsigned char *)&suit_groups)[1],
	 (int)((unsigned char *)&suit_groups)[2],
	 (int)((unsigned char *)&suit_groups)[3]);

  CanonicalCards::ToCanon(cards + 1, 1, suit_groups, canon_cards + 1);
  UpdateSuitGroups(cards + 1, 1, suit_groups, &new_suit_groups);
  suit_groups = new_suit_groups;
  printf("Second canon card: ");
  OutputCard(canon_cards[1]);
  printf("\n");
  printf("Suit groups: %i %i %i %i\n",
	 (int)((unsigned char *)&suit_groups)[0],
	 (int)((unsigned char *)&suit_groups)[1],
	 (int)((unsigned char *)&suit_groups)[2],
	 (int)((unsigned char *)&suit_groups)[3]);

  CanonicalCards::ToCanon(cards + 2, 1, suit_groups, canon_cards + 2);
  UpdateSuitGroups(cards + 2, 1, suit_groups, &new_suit_groups);
  suit_groups = new_suit_groups;
  printf("Third canon card: ");
  OutputCard(canon_cards[2]);
  printf("\n");
  printf("Suit groups: %i %i %i %i\n",
	 (int)((unsigned char *)&suit_groups)[0],
	 (int)((unsigned char *)&suit_groups)[1],
	 (int)((unsigned char *)&suit_groups)[2],
	 (int)((unsigned char *)&suit_groups)[3]);
}

