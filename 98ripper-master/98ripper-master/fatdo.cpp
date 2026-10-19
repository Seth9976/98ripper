#include <filesystem> // requires C++17, use -std=c++17
#include "98ripper.h"

#pragma pack(push, 1) // tell compiler not to add alignment padding here
struct DODirEntry
{
	char fileName[8];
	char suffix[3];
	uint16_t cluster;
	uint8_t reserved[3];
};

const uint8_t DOFATbytes[14] = { 0x44,0x4F,0x2E, 0x03,0x40,0, 0x05,0x60,0, 0x07,0x80,0, 0x09,0xA0 };

#pragma pack(pop)

uint8_t* fbuf;
uint8_t* DOactiveTable;
uint8_t* DOdataSectionPtr;
uint8_t* DOdataSectionEnd;
uint32_t DOclusterCount, DOclusterSize;

bool NextDOCluster(uint32_t* cluster, const filesystem::path& relativepath)
// Files are stored as a chain of clusters, treated as a FAT linked list. Call this to get the
// current cluster's "next" link. Returns true if the new cluster is valid.
{
	if (*cluster >= DOclusterCount)
	{
		Error("Previous cluster out of bounds for " + relativepath.u8string());
		return false;
	}

	uint32_t maxresult;

	maxresult = 0xFFF;
	// 12-bit result value requires some shifting to get the right nibbles.
	uint8_t* readptr = DOactiveTable + *cluster * 3 / 2;
	if ((*cluster & 1) == 0)
		*cluster = (*readptr) | ((*(readptr + 1) & 0xF) << 8);
	else
		*cluster = ((*readptr) >> 4) | ((*(readptr + 1)) << 4);

	if (*cluster < maxresult - 0xF && *cluster >= DOclusterCount)
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

uint8_t* GetDOClusterPtr(uint32_t cluster)
// Returns a raw pointer to the start of the given cluster. Returns NULL if there's an error.
{
	if (cluster < 2 || cluster >= DOclusterCount)
		return NULL;
	auto result = DOdataSectionPtr + (cluster - 2) * DOclusterSize;
	if (result >= DOdataSectionEnd)
		return NULL;
	return result;
}

bool CheckDOFileMetadata(DODirEntry* item)
// Returns true if the FAT directory entry looks legit.
{
	return (item->cluster < DOclusterCount
		&& item->fileName[0] != 0);
}

void DumpDOFile(DODirEntry* dirptr, const filesystem::path& relativepath, uint32_t cluster, uint8_t* clusterPtr, bool linearfile)
{
	auto dumpfilepath = options.outputPath / imagename / relativepath;
	dumpfilepath = CheckAlreadyDumped(dumpfilepath);
	auto dodump = !(options.listOnly | options.idOnly);
	ofstream* dumpfile = NULL;
	if (dodump)
		dumpfile = new ofstream(dumpfilepath, ios::trunc | ios::binary);
	size_t filesize = 0;

	while (true)
	{
		if (clusterPtr == NULL)
		{
			Verbose("Invalid cluster " + to_string(cluster) + " in " + relativepath.u8string());
			break;
		}

		// For normal files, keep seeking to the next chained cluster until the whole file has
		// been dumped. For deleted files, the cluster chain isn't available, so assume the file
		// was probably in a contiguous set of clusters and just dump the whole thing linearry.
		if (linearfile & dodump)
		{
			dumpfile->write((char*)clusterPtr, DOclusterSize); // or just dump 1 cluster, whatever
			break;
		}

		if (dodump)
			dumpfile->write((char*)clusterPtr, DOclusterSize);
		filesize += DOclusterSize;

		if (!NextDOCluster(&cluster, relativepath) || cluster >= 0xC0)
		{
			if (options.verbose || options.listOnly)
				cout << logprefix << relativepath.u8string() << "\t" << to_string(filesize) << " bytes\n";
			break;
		}

		clusterPtr = GetDOClusterPtr(cluster);
	}

	if (dodump)
	{
		dumpfile->close();
		if (!dumpfile)
			Error("Failed to write " + dumpfilepath.u8string());
	}
}

void ScanDODir(DODirEntry* dirptr, const filesystem::path& relativepath, uint32_t cluster)
{
	uint8_t* endptr = DOdataSectionPtr;

	if (!options.listOnly && !options.idOnly)
	{
		auto dumpdir = options.outputPath / imagename / relativepath;
		if (!CreateOutputDirectory(dumpdir))
			return;
	}

	int fileindex = -1;
	while ((uint8_t*)dirptr < endptr)
	{
		fileindex++;

		// End of list. Unless there's a single null entry and normal files resume afterward,
		// a presumable copy protection attempt.
		if (dirptr->fileName[0] == 0)
		{
			dirptr++;
			if ((uint8_t*)dirptr < endptr && CheckDOFileMetadata(dirptr))
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
			// Shift-JIS in filenames is mostly single-byte katakana, A1..DF range, but also can
			// have double-byte characters.
			if ((unsigned char)newfilename[i] >= 0x80)
			{
				hasShiftJis = true;
				// Replace invalid char with underscore, if encountered. (Obfuscated filename?)
				if ((unsigned char)newfilename[i] == 0x80
					|| (unsigned char)newfilename[i] == 0xA0
					|| (unsigned char)newfilename[i] >= 0xF0)
					newfilename[i] = '_';
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
			newdirptr = GetDOClusterPtr(dirptr->cluster);
			if (newdirptr == NULL)
			{
				Error("Invalid cluster " + to_string(dirptr->cluster) + " in " + newpath.u8string());
				dirptr++;
				continue;
			}
		}

		if (options.filter.empty() || fnmatch(options.filter, newpath.u8string()))
			DumpDOFile(dirptr, newpath.u8string(), dirptr->cluster, newdirptr, deleted);

		dirptr++;
	}
}

bool RipFATDO(uint8_t* filebuffy, size_t buffysize)
{
	fbuf = filebuffy;
	if (options.dumpOnly)
		return false;

	uint32_t bytesPerSector = 0x400;
	uint32_t maxRootDirEntries = 0xC0;

	DOclusterSize = bytesPerSector;
	uint32_t bytesPerTable = 2 * bytesPerSector;
	DOclusterCount = bytesPerTable * 2 / 3; // FAT-DO has 12 bits per cluster

	// The first FAT is wiped out, have to use the 2nd one at 0xC00.
	DOactiveTable = filebuffy + bytesPerSector * 3;

	// Check if FAT table looks valid.
	if (memcmp(DOactiveTable, DOFATbytes, 14) != 0)
	{
		Verbose("Error: Invalid FAT table for FAT-DO");
		return false;
	}
	// Override FAT table start with more valid values.
	/*DOactiveTable[0] = 0xF0;
	DOactiveTable[1] = 0xFF;
	DOactiveTable[2] = 0xFF;*/

	uint8_t* rootDirPtr = DOactiveTable + bytesPerTable;
	DOdataSectionPtr = rootDirPtr + maxRootDirEntries * 32; // should use x16 really? but works as is
	DOdataSectionEnd = filebuffy + buffysize;

	// Check if there's a valid file system item.
	if (!CheckDOFileMetadata((DODirEntry*)rootDirPtr))
	{
		Verbose("First file entry is bad");
		rootDirPtr += sizeof(DODirEntry);
		if (!CheckDOFileMetadata((DODirEntry*)rootDirPtr))
		{
			Verbose("Error: Second file entry is bad");
			return false;
		}
	}

	// Recursively dump everything in the image.
	ScanDODir((DODirEntry*)rootDirPtr, "", 0);

	if (options.idOnly)
		cout << logprefix << "FAT-DO\n";

	return true;
}
