#include <filesystem> // requires C++17, use -std=c++17
#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct FATBootSector
{
	uint8_t jumps[3];
	char OEMName[8];
	uint16_t bytesPerSector;
	uint8_t sectorsPerCluster;
	uint16_t reservedSectors;
	uint8_t allocationTables;
	uint16_t maxRootDirEntries;
	uint16_t totalSectors16;
	uint8_t mediaDescriptor;
	uint16_t sectorsPerTable;
	uint16_t sectorsPerTrack;
	uint16_t headCount;
	uint32_t hiddenSectors;
	uint32_t totalSectors32; // only valid if totalSectors16 == 0, else this is garbage
};

struct FATDirectoryEntry
{
	char fileName[8];
	char suffix[3];
	uint8_t attributes;
	uint8_t reserved;
	uint8_t createdAtMsecs;
	uint16_t createdAtTime;
	uint16_t createdAtDate;
	uint16_t lastAccessDate;
	uint16_t clusterHighWord; // only in fat32, otherwise it's something else
	uint16_t lastModifyDate;
	uint16_t lastModifyTime;
	uint16_t cluster;
	uint32_t fileSize;
};

const uint8_t normalFATbytes[14] = { 0xF0,0xFF,0xFF, 0x03,0x40,0, 0x05,0x60,0, 0x07,0x80,0, 0x09,0xA0 };

#pragma pack(pop)

uint8_t* activeAllocationTable;
uint8_t* dataSectionPtr;
uint8_t* dataSectionEnd;
uint32_t fatClusterCount, bytesPerCluster;
enum FATType { FAT12, FAT16 };
FATType fatType;

bool GetNextCluster(uint32_t* cluster, const filesystem::path& relativepath)
// Files are stored as a chain of clusters, treated as a FAT linked list. Call this to get the
// current cluster's "next" link. Returns true if the new cluster is valid.
{
	if (*cluster >= fatClusterCount)
	{
		Error("Previous cluster out of bounds for " + relativepath.u8string());
		return false;
	}

	uint32_t maxresult;

	switch (fatType)
	{
		case FAT12:
		{
			maxresult = 0xFFF;
			// 12-bit result value requires some shifting to get the right nibbles.
			uint8_t* readptr = activeAllocationTable + *cluster * 3 / 2;
			if ((*cluster & 1) == 0)
				*cluster = (*readptr) | ((*(readptr + 1) & 0xF) << 8);
			else
				*cluster = ((*readptr) >> 4) | ((*(readptr + 1)) << 4);
			break;
		}

		case FAT16:
		{
			maxresult = 0xFFFF;
			*cluster = *((uint16_t*)(activeAllocationTable + *cluster * 2));
			break;
		}

		default:
			//maxresult = 0x0FFFFFFF; // for FAT32
			Error("Bad FAT type");
			return false;
	}

	if (*cluster < maxresult - 0xF && *cluster >= fatClusterCount)
	{
		Error("Next cluster out of bounds for " + relativepath.u8string());
		return false;
	}

	if (*cluster == 1 || *cluster == maxresult - 9) // FF6
		Error("Invalid FAT link " + to_string(*cluster) + " in " + relativepath.u8string());
	if (*cluster == maxresult - 8) // FF7
		Verbose("Warning: Bad sector in " + relativepath.u8string());

	return (*cluster >= 2 && *cluster < maxresult - 0xF);
}

uint8_t* GetClusterPtr(uint32_t cluster)
// Returns a raw pointer to the start of the given cluster. Returns NULL if there's an error.
{
	if (cluster < 2 || cluster >= fatClusterCount)
		return NULL;
	auto result = dataSectionPtr + (cluster - 2) * bytesPerCluster;
	if (result >= dataSectionEnd)
		return NULL;
	return result;
}

bool ValidateFileMetadata(FATDirectoryEntry* item)
// Returns true if the FAT directory entry looks legit.
{
	return (item->cluster < fatClusterCount
		&& item->fileSize <= (uint32_t)(dataSectionEnd - dataSectionPtr)
		&& (item->attributes & 0xC0) == 0
		&& item->fileName[0] != 0);
}

void DumpFATFile(FATDirectoryEntry* dirptr, const filesystem::path& relativepath, uint32_t cluster, uint8_t* clusterPtr, bool linearfile)
{
	size_t remainingsize = dirptr->fileSize;

	if (options.verbose || options.listOnly)
	{
		cout << logprefix << relativepath.u8string() << "\t" << to_string(remainingsize) << " bytes";

		if ((dirptr->attributes & 1) != 0)
			cout << "\treadonly";
		if ((dirptr->attributes & 2) != 0)
			cout << "\thidden";
		cout << "\n";
	}
	if (options.listOnly || options.idOnly)
		return;

	auto dumpfilepath = options.outputPath / imagename / relativepath;
	dumpfilepath = CheckAlreadyDumped(dumpfilepath);
	ofstream dumpfile(dumpfilepath, ios::trunc | ios::binary);

	while (remainingsize != 0)
	{
		if (clusterPtr == NULL)
		{
			Error("Invalid cluster " + to_string(cluster) + " in " + relativepath.u8string());
			break;
		}
		// For normal files, keep seeking to the next chained cluster until the whole file has
		// been dumped. For deleted files, the cluster chain isn't available, so assume the file
		// was probably in a contiguous set of clusters and just dump the whole thing linearry.
		if (remainingsize <= bytesPerCluster || linearfile)
		{
			dumpfile.write((char*)clusterPtr, remainingsize);
			break;
		}

		dumpfile.write((char*)clusterPtr, bytesPerCluster);
		remainingsize -= bytesPerCluster;

		if (!GetNextCluster(&cluster, relativepath))
			break;

		clusterPtr = GetClusterPtr(cluster);
	}

	dumpfile.close();
	if (!dumpfile)
		Error("Failed to write " + dumpfilepath.u8string());
}

void ScanFATDirectory(FATDirectoryEntry* dirptr, const filesystem::path& relativepath, uint32_t cluster)
{
	bool isroot = (cluster == 0);
	// The FAT12/16 root directory may only run up to the data section start.
	// Normal directories run until end of cluster, and may link to a further cluster.
	uint8_t* endptr = isroot ? dataSectionPtr : (uint8_t*)dirptr + bytesPerCluster;

	if (!options.listOnly && !options.idOnly)
	{
		auto dumpdir = options.outputPath / imagename / relativepath;
		if (!CreateOutputDirectory(dumpdir))
			return;
	}

	int fileindex = -1;
	while (true)
	{
		fileindex++;
		if ((uint8_t*)dirptr >= endptr)
		{
			if (isroot)
				return;
			if (!GetNextCluster(&cluster, relativepath.u8string()))
				return;

			dirptr = (FATDirectoryEntry*)GetClusterPtr(cluster);
			if (dirptr == NULL)
			{
				Error("Invalid cluster " + to_string(cluster) + " in " + relativepath.u8string());
				return;
			}
			endptr = (uint8_t*)dirptr + bytesPerCluster;
		}

		// End of list. Unless there's a single null entry and normal files resume afterward,
		// a presumable copy protection attempt.
		if (dirptr->fileName[0] == 0)
		{
			dirptr++;
			if ((uint8_t*)dirptr < endptr && ValidateFileMetadata(dirptr))
				continue;
			// Next entry's not valid either, give up.
			return;
		}

		// Deleted file.
		bool deleted = ((unsigned char)(dirptr->fileName[0]) == 0xE5);
		if (deleted && !options.undelete)
		{
			Verbose("Ignoring deleted file: ?" + string(dirptr->fileName + 1));
			dirptr++;
			continue;
		}

		// Replacement E5 character.
		if (dirptr->fileName[0] == 5)
			dirptr->fileName[0] = 0xE5;

		// Skip . and .. directories.
		if (dirptr->fileName[0] == '.')
		{
			if (dirptr->fileName[1] == ' ' ||
				(dirptr->fileName[1] == '.' && dirptr->fileName[2] == ' '))
			{
				dirptr++;
				continue;
			}
		}

		// Build a pretty name for this file.
		char newfilename[32];
		int i;
		int j = 0;
		if (deleted)
			j++;
		for (i = 0; i < 8; i++)
		{
			if ((unsigned char)(dirptr->fileName[j]) < 0x20)
				Error("Invalid char #" + to_string(dirptr->fileName[j]) + " in filename");
			if ((unsigned char)(dirptr->fileName[j]) <= 0x20)
				break;
			newfilename[i] = dirptr->fileName[j++];
		}
		newfilename[i++] = '.';
		for (j = 0; j < 3; j++)
		{
			if ((unsigned char)(dirptr->suffix[j]) < 0x20)
				Error("Invalid char #" + to_string(dirptr->suffix[j]) + " in suffix");
			if ((unsigned char)(dirptr->suffix[j]) <= 0x20)
				break;
			newfilename[i++] = dirptr->suffix[j];
		}

		if (newfilename[i - 1] == '.')
			i--;
		newfilename[i] = 0;

		bool hasShiftJis = false;
		while (--i >= 0)
		{
			// Shift-JIS in filenames should be limited to single-byte katakana, A1..DF range.
			if ((unsigned char)newfilename[i] >= 0x80)
			{
				hasShiftJis = true;
				// Replace invalid char with underscore, if encountered. (Obfuscated filename?)
				if ((unsigned char)newfilename[i] == 0x80
					|| (unsigned char)newfilename[i] == 0xA0
					|| (unsigned char)newfilename[i] >= 0xF0)
					newfilename[i] = '_';
				// Warn about double-byte, if encountered.
				else if ((unsigned char)newfilename[i] < 0xA1 || (unsigned char)newfilename[i] > 0xDF)
					Error("Double-byte in filename");
			}
			else
				newfilename[i] = tolower(newfilename[i]);
		}

		auto finalname = (hasShiftJis ? ShiftJisToUTF8(newfilename) : string(newfilename));
		if (deleted)
			finalname = to_string(fileindex) + '.' + finalname;

		auto newpath = relativepath / finalname;

		// Figure out where the file data lies, if valid, and dump it.
		uint8_t* newdirptr = NULL;
		if (dirptr->cluster != 0)
		{
			newdirptr = GetClusterPtr(dirptr->cluster);
			if (newdirptr == NULL)
			{
				Error("Invalid cluster " + to_string(dirptr->cluster) + " in " + newpath.u8string());
				dirptr++;
				continue;
			}
		}
		if (dirptr->fileSize > 0x80000000)
		{
			Error("Invalid filesize for " + newpath.u8string());
			break;
		}
		if ((dirptr->attributes & 0x10) == 0)
		{
			if (options.filter.empty() || fnmatch(options.filter, newpath.u8string()))
				DumpFATFile(dirptr, newpath.u8string(), dirptr->cluster, newdirptr, deleted);
		}
		else
		{
			Verbose(newpath.u8string() + "\t[DIR]");
			if (newdirptr != NULL)
				ScanFATDirectory((FATDirectoryEntry*)newdirptr, newpath, dirptr->cluster);
		}

		dirptr++;
	}
}

bool EvaluateFATBootSector(uint8_t* filebuffy, size_t buffysize)
{
	auto header = (FATBootSector*)filebuffy;
	bool isfloppy = false;

	Verbose("FAT OEM name: \"" + ShiftJisToUTF8(string(header->OEMName, 8).data()) + "\"");
	string tmpstr = "FAT media descriptor: \"" + to_string(header->mediaDescriptor) + "\" ";
	if (header->mediaDescriptor == 0xF0 || header->mediaDescriptor >= 0xF9)
	{
		Verbose(tmpstr + "Floppy");
		isfloppy = true;
	}
	else if (header->mediaDescriptor == 0xF8)
	{
		Verbose(tmpstr + "Hard drive");
	}
	else
	{
		Verbose("Error: Unknown " + tmpstr);
		return false;
	}

	if (header->bytesPerSector != 256 && header->bytesPerSector != 512 && header->bytesPerSector != 1024 && header->bytesPerSector != 2048)
	{
		Verbose("Error: Unexpected FAT bytesPerSector " + to_string(header->bytesPerSector));
		return false;
	}
	if (header->sectorsPerCluster != 1 && header->sectorsPerCluster != 2 && header->sectorsPerCluster != 4
		&& header->sectorsPerCluster != 8 && header->sectorsPerCluster != 16 && header->sectorsPerCluster != 128)
	{
		Verbose("Error: Unexpected FAT sectorsPerCluster " + to_string(header->sectorsPerCluster));
		return false;
	}
	if (header->reservedSectors != 1 && header->reservedSectors != 2 && header->reservedSectors != 32)
	{
		Verbose("Error: Unexpected FAT reservedSectors " + to_string(header->reservedSectors));
		return false;
	}
	if (header->allocationTables < 1 || header->allocationTables > 2)
	{
		Verbose("Error: Unexpected FAT allocationTables " + to_string(header->allocationTables));
		return false;
	}
	if ((header->maxRootDirEntries > 255 && isfloppy) || header->maxRootDirEntries > 4000)
	{
		Verbose("Error: Unexpected FAT maxRootDirEntries " + to_string(header->maxRootDirEntries));
		return false;
	}

	if (header->totalSectors16 != 0)
		header->totalSectors32 = header->totalSectors16;
	uint32_t totalclusters = header->totalSectors32 / header->sectorsPerCluster;
	bytesPerCluster = header->sectorsPerCluster * header->bytesPerSector;
	uint32_t bytesPerTable = header->sectorsPerTable * header->bytesPerSector;

	Verbose("FAT filesystem has " + to_string(header->totalSectors32)
		+ " sectors, " + to_string(totalclusters)
		+ " clusters, " + to_string(header->totalSectors32 * header->bytesPerSector) + " bytes");
	if (totalclusters < 4085)
	{
		fatType = FAT12;
		fatClusterCount = bytesPerTable * 2 / 3; // FAT12 has 12 bits per cluster
	}
	else if (totalclusters < 65525)
	{
		fatType = FAT16;
		fatClusterCount = bytesPerTable / 2;
	}
	else
	{
		Error("Too many clusters, probably FAT32, not supported");
		return false;
	}

	Verbose("FAT bytes/sector=" + to_string(header->bytesPerSector)
		+ ", sectors/cluster=" + to_string(header->sectorsPerCluster)
		+ ", sectors/table=" + to_string(header->sectorsPerTable)
		+ ", reservedSectors=" + to_string(header->reservedSectors)
		+ ", clusters=" + to_string(fatClusterCount));

	activeAllocationTable = filebuffy + header->reservedSectors * header->bytesPerSector;

	// If a header claims there's only one allocation table, it may be mistaken, check it.
	if (header->allocationTables == 1)
	{
		if (memcmp(activeAllocationTable, activeAllocationTable + bytesPerTable, 120) == 0)
		{
			Verbose("Warning: Header claims 1 table but there are 2");
			header->allocationTables = 2;
		}
	}

	uint8_t* rootDirPtr = activeAllocationTable + header->allocationTables * bytesPerTable;
	dataSectionPtr = rootDirPtr + header->maxRootDirEntries * 32;
	dataSectionEnd = filebuffy + buffysize;

	if (options.selectFAT >= header->allocationTables)
	{
		Verbose("Error: Can't select FAT " + to_string(options.selectFAT) + ", image has [0.."
			+ to_string(header->allocationTables - 1) + "]");
		return true;
	}
	activeAllocationTable += options.selectFAT * bytesPerTable;
	Verbose("FAT has tables [0.." + to_string(header->allocationTables - 1)
		+ "], using " + to_string(options.selectFAT));
	//for (int i = 0; i < fatClusterCount; i++) Verbose(to_string(i)+":"+to_string(GetFATLink(i)));

	// Recursively dump everything in the image.
	ScanFATDirectory((FATDirectoryEntry*)rootDirPtr, "", 0);
	return true;
}

bool AutoDetectFATBootSector(uint8_t* filebuffy, size_t buffysize)
{
	uint32_t bytesPerSector = 0x400;
	uint32_t maxRootDirEntries = 0xC0;

	bytesPerCluster = bytesPerSector;
	uint32_t bytesPerTable = 2 * bytesPerSector;
	fatClusterCount = bytesPerTable * 2 / 3; // FAT12 has 12 bits per cluster

	// Try the first 4 sectors for FAT table and file list.
	for (int i = 1; i < 4; i++)
	{
		activeAllocationTable = filebuffy + i * bytesPerSector;
		Verbose("Check for FAT table at " + to_string(activeAllocationTable - filebuffy));
		uint8_t* rootDirPtr = activeAllocationTable + 2 * bytesPerTable;

		// Check if 1st FAT table would be valid.
		if (activeAllocationTable[0] < 0xF0
			|| activeAllocationTable[1] != 0xFF || activeAllocationTable[2] != 0xFF)
		{
			Verbose("Error: FAT table 0 has unexpected first 3 bytes.");
			return false;
		}

		if (memcmp(activeAllocationTable, activeAllocationTable + bytesPerTable, bytesPerTable >> 1) != 0)
		{
			Verbose("Error: FAT table autodetect didn't find matching tables.");
			// The most common FAT start clusters are 3, 4, 5... see anything close to that in the first table?
			uint8_t matches = 0;
			for (int j = 3; j < 14; j++)
			{
				if ((uint8_t)(*(activeAllocationTable + j)) == normalFATbytes[j])
					matches++;
			}
			if (matches > 5)
				Verbose("Try to use FAT table 0 anyway, looks possibly legit");
			else
			{
				// Check if 2nd FAT table might be valid independently.
				activeAllocationTable += bytesPerSector;
				matches = 0;
				for (int j = 3; j < 14; j++)
				{
					if ((uint8_t)(*(activeAllocationTable + j)) == normalFATbytes[j])
						matches++;
				}
				if (matches > 5)
					Verbose("Try to use FAT table 1 anyway, looks possibly legit");
				else
					continue;
			}
		}

		dataSectionPtr = rootDirPtr + maxRootDirEntries * 32;
		dataSectionEnd = filebuffy + buffysize;

		// Validate first file system item.
		if (!ValidateFileMetadata((FATDirectoryEntry*)rootDirPtr))
		{
			Verbose("Error: Autodetected FAT tables, but first file entry is bad");
			rootDirPtr += sizeof(FATDirectoryEntry);
			if (!ValidateFileMetadata((FATDirectoryEntry*)rootDirPtr))
			{
				Verbose("Error: Second entry also bad");
				continue;
			}
		}

		// Recursively dump everything in the image.
		activeAllocationTable += options.selectFAT * bytesPerTable;
		Verbose("Warning: Autodetected FAT table, may not be correct.");
		ScanFATDirectory((FATDirectoryEntry*)rootDirPtr, "", 0);
		return true;
	}

	Verbose("Error: No FAT table could be found");
	return false;
}

bool RipFAT(uint8_t* filebuffy, size_t buffysize)
{
	if (options.dumpOnly)
		return false;
	// FAT partitions should start with a standard boot sector. In some cases, the boot sector has
	// weird code and text; or a custom TDOS header etc. But the FAT tables and file data may be
	// right where you'd expect if it had a standard header. So if the boot sector looks weird,
	// try to autodetect the FAT tables anyway.
	if (!EvaluateFATBootSector(filebuffy, buffysize))
		if (!AutoDetectFATBootSector(filebuffy, buffysize))
			return false;

	if (options.idOnly)
	{
		switch (fatType)
		{
			case FAT12:
				cout << logprefix << "FAT12\n";
				break;
			case FAT16:
				cout << logprefix << "FAT16\n";
				break;
		}
	}
	return true;
}
