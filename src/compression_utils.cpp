#include <stdio.h>
#include <stdlib.h>

#include "compression_utils.h"
// #include "compressor.h"
#include "io.h"

// This would come from compressor.h if we were including it
#define COMPRESSOR_DISTRIBUTION_SIZE		256

const int64_t g_defaultDistribution[COMPRESSOR_DISTRIBUTION_SIZE] = {
  3094512, 3142245, 1152792, 1175333, 575302, 589007, 341972, 354210, 227822, 237473, 163464, 170516, 121951, 127527, 92873, 96867, 71306, 73767, 57401, 59632, 48079, 50387, 39587, 40906, 
  32332, 33909, 26627, 27953, 22723, 23575, 19548, 20464, 17077, 17835, 14806, 15286, 12879, 13370, 11711, 11746, 10507, 10639, 9154, 9431, 8418, 8542, 7444, 7845, 6531, 6655, 5601, 5787, 
  5182, 5386, 4446, 4718, 4016, 4349, 3549, 3767, 3254, 3435, 2900, 3097, 2655, 2793, 2412, 2467, 2123, 2255, 1958, 1991, 1827, 1964, 1777, 1722, 1615, 1555, 1507, 1517, 1489, 1466, 1358, 
  1367, 1290, 1273, 1295, 1218, 1239, 1173, 1156, 1085, 1081, 1103, 988, 986, 892, 843, 763, 780, 775, 751, 745, 821, 827, 719, 710, 666, 626, 579, 568, 507, 579, 499, 517, 492, 498, 439, 
  534, 467, 461, 444, 445, 459, 464, 436, 436, 361, 430, 372, 355, 333, 373, 330, 340, 345, 331, 306, 333, 273, 329, 266, 280, 277, 296, 262, 270, 242, 273, 250, 266, 236, 257, 220, 240, 
  220, 257, 216, 235, 195, 233, 204, 238, 184, 216, 179, 186, 174, 194, 179, 204, 146, 176, 177, 216, 168, 162, 171, 194, 144, 173, 143, 143, 149, 170, 144, 130, 151, 107, 130, 127, 123, 
  131, 127, 133, 138, 119, 119, 122, 109, 124, 102, 114, 102, 101, 97, 105, 84, 107, 101, 87, 75, 95, 90, 102, 62, 120, 76, 77, 82, 99, 75, 84, 70, 88, 65, 96, 64, 106, 65, 76, 67, 67, 
  73, 73, 62, 68, 54, 71, 56, 81, 48, 63, 59, 65, 51, 54, 40, 62, 40, 48, 44, 67, 33, 55, 5764
};

#if 0
void CompressCallback(void *context, int channel, unsigned char *buf, int n) {
  Writer *writer = (Writer *)context;
  writer->WriteNBytes(buf, n);
}

// This routine copies *compressed* bytes into buf.
void DecompressCallback(void *context, int channel, unsigned char *buf, int n) {
  Reader *reader = (Reader *)context;
  // Will there always be n bytes available to read?  Not sure.
  reader->ReadNBytesOrDie(n, buf);
}
#endif

void EJCompressCallback(void *context, unsigned char *buf, int n) {
  Writer *writer = (Writer *)context;
  writer->WriteNBytes(buf, n);
}

// This routine copies *compressed* bytes into buf.
void EJDecompressCallback(void *context, unsigned char *buf, int n) {
  Reader *reader = (Reader *)context;
  // Will there always be n bytes available to read?  Not sure.
  reader->ReadNBytesOrDie(n, buf);
}
