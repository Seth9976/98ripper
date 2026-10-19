#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct NFDHeader
{
	char signature[16];
	char comment[256];
	uint32_t headerSize;
	uint8_t writeProtected;
	uint8_t heads; // disk surfaces, 1 or 2
	uint8_t reserved[10];
};

struct NFDSectorMeta
{
	uint8_t cylinder, head, sector; // cylinder 0xFF means not present
	uint8_t sectorSizeShift; // bytes per sector = 128 << sectorSizeShift
	uint8_t stuff[12]; // various emulation-related values
};
#pragma pack(pop)

struct NFDSectorList
{
	uint32_t id, ofs;
};

bool RipNFD(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (NFDHeader*)filebuffy;
	if (memcmp(header->signature, "T98FDDIMAGE.R", 13) != 0)
	{
		Verbose("Error: Missing T98 sig");
		return false;
	}
	if (header->signature[13] != '0' && header->signature[13] != '1')
	{
		Error("Unexpected T98 rev");
		return true;
	}
	bool rev1 = (header->signature[13] == '1');
	if (rev1) { Error("REV1"); return true; }

	if (header->headerSize != 0x10A10)
	{
		Verbose("Unexpected header size: " + to_string(header->headerSize));
		header->headerSize = 0x10A10;
	}
	if (header->heads == 0 || header->heads > 2)
	{
		Error("Unexpected heads: " + to_string(header->heads));
		return true;
	}

	header->comment[255] = 0; // null-terminate just in case
	Verbose("Valid T98 sig found, " + string(header->signature + 12, 3) + ", comment \"" + ShiftJisToUTF8(header->comment) + "\"");

	// Sectors in the file aren't necessarily sequential, so build a list from the following index.
	// The number of present sectors may vary and isn't explicitly given, and number of surfaces could be 1 or 2.
	// Therefore convert the index to a sorted list with the sort key being cylinder.head.sector.sectorsize,
	// which always places things in the right order regardless of number of heads or sectors given.
	auto listp = filebuffy + sizeof(NFDHeader);
	int numsectors = (filebuffy + header->headerSize - listp) / sizeof(NFDSectorMeta);
	if (numsectors != 4239)
	{
		Error("Unexpected numsectors: " + to_string(numsectors));
		return true;
	}

	NFDSectorList sectorlist[numsectors];
	auto readp = (NFDSectorMeta*)listp;
	uint32_t readofs = header->headerSize;
	int foundsectors = 0;
	for (int i = 0; i < numsectors; i++)
	{
		if (readp->cylinder != 0xFF && readp->sector != 0)
		{
			if (readp->sectorSizeShift > 4)
			{
				Error("Unexpected sector size: 128 << " + to_string(readp->sectorSizeShift));
				return true;
			}
			uint32_t newid = (readp->cylinder << 24) + (readp->head << 16) + (readp->sector << 8) + readp->sectorSizeShift;
			// Immediate insert using linear search.
			int j = foundsectors;
			while (j != 0 && newid < sectorlist[j - 1].id)
			{
				sectorlist[j] = sectorlist[j - 1];
				j--;
			}
			sectorlist[j].id = newid;
			sectorlist[j].ofs = readofs;
			foundsectors++;
			readofs += 128 << readp->sectorSizeShift;
			if (readofs > filesize)
			{
				Error("Sector ofs out of bounds");
				return true;
			}
		}
		readp++;
	}

	size_t tempbuffysize = filesize - header->headerSize;
	uint8_t tempbuffy[tempbuffysize];
	uint32_t writeofs = 0;
	for (int i = 0; i < foundsectors; i++)
	{
		//cout << i << ": " << (sectorlist[i].id >> 8) << " ofs " << sectorlist[i].ofs << " size " << (sectorlist[i].id & 0xFF) << "\n";
		auto sectorsize = 128 << (sectorlist[i].id & 0xFF);
		memcpy(tempbuffy + writeofs, filebuffy + sectorlist[i].ofs, sectorsize);
		writeofs += sectorsize;
	}

	DispatchFAT(tempbuffy, tempbuffysize, true);
	return true;
}
