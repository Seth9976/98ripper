#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct FDDHeader
{
	char signature[8];
	char comment[128];
	uint16_t writeProtected, special;
	uint8_t reserved[80];
};

struct FDDSectorMeta
{
	uint8_t cylinder, head, sector; // cylinder 0xFF means not present
	uint8_t sectorSizeShift; // bytes per sector = 128 << sectorSizeShift
	uint8_t fillByte; // if sector has nothing but this byte repeated
	uint8_t stuff[3];
	uint32_t ofs;
};
#pragma pack(pop)

struct FDDSectorList
{
	uint32_t id, ofs;
};

bool RipFDD(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (FDDHeader*)filebuffy;
	if (memcmp(header->signature, "VFD1.", 5) != 0)
	{
		Verbose("Error: Missing VFD sig");
		return false;
	}

	header->comment[127] = 0; // null-terminate just in case
	Verbose("Valid VFD header found, comment: \"" + ShiftJisToUTF8(header->comment) + "\"");

	// Sectors in the file aren't necessarily sequential, so build a list from the following index.
	// The number of present sectors may vary and isn't explicitly given, and number of surfaces could be 1 or 2.
	// Therefore convert the index to a sorted list with the sort key being cylinder.head.sector.sectorsize,
	// which always places things in the right order regardless of number of heads or sectors given.
	auto listp = filebuffy + sizeof(FDDHeader);
	int numsectors = 4160;
	FDDSectorList sectorlist[numsectors];
	auto readp = (FDDSectorMeta*)listp;
	int foundsectors = 0;
	size_t tempbuffysize = 0;

	for (int i = 0; i < numsectors; i++)
	{
		if (readp->cylinder != 0xFF && readp->sector != 0)
		{
			if (readp->sectorSizeShift > 4)
			{
				Error("Unexpected sector size: 128 << " + to_string(readp->sectorSizeShift));
				return true;
			}
			uint32_t sectorsize = (128 << readp->sectorSizeShift);
			uint32_t newid = (readp->cylinder << 24) + (readp->head << 16) + (readp->sector << 8) + readp->sectorSizeShift;
			// Immediate insert using linear search.
			int j = foundsectors;
			while (j != 0 && newid < sectorlist[j - 1].id)
			{
				sectorlist[j] = sectorlist[j - 1];
				j--;
			}
			sectorlist[j].id = newid;
			if (readp->ofs == 0xFFFFFFFF)
			{
				sectorlist[j].ofs = 0xFFFFFF00 + readp->fillByte;
			}
			else if (readp->ofs + sectorsize > filesize)
			{
				Error("Sector ofs out of bounds");
				return true;
			}
			else sectorlist[j].ofs = readp->ofs;

			tempbuffysize += sectorsize;
			foundsectors++;
		}
		readp++;
	}

	uint8_t tempbuffy[tempbuffysize];
	uint32_t writeofs = 0;
	for (int i = 0; i < foundsectors; i++)
	{
		//cout << i << ": " << (sectorlist[i].id >> 8) << " ofs " << sectorlist[i].ofs << " size " << (sectorlist[i].id & 0xFF) << "\n";
		auto sectorsize = 128 << (sectorlist[i].id & 0xFF);
		if ((sectorlist[i].ofs & 0xFF000000) == 0xFF000000)
			memset(tempbuffy + writeofs, sectorlist[i].ofs & 0xFF, sectorsize);
		else
			memcpy(tempbuffy + writeofs, filebuffy + sectorlist[i].ofs, sectorsize);
		writeofs += sectorsize;
	}

	DispatchFAT(tempbuffy, tempbuffysize, true);
	return true;

	size_t headersize = 0xC3FC;
	DispatchFAT(filebuffy + headersize, filesize - headersize, true);
	return true;
}
