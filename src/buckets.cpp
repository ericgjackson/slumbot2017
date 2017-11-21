#include <stdio.h>
#include <stdlib.h>

#include "board_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "files.h"
#include "game.h"
#include "io.h"

Buckets::Buckets(const CardAbstraction &ca, bool numb_only) {
  BoardTree::Create();
  unsigned int max_street = Game::MaxStreet();
  none_ = new bool[max_street + 1];
  short_buckets_ = new unsigned short *[max_street + 1];
  int_buckets_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    short_buckets_[st] = nullptr;
    int_buckets_[st] = nullptr;
  }
  char buf[500];
  num_buckets_ = new unsigned int[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (ca.Bucketing(st) == "none") {
      none_[st] = true;
      num_buckets_[st] = 0;
      continue;
    }
    none_[st] = false;
    sprintf(buf, "%s/num_buckets.%s.%i.%i.%i.%s.%i", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	    max_street, ca.Bucketing(st).c_str(), st);
    Reader reader(buf);
    num_buckets_[st] = reader.ReadUnsignedIntOrDie();
  }

  if (! numb_only) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (none_[st]) continue;
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int num_hands = num_boards * num_hole_card_pairs;
      long long int lli_num_hands = num_hands;
  
      sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	      Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	      max_street, ca.Bucketing(st).c_str(), st);
      Reader reader(buf);
      long long int file_size = reader.FileSize();
      if (file_size == lli_num_hands * 2) {
	short_buckets_[st] = new unsigned short[num_hands];
	for (unsigned int h = 0; h < num_hands; ++h) {
	  short_buckets_[st][h] = reader.ReadUnsignedShortOrDie();
	}
      } else if (file_size == lli_num_hands * 4) {
	int_buckets_[st] = new unsigned int[num_hands];
	for (unsigned int h = 0; h < num_hands; ++h) {
	  int_buckets_[st][h] = reader.ReadUnsignedIntOrDie();
	}
      } else {
	fprintf(stderr,
		"BucketsInstance::Initialize: Unexpected file size %lli\n",
		file_size);
	exit(-1);
      }
    }
  }

}

Buckets::~Buckets(void) {
  unsigned int max_street = Game::MaxStreet();
  if (short_buckets_) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (short_buckets_[st]) delete [] short_buckets_[st];
    }
    delete [] short_buckets_;
  }
  if (int_buckets_) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (int_buckets_[st]) delete [] int_buckets_[st];
    }
    delete [] int_buckets_;
  }
  delete [] none_;
  delete [] num_buckets_;
}

BucketsFile::BucketsFile(const CardAbstraction &ca) {
  BoardTree::Create();
  unsigned int max_street = Game::MaxStreet();
  none_ = new bool[max_street + 1];
  num_buckets_ = new unsigned int[max_street + 1];
  shorts_ = new bool[max_street + 1];
  readers_ = new Reader *[max_street + 1];
  char buf[500];
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (ca.Bucketing(st) == "none") {
      none_[st] = true;
      num_buckets_[st] = 0;
      shorts_[st] = false;
      readers_[st] = nullptr;
      continue;
    }
    none_[st] = false;
    sprintf(buf, "%s/num_buckets.%s.%i.%i.%i.%s.%i", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	    max_street, ca.Bucketing(st).c_str(), st);
    Reader reader(buf);
    num_buckets_[st] = reader.ReadUnsignedIntOrDie();
  }

  for (unsigned int st = 0; st <= max_street; ++st) {
    if (none_[st]) continue;
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int num_hands = num_boards * num_hole_card_pairs;
    long long int lli_num_hands = num_hands;
    sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	    max_street, ca.Bucketing(st).c_str(), st);
    readers_[st] = new Reader(buf);
    long long int file_size = readers_[st]->FileSize();
    if (file_size == lli_num_hands * 2) {
      shorts_[st] = true;
    } else if (file_size == lli_num_hands * 4) {
      shorts_[st] = false;
    } else {
      fprintf(stderr,
	      "BucketsFile::BucketsFile: Unexpected file size %lli\n",
	      file_size);
      exit(-1);
    }
  }
}

BucketsFile::~BucketsFile(void) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    delete readers_[st];
  }
  delete [] none_;
  delete [] num_buckets_;
  delete [] shorts_;
  delete [] readers_;
}

unsigned int BucketsFile::Bucket(unsigned int st, unsigned int h) const {
  if (none_[st]) {
    fprintf(stderr, "No buckets on street %u\n", st);
    exit(-1);
  }
  if (shorts_[st]) {
    readers_[st]->SeekTo(((long long int)h) * 2);
    return readers_[st]->ReadUnsignedShortOrDie();
  } else {
    readers_[st]->SeekTo(((long long int)h) * 4);
    return readers_[st]->ReadUnsignedIntOrDie();
  }
}
