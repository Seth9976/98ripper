// A custom replacement for the fnmatch function by Kirinn Bunnylin/Mooncore.
// Needed to eliminate the libiberty dependency on Windows...
// Uses recursion, so performance isn't the best.
// I consider this algorithm and implementation to be public domain.

// The filter pattern takes * and ? wildcards, and for anything else does a literal bytewise match.
// There is no escape character, a backslash is just a normal character.
// The comparison is always case-sensitive.
// The filename can be given with or without a path. Use the target platform's native directory separator.
// If the input filter doesn't have a directory separator character, the path component of filename is ignored.
// A suffixless pattern or filename is considered to end with a dot for comparison purposes.
// An initial directory separator character in the pattern or filename is ignored.
// Option flags are not implemented.
//
// Returns true for match found.

#include <string>
#include <filesystem> // requires C++17, use -std=c++17
using namespace std;

bool MatchPart(const char* pattern, const char* filename)
{
	if (*pattern == '*')
	{
		while (*(++pattern) == '*') ; // multiple * in a row: treat as a single *
		if (*pattern == 0) // pattern has * as last character: remaining match is always true
			return true;
		while (*filename != 0 && !MatchPart(pattern, filename))
			filename++;
	}

	while (true)
	{
		if (*pattern == '*')
			return MatchPart(pattern, filename);

		if (*pattern == 0 || *filename == 0)
			break;

		if (*pattern != '?' && *pattern != *filename)
			return false;
		pattern++; filename++;
	}
	// Anything left in either the pattern or filename now means the match was incomplete.
	return *pattern == 0 && *filename == 0;
}

bool fnmatch(string pattern, string filename)
{
	auto separator = filesystem::path::preferred_separator;

	auto last_separator = pattern.find_last_of(separator);
	if (last_separator == string::npos)
	{
		// If the pattern doesn't have a directory separator, ignore the filename's directory part.
		filename = filesystem::path(filename).filename().u8string();
		// If either string doesn't contain a dot and doesn't end in *, add a dot at the end.
		if (filename.back() != '*' && filename.find('.') == string::npos)
			filename += '.';
		if (pattern.back() != '*' && pattern.find('.') == string::npos)
			pattern += '.';
	}
	else
	{
		// If either string's filename part doesn't contain a dot and doesn't end in *, add a dot at the end.
		if (pattern.back() != '*' && pattern.find('.', last_separator) == string::npos)
			pattern += '.';
		last_separator = filename.find_last_of(separator);
		if (filename.back() != '*' && filename.find('.', last_separator) == string::npos)
			filename += '.';
	}

	auto p = pattern.c_str();
	auto fn = filename.c_str();
	// Ignore directory separator if it's the first character.
	if (*p == separator) p++;
	if (*fn == separator) fn++;

	return MatchPart(p, fn);
}

#ifdef bonk
#include <iostream>
bool Test(const string& pat, const string& filename, bool expected)
{
	cout << "'" << pat << "' ? '" << filename << "' :\t";
	auto result = fnmatch(pat, filename);
	cout << (result ? "MATCH" : "NO") << "\t";
	if (result == expected)
		cout << "OK\n";
	else
		cout << "FAIL\n";
	return (result == expected);
}

bool TestFNMatch()
{
	cout << "Pattern ? Filename : Result\n";
	return Test("", "", true)
		& Test("", "a", false)
		& Test("a", "", false)

		& Test("a", "a", true)
		& Test("a", "b", false)

		& Test("abc", "abc", true)
		& Test("abd", "abc", false)
		& Test("a", "abc", false)
		& Test("ab", "abc", false)
		& Test("bc", "abc", false)

		& Test("*", "", true)
		& Test("***", "", true)
		& Test("*", "abc", true)
		& Test("a*", "a", true)
		& Test("*a", "a", true)
		& Test("a*", "abc", true)
		& Test("a*c", "abc", true)
		& Test("a**c", "abc", true)
		& Test("*c", "abc", true)
		& Test("a*c*g*j", "abcdefghij", true)
		& Test("*a*b*c*", "abc", true)
		& Test("*ab", "abc", false)
		& Test("bc*", "abc", false)

		& Test("?", "", false)
		& Test("?", "a", true)
		& Test("???", "abc", true)
		& Test("a?c", "abc", true)
		& Test("?", "abc", false)
		& Test("a?bc", "abc", false)
		& Test("?abc", "abc", false)
		& Test("abc?", "abc", false)
		& Test("*?*", "abc", true)
		& Test("abc*abc", "abcabcabc", true)

		& Test("abc", "dir/abc", true)
		& Test("*", "dir/abc", true)
		& Test("???", "dir/abc", true)
		& Test("dir", "dir/abc", false)
		& Test("dir/abc", "dir/abc", true)
		& Test("dir/abc", "abc", false)
		& Test("*/*", "dir/abc", true)
		& Test("*/abc", "dir/abc", true)

		& Test("abc.ext", "abc.ext", true)
		& Test("abc?ext", "abc.ext", true) // fail! only expected to work on posix...
		& Test("ab*xt", "abc.ext", true) // fail! only expected to work on posix...
		& Test("abc.", "abc", true)
		& Test("abc", "abc.", true)
		& Test("abc", "abc.ext", false)
		& Test("abc.ex", "abc.ext", false)
		& Test("abc.ext", "abc", false)
		& Test("abc.*", "abc.ext", true)
		& Test("abc.*", "abc.def.ext", true)
		& Test("abc*", "abc.ext", true)
		& Test("abc.", "abc.ext", false)
		& Test("*.", "abc.ext", false)
		& Test("*.*", "abc.ext", true)
		& Test("*.", "abc", true)
		& Test("*.", "abc.", true)
		& Test("*.", "abc..", true)
		& Test(".ext", ".ext", true)
		& Test("*.ext", ".ext", true)
		& Test("*.ext", "...ext", true)
		& Test("abc.", "dir/abc", true)
		& Test("abc", "dir.ext/abc", true)

		& Test("/a", "a", true)
		& Test("/abc", "dir/abc", false)
		& Test("dir/abc", "/dir/abc", true)
		& Test("/*", "dir.ext/abc.def", true)
		;
}
#endif
