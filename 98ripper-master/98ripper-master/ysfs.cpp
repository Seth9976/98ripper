#include "98ripper.h"

// Used by Falcom's Ys series at least. I don't know what the real name is, so call it YSFS.
//
// File table is found at 0x1000, 0x2000, or 0x2400... try every sector from 0x1000 to 0x2C00.
// Empty entries are all FF values. Valid entries can appear after intermediate empty entries.
// Assume the table is 0x1000 bytes.

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct YSFSDirectoryEntry
{
	char name[7]; // always ends with null byte
	uint8_t reserved; // always 0x4B or 0x4D?
	uint8_t reserved2; // always 00?
	uint8_t type; // file type - varies by game
	// Ys 3: PRG 0x01, BIOS 0x07, PRG 0x10, PRG/BOT 0x38, ARE map? 0x38, workbuf 0x40, MAG 0x68, GETPRG 0x68, EXTPRG 0x90
	uint8_t something, something2;
	uint16_t firstsector, lastsector;

	// Sectors are LSB on Ys 2 and Ys 3, but MSB on Ys 1 (or it's track + sector).
	// Ys 2 A: first file 000A-0019 = 0x2400..?, last file 026E-0285 = ..0xA13FF
	// Ys 2 B: 0000 = 0x1800?, first file 000A-000B = 0x2400..0x37FF?, last file 02C7-02C7 = 0xB3400..0xB37FF
};
#pragma pack(pop)

bool RipYSFS(uint8_t* filebuffy, size_t buffysize)
{
	if (options.dumpOnly)
		return false;
	if (buffysize < 0x80000)
	{
		Verbose("File too tiny");
		return false;
	}

	// Validate the header.
	//Verbose("Header found: " + string((char*)(filebuffy + 0x1000)));

	auto dumpdir = options.outputPath / imagename;
	if (!options.listOnly && !options.idOnly)
	{
		if (!CreateOutputDirectory(dumpdir))
			return true;
	}

	auto direntry = (YSFSDirectoryEntry*)(filebuffy + 0x1000);

	if (options.idOnly)
		cout << logprefix << "YSFS\n";
	return true;
}
