#include <filesystem>
#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct ISODescriptor
{
	uint8_t type;
	char signature[5];
	uint8_t version, reserved1;
	char systemId[32];
	char volumeId[32];
	uint8_t unused[8];
	uint32_t volumeBlockCountLSB, volumeBlockCountMSB;
	uint8_t stuff[40];
	uint16_t logicalBlockSizeLSB, logicalBlockSizeMSB;
	uint32_t pathTableSizeLSB, pathTableSizeMSB;
	uint32_t pathTableLBA, pathTableOptLBA, pathTableLBAMSB, pathTableOptLBAMSB;
	uint8_t rootdir[34];
	uint8_t moreStuff[1858];
};

struct ISODirectoryRecord
{
	uint8_t recordLength, exAttribLength;
	uint32_t LBALSB, LBAMSB, dataLengthLSB, dataLengthMSB;
	uint8_t date[7];
	uint8_t flags, interleavedUnitSize, interleaveGapSize;
	uint16_t seqNumberLSB, seqNumberMSB;
	uint8_t filenameLength;
	char filename[];
};
#pragma pack(pop)

ISODescriptor* primaryVolume = NULL;
ISODescriptor* jolietVolume = NULL;
uint8_t* ISObuffy;
size_t ISOsize;

void ScanISODir(ISODirectoryRecord* record, const filesystem::path& relativepath)
{
	auto dirofs = record->LBALSB * primaryVolume->logicalBlockSizeLSB;
	auto endofs = dirofs + record->dataLengthLSB;
	if (endofs > ISOsize)
	{
		Error("Directory \"" + relativepath.u8string() + "\" LBA out of bounds");
		return;
	}

	auto dumpdir = options.outputPath / imagename / relativepath;
	if (!options.listOnly && !options.idOnly)
	{
		if (!CreateOutputDirectory(dumpdir))
			return;
	}

	while (dirofs < endofs)
	{
		auto entry = (ISODirectoryRecord*)(ISObuffy + dirofs);
		if (entry->recordLength < 34)
		{
			if (entry->recordLength == 0)
			{
				// Continues in the next logical block...
				auto mask = (primaryVolume->logicalBlockSizeLSB - 1) ^ 0xFFFFFFFF;
				dirofs = (dirofs + primaryVolume->logicalBlockSizeLSB) & mask;
				continue;
			}
			Error("Unexpected record length " + to_string(entry->recordLength) + " at " + to_string(dirofs));
			break;
		}
		dirofs += entry->recordLength;

		// Ignore dot and dot-dot virtual directories.
		if ((entry->flags & 2) != 0 && entry->filenameLength == 1 && entry->filename[0] < 0x20)
			continue;
		// Remove ;1 from end of filenames.
		if (entry->filenameLength >= 3 && entry->filename[entry->filenameLength - 1] == '1' && entry->filename[entry->filenameLength - 2] == ';')
			entry->filenameLength -= 2;

		auto tempchar = entry->filename[entry->filenameLength];
		entry->filename[entry->filenameLength] = 0;
		auto filenamestr = ToLower(ShiftJisToUTF8(entry->filename));
		entry->filename[entry->filenameLength] = tempchar;
		auto newpath = relativepath / filenamestr;

		if ((entry->flags & 2) == 0)
		{
			if (options.filter.empty() || fnmatch(options.filter, newpath.u8string()))
			{
				if (options.listOnly || options.verbose)
				{
					cout << logprefix << newpath.u8string() << "\t" << to_string(entry->dataLengthLSB) << " bytes";

					if ((entry->flags & 1) != 0)
						cout << "\thidden";
					cout << "\n";
				}

				auto fileofs = entry->LBALSB * primaryVolume->logicalBlockSizeLSB;
				auto fileend = fileofs + entry->dataLengthLSB;
				if (fileend > ISOsize)
					Error("Directory \"" + newpath.u8string() + "\" LBA out of bounds");
				else if (!(options.listOnly | options.idOnly))
					Dump(dumpdir / filenamestr, ISObuffy + fileofs, entry->dataLengthLSB);
			}
		}
		else
		{
			Verbose(newpath.u8string() + "\t[DIR]");
			ScanISODir(entry, newpath);
		}
	}
}

bool RipISO(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 800000)
	{
		Verbose("File size too tiny");
		return false;
	}

	// Validate the headers.
	auto header = (ISODescriptor*)(filebuffy + 0x8000);
	while ((uint8_t*)header - filebuffy + sizeof(ISODescriptor) < filesize)
	{
		if (memcmp(header->signature, "CD001", 5) != 0)
		{
			Verbose("Error: Missing signature");
			return false;
		}
		if (header->version != 1)
		{
			Verbose("Error: Version is " + to_string(header->version));
			return false;
		}

		if (header->type == 1)
			primaryVolume = header;
		else if (header->type == 2)
			jolietVolume = header;

		if (header->type == 0xFF)
			break;
		header++;
	}

	if (primaryVolume == NULL)
	{
		Verbose("Error: no primary volume descriptor");
		return false;
	}
	if (primaryVolume->logicalBlockSizeLSB != 0x800)
	{
		Error("Unexpected logical block size: " + to_string(primaryVolume->logicalBlockSizeLSB));
		return true;
	}

	// If the rootdir dataLength value is bogus (rare, but it happens), check if there's a Joliet
	// descriptor available. This will point to a widechar version of the directory record, which
	// typically immediately follows the regular one. The start offset given there can be used as
	// the true end of the primary directory record.
	auto rootrecord = (ISODirectoryRecord*)primaryVolume->rootdir;
	if (rootrecord->dataLengthLSB < 34)
	{
		Verbose("Warning: primary volume descriptor has bad root length: " + to_string(rootrecord->dataLengthLSB));
		if (jolietVolume != NULL)
		{
			auto jolietrecord = (ISODirectoryRecord*)jolietVolume->rootdir;
			if (rootrecord->LBALSB < jolietrecord->LBALSB)
			{
				auto delta = (jolietrecord->LBALSB - rootrecord->LBALSB) * primaryVolume->logicalBlockSizeLSB;
				Verbose("Using prime-Joliet delta as root length: " + to_string(delta));
				rootrecord->dataLengthLSB = delta;
			}
		}
	}

	ISObuffy = filebuffy;
	ISOsize = filesize;
	ScanISODir(rootrecord, "");

	if (options.idOnly)
		cout << logprefix << "ISO\n";

	return true;
}
