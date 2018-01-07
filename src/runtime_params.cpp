#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "params.h"
#include "runtime_params.h"

using namespace std;

unique_ptr<Params> CreateRuntimeParams(void) {
  unique_ptr<Params> params(new Params());
  params->AddParam("RuntimeConfigName", P_STRING);
  params->AddParam("Iteration", P_INT);
  params->AddParam("QuantizedStreets", P_STRING);
  params->AddParam("MinProb", P_DOUBLE);
  params->AddParam("FoldRoundUp", P_DOUBLE);
  params->AddParam("FoldRoundDown", P_DOUBLE);
  params->AddParam("Purify", P_BOOLEAN);
  params->AddParam("SampledPurify", P_BOOLEAN);
  params->AddParam("ProbsInMemory", P_BOOLEAN);
  params->AddParam("BucketMemoryStreets", P_STRING);
  params->AddParam("RespectPotFrac", P_BOOLEAN);
  params->AddParam("UseSupervisor", P_BOOLEAN);
  params->AddParam("FoldToAlternativeStreets", P_STRING);
  params->AddParam("CallAlternativeStreets", P_STRING);
  params->AddParam("FTANN", P_BOOLEAN);
  params->AddParam("EvalOverrides", P_BOOLEAN);
  params->AddParam("OverrideMinPotSize", P_INT);
  params->AddParam("MinNeighborFolds", P_INT);
  params->AddParam("MinNeighborFrac", P_DOUBLE);
  params->AddParam("MinAlternativeFolds", P_INT);
  params->AddParam("MinActualAlternativeFolds", P_INT);
  params->AddParam("MinFracAlternativeFolded", P_DOUBLE);
  params->AddParam("PriorAlternatives", P_BOOLEAN);
  params->AddParam("FewerAllInBets", P_BOOLEAN);
  params->AddParam("TranslateToLarger", P_BOOLEAN);
  params->AddParam("TranslateBetToCall", P_BOOLEAN);
  params->AddParam("NoSmallBets", P_BOOLEAN);
  params->AddParam("TranslationMethod", P_INT);
  params->AddParam("PreflopProbType", P_STRING);
  params->AddParam("FlopProbType", P_STRING);
  params->AddParam("TurnProbType", P_STRING);
  params->AddParam("RiverProbType", P_STRING);
  // UISP, UIR, etc.
  params->AddParam("PreflopProbSource", P_STRING);
  params->AddParam("FlopProbSource", P_STRING);
  params->AddParam("TurnProbSource", P_STRING);
  params->AddParam("RiverProbSource", P_STRING);
  params->AddParam("QMinProb", P_DOUBLE);
  params->AddParam("QFoldRoundUp", P_DOUBLE);
  params->AddParam("NumToBlend", P_INT);
  params->AddParam("HardCodedRootStrategy", P_BOOLEAN);
  params->AddParam("HardCodedR200Strategy", P_BOOLEAN);
  params->AddParam("HardCodedR250Strategy", P_BOOLEAN);
  params->AddParam("HardCodedR200R600Strategy", P_BOOLEAN);
  params->AddParam("HardCodedR200R800Strategy", P_BOOLEAN);
  params->AddParam("NearestNeighbors", P_BOOLEAN);
  params->AddParam("NNDisk", P_BOOLEAN);

  return params;
}
