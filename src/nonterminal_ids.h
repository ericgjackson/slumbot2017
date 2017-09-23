#ifndef _NONTERMINAL_IDS_H_
#define _NONTERMINAL_IDS_H_

class BettingTree;

void AssignNonterminalIDs(Node *root, unsigned int ***ret_num_nonterminals);
void AssignNonterminalIDs(BettingTree *betting_tree,
			  unsigned int ***ret_num_nonterminals);
unsigned int **CountNumNonterminals(Node *root);
unsigned int **CountNumNonterminals(BettingTree *betting_tree);

#endif
