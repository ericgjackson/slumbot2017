#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include "board_tree.h"
#include "canonical_cards.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"

using namespace std;

class Container {
public:
  template<class T> void Value(T *val);
  int IValue(void);
  double DValue(void);
};

int Container::IValue(void) {
  fprintf(stderr, "IValue\n");
  return 1;
}

double Container::DValue(void) {
  fprintf(stderr, "DValue\n");
  return 2.0;
}

template<>
void Container::Value<int>(int *ival) {
  *ival = 1;
}

template<>
void Container::Value<double>(double *dval) {
  *dval = 2.0;
}

template <class T>
class Polygon {
public:
  Polygon(void) {num_vertices_ = 4;}
protected:
  int num_vertices_;
};

template <typename T>
class Rectangle : public Polygon<T> {
public:
  Rectangle(void);
};

template <typename T>
Rectangle<T>::Rectangle(void) : Polygon<T>() {
  printf("num_vertices %i\n", Polygon<T>::num_vertices_);
}

template <class T>
class MyPair {
public:
  MyPair(T a, T b) {vals_[0] = a; vals_[1] = b;}
  T Sum(void);
private:
  T vals_[2];
};

template <class T>
T MyPair<T>::Sum(void) {
  return vals_[0] + vals_[1];
}


template <class MyType>
MyType Half(MyType a) {
  return a / 2;
}

class Object {
public:
  Object(void);
  ~Object(void);
private:
};

Object::Object(void) {
}

Object::~Object(void) {
}

class State {
public:
  State(void);
  ~State(void) {}
  State(const State &other);
private:
  shared_ptr<unsigned int *> street_buckets_;
  shared_ptr<double> opp_probs_;
  shared_ptr<double> total_card_probs_;
};

State::State(void) {
#if 0
  unsigned int max_street = Game::MaxStreet();
  shared_ptr<unsigned int *> sb(new unsigned int *[max_street + 1]);
  street_buckets_ = sb;
#endif
  street_buckets_ = make_shared<unsigned int *>();
#if 0
  for (unsigned int st = 0; st <= max_street; ++st) {
    street_buckets_[st] = new unsigned int[1000];
  }
#endif
  shared_ptr<double> op(new double[1000]);
  opp_probs_ = op;
  shared_ptr<double> tcp(new double[50]);
  total_card_probs_ = tcp;
}

State::State(const State &other) {
  street_buckets_ = other.street_buckets_;
  opp_probs_ = other.opp_probs_;
  total_card_probs_ = other.total_card_probs_;
}

static void TestSharedPointers(void) {
  State state;
  State copy = state;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);

  TestSharedPointers();
  exit(0);
  
  string a = "foobar";
  string b = string(a, 3);
  printf("%s\n", b.c_str());
  exit(0);
  
  char *buf = new char[10];
  for (unsigned int i = 0; i < 10; ++i) {
    buf[i] = 1;
  }
  // Test byte alignment
  unsigned long long int ull = *(unsigned long long int *)&buf[2];
  fprintf(stderr, "ULL %llu\n", ull);

  Writer xwriter("/home/eric/nntorch/nn/xxx");
  xwriter.WriteDouble(4.5);
  xwriter.WriteDouble(2.7213);
  xwriter.WriteDouble(1.5);
  xwriter.WriteDouble(5.7213);
  xwriter.WriteDouble(3.5);
  xwriter.WriteDouble(7.1);
  Writer ywriter("/home/eric/nntorch/nn/yyy");
  ywriter.WriteDouble(1.0);
  ywriter.WriteDouble(2.0);
  ywriter.WriteDouble(3.0);
#if 0
  HandValueTree::Create();
  BoardTree::Create();
  const Card *board = BoardTree::Board(3, 0);
  OutputFiveCards(board);
  printf("\n");
  fflush(stdout);
  HandTree hand_tree(3, 0, 3);
  const CanonicalCards *hands = hand_tree.Hands(3, 0);
  printf("Num: %u\n", hands->NumRaw());
  const Card *hole_cards0 = hands->Cards(0);
  OutputTwoCards(hole_cards0);
  printf(" %u\n", hands->HandValue(0));
  const Card *hole_cards1080 = hands->Cards(1080);
  OutputTwoCards(hole_cards1080);
  printf(" %u\n", hands->HandValue(1080));
  int i3 = 3;
  double d3 = 3;
  printf("Half int %i\n", Half(i3));
  printf("Half double %f\n", Half(d3));
  MyPair<int> ip(5, 3);
  printf("Sum: %i\n", ip.Sum());
  MyPair<double> dp(1.5, 3.3);
  printf("Sum: %f\n", dp.Sum());
#endif
  Container c;
  int i = c.IValue();
  printf("i=%i\n", i);
  double d = c.DValue();
  printf("d=%f\n", d);
}
