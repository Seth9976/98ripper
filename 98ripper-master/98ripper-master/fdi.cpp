#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct FDIHeader
{
	uint32_t dummy, fddType, headerSize, fddSize, sectorSize, sectors, surfaces, cylinders;
};
#pragma pack(pop)

bool RipFDI(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 160000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (FDIHeader*)filebuffy;
	if (header->dummy != 0)
		Verbose("Warning: First dword " + to_string(header->dummy));
	if (header->fddType != 0x90 && header->fddType != 0x30 && header->fddType != 0x10)
	{
		Verbose("Error: Unknown FDD type " + to_string(header->fddType));
		return false;
	}
	if (header->headerSize < 1024 || header->headerSize > 4096)
	{
		Verbose("Error: Unexpected headerSize " + to_string(header->headerSize));
		return false;
	}
	if (header->fddSize < 160000 || header->fddSize > 2 * 1024 * 1024 || header->fddSize + header->headerSize > filesize)
	{
		Verbose("Error: Unexpected fddSize " + to_string(header->fddSize));
		return false;
	}
	if (header->sectorSize != 128 && header->sectorSize != 256 && header->sectorSize != 512 && header->sectorSize != 1024)
	{
		Verbose("Error: Unexpected sectorSize " + to_string(header->sectorSize));
		return false;
	}
	if (header->sectors < 8 || header->sectors > 26)
	{
		Verbose("Error: Unexpected sectors " + to_string(header->sectors));
		return false;
	}
	if (header->surfaces < 2 || header->surfaces > 2)
	{
		Verbose("Error: Unexpected surfaces " + to_string(header->surfaces));
		return false;
	}
	if (header->cylinders < 77 || header->cylinders > 80)
	{
		Verbose("Error: Unexpected cylinders " + to_string(header->cylinders));
		return false;
	}

	Verbose("Valid header found: " + to_string(header->fddSize) + "-byte image");
	DispatchFAT(filebuffy + header->headerSize, header->fddSize, true);
	return true;
}
