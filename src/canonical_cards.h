#ifndef _CANONICAL_CARDS_H_
#define _CANONICAL_CARDS_H_

#include <memory>

#include "cards.h"

class CanonicalCards {
 public:
  CanonicalCards(void) {}
  CanonicalCards(unsigned int n, const Card *previous,
		 unsigned int num_previous,
		 unsigned int previous_suit_groups,
		 bool maintain_suit_groups);
  virtual ~CanonicalCards(void);
  void SortByHandStrength(const Card *board);
  static bool ToCanon2(const Card *cards, unsigned int num_cards,
		       unsigned int suit_groups, Card *canon_cards);
  static void ToCanon(const Card *cards, unsigned int num_cards,
		      unsigned int suit_groups, Card *canon_cards);
  unsigned int NumVariants(unsigned int i) const {return num_variants_[i];}
  unsigned int Canon(unsigned int i) const {return canon_[i];}
  unsigned int N(void) const {return n_;}
  unsigned int NumRaw(void) const {return num_raw_;}
  unsigned int NumCanon(void) const {return num_canon_;}
  const Card *Cards(unsigned int i) const {return &cards_[i * n_];}
  unsigned int HandValue(unsigned int i) const {return hand_values_[i];}
  unsigned int SuitGroups(unsigned int i) const {return suit_groups_[i];}
 protected:
  unsigned int NumMappings(const Card *cards, unsigned int n,
			   unsigned int old_suit_groups);

  unsigned int n_;
  unique_ptr<Card []> cards_;
  unique_ptr<unsigned int []> hand_values_;
  unique_ptr<unsigned char []> num_variants_;
  unique_ptr<unsigned int []> canon_;
  unsigned int num_raw_;
  unsigned int num_canon_;
  unique_ptr<unsigned int []> suit_groups_;
};

void UpdateSuitGroups(const Card *cards,
		      unsigned int num_cards,
		      const unsigned int old_suit_groups,
		      unsigned int *new_suit_groups);

#endif
