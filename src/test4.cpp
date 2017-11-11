// Want to see if I can use templates more aggressively.
// Regrets can be chars, shorts, ints or doubles.
// Sumprobs can be some or all of these things as well.
// Different streets may have different types.

template <class T>
class CFRStreetValues {
public:
  CFRStreetValues(unsigned int num_hands, unsigned int num_succs);
  virtual ~CFRStreetValues(void);
  void Allocate(void);
  void Clear(void);
  void Probs(unsigned int h, double *probs);
protected:
  unsigned int num_hands_;
  unsigned int num_succs_;
  T *data_;
};

template <typename T>
CFRStreetValues<T>::CFRStreetValues(unsigned int num_hands,
				    unsigned int num_succs) {
  num_hands_ = num_hands;
  num_succs_ = num_succs;
}

template <typename T>
CFRStreetValues<T>::~CFRStreetValues(void) {
  delete [] data_;
}

template <typename T>
void CFRStreetValues<T>::Allocate(void) {
  unsigned int num = num_hands_ * num_succs_;
  data_ = new T[num];
}

template <typename T>
void CFRStreetValues<T>::Clear(void) {
  unsigned int num = num_hands_ * num_succs_;
  for (unsigned int i = 0; i < num; ++i) {
    data_[i] = 0;
  }
}

template <typename T>
void CFRStreetValues<T>::Probs(unsigned int h, double *probs) {
  T *my_vals = data_ + h * num_succs_;
  double sum = 0;
  for (unsigned int s = 0; s < num_succs_; ++s) {
    T v = my_vals[s];
    if (v > 0) sum += v;
  }
  if (sum == 0) {
    for (unsigned int s = 0; s < num_succs_; ++s) {
      probs[s] = s == 0 ? 1.0 : 0;
    }
  } else {
    for (unsigned int s = 0; s < num_succs_; ++s) {
      T v = my_vals[s];
      if (v > 0) probs[s] = v / sum;
      else       probs[s] = 0;
    }
  }
}

int main(int argc, char *argv[]) {
  CFRStreetValues<int> street_values(10, 3);
  street_values.Allocate();
  street_values.Clear();
}
