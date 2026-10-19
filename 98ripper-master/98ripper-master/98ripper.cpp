/*                                                                          *
 * 98ripper                                                                 *
 * Copyright 2020-2024 :: Kirinn Bunnylin / MoonCore                        *
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
#include "98ripper.h"
using namespace std;

CommandlineOptions options = {};
iconv_t iconvdescriptor;
string imagename;
string logprefix;
int errorcount = 0;

set<filesystem::path> dumpedFiles; // set of files written out, to detect duplicates

// Call Error() when image format definitely known and something goes wrong.
// Use Verbose("Error:...") instead when image header reading fails, since most headers are
// expected to not parse when trying every possible converter on an image.
void Error(const string& txt)
{
	cerr << logprefix << "Error: " << txt << "\n";
	errorcount++;
}

void Verbose(const string& txt)
{
	if (options.verbose)
		cout << logprefix << txt << "\n";
}

string ShiftJisToUTF8(char* shiftjistxt)
{
	size_t inputavailable = strlen(shiftjistxt);
	size_t maxout = inputavailable * 4;
	size_t outputavailable = maxout;
	char utf8txt[maxout];
	char* outputptr = utf8txt;
	char** inputstringref = &shiftjistxt;
	iconv(iconvdescriptor, inputstringref, &inputavailable, &outputptr, &outputavailable);
	return string(utf8txt, maxout - outputavailable);
}

string ToLower(string input)
{
	transform(input.begin(), input.end(), input.begin(), [](unsigned char c){ return tolower(c); });
	return input;
}

bool CreateOutputDirectory(const filesystem::path& dirpath)
{
	error_code ec;
	filesystem::create_directories(dirpath, ec);
	if (!ec)
		return true;
	Error("Failed to create " + dirpath.u8string() + ": " + ec.message());
	return false;
}

filesystem::path CheckAlreadyDumped(const filesystem::path& filepath)
{
	// If a particular output file path has already been seen, the image may contain multiple
	// copies of the same-named file, perhaps in error, perhaps as copy protection. For best rips,
	// extra copies need to be renamed with an extra underscore, so no files get overwritten.
	auto fp = filepath;
	while (dumpedFiles.find(fp) != dumpedFiles.end())
	{
		auto oldpath = fp.filename().u8string();
		fp.replace_filename(fp.stem().u8string() + "_" + fp.extension().u8string());
		Verbose("Warning: Renaming duplicate file " + oldpath + " -> " + fp.filename().u8string());
	}
	dumpedFiles.insert(fp);
	return fp;
}

void Dump(const filesystem::path& dumpfilepath, uint8_t* srcbuffy, size_t writesize, uint8_t* src2, size_t size2)
{
	auto outpath = CheckAlreadyDumped(dumpfilepath);
	//cout << "Dumping " << outpath.u8string() << "_\n";
	ofstream dumpfile(outpath, ios::trunc | ios::binary);
	dumpfile.write((char*)srcbuffy, writesize);
	if (src2 != NULL && size2 != 0)
		dumpfile.write((char*)src2, size2);
	dumpfile.close();
	if (!dumpfile)
		Error("Failed to write file");
}

class Algorithm
{
	public:
	string extension;
	string description;
	bool isAlias;
	bool (*Ripper)(uint8_t* filebuffy, size_t filesize);

	Algorithm(const string& _extension, const string& _description, bool _isalias, bool (*_Ripper)(uint8_t* filebuffy, size_t filesize))
	{
		extension = _extension;
		description = _description;
		Ripper = _Ripper;
		isAlias = _isalias;
	}

	bool TryRip(uint8_t* filebuffy, size_t filesize)
	{
		Verbose("Trying to rip as " + description + "...");
		return Ripper(filebuffy, filesize);
	}
};

vector<Algorithm*> algorithmList = {
	new Algorithm("", "Raw FAT Disk Image", false, RipFAT),
	new Algorithm(".hdm", "HDM headerless disk image", true, RipFAT),
	new Algorithm("", "Raw FAT8 Disk Image", false, RipFAT8),
	new Algorithm("", "Elf DOS format", false, RipElfDos),
	new Algorithm("", "MAKO-DOS format", false, RipMakoDos),
	//new Algorithm("", "YSFS format", false, RipYSFS),
	new Algorithm(".iso", ".ISO: ISO-9660 disc image", false, RipISO),
	new Algorithm(".dcp", ".DCP: Disk Copy Pro", false, RipDCP),
	new Algorithm(".dcu", ".DCU: Disk Copy Pro", true, RipDCP),
	new Algorithm(".dim", ".DIM: DIFC image", false, RipDIM),
	new Algorithm(".nhd", ".NHD: T98-Next Hard Drive", false, RipNHD),
	new Algorithm(".d88", ".D88: Pasopia floppy disk image", false, RipD88),
	new Algorithm(".fdd", ".FDD: Virtual-98 Floppy Disk Dump", false, RipFDD),
	new Algorithm(".fdi", ".FDI: Floppy Disk Image", false, RipFDI),
	new Algorithm(".hdi", ".HDI: Hard Disk Image", false, RipHDI),
	new Algorithm(".dip", ".DIP: Disk image", false, RipDIP),
	new Algorithm(".nfd", ".NFD: T98-Next Floppy Disk", false, RipNFD),
	new Algorithm(".fim", ".FIM: Wizard98 Floppy Image", false, RipFIM),
	};

// -------------------------------------------------------------------

void PrintHelp()
{
	cout << "98ripper - version 7 - Rip files from PC-98 disk images\n\n"
		"Usage: 98ripper [options] <image files...>\n"
		"       98ripper -r [options] <directory>\n\n"
		"Options:\n"
		"  -o --output=<path>   Set the output path, default=<working directory>\n"
		"                       Ripped images automatically go into unique subfolders.\n"
		"  -i --id              Identify image type and data errors without extracting\n"
		"  -l --list            List image contents without extracting\n"
		"  -d --dump            Dump raw image without extracting\n"
		"  -r --recursive       Rip everything under input folder and its subfolders\n"
		"  -u --undelete        Try to dump deleted files too\n"
		"  -t --table=<index>   Select file table to use, default=0\n"
		"  -f --filter=<text>   Only list/extract files matching this, wildcards *?\n"
		"  -v --verbose         Verbose output\n"
		"\nSupported formats:\n"
		"  fat12  fat16  fat8  fat-do  makodos  elfdos\n";
	for (auto algo : algorithmList)
	{
		if (!algo->extension.empty())
			cout << "  " << algo->extension;
	}
	cout << "\n";
}

void ResolveFiles(const string& inputpath)
// Adds all matching files (optionally recursively, with wildcards) to the input files list.
// If the input path is a directory, it is resolved as "directory/*".
{
	try
	{
		filesystem::path newpath(inputpath);
		if (filesystem::is_directory(newpath))
			newpath /= "*";
		else if (filesystem::exists(newpath))
		{
			options.inputFileList.push_back(newpath);
			return;
		}

		// The input path either contains wildcards or doesn't exist.
		const auto parentpath = (newpath.parent_path().empty() ? "." : newpath.parent_path());
		const auto pathmatcher = newpath.filename().u8string();

		// (The recursive iterator is sadly type-incompatible with the non-recursive one, so this looks inelegant.)
		if (options.recursive)
		{
			for (auto f : filesystem::recursive_directory_iterator(parentpath, filesystem::directory_options::follow_directory_symlink))
			{
				if (!filesystem::is_directory(f.path()) &&
					fnmatch(pathmatcher, f.path().filename().u8string()))
				{
					options.inputFileList.push_back(f.path());
				}
			}
		}
		else
		{
			for (auto f : filesystem::directory_iterator(parentpath))
			{
				if (!filesystem::is_directory(f.path()) &&
					fnmatch(pathmatcher, f.path().filename().u8string()))
				{
					options.inputFileList.push_back(f.path());
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		cerr << "[" << inputpath << "] " << e.what() << "\n";
		errorcount++;
	}
}

bool ParseArgs(int argc, char* argv[])
{
	const char* const short_opts = "o:t:f:ildruvh?";
	const option long_opts[] = {
		{ "output", required_argument, NULL, 'o' },
		{ "table", required_argument, NULL, 't' },
		{ "filter", required_argument, NULL, 'f' },
		{ "id", no_argument, NULL, 'i' },
		{ "list", no_argument, NULL, 'l' },
		{ "dump", no_argument, NULL, 'd' },
		{ "recursive", no_argument, NULL, 'r' },
		{ "undelete", no_argument, NULL, 'u' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, no_argument, NULL, 0 }
	};
	extern int optind;
	string _optarg;

	options.outputPath = ".";

	while (true)
	{
		int opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
		if (opt == -1) break;

		if (optarg)
		{
			if (optarg[0] == '=' || optarg[0] == ':')
				_optarg = string(optarg + 1);
			else
				_optarg = string(optarg);
		}

		switch (opt)
		{
			case 'o':
				// Empty path defaults to working directory.
				if (_optarg == "")
					_optarg = ".";
				// Trim trailing slashes, unless it's the only thing in the string.
				while (_optarg.size() > 1 && (_optarg.back() == '/' || _optarg.back() == '\\'))
					_optarg.erase(_optarg.size() - 1);
				options.outputPath = _optarg;
				break;

			case 'f':
				options.filter = ToLower(_optarg);
				break;

			case 't':
			{
				auto result = from_chars(_optarg.data(), _optarg.data() + _optarg.size(), options.selectFAT);
				if (result.ec != errc())
				{
					cerr << make_error_condition(result.ec).message() << ": -t\n";
					return false;
				}
				break;
			}

			case 'i':
				options.idOnly = true;
				break;

			case 'l':
				options.listOnly = true;
				break;

			case 'd':
				options.dumpOnly = true;
				break;

			case 'r':
				options.recursive = true;
				break;

			case 'u':
				options.undelete = true;
				break;

			case 'v':
				options.verbose = true;
				break;

			case '?':
			case 'h':
			default:
				return false;
		}
	}

	if (optind == argc)
	{
		cerr << "You must specify an image file\n";
		return false;
	}

	while (optind < argc)
		ResolveFiles(argv[optind++]);

	return true;
}

bool DispatchFAT(uint8_t* buffy, size_t buffysize, bool knowncontainer)
// Tries each filesystem ripper on the given buffer until one works.
{
	if (options.dumpOnly)
	{
		auto dumpfilepath = options.outputPath / (imagename + ".dump");
		Verbose(dumpfilepath.u8string());
		Dump(dumpfilepath, buffy, buffysize);
		return true;
	}
	if (RipFATDO(buffy, buffysize))
		return true;
	if (RipFAT(buffy, buffysize))
		return true;
	if (RipFAT8(buffy, buffysize))
		return true;
	if (RipMakoDos(buffy, buffysize))
		return true;
	if (RipElfDos(buffy, buffysize))
		return true;
	if (knowncontainer)
		Error("Image format is ok, but no recognised filesystem inside image");
	return false;
}

bool Dispatch(uint8_t* filebuffy, size_t filesize, string extension)
// Tries every known ripper algorithm on the given buffer until one works.
{
	extension = ToLower(extension);

	for (auto algo : algorithmList)
	{
		if (extension.compare(algo->extension) == 0)
		{
			if (algo->TryRip(filebuffy, filesize))
				return true;
		}
	}
	for (auto algo : algorithmList)
	{
		if (extension.compare(algo->extension) != 0 && !algo->isAlias)
		{
			if (algo->TryRip(filebuffy, filesize))
				return true;
		}
	}
	return false;
}

void ProcessFiles()
// Reads all input files into memory one at a time and forwards them to Dispatch.
{
	uint8_t* filebuffy = NULL;
	size_t buffysize = 0;
	iconvdescriptor = iconv_open("UTF-8", "SHIFT_JIS");

	for (auto inputfilepath : options.inputFileList)
	{
		error_code errcode;
		logprefix = "[" + inputfilepath.u8string() + "] ";

		size_t filesize = filesystem::file_size(inputfilepath, errcode);
		if (errcode)
		{
			Error(errcode.message());
			continue;
		}
		if (filesize > buffysize)
		{
			if (filebuffy != NULL)
				delete[] filebuffy;
			filebuffy = new uint8_t[filesize];
			buffysize = filesize;
		}

		ifstream inputfile(inputfilepath, ios::in | ios::binary);
		if (!inputfile)
		{
			Error("Failed to open");
			continue;
		}
		inputfile.read((char*)filebuffy, filesize);
		if (!inputfile)
		{
			Error("Read only " + to_string(inputfile.gcount()) + " bytes of " + to_string(filesize));
		}
		else
		{
			imagename = inputfilepath.stem().u8string();
			logprefix = "[" + imagename + "] ";
			if (!Dispatch(filebuffy, filesize, inputfilepath.extension().u8string()))
				Error("No rip algorithm worked");
		}
		inputfile.close();
	}

	if (filebuffy != NULL)
		delete[] filebuffy;
	iconv_close(iconvdescriptor);
}

int main(int argc, char* argv[])
{
	if (!ParseArgs(argc, argv))
	{
		PrintHelp();
		return 1;
	}

	if (options.inputFileList.size() == 0)
	{
		cerr << "No matching input files\n";
		return 1;
	}

	if (options.outputPath != "." && !CreateOutputDirectory(options.outputPath))
		return 1;

	ProcessFiles();

	if (options.verbose)
		cout << errorcount << " errors\n";
	return errorcount;
}
