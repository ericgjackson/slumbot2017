#ifndef _GAME_PARAMS_H_
#define _GAME_PARAMS_H_

#include <memory>

using namespace std;

class Params;

unique_ptr<Params> CreateGameParams(void);

#endif
