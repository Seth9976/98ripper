// This is a custom replacement for the fnmatch function, to remove the libiberty dependency on Windows.
// Unlike the rest of 98ripper, I consider this algorithm and implementation to be public domain.

#ifndef	_FNMATCH_H
#define	_FNMATCH_H	1

using namespace std;
bool fnmatch(string pattern, string filename);

#endif
