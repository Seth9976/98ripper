#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct HDIHeader
{
	uint32_t dummy, hddType, headerSize, hddSize, sectorSize, sectors, heads, cylinders;
};

// PC-98 hard drive layout:
// Sector 0: MBR, starts with EB xx xx xx IPL1. Sector must end with magic boot flag bytes 55 AA.
// Sector 1: 16 partition table entries, 32 bytes each.
// Sectors 2-15: Probably more boot code.
// Sectors 16-end: Partition contents.
//
// EB 0A 90 90 means jump execution ahead 0xA bytes, skipping the signature; the 90's are NOPs.

struct IPLPartitionEntry
{
	uint8_t mid; // 0x80 - boot
	uint8_t sid; // 0x80 - active
	uint16_t dummy;
	uint8_t iplSector;
	uint8_t iplHead;
	uint16_t iplCylinder;
	uint8_t startSector;
	uint8_t startHead;
	uint16_t startCylinder;
	uint8_t endSector;
	uint8_t endHead;
	uint16_t endCylinder;
	char name[16];
};
#pragma pack(pop)

struct Partition
{
	string name;
	uint8_t* startPtr;
	size_t sizeBytes;
};

bool RipIPL(uint8_t* filebuffy, size_t buffysize, size_t sectorsize, size_t heads, size_t sectors)
{
	vector<Partition> partitionlist;

	// Collect list of partitions.
	auto partptr = (IPLPartitionEntry*)(filebuffy + sectorsize);
	for (int partindex = 0; partindex < 16; partindex++)
	{
		if ((partptr->startSector | partptr->startHead | partptr->startCylinder) == 0)
			break;
		Partition newpart;
		auto startSectorLinear =
			(partptr->startCylinder * heads + partptr->startHead) * sectors + partptr->startSector;
		auto endSectorLinear =
			((partptr->endCylinder + 1) * heads + partptr->endHead) * sectors + partptr->endSector;

		// For some reason the end cylinder is one short, filesystem only matches up with +1.
		/*Verbose("Part from chs " + to_string(partptr->startCylinder) + "/"
			+ to_string(partptr->startHead) + "/" + to_string(partptr->startSector)
			+ " to end " + to_string(partptr->endCylinder) + "/" + to_string(partptr->endHead)
			+ "/" + to_string(partptr->endSector));*/
		newpart.startPtr = filebuffy + startSectorLinear * sectorsize;
		newpart.sizeBytes = (endSectorLinear - startSectorLinear) * sectorsize;
		newpart.name = string(partptr->name, 16);
		Verbose("Partition \"" + newpart.name + "\" at sectors "
			+ to_string(startSectorLinear) + "-" + to_string(endSectorLinear - 1) + ", "
			+ to_string(newpart.sizeBytes) + " bytes");

		if (newpart.startPtr + newpart.sizeBytes > filebuffy + buffysize)
		{
			Error("Partition out of bounds");
			break;
		}

		partitionlist.push_back(newpart);
		partptr++;
	}

	for (auto part : partitionlist)
	{
		//Dump(imagename + ".bin", (char*)part.startPtr, part.sizeBytes);
		if (!DispatchFAT(part.startPtr, part.sizeBytes, true))
			return false;
	}

	return true;
}

bool RipHDI(uint8_t* filebuffy, size_t filesize)
{
	if (filesize < 2000000)
	{
		Verbose("File size too small");
		return false;
	}

	// Validate the header.
	auto header = (HDIHeader*)filebuffy;
	if (header->dummy != 0)
		Verbose("Warning: First dword " + to_string(header->dummy));
	if (header->hddType > 0x14 && header->hddType != 0x28)
	{
		Verbose("Error: Unknown HDD type " + to_string(header->hddType));
		return false;
	}
	if (header->headerSize < 1024 || header->headerSize > 4096)
	{
		Verbose("Error: Unexpected headerSize " + to_string(header->headerSize));
		return false;
	}
	if (header->hddSize < 2000000 || header->hddSize > 512 * 1024 * 1024 || header->hddSize + header->headerSize > filesize)
	{
		Verbose("Error: Unexpected hddSize " + to_string(header->hddSize));
		return false;
	}
	if (header->sectorSize != 128 && header->sectorSize != 256 && header->sectorSize != 512
		&& header->sectorSize != 1024 && header->sectorSize != 2048)
	{
		Verbose("Error: Unexpected sectorSize " + to_string(header->sectorSize));
		return false;
	}
	if (header->sectors < 5 || header->sectors > 40)
	{
		Verbose("Error: Unexpected sectors " + to_string(header->sectors));
		return false;
	}
	if (header->heads < 4 || header->heads > 16)
	{
		Verbose("Error: Unexpected heads " + to_string(header->heads));
		return false;
	}
	if (header->cylinders < 31 || header->cylinders > 1440)
	{
		Verbose("Error: Unexpected cylinders " + to_string(header->cylinders));
		return false;
	}

	// Validate the boot sector.
	filebuffy += header->headerSize;
	if (memcmp(filebuffy + 4, "IPL1", 4) != 0)
	{
		Verbose("Error: IPL1 boot sector not found");
		return false;
	}
	if (*(filebuffy + header->sectorSize - 2) != 0x55 || *(filebuffy + header->sectorSize - 1) != 0xAA)
	{
		Verbose("Error: IPL1 boot sector missing magic number");
		return false;
	}

	Verbose("Valid header found: " + to_string(header->hddSize) + "-byte image, CHS "
		+ to_string(header->cylinders) + "/"+ to_string(header->heads) + "/"+ to_string(header->sectors));
	RipIPL(filebuffy, header->hddSize, header->sectorSize, header->heads, header->sectors);
	return true;
}
