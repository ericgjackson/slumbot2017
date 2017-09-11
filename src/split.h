#ifndef _SPLIT_H_
#define _SPLIT_H_

#include <string>
#include <vector>

using namespace std;

void Split(const char *line, char sep, bool allow_empty, vector<string> *comps);
void ParseDoubles(const string &s, vector<double> *values);
void ParseInts(const string &s, vector<int> *values);
void ParseUnsignedInts(const string &s, vector<unsigned int> *values);

#endif
