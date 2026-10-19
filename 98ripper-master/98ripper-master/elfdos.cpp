#include "98ripper.h"

// The most common ELFDOS format:
// Signature " ELFDOS FORMAT " at 0x0003.
// File index table at 0xA0000, image size is typically 0x134000 (1261568) bytes.

// Each file entry in the index is 24 bytes, LSB byte order:
//   char[13]: filename, including dot and suffix, null-padded
//   1 byte: track
//   2 bytes: byte offset within the track
//   4 bytes: size in bytes
//   4 bytes: copy of size?

// Alternative ELFDOS format used in Dragon Knight 2:
// Same signature, but file index table at 0x400, each entry is 24 bytes:
//   char[14]: filename, including dot and suffix, null-padded
//   2 bytes: sector
//   2 bytes: byte offset within the sector
//   2 bytes: size in bytes
//   4 bytes: zeroed out, unused?

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct ElfDosBootSector
{
	uint8_t jumps[3];
	char formatName[15];
};

struct ElfDosDirectoryEntry
{
	char fileName[13];
	uint8_t startTrack; // cylinder*head = track, each track contains sectors which contain bytes
	uint16_t startOffset; // byte offset within track
	uint32_t fileSize;
	uint32_t fileSize2; // always copy of fileSize? Foxy 2 first disk has a discrepancy...
};

struct ElfDosAltDirEntry
{
	char fileName[14];
	uint16_t startSector;
	uint16_t startOffset; // byte offset within sector
	uint16_t fileSize;
	uint32_t unused; // always zeroed out?
};
#pragma pack(pop)

bool RipElfDos(uint8_t* filebuffy, size_t buffysize)
{
	if (options.dumpOnly)
		return false;
	if (buffysize < 0xB0000)
	{
		Verbose("File too tiny");
		return false;
	}

	// Validate the header. Not necessarily in place on every disk of multidisk games.
	auto header = (ElfDosBootSector*)filebuffy;
	if (memcmp(header->formatName, " ELFDOS FORMAT ", 15) != 0)
		Verbose("Missing elfdos sig");
	else
		Verbose("Valid Elf DOS header found");

	// Check for a first valid entry in the file index list.
	uint32_t bytesPerTrack = 8 * 1024; // maybe wrong if floppy is of unusual size
	uint32_t bytesPerSector = 0x400;
	auto direntry = (ElfDosDirectoryEntry*)(filebuffy + 0xA0000);
	auto diralt = (ElfDosAltDirEntry*)(filebuffy + 0x400);

	bool dirvalid =
		(direntry->fileName[0] > ' ' &&
		direntry->fileSize == direntry->fileSize2 &&
		direntry->fileSize != 0 &&
		direntry->startTrack * bytesPerTrack + direntry->startOffset + direntry->fileSize <= buffysize);

	bool altvalid =
		(diralt->fileName[0] > ' ' &&
		diralt->fileSize != 0 &&
		diralt->startSector == 8 &&
		diralt->startOffset < bytesPerSector &&
		diralt->unused == 0 &&
		diralt->startSector * bytesPerSector + diralt->startOffset + diralt->fileSize <= buffysize);

	if (!dirvalid && !altvalid)
	{
		Verbose("Error: Elf DOS file entry list looks invalid");
		return false;
	}

	auto dumpdir = options.outputPath / imagename;
	if (!options.listOnly && !options.idOnly)
	{
		if (!CreateOutputDirectory(dumpdir))
			return true;
	}

	if (dirvalid)
	{
		while (direntry->fileName[0] != 0)
		{
			auto filename = ToLower(string(direntry->fileName, 12));
			if (options.filter.empty() || fnmatch(options.filter, filename))
			{
				if (options.verbose || options.listOnly)
					cout << logprefix << filename << "\t" << to_string(direntry->fileSize) << " bytes\n";

				auto readofs = direntry->startTrack * bytesPerTrack + direntry->startOffset;
				if (direntry->fileSize != direntry->fileSize2)
				{
					Verbose("Warning: Unexpected size2");
					// Use whichever size is larger and doesn't go out of bounds...
					if (direntry->fileSize2 > direntry->fileSize && readofs + direntry->fileSize2 <= buffysize)
						direntry->fileSize = direntry->fileSize2;
				}

				if (!options.listOnly && !options.idOnly)
				{
					if (readofs + direntry->fileSize > buffysize)
						Error("File data out of bounds");
					else
					{
						if (readofs > 0xA0000 || readofs + direntry->fileSize <= 0xA0000)
							Dump(dumpdir / filename, filebuffy + readofs, direntry->fileSize);
						else
						{
							auto size1 = 0xA0000 - readofs;
							auto size2 = direntry->fileSize - size1;
							Verbose("File data crosses directory index up to " + to_string(0xA2000 + size2));
							Dump(dumpdir / filename, filebuffy + readofs, size1, filebuffy + 0xA2000, size2);
						}
					}
				}
			}

			direntry++;
		}
		if (options.idOnly)
			cout << logprefix << "ElfDOS\n";
		return true;
	}

	while (diralt->fileName[0] != 0)
	{
		auto filename = ToLower(string(diralt->fileName, 12));
		if (options.filter.empty() || fnmatch(options.filter, filename))
		{
			if (options.verbose || options.listOnly)
				cout << logprefix << filename << "\t" << to_string(diralt->fileSize) << " bytes\n";

			auto readofs = diralt->startSector * bytesPerSector + diralt->startOffset;

			if (!options.listOnly && !options.idOnly)
			{
				if (readofs + diralt->fileSize > buffysize)
					Error("File data out of bounds");
				else
					Dump(dumpdir / filename, filebuffy + readofs, diralt->fileSize);
			}
		}

		diralt++;
	}
	if (options.idOnly)
		cout << logprefix << "ElfDOS type 2\n";
	return true;
}
