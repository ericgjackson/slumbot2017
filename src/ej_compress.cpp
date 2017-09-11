#include "ej_compress.h"

const long long int g_ej_defaultDistribution[COMPRESSOR_DISTRIBUTION_SIZE] = {
  3094512, 3142245, 1152792, 1175333, 575302, 589007, 341972, 354210, 227822, 237473, 163464, 170516, 121951, 127527, 92873, 96867, 71306, 73767, 57401, 59632, 48079, 50387, 39587, 40906, 
  32332, 33909, 26627, 27953, 22723, 23575, 19548, 20464, 17077, 17835, 14806, 15286, 12879, 13370, 11711, 11746, 10507, 10639, 9154, 9431, 8418, 8542, 7444, 7845, 6531, 6655, 5601, 5787, 
  5182, 5386, 4446, 4718, 4016, 4349, 3549, 3767, 3254, 3435, 2900, 3097, 2655, 2793, 2412, 2467, 2123, 2255, 1958, 1991, 1827, 1964, 1777, 1722, 1615, 1555, 1507, 1517, 1489, 1466, 1358, 
  1367, 1290, 1273, 1295, 1218, 1239, 1173, 1156, 1085, 1081, 1103, 988, 986, 892, 843, 763, 780, 775, 751, 745, 821, 827, 719, 710, 666, 626, 579, 568, 507, 579, 499, 517, 492, 498, 439, 
  534, 467, 461, 444, 445, 459, 464, 436, 436, 361, 430, 372, 355, 333, 373, 330, 340, 345, 331, 306, 333, 273, 329, 266, 280, 277, 296, 262, 270, 242, 273, 250, 266, 236, 257, 220, 240, 
  220, 257, 216, 235, 195, 233, 204, 238, 184, 216, 179, 186, 174, 194, 179, 204, 146, 176, 177, 216, 168, 162, 171, 194, 144, 173, 143, 143, 149, 170, 144, 130, 151, 107, 130, 127, 123, 
  131, 127, 133, 138, 119, 119, 122, 109, 124, 102, 114, 102, 101, 97, 105, 84, 107, 101, 87, 75, 95, 90, 102, 62, 120, 76, 77, 82, 99, 75, 84, 70, 88, 65, 96, 64, 106, 65, 76, 67, 67, 
  73, 73, 62, 68, 54, 71, 56, 81, 48, 63, 59, 65, 51, 54, 40, 62, 40, 48, 44, 67, 33, 55, 5764
};

inline unsigned int ZigZagEncode32(int n) {
  return (n << 1) ^ (n >> 31);
}

static void CreateOptimalTree(OptimalTreeNode * __restrict entries,
			      int &count,
			      const long long int * __restrict distribution,
			      long long int sum, int start, int end,
			      int uniformStart) {
  if (end - start > 2) {
    int middle = 0;
    long long int halfSum = 0;

    if (sum > 0 && start < uniformStart) {
      for (middle = start; middle < std::min(end - 1, uniformStart); middle++) {
	if (halfSum >= (sum + 1) / 2) break;
	// lower half bigger for odd-length ranges
	// if (halfSum >= std::max(1LL, sum / 2)) break;
	// upper half bigger for odd-length ranges
	halfSum += distribution[middle];
      }
    } else {
      // balanced subtree for 0-prob ranges and ranges where
      // start >= uniformStart
      middle = start + (end - start) / 2;
    }

    assert(start < middle);
    assert(middle < end);

    int current = count++;

    CreateOptimalTree(entries, count, distribution, halfSum, start, middle,
		      uniformStart);
    entries[current].Middle = middle;
    entries[current].SecondChild = count;
    CreateOptimalTree(entries, count, distribution, sum - halfSum, middle, end,
		      uniformStart);
  } else if (end - start == 2) {
    entries[count].Middle = start + 1;
    entries[count].SecondChild = 0;
    count++;
  }
}

void CreateOptimalTree(OptimalTreeNode * __restrict entries, int entryCount,
		       const long long int * __restrict distribution,
		       int distributionSize) {
  assert(entryCount >= distributionSize);

  // last value in distribution is the total probability of [distributionSize-1..entryCount-1]
  long long int sum = 0;
  for (int i = 0; i < distributionSize; i++) sum += distribution[i];
  int count = 0;
  CreateOptimalTree(entries, count, distribution, sum, 0, entryCount,
		    distributionSize - 1);
  if (count != entryCount - 1) throw "CreateOptimalTree fail";
}

int Predict(int n, int w, int nw) {
  return abs(w - nw) < abs(n - nw) ? n : w;
}

bool EJEncodeRegret(unsigned int * __restrict residual,
		    unsigned int * __restrict residualW,
		    const int * __restrict data,
		    const int * __restrict northData, int dataLength,
		    int stride) {
  if (northData != NULL) {
    int nonzeroGrad = 0;
    int nonzeroW = 0;
    for (int offset = 0; offset < stride; ++offset) {
      residualW[offset] = residual[offset] =
	ZigZagEncode32(data[offset] - northData[offset]);

      if (residual[offset])  ++nonzeroGrad;
      if (residualW[offset]) ++nonzeroW;

      for (int i = offset + stride; i < dataLength; i += stride) {
	int n = northData[i];
	int nw = northData[i - stride];
	int w = data[i - stride];
	int p = Predict(n, w, nw);

	residual[i] = ZigZagEncode32(data[i] - p);
	if (residual[i] != 0) nonzeroGrad++;
			
	residualW[i] = ZigZagEncode32(data[i] - w);
	if (residualW[i] != 0) nonzeroW++;
      }
    }
    return nonzeroGrad < nonzeroW;
  } else {
    for (int offset = 0; offset < stride; ++offset) {
      residualW[offset] = ZigZagEncode32(data[offset]);

      for (int i = offset + stride; i < dataLength; i += stride) {
	residualW[i] = ZigZagEncode32(data[i] - data[i - stride]);
      }
    }
	  
    return false;
  }
}

void EJDecodeRegret(int * __restrict data,
		    const int * __restrict northData, int i,
		    int predictor, int residual, int stride) {
  if (i < stride) {
    if (northData != NULL) data[i] = residual + northData[i];
    else                   data[i] = residual;
  } else {
    if (northData != NULL) {
      if (predictor == 0) {
	data[i] = residual + data[i - stride];
      } else {
	int n = northData[i];
	int nw = northData[i - stride];
	int w = data[i - stride];
	int p = Predict(n, w, nw);
	data[i] = residual + p;
      }
    } else {
      data[i] = residual + data[i - stride];
    }
  }
}

EJCompressor::EJCompressor(const long long int *old_distribution,
			   long long int *new_distribution,
			   EJWRITEBYTES_CALLBACK write_callback,
			   void *write_context) :
  zero_context_(kZeroContextBits), block_context_(kBlockContextBits),
  new_distribution_(new_distribution), write_callback_(write_callback),
  write_context_(write_context)
{
  WriteBytes((unsigned char *)kCompressorID, 4);

  optimal_tree_ = (OptimalTreeNode *)
    malloc(kOptimalSize * sizeof(OptimalTreeNode));
  CreateOptimalTree(optimal_tree_, kOptimalSize, old_distribution,
		    COMPRESSOR_DISTRIBUTION_SIZE);

  WriteBytes((unsigned char *)old_distribution,
	     COMPRESSOR_DISTRIBUTION_SIZE * sizeof(int64_t));

  memset(new_distribution_, 0,
	 COMPRESSOR_DISTRIBUTION_SIZE * sizeof(int64_t));

  encoder_.Init(write_callback_, write_context_);
}

EJCompressor::~EJCompressor(void) {
  encoder_.FlushData();
  free(optimal_tree_);
}
