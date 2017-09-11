#ifndef _CARD_ABSTRACTION_PARAMS_H_
#define _CARD_ABSTRACTION_PARAMS_H_

#include <memory>

using namespace std;

class Params;

unique_ptr<Params> CreateCardAbstractionParams(void);

#endif
