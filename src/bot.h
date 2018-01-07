#ifndef _BOT_H_
#define _BOT_H_

#include <string>

using namespace std;

class Agent;

class Bot {
 public:
  Bot(Agent *agent);
  ~Bot(void);
  void Connect(const char *ip_addr, int port);
  void Run(void);
 protected:
  void SendMessage(string msg);
  bool ReceiveMessage(string *message);
  void Close(void);
  bool IsComplete(const char *str, int len);
  void HandleStateChange(void);
  void SendAction(string action);
  void SendAction(char action);
  void SendFold(void);
  void SendCall(void);
  void SendRaise(void);
  void SendRaise(unsigned int final_in_pot);

  Agent *agent_;
  // It is not changed during a call to HandleStateChange()
  string current_game_state_string_;
  int    sockfd_;
  bool   match_over_;
};

#endif
