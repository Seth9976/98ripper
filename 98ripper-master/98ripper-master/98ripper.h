/*                                                                          *
 * 98ripper                                                                 *
 * Copyright 2020-2023 :: Kirinn Bunnylin / MoonCore                        *
 *                                                                          *
 * Available under the zlib license:                                        *
 *                                                                          *
 * This software is provided 'as-is', without any express or implied        *
 * warranty.  In no case will the authors be held liable for any damages    *
 * arising from the use of this software.                                   *
 *                                                                          *
 * Permission is granted to anyone to use this software for any purpose,    *
 * including commercial applications, and to alter it and redistribute it   *
 * freely, subject to the following restrictions:                           *
 *                                                                          *
 * 1. The origin of this software must not be misrepresented; you must not  *
 *    claim that you wrote the original software. If you use this software  *
 *    in a product, an acknowledgment in the product documentation would be *
 *    appreciated but is not required.                                      *
 * 2. Altered source versions must be plainly marked as such, and must not  *
 *    be misrepresented as being the original software.                     *
 * 3. This notice may not be removed or altered from any source             *
 *    distribution.                                                         *
 *                                                                          */

#ifndef	_98RIPPER_H
#define	_98RIPPER_H	1

#include <iostream>
#include <filesystem> // requires C++17, use -std=c++17
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#include <charconv>
#include "iconv.h"
#include "getopt.h"
#include "_fnmatch.h"
using namespace std;

struct CommandlineOptions
{
	vector<filesystem::path> inputFileList;
	filesystem::path outputPath;
	string filter;
	int selectFAT;
	bool idOnly, listOnly, dumpOnly, recursive, undelete, verbose;
};
extern CommandlineOptions options;
extern iconv_t iconvdescriptor;
extern string imagename;
extern string logprefix;
extern int errorcount;

void Error(const string& txt);
void Verbose(const string& txt);
string ShiftJisToUTF8(char* shiftjistxt);
string ToLower(string input);
bool CreateOutputDirectory(const filesystem::path& dirpath);
filesystem::path CheckAlreadyDumped(const filesystem::path& filepath);
void Dump(const filesystem::path& dumpfilepath, uint8_t* srcbuffy, size_t writesize, uint8_t* src2 = NULL, size_t size2 = 0);

bool RipFAT(uint8_t* filebuffy, size_t buffysize);
bool RipFAT8(uint8_t* filebuffy, size_t filesize);
bool RipFATDO(uint8_t* filebuffy, size_t filesize);
bool RipMakoDos(uint8_t* filebuffy, size_t buffysize);
bool RipElfDos(uint8_t* filebuffy, size_t buffysize);
bool DispatchFAT(uint8_t* buffy, size_t buffysize, bool knowncontainer);
bool RipIPL(uint8_t* filebuffy, size_t buffysize, size_t sectorsize, size_t heads, size_t sectors);
bool RipISO(uint8_t* filebuffy, size_t buffysize);
bool RipDCP(uint8_t* filebuffy, size_t buffysize);
bool RipDIM(uint8_t* filebuffy, size_t buffysize);
bool RipNHD(uint8_t* filebuffy, size_t buffysize);
bool RipNFD(uint8_t* filebuffy, size_t buffysize);
bool RipD88(uint8_t* filebuffy, size_t buffysize);
bool RipFDD(uint8_t* filebuffy, size_t buffysize);
bool RipFDI(uint8_t* filebuffy, size_t buffysize);
bool RipHDI(uint8_t* filebuffy, size_t buffysize);
bool RipDIP(uint8_t* filebuffy, size_t buffysize);
bool RipFIM(uint8_t* filebuffy, size_t buffysize);
#endif
