#ifndef _FILES_H_
#define _FILES_H_

#include <string>

using namespace std;

class Files {
public:
  static void Init(void);
  static const char *OldCFRBase(void);
  static const char *NewCFRBase(void);
  static const char *StaticBase(void);
private:
  static string old_cfr_base_;
  static string new_cfr_base_;
  static string static_base_;
};

#endif
