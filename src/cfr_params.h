#ifndef _CFR_PARAMS_H_
#define _CFR_PARAMS_H_

#include <memory>

using namespace std;

class Params;

unique_ptr<Params> CreateCFRParams(void);

#endif
