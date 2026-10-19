#include "98ripper.h"

bool RipDCP(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 57000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	if (*filebuffy == 0 || *filebuffy > 0x21)
	{
		Verbose("Error: Unexpected first byte: " + to_string(*filebuffy));
	}
	size_t headersize;
	for (headersize = 1; headersize < 162; headersize++)
	{
		if (*(filebuffy + headersize) > 1)
		{
			Verbose("Error: Unexpected byte in track map: " + to_string(*(filebuffy + headersize)));
			return false;
		}
	}

	Verbose("DCP header found");
	DispatchFAT(filebuffy + headersize, filesize - headersize, true);
	return true;
}
