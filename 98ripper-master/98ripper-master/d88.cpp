#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
// D88 format notes:
//   https://gist.github.com/barbeque/33ee77a440fb9796d309bdc980bb067a
//   https://www.pc98.org/project/doc/d88.html
struct D88Header
{
	char name[17];
	uint8_t reserved[9];
	uint8_t writeprotection;
	uint8_t mediatype;
	uint32_t filesize; // including this header
};

struct D88SectorHeader
{
	uint8_t cylinder, head, sector, sectorSizeShift;
	uint16_t sectorsPerTrack;
	uint8_t density, deletedData, floppyCtrlStatus;
	uint8_t reserved[5];
	uint16_t dataBytes; // maybe incorrect, assume whole sector always present
};
#pragma pack(pop)

struct D88SectorList
{
	uint32_t id;
	uint8_t* srcp;
};

bool RipD88(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (D88Header*)filebuffy;
	if ((header->mediatype & 0xF) != 0 || header->mediatype > 0x40)
	{
		Verbose("Error: Unexpected media type " + to_string(header->mediatype));
		return false;
	}
	if (header->filesize != filesize)
	{
		Verbose("Error: Wrong filesize in header: " + to_string(header->filesize));
		return false;
	}

	// D88 images are arranged in tracks, which each contain multiple sectors. There's a list of
	// absolute track start offsets right after the header. The tracks aren't guaranteed to be in
	// sequential order; neither the sectors in the tracks. The full disk image needs to be pieced
	// together sector by sector.
	auto tracklistptr = (uint32_t*)(filebuffy + 0x20);
	if (*tracklistptr >= filesize)
	{
		Verbose("Error: First track ptr out of bounds");
		return false;
	}

	header->reserved[0] = 0; // ensure name is null-terminated
	Verbose("Valid D88 header found: \"" + ShiftJisToUTF8(header->name) + "\"");

	// The track list should be 164 dwords long, but may be only 160. The lowest-numbered offset
	// encountered should point to right past the end of the track list. This is normally the
	// first offset in the list.
	auto firstsectorptr = filebuffy + filesize - 0x20;

	while ((void*)tracklistptr < firstsectorptr)
	{
		if (*tracklistptr >= filesize)
		{
			Error("Sector offset out of bounds: " + to_string(*tracklistptr));
			break;
		}
		if (*tracklistptr != 0)
		{
			auto sectorheader = (D88SectorHeader*)(filebuffy + *tracklistptr);
			if ((uint8_t*)sectorheader < firstsectorptr)
				firstsectorptr = (uint8_t*)sectorheader;
		}
		tracklistptr++;
	}

	// Assume sectors are saved in random order but contiguously. Build a list of all sectors in
	// the file, nothing where and how much data each contains. Sort the list, using
	// cylinder.head.sector.sectorsize as the sort key, which always places things in the right
	// order regardless of number of heads or sectors given.
	D88SectorList sectorlist[164 * 26];
	int foundsectors = 0;

	auto readptr = firstsectorptr;
	while (readptr < filebuffy + filesize)
	{
		auto sectorheader = (D88SectorHeader*)readptr;

		if (sectorheader->sectorSizeShift > 4)
		{
			Error("Unexpected sector size: 128 << " + to_string(sectorheader->sectorSizeShift) + " @ " + to_string(readptr - filebuffy));
			break;
		}
		readptr += sizeof(D88SectorHeader);

		uint32_t newid = (sectorheader->cylinder << 24) + (sectorheader->head << 16)
			+ (sectorheader->sector << 8) + sectorheader->sectorSizeShift;
		// Immediate insert using linear search.
		int j = foundsectors;
		while (j != 0 && newid < sectorlist[j - 1].id)
		{
			sectorlist[j] = sectorlist[j - 1];
			j--;
		}
		sectorlist[j].id = newid;
		sectorlist[j].srcp = readptr;
		foundsectors++;
		readptr += 128 << sectorheader->sectorSizeShift;
	}

	uint8_t tempbuffy[filesize];
	size_t writeofs = 0;
	for (int i = 0; i < foundsectors; i++)
	{
		auto sectorsize = 128 << (sectorlist[i].id & 0xFF);
		if (sectorlist[i].srcp + sectorsize > filebuffy + filesize)
		{
			Error("Sector out of bounds @ " + to_string(sectorlist[i].srcp - filebuffy));
			break;
		}
		memcpy(tempbuffy + writeofs, sectorlist[i].srcp, sectorsize);
		writeofs += sectorsize;
	}

	DispatchFAT(tempbuffy, writeofs, true);
	return true;
}
