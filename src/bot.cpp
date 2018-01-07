#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <string>

#include "agent.h"
#include "bot.h"

using namespace std;

Bot::Bot(Agent *agent) {
  agent_ = agent;
  sockfd_ = -1;
  match_over_ = true;
  current_game_state_string_ = "";
}

Bot::~Bot(void) {
}

// ip_addr could, for example, be "10.0.0.1" or "127.0.0.1" for loopback
// connection
void Bot::Connect(const char *hostname, int port) {
  fprintf(stderr, "Attempting to connect to %s on port %i...\n", hostname,
	  port);

  int    rc;
  struct sockaddr_in   addr;

  /*************************************************/
  /* Create an AF_INET stream socket               */
  /*************************************************/
  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd_ < 0) {
    fprintf(stderr, "socket() returned %i\n", sockfd_);
    exit(-1);
  }

  /*************************************************/
  /* Initialize the socket address structure       */
  /*************************************************/
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;

  struct hostent *hostent;
  hostent = gethostbyname( hostname );
  if (hostent == NULL) {
    fprintf(stderr, "ERROR: could not look up address for %s\n", hostname);
    exit(-1);
  }
  memcpy(&addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);

  addr.sin_port        = htons(port);

  /*************************************************/
  /* Connect to the server.  Try up to 10 times.   */
  /*************************************************/
  unsigned int i;
  for (i = 0; i < 10; ++i) {
    rc = connect(sockfd_,
		 (struct sockaddr *)&addr,
		 sizeof(struct sockaddr_in));
    if (rc == 0) {
      break;
    }
    fprintf(stderr, "Error returned from connect rc %i errno %i\n", rc,
	    errno);
    sleep(30);
  }
  if (i == 10) {
    fprintf(stderr, "Connect failed ten times; giving up\n");
    exit(-1);
  }

  match_over_ = false;
  SendMessage("VERSION:2.0.0");
  fprintf(stderr, "Successful connection!\n");
}

/**
 * Send an action string (action should be r??, c, or f, where ?? is the final
 *   amount in the pot from a player in chips).  (Although I don't think we
 *   supply the amount during limit.)
 * Usually called during HandleStateChange. 
 * Action will be in response to
 * the state in current_game_state_string_.
 */
void Bot::SendAction(string action) {
  string msg = current_game_state_string_;
  msg += ":";
  msg += action;
  SendMessage(msg);
}

/**
 * Send an action (action should be r, c, or f).
 * Usually called during handleStateChange. 
 * Action will be in response to
 * the state in current_game_state_string_.
 */
void Bot::SendAction(char action) {
  string msg;
  msg += action;
  SendAction(msg);
}

// This method is called for limit
void Bot::SendRaise(void) {
  SendAction('r');
}

/**
 * send a raise action. The final in pot is the total YOU want to have
 * put in the pot after the raise (ie including previous amounts from
 * raises, calls, and blinds.
 * Don't think this is used for limit
 */
void Bot::SendRaise(unsigned int final_in_pot) {
  string msg = "r";
  char buf[100];
  sprintf(buf, "%i", final_in_pot);
  msg += buf;
  SendAction(msg);
}

/**
 * send a call action.
 */
void Bot::SendCall(void) {
  SendAction('c');
}

/**
 * send a fold action.
 */
void Bot::SendFold(void) {
  SendAction('f');
}

/**
 * Start the client. Should call connect() before running.
 */
void Bot::Run(void) {
  while (true){
    string message;
    if (! ReceiveMessage(&message)) {
      fprintf(stderr, "ReceiveMessage returned false\n");
      break;
    }
    // fprintf(stderr, "Received message: \"%s\"\n", message.c_str());
    if (! strncmp(message.c_str(), "MATCHSTATE:", 11)) {
      current_game_state_string_ = message;
      unsigned int bet_to;
      BotAction ba = agent_->HandleStateChange(current_game_state_string_,
					       &bet_to);
      if (ba == BA_FOLD) {
	// fprintf(stderr, "Sending fold\n");
	SendFold();
      } else if (ba == BA_CALL) {
	// fprintf(stderr, "Sending call\n");
	SendCall();
      } else if (ba == BA_BET) {
	// fprintf(stderr, "Sending raise %i\n", bet_to);
	SendRaise(bet_to);
      } else {
	// Do nothing
	// fprintf(stderr, "Sending nothing\n");
      }
    } else if (message == "ENDGAME" || message == "#GAMEOVER"){
      // Do we still get messages like this in version 2.0?  It's not
      // mentioned in protocol description.
      fprintf(stderr, "Received message %s\n", message.c_str());
      break;
    } else {
      fprintf(stderr, "Unexpected message: \"%s\"\n", message.c_str());
    }
  }
  fprintf(stderr, "Exited loop\n");
  Close();
}
    
/**
 * Close the connection. Called when match ends.
 */
void Bot::Close(void) {
  fprintf(stderr, "Close() called\n");
  match_over_ = true;
  close(sockfd_);
}

/**
 * Receive a message from the server.
 */
bool Bot::ReceiveMessage(string *message) {
  char buf[10000];
  int blen = 0;
  do {
    // read() only returns zero when the socket is closed
    int nr = read(sockfd_, &buf[blen], 1);
    if (nr == 0) {
      fprintf(stderr, "read() returned zero; errno %i, sockfd_ %i\n", errno,
	      sockfd_);
      *message = "";
      return false;
    }
    if (nr != 1) {
      fprintf(stderr, "read returned %i\n", nr);
      close(sockfd_);
      exit(-1);
    }
    ++blen;
    
  } while(! IsComplete(buf, blen) && blen < 10000);
  if (blen == 10000) {
    fprintf(stderr, "Buffer overflow\n");
    *message = "";
    return true;
  }
  buf[blen - 2] = 0;
  *message = buf;
  return true;
}

/**
 * Test if the message is complete (ends in \r\n).
 */
bool Bot::IsComplete(const char *str, int len) {
  return (len >= 2 && str[len-1] == '\n' && str[len-2] == '\r');
}

/**
 * Send a message to the server. Appends \r\n.
 */
void Bot::SendMessage(string msg) {
  // showVerbose("CLIENT SENDS:"+message);
  if (! match_over_){
    // fprintf(stderr, "Calling send \"%s\"\n", msg.c_str());
    msg += "\r\n";
#if 0
    timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(stderr, "Secs %i usecs %i\n", (int)tv.tv_sec, (int)tv.tv_usec);
#endif
    unsigned int len = send(sockfd_, msg.data(), msg.size(), 0);
    if (len != msg.size()) {
      fprintf(stderr, "Sending %i bytes failed; %i returned\n",
	      (int)msg.size() + 1, len);
      close(sockfd_);
      exit(-1);
    }
    // fprintf(stderr, "SendMessage: send() returned %i (success)\n", len);
  }
}
