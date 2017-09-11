#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "cards.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_evaluator.h"
#include "io.h"
#include "params.h"

using namespace std;

static void DealOneCard(void) {
  Card max_card = Game::MaxCard();
  Card c1;
  unsigned int *tree = new unsigned int[max_card + 1];
  for (c1 = 0; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    unsigned int i1 = c1;
    tree[i1] = Rank(c1);
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.1", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 0; i1 <= (unsigned int)max_card; ++i1) {
    writer.WriteUnsignedInt(tree[i1]);
  }
}

static void DealTwoCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[2];
  Card c1, c2;
  unsigned int **tree = new unsigned int *[max_card + 1];
  for (c1 = 1; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int *tree1 = new unsigned int[i1];
    tree[i1] = tree1;
    for (c2 = 0; c2 < c1; ++c2) {
      printf("  ");
      OutputCard(c2);
      printf("\n");
      fflush(stdout);
      cards[1] = c2;
      tree1[(int)c2] = he->Evaluate(cards, 2);
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.2", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 1; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int *tree1 = tree[i1];
    for (unsigned int i2 = 0; i2 < i1; ++i2) {
      writer.WriteUnsignedInt(tree1[i2]);
    }
  }
}

static void DealThreeCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[3];
  Card c1, c2, c3;
  unsigned int ***tree = new unsigned int **[max_card + 1];
  for (c1 = 2; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int **tree1 = new unsigned int *[i1];
    tree[i1] = tree1;
    for (c2 = 1; c2 < c1; ++c2) {
      cards[1] = c2;
      unsigned int i2 = c2;
      unsigned int *tree2 = new unsigned int[i2];
      tree1[i2] = tree2;
      for (c3 = 0; c3 < c2; ++c3) {
	cards[2] = c3;
	unsigned int i3 = c3;
	tree2[i3] = he->Evaluate(cards, 3);
      }
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.3", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 2; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int **tree1 = tree[i1];
    for (unsigned int i2 = 1; i2 < i1; ++i2) {
      unsigned int *tree2 = tree1[i2];
      for (unsigned int i3 = 0; i3 < i2; ++i3) {
	writer.WriteUnsignedInt(tree2[i3]);
      }
    }
  }
}

static void DealFourCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[4];
  Card c1, c2, c3, c4;
  unsigned int ****tree = new unsigned int ***[max_card + 1];
  for (c1 = 3; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int ***tree1 = new unsigned int **[i1];
    tree[i1] = tree1;
    for (c2 = 2; c2 < c1; ++c2) {
      cards[1] = c2;
      unsigned int i2 = c2;
      unsigned int **tree2 = new unsigned int *[i2];
      tree1[i2] = tree2;
      for (c3 = 1; c3 < c2; ++c3) {
	cards[2] = c3;
	unsigned int i3 = c3;
	unsigned int *tree3 = new unsigned int[i3];
	tree2[i3] = tree3;
	for (c4 = 0; c4 < c3; ++c4) {
	  cards[3] = c4;
	  unsigned int i4 = c4;
	  tree3[i4] = he->Evaluate(cards, 4);
	}
      }
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.4", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 3; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int ***tree1 = tree[i1];
    for (unsigned int i2 = 2; i2 < i1; ++i2) {
      unsigned int **tree2 = tree1[i2];
      for (unsigned int i3 = 1; i3 < i2; ++i3) {
	unsigned int *tree3 = tree2[i3];
	for (unsigned int i4 = 0; i4 < i3; ++i4) {
	  writer.WriteUnsignedInt(tree3[i4]);
	}
      }
    }
  }
}

// Is this Holdem specific?
static void DealFiveCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[7];
  Card c1, c2, c3, c4, c5;
  unsigned int *****tree = new unsigned int ****[max_card + 1];
  for (c1 = 4; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int ****tree1 = new unsigned int ***[i1];
    tree[i1] = tree1;
    for (c2 = 3; c2 < c1; ++c2) {
      fflush(stdout);
      cards[1] = c2;
      unsigned int i2 = c2;
      unsigned int ***tree2 = new unsigned int **[i2];
      tree1[i2] = tree2;
      for (c3 = 2; c3 < c2; ++c3) {
	cards[2] = c3;
	unsigned int i3 = c3;
	unsigned int **tree3 = new unsigned int *[i3];
	tree2[i3] = tree3;
	for (c4 = 1; c4 < c3; ++c4) {
	  cards[3] = c4;
	  unsigned int i4 = c4;
	  unsigned int *tree4 = new unsigned int[i4];
	  tree3[i4] = tree4;
	  for (c5 = 0; c5 < c4; ++c5) {
	    cards[4] = c5;
	    unsigned int i5 = c5;
	    tree4[i5] = he->Evaluate(cards, 5);
	  }
	}
      }
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.5", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 4; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int ****tree1 = tree[i1];
    for (unsigned int i2 = 3; i2 < i1; ++i2) {
      unsigned int ***tree2 = tree1[i2];
      for (unsigned int i3 = 2; i3 < i2; ++i3) {
	unsigned int **tree3 = tree2[i3];
	for (unsigned int i4 = 1; i4 < i3; ++i4) {
	  unsigned int *tree4 = tree3[i4];
	  for (unsigned int i5 = 0; i5 < i4; ++i5) {
	    writer.WriteUnsignedInt(tree4[i5]);
	  }
	}
      }
    }
  }
}

static void DealSixCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[7];
  Card c1, c2, c3, c4, c5, c6;
  unsigned int ******tree = new unsigned int *****[max_card + 1];
  for (c1 = 5; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int *****tree1 = new unsigned int ****[i1];
    tree[i1] = tree1;
    for (c2 = 4; c2 < c1; ++c2) {
      cards[1] = c2;
      unsigned int i2 = c2;
      unsigned int ****tree2 = new unsigned int ***[i2];
      tree1[i2] = tree2;
      for (c3 = 3; c3 < c2; ++c3) {
	cards[2] = c3;
	unsigned int i3 = c3;
	unsigned int ***tree3 = new unsigned int **[i3];
	tree2[i3] = tree3;
	for (c4 = 2; c4 < c3; ++c4) {
	  cards[3] = c4;
	  unsigned int i4 = c4;
	  unsigned int **tree4 = new unsigned int *[i4];
	  tree3[i4] = tree4;
	  for (c5 = 1; c5 < c4; ++c5) {
	    cards[4] = c5;
	    unsigned int i5 = c5;
	    unsigned int *tree5 = new unsigned int[i5];
	    tree4[i5] = tree5;
	    for (c6 = 0; c6 < c5; ++c6) {
	      cards[5] = c6;
	      unsigned int i6 = c6;
	      tree5[i6] = he->Evaluate(cards, 6);
	    }
	  }
	}
      }
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.6", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 5; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int *****tree1 = tree[i1];
    for (unsigned int i2 = 4; i2 < i1; ++i2) {
      unsigned int ****tree2 = tree1[i2];
      for (unsigned int i3 = 3; i3 < i2; ++i3) {
	unsigned int ***tree3 = tree2[i3];
	for (unsigned int i4 = 2; i4 < i3; ++i4) {
	  unsigned int **tree4 = tree3[i4];
	  for (unsigned int i5 = 1; i5 < i4; ++i5) {
	    unsigned int *tree5 = tree4[i5];
	    for (unsigned int i6 = 0; i6 < i5; ++i6) {
	      writer.WriteUnsignedInt(tree5[i6]);
	    }
	  }
	}
      }
    }
  }
}

// This is not as general as it could be.  Holdem specific.
static void DealSevenCards(HandEvaluator *he) {
  Card max_card = Game::MaxCard();
  Card cards[7];
  Card c1, c2, c3, c4, c5, c6, c7;
  unsigned int *******tree = new unsigned int ******[max_card + 1];
  for (c1 = 6; c1 <= max_card; ++c1) {
    OutputCard(c1);
    printf("\n");
    fflush(stdout);
    cards[0] = c1;
    unsigned int i1 = c1;
    unsigned int ******tree1 = new unsigned int *****[i1];
    tree[i1] = tree1;
    for (c2 = 5; c2 < c1; ++c2) {
      printf("  ");
      OutputCard(c2);
      printf("\n");
      fflush(stdout);
      cards[1] = c2;
      unsigned int i2 = c2;
      unsigned int *****tree2 = new unsigned int ****[i2];
      tree1[i2] = tree2;
      for (c3 = 4; c3 < c2; ++c3) {
	cards[2] = c3;
	unsigned int i3 = c3;
	unsigned int ****tree3 = new unsigned int ***[i3];
	tree2[i3] = tree3;
	for (c4 = 3; c4 < c3; ++c4) {
	  cards[3] = c4;
	  unsigned int i4 = c4;
	  unsigned int ***tree4 = new unsigned int **[i4];
	  tree3[i4] = tree4;
	  for (c5 = 2; c5 < c4; ++c5) {
	    cards[4] = c5;
	    unsigned int i5 = c5;
	    unsigned int **tree5 = new unsigned int *[i5];
	    tree4[i5] = tree5;
	    for (c6 = 1; c6 < c5; ++c6) {
	      cards[5] = c6;
	      unsigned int i6 = c6;
	      unsigned int *tree6 = new unsigned int[i6];
	      tree5[i6] = tree6;
	      for (c7 = 0; c7 < c6; ++c7) {
		cards[6] = c7;
		unsigned int i7 = c7;
		tree6[i7] = he->Evaluate(cards, 7);
	      }
	    }
	  }
	}
      }
    }
  }
  char buf[500];
  sprintf(buf, "%s/hand_value_tree.%s.%i.%i.7", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits());
  Writer writer(buf);
  for (unsigned int i1 = 6; i1 <= (unsigned int)max_card; ++i1) {
    unsigned int ******tree1 = tree[i1];
    for (unsigned int i2 = 5; i2 < i1; ++i2) {
      unsigned int *****tree2 = tree1[i2];
      for (unsigned int i3 = 4; i3 < i2; ++i3) {
	unsigned int ****tree3 = tree2[i3];
	for (unsigned int i4 = 3; i4 < i3; ++i4) {
	  unsigned int ***tree4 = tree3[i4];
	  for (unsigned int i5 = 2; i5 < i4; ++i5) {
	    unsigned int **tree5 = tree4[i5];
	    for (unsigned int i6 = 1; i6 < i5; ++i6) {
	      unsigned int *tree6 = tree5[i6];
	      for (unsigned int i7 = 0; i7 < i6; ++i7) {
		writer.WriteUnsignedInt(tree6[i7]);
	      }
	    }
	  }
	}
      }
    }
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <config file>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);

  HandEvaluator *he = HandEvaluator::Create(Game::GameName());
  unsigned int num_cards = 0;
  for (unsigned int s = 0; s <= Game::MaxStreet(); ++s) {
    num_cards += Game::NumCardsForStreet(s);
  }
  if (num_cards == 1) {
    DealOneCard();
  } else if (num_cards == 2) {
    DealTwoCards(he);
  } else if (num_cards == 3) {
    DealThreeCards(he);
  } else if (num_cards == 4) {
    DealFourCards(he);
  } else if (num_cards == 5) {
    DealFiveCards(he);
  } else if (num_cards == 6) {
    DealSixCards(he);
  } else if (num_cards == 7) {
    DealSevenCards(he);
  } else {
    fprintf(stderr, "Unsupported number of cards: %u\n", num_cards);
    exit(-1);
  }
  delete he;
}
