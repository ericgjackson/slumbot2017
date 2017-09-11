#ifndef _BETTING_ABSTRACTION_PARAMS_H_
#define _BETTING_ABSTRACTION_PARAMS_H_

#include <memory>

using namespace std;

class Params;

unique_ptr<Params> CreateBettingAbstractionParams(void);

#endif
