#include <stdio.h>
#include <stdlib.h>

#include "cfr_value_type.h"

#if 0
unsigned int ValueSize(CFRValueType t) {
  if (t == CFR_CHAR)        return sizeof(unsigned char);
  else if (t == CFR_SHORT)  return sizeof(unsigned short);
  else if (t == CFR_INT)    return sizeof(int);
  else if (t == CFR_DOUBLE) return sizeof(double);
  else if (t == CFR_DOUBLE) return sizeof(double);
  fprintf(stderr, "Unknown value type %i\n", (int)t);
  exit(-1);
}
#endif
