#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

using namespace std;

// typedef unsigned char byte;
// typedef unsigned short ushort;
// typedef unsigned int uint;

const int kBlockSize = 16;
const int kNumBitModelTotalBits = 11;
const unsigned int kBitModelTotal = (1 << kNumBitModelTotalBits);
const int kNumMoveBits = 4;
const int kNumTopBits = 24;
const unsigned int kTopValue = (1 << kNumTopBits);
const int kOptimalBits = 10;
const int kOptimalSize = 1 << kOptimalBits;
const int kZeroContextBits = 16;
const int kBlockContextBits = 12;
const int kOptimalContextBits = 10;
const int kOptimalContextSize = 1 << kOptimalContextBits;
const char kCompressorID[] = "Cmpr";
extern const long long int g_ej_defaultDistribution[];
#define COMPRESSOR_DISTRIBUTION_SIZE		256

typedef void (*EJWRITEBYTES_CALLBACK)(void *context, unsigned char *buf, int n);
typedef void (*EJREADBYTES_CALLBACK)(void *context, unsigned char *buf, int n);

int Predict(int n, int w, int nw);
bool EJEncodeRegret(unsigned int * __restrict residual,
		    unsigned int * __restrict residualW,
		    const int * __restrict data,
		    const int * __restrict northData, int dataLength,
		    int stride);
void EJDecodeRegret(int * __restrict data, const int * __restrict northData,
		    int i, int predictor, int residual, int stride);

class EJCompressor;

class CEncoder {
  unsigned int _cacheSize;
  unsigned char _cache;
public:
  unsigned long long int low_;
  unsigned int range_;
  EJWRITEBYTES_CALLBACK write_callback_;
  void *write_context_;

  void Init(EJWRITEBYTES_CALLBACK callback, void *context) {
    low_ = 0;
    range_ = 0xFFFFFFFF;
    write_callback_ = callback;
    write_context_ = context;
    _cacheSize = 1;
    _cache = 0;
  }

  void FlushData() {
    for (int i = 0; i < 5; i++)
      ShiftLow();
  }

  inline void WriteByte(unsigned char x) {
    write_callback_(write_context_, &x, 1);
  }

  inline void ShiftLow() {
    if ((unsigned int)low_ <
	(unsigned int)0xFF000000 || (int)(low_ >> 32) != 0) {
      unsigned char temp = _cache;
      do {
	WriteByte((unsigned char)(temp + (unsigned char)(low_ >> 32)));
	temp = 0xFF;
      } while(--_cacheSize != 0);
      _cache = (unsigned char)((unsigned int)low_ >> 24);
    }
    _cacheSize++;
    low_ = (unsigned int)low_ << 8;
  }
};

class CDecoder {
public:
  unsigned int range_;
  unsigned int code_;
  EJREADBYTES_CALLBACK read_callback_;
  void *read_context_;

  unsigned char ReadByte(void) {
    unsigned char x;
    read_callback_(read_context_, &x, 1);
    return x;
  }

  void Init(EJREADBYTES_CALLBACK callback, void *context) {
    code_ = 0;
    range_ = 0xFFFFFFFF;
    read_callback_ = callback;
    read_context_ = context;
    for (int i = 0; i < 5; i++)
      code_ = (code_ << 8) | ReadByte();
  }
};

class CBitModel {
private:
  unsigned short prob_;
public:
  CBitModel() {
    prob_ = kBitModelTotal / 2;
  }

  inline void UpdateModel(unsigned int symbol) {
    if (symbol == 0) prob_ += (kBitModelTotal - prob_) >> kNumMoveBits;
    else             prob_ -= prob_ >> kNumMoveBits;
  }

  inline unsigned short GetProb() const {return prob_;}
};

class CBitEncoder: public CBitModel {
public:
  void Encode(CEncoder *encoder, unsigned int symbol) {
    unsigned int newBound = (encoder->range_ >> kNumBitModelTotalBits) *
      GetProb();

    if (symbol == 0) {
      encoder->range_ = newBound;
      UpdateModel(0);
    } else {
      encoder->low_ += newBound;
      encoder->range_ -= newBound;
      UpdateModel(1);
    }

    if (encoder->range_ < kTopValue) {
      encoder->range_ <<= 8;
      encoder->ShiftLow();
    }
  }
};

class CBitDecoder: public CBitModel {
public:
  unsigned int Decode(CDecoder * __restrict decoder) {
    unsigned int newBound =
      (decoder->range_ >> kNumBitModelTotalBits) * GetProb();
    unsigned int r;

    if (decoder->code_ < newBound) {
      decoder->range_ = newBound;
      UpdateModel(0);
      r = 0;
    } else {
      decoder->range_ -= newBound;
      decoder->code_ -= newBound;
      UpdateModel(1);
      r = 1;
    }

    if (decoder->range_ < kTopValue) {
      decoder->code_ = (decoder->code_ << 8) | decoder->ReadByte();
      decoder->range_ <<= 8;
    }

    return r;
  }
};

class CBitTreeEncoder {
  int NumBitLevels;
  CBitEncoder *Models;

public:
  CBitTreeEncoder() {
    Models = NULL;
  }

  CBitTreeEncoder(int n) {
    Init(n);
  }

  void Init(int n) {
    NumBitLevels = n;
    Models = new CBitEncoder[1 << NumBitLevels];
  }

  ~CBitTreeEncoder() {
    delete [] Models;
  }

  void Encode(CEncoder *rangeEncoder, uint symbol) {
    uint modelIndex = 1;
    for (int bitIndex = NumBitLevels; bitIndex != 0 ;) {
      bitIndex--;
      uint bit = (symbol >> bitIndex) & 1;
      Models[modelIndex].Encode(rangeEncoder, bit);
      modelIndex = (modelIndex << 1) | bit;
    }
  }
};

class CBitTreeDecoder {
  int NumBitLevels;
  CBitDecoder *Models;

public:
  CBitTreeDecoder() {
    Models = NULL;
  }

  CBitTreeDecoder(int n) {
    Init(n);
  }

  void Init(int n) {
    NumBitLevels = n;
    Models = new CBitDecoder[1 << NumBitLevels];
  }

  ~CBitTreeDecoder() {
    delete[] Models;
  }

  unsigned int Decode(CDecoder *rangeDecoder) {
    uint modelIndex = 1;
    for (int bitIndex = NumBitLevels; bitIndex != 0; bitIndex--) {
      modelIndex = (modelIndex << 1) + Models[modelIndex].Decode(rangeDecoder);
    }
    return modelIndex - (1 << NumBitLevels);
  }
};

#pragma pack(2)
struct OptimalTreeNode
{
	short SecondChild;
	short Middle;
};
#pragma pack()

class OptimalTreeEncoder {
  CBitEncoder *encoders_;

public:
  OptimalTreeEncoder(void) {
    encoders_ = new CBitEncoder[kOptimalSize];
  }
  ~OptimalTreeEncoder(void) {
    delete [] encoders_;
  }
  void Encode(CEncoder *encoder, unsigned int symbol,
	      OptimalTreeNode const * __restrict tree) {
    assert(symbol < kOptimalSize);
    int i = 0;
    int lowerBound = 0;
    int upperBound = kOptimalSize;

    do {
      if (symbol < (unsigned int)tree[i].Middle) {
	encoders_[i].Encode(encoder, 0);
	upperBound = tree[i].Middle;
	i++;
      }	else {
	encoders_[i].Encode(encoder, 1);
	lowerBound = tree[i].Middle;
	i = tree[i].SecondChild;
      }
    } while (upperBound - lowerBound > 1);
  }
};

class OptimalTreeDecoder {
  CBitDecoder *decoders_;
public:
  OptimalTreeDecoder(void) {
    decoders_ = new CBitDecoder[kOptimalSize];
  }
  ~OptimalTreeDecoder(void) {
    delete [] decoders_;
  }
  unsigned int Decode(CDecoder *decoder,
		      OptimalTreeNode const * __restrict tree) {
    int i = 0;
    int lowerBound = 0;
    int upperBound = kOptimalSize;

    do {
      assert(i < kOptimalSize);

      if (! decoders_[i].Decode(decoder)) {
	upperBound = tree[i].Middle;
	i++;
      } else {
	lowerBound = tree[i].Middle;
	i = tree[i].SecondChild;
      }

    } while (upperBound - lowerBound > 1);

    return lowerBound;
  }
};

class Context {
public:
  unsigned int context;

public:
  Context() {
    context = 0;
  }

  void Reset() {
    // least likely
    context = (0xffffffff & (kOptimalContextSize - 1)) - 1;
  }

  operator unsigned int () const { return context; }

  void operator = (unsigned int x) {
    assert(x < kOptimalContextSize);
    context = x;
  }
};

class BitContext {
  unsigned int num_bits_;
  unsigned int context_;

public:
  BitContext(unsigned int num_bits) {
    num_bits_ = num_bits;
    context_ = 0;
  }

  void Reset() {
    context_ = 0;
  }

  void Zero() {
    context_ = (context_ << 1) & ((1 << num_bits_) - 1);
  }

  void One() {
    context_ = ((context_ << 1) | 1) & ((1 << num_bits_) - 1);
  }

  void Update(uint x) {
    context_ = ((context_ << 1) | (x & 1)) & ((1 << num_bits_) - 1);
  }

  uint GetLowBits(int n) const {
    assert(n <= (int)num_bits_);
    return context_ & ((1 << n) - 1);
  }

  operator uint () const { return context_; }
};

class EJLargeEncoder {
  CBitTreeEncoder lowEncoder;
  CBitTreeEncoder highEncoder;

public:
 EJLargeEncoder() : lowEncoder(17), highEncoder(16) {}

  void Encode(CEncoder *encoder, uint symbol) {
    uint low = symbol & 0xffff;
    if (symbol > 0xffff) low |= 0x10000;
    lowEncoder.Encode(encoder, low);

    if (symbol > 0xffff) highEncoder.Encode(encoder, symbol >> 16);
  }
};

class EJLargeDecoder
{
  CBitTreeDecoder lowDecoder;
  CBitTreeDecoder highDecoder;

public:
 EJLargeDecoder(void) : lowDecoder(17), highDecoder(16) {}

  unsigned int Decode(CDecoder *decoder) {
    unsigned int symbol = lowDecoder.Decode(decoder);

    if (symbol > 0xffff) {
      symbol &= 0xffff;
      symbol |= highDecoder.Decode(decoder) << 16;
    }

    return symbol;
  }
};

void CreateOptimalTree(OptimalTreeNode * __restrict entries, int entryCount,
		       const long long int * __restrict distribution,
		       int distributionSize);

class EJCompressor {
public:
  CEncoder encoder_;
  CBitEncoder zero_encoder_[2][1 << kZeroContextBits];
  CBitEncoder block_encoder_[2][1 << kBlockContextBits];
  EJLargeEncoder large_encoder_;
  CBitEncoder predictor_encoder_;
  BitContext zero_context_;
  BitContext block_context_;
  long long int *new_distribution_;
  Context optimal_context_;
  OptimalTreeNode *optimal_tree_;
  OptimalTreeEncoder optimal_encoder_[2][kOptimalContextSize];
  EJWRITEBYTES_CALLBACK write_callback_;
  void *write_context_;

 EJCompressor(const long long int *old_distribution,
	      long long int *new_distribution,
	      EJWRITEBYTES_CALLBACK write_callback,
	      void *write_context);
  
 virtual ~EJCompressor(void);

  inline void WriteByte(unsigned char x) {
    write_callback_(write_context_, &x, 1);
  }

  inline void WriteBytes(unsigned char *data, int n) {
    write_callback_(write_context_, data, n);
  }
 
 inline unsigned int GetOptimalContext(unsigned int symbol) {
    assert(symbol > 0);
    return std::min(symbol - 1, (uint)kOptimalContextSize - 1);
  }

  inline void CompressNonzeroSymbol(unsigned int symbol, int i,
				    const int predictor) {
    assert(symbol != 0);

    new_distribution_[std::min(symbol - 1,
			  (unsigned int)COMPRESSOR_DISTRIBUTION_SIZE - 1)]++;
#if 0
    stats->MaxEncoderSymbol = std::max(stats->MaxEncoderSymbol,
				       (int)(symbol - 1));
    stats->SymbolSum += symbol;
#endif

    if (symbol < (unsigned int)kOptimalSize) {
      optimal_encoder_[predictor][optimal_context_].Encode(&encoder_,
							   symbol - 1,
							   optimal_tree_);
      optimal_context_ = GetOptimalContext(symbol);
      // stats->EncoderSizes[0]++;
    } else {
      optimal_encoder_[predictor][optimal_context_].Encode(&encoder_,
							   kOptimalSize - 1,
							   optimal_tree_);
      optimal_context_ = kOptimalContextSize - 1;

      large_encoder_.Encode(&encoder_, symbol - kOptimalSize);
      // stats->EncoderSizes[2]++;
    }
  }

  inline void CompressBlock(const unsigned int *data, int offset, int end,
			    int predictor, int stride) {
    for (int i = offset; i < end; i += stride) {
      unsigned int symbol = data[i];

      if (symbol == 0) {
	zero_encoder_[predictor][zero_context_].Encode(&encoder_, 0);
	zero_context_.Zero();
      } else {
	zero_encoder_[predictor][zero_context_].Encode(&encoder_, 1);
	zero_context_.One();

	CompressNonzeroSymbol(symbol, i, predictor);
      }
    }
  }

  inline bool IsBlockZero(const unsigned int *data, int offset, int end,
			  int stride) {
    int x = 0;

    for (int i = offset; i < end; i += stride) {
      // could also return false if nonzero but this vectorizes nicely
      x |= data[i];
    }

    return x == 0;
  }
  
  inline void DoBlock(const unsigned int *data, int i, int end, int predictor,
		      int stride) {
    if (IsBlockZero(data, i, end, stride)) {
      block_encoder_[predictor][block_context_].Encode(&encoder_, 0);
      block_context_.Zero();
      // stats->EncoderZeroBlocks++;
    } else {
      block_encoder_[predictor][block_context_].Encode(&encoder_, 1);
      block_context_.One();
      CompressBlock(data, i, end, predictor, stride);
    }

    // stats->EncoderTotalBlocks++;
  }

  inline void Compress(const unsigned int *data, int dataLength, int stride,
		       int predictor) {
    zero_context_.Reset();
    block_context_.Reset();
    optimal_context_.Reset();

    predictor_encoder_.Encode(&encoder_, predictor);

    int offset;
    int incr = kBlockSize * stride;
    int end = dataLength - incr;
    for (offset = 0; offset < stride; ++offset) {
      int i;
      for (i = offset; i <= end; i += incr) {
	DoBlock(data, i, i + incr, predictor, stride);
      }
      
      if (i < dataLength) {
	DoBlock(data, i, dataLength, predictor, stride);
      }
    }

    // stats->TotalInputBytes += dataLength * sizeof(int);
    // stats->CallCount++;
    // if (predictor) stats->ComplexPredictorCount++;
  }

  inline void Compress(const int *currentBoardRegret,
		       const int *previousBoardRegret, int n, int stride) {
    unsigned int *residual = new unsigned int[n];
    unsigned int *residualW = new unsigned int[n];

    if (EJEncodeRegret(residual, residualW, currentBoardRegret,
		       previousBoardRegret, n, stride)) {
      Compress(residual, n, stride, 1);
    } else {
      Compress(residualW, n, stride, 0);
    }

    delete [] residual;
    delete [] residualW;
  }
};

inline uint GetOptimalContext(const Context &prev, unsigned int symbol,
			      const BitContext &zeroContext) {
  assert(symbol > 0);
  return std::min(symbol - 1, (uint)kOptimalContextSize - 1);
}

inline int ZigZagDecode32(unsigned int n) {
  return (int)(n >> 1) ^ -(int)(n & 1);
}

class EJDecompressor {
 public:
  CDecoder decoder_;
  CBitDecoder zero_decoder_[2][1 << kZeroContextBits];
  CBitDecoder block_decoder_[2][1 << kBlockContextBits];
  EJLargeDecoder large_decoder_;
  OptimalTreeNode *optimal_tree_;
  BitContext zero_context_;
  BitContext block_context_;
  Context optimal_context_;
  CBitDecoder predictor_decoder_;
  OptimalTreeDecoder optimal_decoder_[2][kOptimalContextSize];
  EJREADBYTES_CALLBACK read_callback_;
  void *read_context_;

 EJDecompressor(EJREADBYTES_CALLBACK callback, void *context,
		long long int *in_distribution) :
  zero_context_(kZeroContextBits), block_context_(kBlockContextBits),
    optimal_context_(), read_callback_(callback), read_context_(context) {
    char vid[5] = { 0 };
    ReadBytes((unsigned char *)vid, sizeof(int));
    if (strcmp(vid, kCompressorID)) {
      fprintf(stderr, "compressor wrong id");
      exit(-1);
    }

    long long int *distribution;
    if (in_distribution == NULL) {
      distribution = (long long int *)malloc(COMPRESSOR_DISTRIBUTION_SIZE *
					     sizeof(long long int));
    } else {
      distribution = in_distribution;
    }
    ReadBytes((unsigned char *)distribution,
	      COMPRESSOR_DISTRIBUTION_SIZE * sizeof(long long int));

    optimal_tree_ = (OptimalTreeNode *)
      malloc(kOptimalSize * sizeof(OptimalTreeNode));
    CreateOptimalTree(optimal_tree_, kOptimalSize, distribution,
		      COMPRESSOR_DISTRIBUTION_SIZE);

    if (in_distribution == NULL) {
      free(distribution);
    }

    decoder_.Init(read_callback_, read_context_);
  }
  
  int Decompress(int *data, const int *northData, int dataLength,
		 int stride) {
    zero_context_.Reset();
    block_context_.Reset();
    optimal_context_.Reset();

    int predictor = predictor_decoder_.Decode(&decoder_);

    int incr = kBlockSize * stride;
    int end = dataLength - incr;
    for (int offset = 0; offset < stride; ++offset) {
      int i;
      for (i = offset; i <= end; i += incr) {
	if (i == offset) {
	  DoBlockSlow(data, northData, i, i + incr, predictor, stride);
	} else {
	  if (northData != NULL && predictor != 0) {
	    DoBlockFast(data, northData, i, i + incr, 1, stride);
	  } else {
	    DoSimpleBlockFast(data, i, i + incr, 0, stride);
	  }
	}
      }
      DoBlockSlow(data, northData, i, dataLength, predictor, stride);
    }

    return predictor;
  }

  inline void DoBlockSlow(int * __restrict data,
			  int const * __restrict northData, int i, int end,
			  int predictor, int stride) {
    // Does GotBlock() always return true?
    if (GotBlock(predictor)) {
      for (int j = i; j < end; j += stride) {
	EJDecodeRegret(data, northData, j, predictor,
		       DecompressSymbol(predictor), stride);
      }
    } else {
      for (int j = i; j < end; j += stride) {
	EJDecodeRegret(data, northData, j, predictor, 0, stride);
      }
    }
  }

  inline void DoBlockFast(int * __restrict data,
			  const int * __restrict northData, int i, int end,
			  int predictor, int stride) {
    if (GotBlock(predictor)) {
      for (int j = i; j < end; j += stride) {
	int p = Predict(northData[j], data[j - stride], northData[j - stride]);
	data[j] = DecompressSymbol(predictor) + p;
      }
    } else {
      for (int j = i; j < end; j += stride) {
	int p = Predict(northData[j], data[j - stride], northData[j - stride]);
	data[j] = p;
      }
    }
  }

  inline void DoSimpleBlockFast(int * __restrict data, int i,
				int end, int predictor, int stride) {
    if (GotBlock(predictor)) {
      for (int j = i; j < end; j += stride) {
	data[j] = DecompressSymbol(predictor) + data[j - stride];
      }
    } else {
      int x = data[i-stride];

      for (int j = i; j < end; j += stride) {
	data[j] = x;
      }
    }
  }

  inline bool GotBlock(int predictor) {
    bool gotBlock =
      block_decoder_[predictor][block_context_].Decode(&decoder_) != 0;
    block_context_.Update(gotBlock ? 1 : 0);
    return gotBlock;
  }

  inline unsigned int DecompressNonzeroSymbol(int predictor) {
    unsigned int symbol =
      optimal_decoder_[predictor][optimal_context_].Decode(&decoder_,
							   optimal_tree_) + 1;
    optimal_context_ = GetOptimalContext(optimal_context_, symbol,
					 zero_context_);

    if (symbol == (unsigned int)kOptimalSize) {
      symbol = large_decoder_.Decode(&decoder_) + kOptimalSize;
    }

    return ZigZagDecode32(symbol);
  }

  inline unsigned int DecompressSymbol(int predictor) {
    auto notZero = zero_decoder_[predictor][zero_context_].Decode(&decoder_);
    zero_context_.Update(notZero);
    return notZero ? DecompressNonzeroSymbol(predictor) : 0;
  }

  inline unsigned char ReadByte(void) {
    unsigned char x;
    read_callback_(read_context_, &x, 1);
    return x;
  }

  inline void ReadBytes(unsigned char *data, int n) {
    read_callback_(read_context_, data, n);
  }
};
