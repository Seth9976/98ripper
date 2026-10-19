#include "98ripper.h"

struct DIMHeader
{
	uint8_t mediaType;
	uint8_t trackMap[160];
	uint8_t reserved[10];
	char signature[13];
	uint8_t reserved2[72];
};

bool RipDIM(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 57000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (DIMHeader*)filebuffy;
	if (header->mediaType > 3)
	{
		Verbose("Error: Unexpected first byte: " + to_string(header->mediaType));
	}
	for (int i = 0; i < 160; i++)
	{
		if (header->trackMap[i] > 1)
		{
			Verbose("Error: Unexpected byte in track map: " + to_string(header->trackMap[i]));
			return false;
		}
	}
	for (int i = 0; i < 10; i ++)
	{
		if (header->reserved[i] != 0)
		{
			Verbose("Error: Unexpected byte in 2nd header: " + to_string(header->reserved[i]));
			return false;
		}
	}
	if (memcmp(header->signature, "DIFC HEADER  ", 13) != 0)
	{
		Verbose("Error: Missing DIFC sig");
		return false;
	}

	Verbose("DIM header found");
	DispatchFAT(filebuffy + 256, filesize - 256, true);
	return true;
}
