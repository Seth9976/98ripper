#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct NHDHeader
{
	char signature[16];
	char comment[256];
	uint32_t headerSize;
	uint32_t cylinders;
	uint16_t heads; // disk surfaces
	uint16_t sectors;
	uint16_t sectorSize;
};
#pragma pack(pop)

bool RipNHD(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (NHDHeader*)filebuffy;
	if (memcmp(header->signature, "T98HDDIMAGE", 11) != 0)
	{
		Verbose("Error: Missing T98 sig");
		return false;
	}

	header->comment[255] = 0; // null-terminate just in case
	Verbose("Valid T98 sig found, comment \"" + ShiftJisToUTF8(header->comment) + "\"");

	RipIPL(filebuffy + header->headerSize, filesize - header->headerSize,
		header->sectorSize, header->heads, header->sectors);
	return true;
}
