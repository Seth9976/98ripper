#include "98ripper.h"

bool RipDIP(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	if (*((uint32_t*)filebuffy) != 0x13000801 || *((uint32_t*)filebuffy + 1) != 0x10041)
	{
		Verbose("Error: Missing DIP magic bytes");
		return false;
	}

	Verbose("Valid DIP magic bytes found");
	Verbose("Header remark: " + ShiftJisToUTF8((char*)filebuffy + 0x10));

	size_t headersize = 0x100;
	DispatchFAT(filebuffy + headersize, filesize - headersize, true);
	return true;
}
