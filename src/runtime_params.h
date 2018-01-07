#ifndef _RUNTIME_PARAMS_H_
#define _RUNTIME_PARAMS_H_

#include <memory>

using namespace std;

class Params;

unique_ptr<Params> CreateRuntimeParams(void);

#endif
