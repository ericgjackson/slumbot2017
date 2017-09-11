#ifndef _CARDS_H_
#define _CARDS_H_

#include <string>

using namespace std;

// These need to be before the include of game.h
typedef unsigned int Card;
Card MakeCard(int rank, int suit);

#include "game.h"

#define Rank(card)           (card / Game::NumSuits())
#define Suit(card)           (card % Game::NumSuits())

void OutputRank(unsigned int rank);
void OutputCard(Card card);
void CardName(Card c, string *name);
void OutputTwoCards(Card c1, Card c2);
void OutputTwoCards(const Card *cards);
void OutputThreeCards(Card c1, Card c2, Card c3);
void OutputThreeCards(const Card *cards);
void OutputFourCards(Card c1, Card c2, Card c3, Card c4);
void OutputFourCards(const Card *cards);
void OutputFiveCards(Card c1, Card c2, Card c3, Card c4, Card c5);
void OutputFiveCards(const Card *cards);
void OutputSixCards(Card c1, Card c2, Card c3, Card c4, Card c5, Card c6);
void OutputSixCards(const Card *cards);
void OutputSevenCards(Card c1, Card c2, Card c3, Card c4, Card c5,
		      Card c6, Card c7);
void OutputSevenCards(const Card *cards);
void OutputNCards(const Card *cards, unsigned int n);
Card ParseCard(const char *str);
void ParseTwoCards(const char *str, bool space_separated, Card *cards);
void ParseThreeCards(const char *str, bool space_separated, Card *cards);
void ParseFiveCards(const char *str, bool space_separated, Card *cards);
bool InCards(Card c, const Card *cards, unsigned int num_cards);
int MaxSuit(Card *board, unsigned int num_board);

#endif
