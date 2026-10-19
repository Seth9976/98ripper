#include "98ripper.h"

// Used by Forest's Metal & Lace 1.
//
// Header and file table sits at 0x1000. Each file entry is 8+3 chars, and filesize.
// List ends with 0xFFFFFFFF entries.
// Filedata starts at 0x2400, appearing in the same order as the file table, with no space wasted
// between files.

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct MakoDosDirectoryEntry
{
	char name[8];
	char suffix[3];
	uint8_t reserved;
	uint32_t fileSize;
};
#pragma pack(pop)

bool RipMakoDos(uint8_t* filebuffy, size_t buffysize)
{
	if (options.dumpOnly)
		return false;
	if (buffysize < 0x80000)
	{
		Verbose("File too tiny");
		return false;
	}

	// Validate the header.
	if (memcmp(filebuffy + 0x1000, "MAKO-DOS ", 9) != 0)
	{
		Verbose("Error: Missing mako-dos sig");
		return false;
	}
	Verbose("Header found: " + string((char*)(filebuffy + 0x1000)));

	auto dumpdir = options.outputPath / imagename;
	if (!options.listOnly && !options.idOnly)
	{
		if (!CreateOutputDirectory(dumpdir))
			return true;
	}

	auto direntry = (MakoDosDirectoryEntry*)(filebuffy + 0x1020);
	auto readofs = 0x2400;

	while ((unsigned char)direntry->name[0] != 0xFF)
	{
		char filename[13];
		int i;
		for (i = 0; i < 8; i++)
		{
			if ((unsigned char)direntry->name[i] <= 0x20)
				break;
			else
				filename[i] = direntry->name[i];
		}
		filename[i++] = '.';
		for (int j = 0; j < 3; j++)
		{
			if ((unsigned char)direntry->suffix[j] <= 0x20)
				break;
			else
				filename[i++] = direntry->suffix[j];
		}
		if (filename[i - 1] == '.')
			i--;
		filename[i] = 0;
		auto filenamestr = ToLower(ShiftJisToUTF8(filename));

		if (readofs + direntry->fileSize > buffysize)
		{
			Error("File \"" + filenamestr + "\" out of bounds");
			break;
		}

		if (options.filter.empty() || fnmatch(options.filter, filenamestr))
		{
			if (options.verbose || options.listOnly)
				cout << logprefix << filenamestr << "\t" << to_string(direntry->fileSize) << " bytes\n";

			if (!options.listOnly && !options.idOnly)
				Dump(dumpdir / filenamestr, filebuffy + readofs, direntry->fileSize);
		}

		readofs += direntry->fileSize;
		direntry++;
	}

	if (options.idOnly)
		cout << logprefix << "MAKO-DOS\n";
	return true;
}
