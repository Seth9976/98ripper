#include "98ripper.h"

bool RipFIM(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto dwords = (uint32_t*)filebuffy;
	if (dwords[0] == 0 || dwords[0] > 2 || (dwords[1] != 0x2000 && dwords[1] != 0x1A00) || dwords[2] != 0x9A)
	{
		Verbose("Error: Missing FIM magic bytes");
		return false;
	}

	Verbose("Valid FIM magic bytes found");

	size_t headersize = 0x100;
	DispatchFAT(filebuffy + headersize, filesize - headersize, true);
	return true;
}
