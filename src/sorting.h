#ifndef _SORTING_H_
#define _SORTING_H_

#include <string>
#include <vector>

#include "cards.h"

using namespace std;

void SortCards(Card *cards, unsigned int n);

struct PDILowerCompare {
  bool operator()(const pair<double, int> &p1,
		  const pair<double, int> &p2) {
    if (p1.first < p2.first) {
      return true;
    } else {
      return false;
    }
  }
};

extern PDILowerCompare g_pdi_lower_compare;

struct PDUILowerCompare {
  bool operator()(const pair<double, unsigned int> &p1,
		  const pair<double, unsigned int> &p2) {
    if (p1.first < p2.first) {
      return true;
    } else {
      return false;
    }
  }
};

extern PDUILowerCompare g_pdui_lower_compare;

struct PUIUILowerCompare {
  bool operator()(const pair<unsigned int, unsigned int> &p1,
		  const pair<unsigned int, unsigned int> &p2) {
    if (p1.first < p2.first) {
      return true;
    } else {
      return false;
    }
  }
};

extern PUIUILowerCompare g_puiui_lower_compare;

struct PDSHigherCompare {
  bool operator()(const pair<double, string> &p1,
		  const pair<double, string> &p2) {
    if (p1.first > p2.first) {
      return true;
    } else {
      return false;
    }
  }
};

extern PDSHigherCompare g_pds_higher_compare;

struct PFUILowerCompare {
  bool operator()(const pair<float, unsigned int> &p1,
		  const pair<float, unsigned int> &p2) {
    if (p1.first < p2.first) {
      return true;
    } else {
      return false;
    }
  }
};

extern PFUILowerCompare g_pfui_lower_compare;

#endif
