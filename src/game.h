#ifndef _GAME_H_
#define _GAME_H_

#include <memory>
#include <string>

#include "cards.h"

using namespace std;

class Params;

class Game {
 public:
  static void Initialize(const Params &params);
  static const string &GameName(void) {return game_name_;}
  static unsigned int MaxStreet(void) {return max_street_;}
  static unsigned int NumPlayers(void) {return num_players_;}
  static unsigned int NumRanks(void) {return num_ranks_;}
  static unsigned int HighRank(void) {return num_ranks_ - 1;}
  static unsigned int NumSuits(void) {return num_suits_;}
  static Card MaxCard(void) {
    return MakeCard(num_ranks_ - 1, num_suits_ - 1);
  }
  static unsigned int FirstToAct(unsigned int st) {return first_to_act_[st];}
  static unsigned int SmallBlind(void) {return small_blind_;}
  static unsigned int BigBlind(void) {return big_blind_;}
  static unsigned int Ante(void) {return ante_;}
  static unsigned int NumCardsForStreet(unsigned int st) {
    return num_cards_for_street_[st];
  }
  static unsigned int NumHoleCardPairs(unsigned int st) {
    return num_hole_card_pairs_[st];
  }
  static unsigned int NumBoardCards(unsigned int st) {
    return num_board_cards_[st];
  }
  static unsigned int NumCardsInDeck(void) {return num_cards_in_deck_;}
  static unsigned long long int NumCardPermutations(void) {
    return num_card_permutations_;
  }
  static unsigned int StreetPermutations(unsigned int street);
  static unsigned int StreetPermutations2(unsigned int street);
  static unsigned int BoardPermutations(unsigned int street);
 private:
  static string game_name_;
  static unsigned int max_street_;
  static unsigned int num_players_;
  static unsigned int num_ranks_;
  static unsigned int num_suits_;
  static unique_ptr<unsigned int []> first_to_act_;
  static unsigned int small_blind_;
  static unsigned int big_blind_;
  static unsigned int ante_;
  static unique_ptr<unsigned int []> num_cards_for_street_;
  static unsigned int num_cards_in_deck_;
  static unsigned long long int num_card_permutations_;
  static unique_ptr<unsigned int []> num_hole_card_pairs_;
  static unique_ptr<unsigned int []> num_board_cards_;
};

#endif
