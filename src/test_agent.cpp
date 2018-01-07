// New agent is stateful.  That is a hassle for us here.  We can't just
// pass in a turn hand.  We need to pass in all the preceding hands that
// lead up to the turn state.  And the bot actually needs to take the action
// that we expect it to in the previous states.
//
// Tests below run like so:
// ../bin/test_agent holdem_params rk10k_params none_params mb1b2asym_params
//   mb1b1_params tcfrq3p0_params cfrps_params runtime_params 3 200 0 0 debug
//   >& out.test_agent.new

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "split.h"
#include "nl_agent.h"

// STATE:13381:r250c/r400c/cr800c/cr20000c:9dKh|QsAh/As4h6d/9h/Qc:-20000|20000:smb2b2aai|snes
static void Loose4(Agent *agent) {
  printf("Loose4\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:13381:r250:9dKh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:13381:r250c/:9dKh|/As4h6d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:13381:r250c/r400c/:9dKh|/As4h6d/9h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250c/r400c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:13381:r250c/r400c/cr800:9dKh|/As4h6d/9h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250c/r400c/cr800 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:13381:r250c/r400c/cr800c/:9dKh|/As4h6d/9h/Qc";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250c/r400c/cr800c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:13381:r250c/r400c/cr800c/cr20000:"
    "9dKh|/As4h6d/9h/Qc";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K9o r250c/r400c/cr800c/cr20000 action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);
}

static void Loose3P1(Agent *agent) {
  printf("Loose3P1\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:1:8313::|Qd9c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("Q9 root action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:8313:cr200:|Qd9c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("Q9 cr200 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:8313:cr200c/c:|Qd9c/9d2cQh";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("Q9 cr200c/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:8313:cr200c/cr300r900:|Qd9c/9d2cQh";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("Q9 cr200c/cr300r900 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// STATE:8313:cr200c/cr300r900c/r1800r3600c/cr20000c:KhJs|Qd9c/9d2cQh/3s/7s:-20000|20000:smb2b2aai|snes
static void Loose3(Agent *agent) {
  printf("Loose3\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:8313:c:KhJs|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:8313:cr200c/:KhJs|/9d2cQh";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:8313:cr200c/cr300:KhJs|/9d2cQh";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/cr300 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:8313:cr200c/cr300r900c/:KhJs|/9d2cQh/3s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/cr300r900c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state =
    "MATCHSTATE:0:8313:cr200c/cr300r900c/r1800r3600:KhJs|/9d2cQh/3s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/cr300r900c/r1800r3600 action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);

  match_state =
    "MATCHSTATE:0:8313:cr200c/cr300r900c/r1800r3600c/:KhJs|/9d2cQh/3s/7s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/cr300r900c/r1800r3600c/ action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:8313:cr200c/cr300r900c/r1800r3600c/cr20000:"
    "KhJs|/9d2cQh/3s/7s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo cr200c/cr300r900c/r1800r3600c/cr20000 action %i we_bet_to %i\n",
	 ba, we_bet_to);
  fflush(stdout);
}

// STATE:1541:r250r1250c/r1900c/r5700c/cr20000c:QhKd|KsJs/AdJh8c/2s/2c:-20000|20000:smb2b2aai|snes
static void Loose2(Agent *agent) {
  printf("Loose2\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:1541:r250:QhKd|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KQo r250 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:1541:r250r1250c/:QhKd|/AdJh8c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KQo r250r1250c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:1541:r250r1250c/r1900c/:QhKd|/AdJh8c/2s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KQo r250r1250c/r1900c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state =
    "MATCHSTATE:0:1541:r250r1250c/r1900c/r5700c/:QhKd|/AdJh8c/2s/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KQo r250r1250c/r1900c/r5700c action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);

  match_state =
    "MATCHSTATE:0:1541:r250r1250c/r1900c/r5700c/cr20000:QhKd|/AdJh8c/2s/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KQo r250r1250c/r1900c/r5700c/cr20000 action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);
}

// snes_smb2b2.0.fwd.log:STATE:7711:r200c/r300r600c/cr1200c/cr20000c:KcJs|Jd3d/KhAdTc/4d/2s:20000|-20000:smb2b2|snes
static void Loose1(Agent *agent) {
  printf("Loose1\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:7711:r200:KcJs|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/:KcJs|/KhAdTc";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/r300r600:KcJs|/KhAdTc";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c/r300r600 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/r300r600c/:KcJs|/KhAdTc/4d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c/r300r600c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/r300r600c/cr1200:KcJs|/KhAdTc/4d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c/r300r600c/cr1200 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/r300r600c/cr1200c/:KcJs|/KhAdTc/4d/2s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c/r300r600c/cr1200c/ action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7711:r200c/r300r600c/cr1200c/cr20000:"
    "KcJs|/KhAdTc/4d/2s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("KJo r200c/r300r600c/cr1200c/cr20000 action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);
}

// snes_smb2b2.0.fwd.log:STATE:7139:r200c/r300c/r600c/cr1800r5400r20000c:5d3d|Qs6h/Qc3c9s/Qd/Js:-20000|20000:smb2b2|snes
static void ThreeBets(Agent *agent) {
  printf("ThreeBets\n");
  printf("----------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:7139:r200:5d3d|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7139:r200c/:5d3d|/Qc3c9s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7139:r200c/r300c/:5d3d|/Qc3c9s/Qd";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200c/r300c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7139:r200c/r300c/r600c/:5d3d|/Qc3c9s/Qd/Js";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200c/r300c/r600c/ action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7139:r200c/r300c/r600c/cr1800:5d3d|/Qc3c9s/Qd/Js";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200c/r300c/r600c/cr1800 action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:7139:r200c/r300c/r600c/cr1800r5400r20000:"
    "5d3d|/Qc3c9s/Qd/Js";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("53s r200c/r300c/r600c/cr1800r5400r20000 action %i we_bet_to %i\n",
	 ba, we_bet_to);
  fflush(stdout);
}

#if 0
static void Translation(Agent *agent) {
  printf("Translation\n");
  printf("-----------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
}
#endif

// Expect 1/55/43
static void PreflopP1(Agent *agent) {
  printf("PreflopP1\n");
  printf("---------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:1:0::|Ts5h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T5o root action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// Expect:
// 0/1
// 48/52
// Below prob 0.4; sample above; hence 0/1
static void PreflopP0(Agent *agent) {
  printf("PreflopP0\n");
  printf("---------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:1:r300:Ts5h|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T5o r300 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:2:r200:Ts5h|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T5o r200 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:3:r250:Ts5h|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T5o r250 action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// Expect 14.5/85.5
static void FlopP0(Agent *agent) {
  printf("FlopP0\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:4:r300:AsKh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:4:r300c/:AsKh|/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AKo/QJT r300c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// Expect 63.5/36.5
static void FlopP1(Agent *agent) {
  printf("FlopP1\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:1:5::|As9h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:5:r300c/c:|As9h/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT r300c/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// Expect:
// 0/1
// 60/40
static void TurnP0(Agent *agent) {
  printf("TurnP0\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:6:r300:AsKh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:6:r300c/:AsKh|/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:6:r300c/r900c/:AsKh|/QdJcTs/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AKo/QJT2 r300c/r900c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:0:10:r300:AsKh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:10:r300c/:AsKh|/KcQcJd";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:10:r300c/r900c/:AsKh|/KcQcJd/Ts";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AKo/KQJT r300c/r900c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

// Expect: 86/14
static void TurnP1(Agent *agent) {
  printf("TurnP1\n");
  printf("------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:1:5::|As9h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:5:r300c/c:|As9h/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT r300c/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:5:r300c/cc/c:|As9h/QdJcTs/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT2 r300c/cc/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);
}

static void RiverP0(Agent *agent) {
  printf("RiverP0\n");
  printf("-------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:6:r300:AsKh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:6:r300c/:AsKh|/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:6:r300c/r900c/:AsKh|/QdJcTs/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  match_state = "MATCHSTATE:0:6:r300c/r900c/r2700c/:AsKh|/QdJcTs/2c/3c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AKo/QJT23 r300c/r900c/r2700c action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);
}

static void RiverP1(Agent *agent) {
  printf("RiverP1\n");
  printf("-------\n");
  fflush(stdout);
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:1:23::|As9h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:23:r300c/c:|As9h/QdJcTs";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT r300c/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:23:r300c/cr900c/c:|As9h/QdJcTs/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT2 r300c/cr900c/c action %i we_bet_to %i\n", ba, we_bet_to);
  fflush(stdout);

  match_state = "MATCHSTATE:1:23:r300c/cr900c/cr2700c/c:|As9h/QdJcTs/2c/3c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A9o/QJT23 r300c/cr900c/cr2700c/c action %i we_bet_to %i\n", ba,
	 we_bet_to);
  fflush(stdout);
}

#if 0
static void WeakTight1(Agent *agent) {
  printf("WeakTight1\n");
  printf("----------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:1:196:r200c/cr400r1200c/r3600:|Js7c/Th9h8h/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("J7o action %i we_bet_to %i\n", ba, we_bet_to);
}

// Hand 639?
static void WeakTight2(Agent *agent) {
  printf("WeakTight2\n");
  printf("----------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:1:639:r200c/cc/r2200c/r4200:|Ad8h/QhTsJd/Kh/4h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A8o action %i we_bet_to %i\n", ba, we_bet_to);
}

// MATCHSTATE:0:61:r266c/r500r734c/cc/r1500r3000:Kh6s|/Jh8h7h/9h/3d
static void Crash(Agent *agent) {
  printf("Crash\n");
  printf("-----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:61:r266c/r500r734c/cc/r1500r3000:Kh6s|/Jh8h7h/9h/3d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K6o action %i we_bet_to %i\n", ba, we_bet_to);
}

static void Eric1(Agent *agent) {
  printf("Eric1\n");
  printf("-----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r433:AsQs|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AQs root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:0:r433r900r2625:AsQs|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AQs r433r900r2625 action %i we_bet_to %i\n", ba, we_bet_to);
#if 0
  match_state = "MATCHSTATE:0:0:r433r900r2625:AsQs|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AQs r433r900r2625 action %i we_bet_to %i\n", ba, we_bet_to);
#endif
}

static void Nikolai1(Agent *agent) {
  printf("Nikolai1\n");
  printf("--------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:1:4::|8h4c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("84o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:4:r200r433:|8h4c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("84o r200r433 action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:4:r200r433c/r877:|8h4c/Ts6d3c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("84o action %i we_bet_to %i\n", ba, we_bet_to);
}

static void Nikolai2(Agent *agent) {
  printf("Nikolai2\n");
  printf("--------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:1:0:r200r433c/r444r888r2209r3530c/c:|8h4c/Ts6d3c/8d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("84o action %i we_bet_to %i\n", ba, we_bet_to);
}

static void Nikolai3(Agent *agent) {
  printf("Nikolai3\n");
  printf("--------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:39:r255:5h5c|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255 action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:39:r255r750r2250:5h5c|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255r750r2250 action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:39:r255r750r2250c/:5h5c|/9h8h8c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255r750r2250c action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:39:r255r750r2250c/cr4500:5h5c|/9h8h8c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255r750r2250c/cr4500 action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:39:r255r750r2250c/cr4500c/:5h5c|/9h8h8c/Ts";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255r750r2250c/cr4500c/ action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:39:r255r750r2250c/cr4500c/cr20000:5h5c|/9h8h8c/Ts";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r255r750r2250c/cr4500c/cr20000 action %i we_bet_to %i\n", ba, we_bet_to);
}

static void WeakTight(Agent *agent) {
  printf("WeakTight\n");
  printf("---------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r300c/cc/r600r1800c/cr5400:3s3h|/Qs6s4h/3c/Jd";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("33 r300c/cc/r600r1800c/cr5400 action %i we_bet_to %i\n", ba,
	 we_bet_to);
}

static void NN(Agent *agent) {
  printf("NN\n");
  printf("--\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r200c/cr400c/cr800c/cr5000:Ah4c|/As3d2c/3h/8d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("QJo r200c/cr400c/c800/cr5000 action %i we_bet_to %i\n", ba,
	 we_bet_to);
}

// Translate Bet To Call
// In this hand, we map the small bet to a check and elect to raise.
static void TBTC1(Agent *agent) {
  printf("TBTC1\n");
  printf("-----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r450c/cr451:QhJc|/As3d2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("QJo r450c/cr451 action %i we_bet_to %i\n", ba, we_bet_to);
}

// Translate Bet To Call
// In this hand, we map the small bet to a check and elect to call.
static void TBTC2(Agent *agent) {
  printf("TBTC2\n");
  printf("-----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r400c/cr401:QhJc|/As3d2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("QJo r400c/cr401 action %i we_bet_to %i\n", ba, we_bet_to);
}

// Translate Bet To Call
// In this hand, we map the small bet to a check and elect to raise.
// We test interpret processes this hand correctly.
static void TBTC3(Agent *agent) {
  printf("TBTC3\n");
  printf("-----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r450c/cr451r2250c:QhJc|/As3d2c/3h";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("QJo r400c/cr401r2250c action %i we_bet_to %i\n", ba, we_bet_to);
}

static void R200R800Folds(Agent *agent) {
  printf("R200R800 Folds\n");
  printf("--------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  for (unsigned int suited = 0; suited <= 1; ++suited) {
    for (unsigned int hr = 0; hr <= 12; ++hr) {
      for (unsigned int lr = 0; lr <= hr; ++lr) {
	if (suited && hr == lr) continue;
	Card hi = MakeCard(hr, 3);
	Card lo;
	if (suited) lo = MakeCard(lr, 3);
	else        lo = MakeCard(lr, 2);
	match_state = "MATCHSTATE:1:0:r200r800:|";
	string his, los;
	CardName(hi, &his);
	CardName(lo, &los);
	match_state += his;
	match_state += los;
	ba = agent->HandleStateChange(match_state, &we_bet_to);
	if (ba == BA_FOLD) {
	  OutputTwoCards(hi, lo);
	  printf(" folds at r200r800\n");
	  fflush(stdout);
	}
      }
    }
  }
}

static void R200R600Folds(Agent *agent) {
  printf("R200R600 Folds\n");
  printf("--------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  for (unsigned int suited = 0; suited <= 1; ++suited) {
    for (unsigned int hr = 0; hr <= 12; ++hr) {
      for (unsigned int lr = 0; lr <= hr; ++lr) {
	if (suited && hr == lr) continue;
	Card hi = MakeCard(hr, 3);
	Card lo;
	if (suited) lo = MakeCard(lr, 3);
	else        lo = MakeCard(lr, 2);
	match_state = "MATCHSTATE:1:0:r200r600:|";
	string his, los;
	CardName(hi, &his);
	CardName(lo, &los);
	match_state += his;
	match_state += los;
	ba = agent->HandleStateChange(match_state, &we_bet_to);
	if (ba == BA_FOLD) {
	  OutputTwoCards(hi, lo);
	  printf(" folds at r200r600\n");
	  fflush(stdout);
	}
      }
    }
  }
}

// r260c/cc/cc/cr806f
static void Override(Agent *agent) {
  printf("Override\n");
  printf("--------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:0:r260:2hTh|";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T2s root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:0:r260c/:2hTh|/Tc6cAh";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T2s r260c/ action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:0:r260c/cc/:2hTh|/Tc6cAh/Qd";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T2s r260c/cc/ action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:0:r260c/cc/cc/:2hTh|/Tc6cAh/Qd/Ks";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T2s r260c/cc/cc/ action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:0:0:r260c/cc/cc/cr806:2hTh|/Tc6cAh/Qd/Ks";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("T2s r260c/cc/cc/cr806 action %i we_bet_to %i\n", ba, we_bet_to);
}

// cc/cr200c/r3995c/r430:4d7d,*|/3s6d9s/Jh/5c
static void TranslatedAllIn(Agent *agent) {
  printf("TranslatedAllIn\n");
  printf("---------------\n");
  // string match_state = "MATCHSTATE:1:5::|7d4d";
  unsigned int we_bet_to;
  // BotAction ba = agent->HandleStateChange(match_state, state, &we_bet_to);
  // printf("Root 74s action %i we_bet_to %i\n", ba, we_bet_to);
  string match_state = "MATCHSTATE:1:5:cc/cr200c/r3995:|Js9d/9s6d3s/Jh";
  // string match_state = "MATCHSTATE:1:5:cc/c:|Js9d/9s6d3s";
  // string match_state = "MATCHSTATE:1:5::|Js9d";
  BotAction ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("Action %i we_bet_to %i\n", ba, we_bet_to);
}

// STATE:1618:cr300c/r450c/r900c/r2700r17026r20000f:QhJh|6c6d/JdJc4h/5d/Js:17026|-17026:n|e
// Problems:
// 1) We are calling Translate() on our own actions.
// 2) We get to a showdown node and then try to interpret a fold action.
static void Crash1(Agent *agent) {
  printf("Crash1\n");
  printf("------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:0:1618:cr300c/r450c/r900c/r2700r17026r20000f:QhJh|/JdJc4h/5d/Js";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("QJs action %i we_bet_to %i\n", ba, we_bet_to);
}

// With it 8.1b system, expect a fold prob of about 0.7 for 94o (bucket 141).
static void Root(Agent *agent) {
  printf("Root\n");
  printf("----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
#if 0
  match_state = "MATCHSTATE:1:0::|9s4c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("94o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0:cc/c:|9s4c/AcTd4s";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("94o cc/c action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0:cc/cr200c/c:|9s4c/AcTd4s/4d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("94o cc/cr200/c action %i we_bet_to %i\n", ba, we_bet_to);
#endif
  match_state = "MATCHSTATE:1:0:cc/cr200c/cr600c/c:|9s4c/AcTd4s/4d/2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("94o cc/cr200/c600c/c action %i we_bet_to %i\n", ba, we_bet_to);
}
#endif

#if 0
static void Root(Agent *agent) {
  printf("Root\n");
  printf("----\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;
  match_state = "MATCHSTATE:1:0::|9s4c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("94o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|9s5c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("95o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|9s6c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("96o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|9s7c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("97o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|9s8c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("98o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|AsAc";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AA root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|3s2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("32o root action %i we_bet_to %i\n", ba, we_bet_to);
  match_state = "MATCHSTATE:1:0::|Ks7c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("K7o root action %i we_bet_to %i\n", ba, we_bet_to);
}
#endif

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card abstraction params> "
	  "<endgame card abstraction params> "
	  "<base betting abstraction params> "
	  "<endgame betting abstraction params> <base CFR params> "
	  "<endgame CFR params> <runtime params> "
	  "<endgame st> <num endgame its> <its> (optional args)\n", prog_name);
  fprintf(stderr, "Optional arguments:\n");
  fprintf(stderr, "  debug: generate debugging output\n");
  fprintf(stderr, "  eoe: exit on error\n");
  fprintf(stderr, "  fs: fixed seed\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc < 12) {
    Usage(argv[0]);
  }

  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> base_card_params = CreateCardAbstractionParams();
  base_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    base_card_abstraction(new CardAbstraction(*base_card_params));
  unique_ptr<Params> endgame_card_params = CreateCardAbstractionParams();
  endgame_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    endgame_card_abstraction(new CardAbstraction(*endgame_card_params));
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> endgame_betting_params = CreateBettingAbstractionParams();
  endgame_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>
    endgame_betting_abstraction(
			  new BettingAbstraction(*endgame_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> endgame_cfr_params = CreateCFRParams();
  endgame_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig> endgame_cfr_config(new CFRConfig(*endgame_cfr_params));

  unique_ptr<Params> runtime_params = CreateRuntimeParams();
  runtime_params->ReadFromFile(argv[8]);
  unique_ptr<RuntimeConfig>
    runtime_config(new RuntimeConfig(*runtime_params));
  unsigned int endgame_st, num_endgame_its;
  if (sscanf(argv[9], "%u", &endgame_st) != 1) Usage(argv[0]);
  if (sscanf(argv[10], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  unsigned int num_players = Game::NumPlayers();
  unsigned int *iterations = new unsigned int[num_players];
  unsigned int a = 11;
  if (base_betting_abstraction->Asymmetric()) {
    unsigned int it;
    for (unsigned int p = 0; p < num_players; ++p) {
      if ((int)a >= argc) Usage(argv[0]);
      if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
      iterations[p] = it;
    }
  } else {
    unsigned int it;
    if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
    for (unsigned int p = 0; p < num_players; ++p) {
      iterations[p] = it;
    }
  }

  bool debug = false;             // Disabled by default
  bool exit_on_error = false;     // Disabled by default
  for (int i = a; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "debug") {
      debug = true;
    } else if (arg == "eoe") {
      exit_on_error = true;
    } else {
      Usage(argv[0]);
    }
  }

  InitRandFixed();

  BettingTree **betting_trees = new BettingTree *[num_players];
  if (base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] =
	BettingTree::BuildAsymmetricTree(*base_betting_abstraction, p);
    }
  } else {
    BettingTree *betting_tree =
      BettingTree::BuildTree(*base_betting_abstraction);
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] = betting_tree;
    }
  }

  unsigned int small_blind = 50;
  unsigned int stack_size = 20000;
  bool fixed_seed = true;
  NLAgent agent(*base_card_abstraction, *endgame_card_abstraction,
		*base_betting_abstraction, *endgame_betting_abstraction,
		*base_cfr_config, *endgame_cfr_config, *runtime_config,
		iterations, betting_trees, endgame_st, num_endgame_its, debug,
		exit_on_error, fixed_seed, small_blind, stack_size);

#if 0
  PreflopP1(&agent);
  PreflopP0(&agent);
  FlopP0(&agent);
  FlopP1(&agent);
  TurnP0(&agent);
  TurnP1(&agent);
  RiverP0(&agent);
  RiverP1(&agent);
#endif
  // Loose2(&agent);
  // Loose3(&agent);
  // Loose3P1(&agent);
  Loose4(&agent);

  if (base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete betting_trees[p];
    }
  } else {
    delete betting_trees[0];
  }
  delete [] betting_trees;
}
