#include "98ripper.h"

// Described briefly in Microsoft Basic-80 reference manual, section H.11.1
// More info at http://memo.wnishida.com/?date=20070127
// And http://fullmotionvideo.free.fr/phpBB3/viewtopic.php?f=4&t=1229
// And http://fullmotionvideo.free.fr/phpBB3/viewtopic.php?f=4&t=956
// And Seijuro's Disk Tutorial, which may not readily exist online.

// Some translated relevant text:
/*
DISK BASIC Track Map
The PC-8031-2W is equipped with two double-sided double density mini-floppy disks whose physical format is double-sided,
40 tracks/side, 16 sectors/track, 256 bytes/sector, which means that the total capacity after formatting is 320 KiB
(256x16x40x2=327680).

It should be noted that the 18th track on the back side (Surface 1) is where the directory information and the FAT
(File Allocation Table) information described below are stored.

Surface 0:
Track 0, sector 1: Initial Program Loader
Track 0, sector 2 - Track 2, sector 16: DISK Basic code
Track 3 to 39: file data

Surface 1:
Track 0 to 17: file data
Track 18, sectors 1 to 12: FAT directory entries
Track 18, sector 13: ID
Track 18, sectors 14 to 16: FAT cluster tables, three identical copies to dodge bad sectors
Track 19 to 39: file data

DISK BASIC Directory Entry
Look at the structure of a DISK BASIC directory entry. Each entry consists of only 16 bytes. There are six characters
for the file name, three characters for the file extension, one byte for the file attributes, and one byte for the first
cluster number where the file is stored. There's no timestamp or even file size. Attributes include ASCII format vs
binary format, write-protect flag, and read after write flag. There are no subdirectories - DISK BASIC is a flat file
hierarchy. In the track map, directory data is stored in a total of 12 sectors, so a single diskette holds up to
192 different files (12x256/16).

char filename[6];
char extension[3];
unsigned char attr;
unsigned char cluster;
char reserved[5];

FAT8 on DISK BASIC
The physical format of a floppy disk consists of three major parameters: surface, track, and sector. In order to access
the data on the diskette, we need to specify the head number, track number, and sector number, a complicated task. So we
will start with cylinder 0 and surface 0 which is track 0, and all sectors from 1 onward (only sectors start from 1);
then move to cylinder 0 and surface 1 which is track 1, and sector 1 onward.

However, even on a low capacity disk of 320 KiB, the total number of sectors is 1280 (16x40x2), so if you prepare
a sector map with one byte allocated to each sector, this alone will consume 1280 bytes of RAM, or 5 sectors on a disk.
Therefore, "clusters" were introduced to compress the file management information. The file management unit is not one
sector, but "eight sectors". If we prepare a cluster map where each cluster is managed by one byte, the size is
1280/8 = 160 bytes. If you have a cluster map that manages one cluster with one byte, the size is 1280/8 = 160 bytes,
which is a reasonable size to fit in one sector. Larger disks will have larger clusters.

This 1-byte version of the cluster map is the ancestor of FAT. DISK BASIC managed the file storage information with
a "unidirectional link structure" of cluster numbers, where the first 160 bytes (0x00-0x9F) of the sector storing the
FAT represents the status information of the corresponding 160 clusters. Since the directory entry contains the first
cluster number of a file, we can uniquely obtain this first cluster number given the file name. Next, refer to the
status information of the corresponding cluster in FAT. If this value is in the range of 0xC1 to 0xEF, the file data is
finished in the concerned cluster, and the number of sectors allocated to the actual data is this value minus 0xC0
(if 0xC3, 3 sectors are in use; if 0xD9, 25 sectors in use). Note that the file size on the file system is in sectors
and not in bytes.

If the status information of the corresponding cluster is in the range of 0x00 to 0x9F, it means that the file will
continue to the subsequent clusters. Since it is difficult to understand in writing, let's summarize it in virtual code.
read_file is a function to read all the sectors in the file, following the link structure from the specified first
cluster. read_data is an auxiliary function to read the specified number of sectors from the first sector of the given
cluster number. read_data is an auxiliary function to read the specified number of sectors from the first sector of the
given cluster number.

0xFF means unallocated, free to use. 0xFE means reserved for system.


unsigned char fat[160];

void read_file(int first_cluster)
{
  int cluster = first_cluster, stat, sectors;

  while (true)
  {
    stat = (int) fat[cluster];
    switch (stat)
    {
      case 0xC1..0xDF:
        sectors = (int) (fat[idx] - 0xC0);
        read_data(cluster, sectors);
        return;

      case 0x00..0x9F:
        sectors = 8;
        read_data(cluster, sectors);
        cluster = fat[cluster];
        continue;
    }
  }
}
*/

// The following ought to be reliably constant values:
// Sector size: always 256 bytes
// FAT tables: 3
// Sectors/table: 1
// Bytes/table: 256
// Tracks used for the whole dir/ID/FAT tables section: 1
// Clusters/track: 1 if total image size > 500kb, else 2
// Bytes/cluster: same as track size, if total image size < 500kb then halved
//
// For the most common 1MB disks, the directory table is at 0x70F00. The ID section follows within
// a few 0x100 sectors, then 3 hopefully identical FAT tables. The byte distance from start of the
// directory table to end of FAT tables is this image's track size. You can also calculate the
// cluster size from this, same as track size for 1MB disks.

// Problems:
// - Alice Soft's DPS 1 first disk has non-directory items in the last sector of the directory list.
//   That sector could be valid in other images, so have to try parsing it despite errors here...
// - Paragon Sexa Doll has the directory list at 0x70300, which I'm failing to catch correctly...

struct FAT8DirectoryEntry
{
	char fileName[6]; // first byte 00 = deleted file, FF = not used
	char suffix[3];
	uint8_t attributes;
	uint8_t cluster;
	uint8_t reserved[5];
};

uint8_t* fat8ActiveAllocationTable;
uint32_t fat8SectorSize = 256; // always 256 bytes by spec
uint32_t fat8SectorsPerTrack;
uint32_t fat8ClusterSize; // same as track size if total size >500kb, else half that
int fat8AllocationTables = 3; // always 3 by spec
FAT8DirectoryEntry* fat8DirList;
uint8_t* fat8IdSection;
uint8_t* fat8FileBuffy;
size_t fat8FileSize;

uint8_t* GetFAT8ClusterPtr(uint32_t cluster)
// Returns a raw pointer to the start of the given cluster. Returns NULL if there's an error.
{
	if (cluster == 0)
		return NULL;
	auto result = cluster * fat8ClusterSize;
	// With 1MB disks, the first track always uses half-size sectors for some reason, so all
	// offsets must have half a track deducted.
	if (fat8FileSize > 0xF0000)
		result -= 0xD00;
	if (result + fat8ClusterSize > fat8FileSize)
		return NULL;
	return fat8FileBuffy + result;
}

bool ValidateFAT8FileMetadata(FAT8DirectoryEntry* item)
// Returns true if the FAT8 directory entry looks legit.
// Filename may begin with 00 or FF for unused, but otherwise must be a valid shift-jis 1st byte.
{
	return (
		(item->cluster == 0 || item->cluster == 0xFF || GetFAT8ClusterPtr(item->cluster) != NULL)
		&& (item->attributes & 0x0E) == 0
		&& item->reserved[0] == 0xFF && item->reserved[1] == 0xFF
		&& (item->fileName[0] == 0 || item->fileName[0] == 0xFF || (item->fileName[0] >= 0x20 && item->fileName[0] < 0xF0))
		);
}

void DumpFAT8File(const filesystem::path& dumpdir, const string& filename, uint32_t cluster)
{
	auto dumpfilepath = dumpdir / filename;
	dumpfilepath = CheckAlreadyDumped(dumpfilepath);
	auto dodump = !(options.listOnly | options.idOnly);
	ofstream* dumpfile = NULL;
	if (dodump)
		dumpfile = new ofstream(dumpfilepath, ios::trunc | ios::binary);
	size_t filesize = 0;

	while (true)
	{
		auto dataptr = GetFAT8ClusterPtr(cluster);
		if (dataptr == NULL)
		{
			Error("Invalid cluster " + to_string(cluster) + " in " + filename);
			break;
		}

		auto nextcluster = *(fat8ActiveAllocationTable + cluster);
		if (nextcluster >= 0xF0)
		{
			Error("Invalid cluster table value " + to_string(nextcluster) + " at cluster " + to_string(cluster) + " in " + filename);
			break;
		}

		auto datasize = (nextcluster < 0xC0) ? fat8ClusterSize : (nextcluster - 0xC0) * fat8SectorSize;
		// If this is the last cluster and this is not a binary file, maybe the size should be
		// trimmed from the end, removing null bytes until EOF marker 0x1A is encountered...
		filesize += datasize;

		if (dodump)
			dumpfile->write((char*)dataptr, datasize);

		cluster = nextcluster;
		if (cluster >= 0xC0)
		{
			if (options.verbose || options.listOnly)
				cout << logprefix << filename << "\t" << to_string(filesize) << " bytes\n";
			break;
		}
	}

	if (dodump)
	{
		dumpfile->close();
		if (!dumpfile)
			Error("Failed to write " + dumpfilepath.u8string());
		delete dumpfile;
	}
}

void ScanFAT8Directory()
{
	auto dumpdir = options.outputPath / imagename;
	if (!options.listOnly && !options.idOnly)
	{
		if (!CreateOutputDirectory(dumpdir))
			return;
	}

	while ((uint8_t*)fat8DirList < fat8IdSection)
	{
		if (fat8DirList->fileName[0] == 0x0C && fat8DirList->fileName[1] == 'O')
		{
			Verbose("Ignore Overflow entry");
		}
		else if (fat8DirList->cluster < 0xFE && fat8DirList->cluster == *(fat8ActiveAllocationTable + fat8DirList->cluster))
		{
			Verbose("Ignore infinite loop trap");
		}
		else if (fat8DirList->fileName[0] == 0)
		{
			// Deleted file.
			auto nextcluster = *(fat8ActiveAllocationTable + fat8DirList->cluster);
			if (nextcluster != 0xFF && nextcluster != 0)
				Verbose("Warning: deleted file's cluster " + to_string(fat8DirList->cluster) + " not unused: " + to_string(nextcluster));
		}
		else if ((unsigned char)fat8DirList->fileName[0] != 0xFF)
		{
			// Build a pretty name for this file.
			char filename[12];
			int i;
			for (i = 0; i < 6; i++)
				filename[i] = fat8DirList->fileName[i];
			while (i != 0 && (unsigned char)filename[i - 1] <= 0x20)
				i--;
			filename[i++] = '.';
			for (int j = 0; j < 3; j++)
			{
				if ((unsigned char)fat8DirList->suffix[j] <= 0x20)
					break;
				else
					filename[i++] = fat8DirList->suffix[j];
			}
			if (filename[i - 1] == '.')
				i--;
			filename[i] = 0;
			auto filenamestr = ToLower(ShiftJisToUTF8(filename));
			if (filenamestr.empty())
				filenamestr = "file__" + to_string(fat8DirList->cluster);

			if (options.filter.empty() || fnmatch(options.filter, filenamestr))
				DumpFAT8File(dumpdir, filenamestr, fat8DirList->cluster);
		}
		fat8DirList++;
	}
}

bool RipFAT8(uint8_t* filebuffy, size_t filesize)
{
	if (options.dumpOnly)
		return false;
	if (filesize < 640000)
	{
		Verbose("File too tiny"); // add support if any smaller disks found
		return false;
	}
	fat8FileBuffy = filebuffy;
	fat8FileSize = filesize;
	fat8SectorsPerTrack = (filesize > 720000) ? 26 : 16;
	fat8ClusterSize = fat8SectorSize * fat8SectorsPerTrack;
	// Cluster size is halved if total disk size is < 500kb.
	if (filesize < 0x7A000)
		fat8ClusterSize >>= 1;

	// Detect and validate filesystem components.
	// On 1MB (2HD) disks, cluster 0x46 (70) at 0x70F00 has the directory entry table.
	// On 640kb (2DD) disks - cluster 0x50 (80) at 0x50000
	for (int i = 0x46; i < 0x52; i++)
	{
		fat8DirList = (FAT8DirectoryEntry*)GetFAT8ClusterPtr(i);
		if (
			(((uint32_t*)fat8DirList)[0] == 0x07070707 && ((uint32_t*)fat8DirList)[1] == 0x07070707
			&& fat8DirList->cluster == 0x9F) // trapped first entry used by some Soft Plan titles
			|| ValidateFAT8FileMetadata(fat8DirList))
		{
			Verbose("Maybe valid FAT8 dir entry at cluster " + to_string(i));
			break;
		}
		fat8DirList++;
		if (i == 0x50)
		{
			Verbose("Error: Valid FAT8 first directory item not found");
			return false;
		}
	}

	// The ID section and FAT tables are the last 4 sectors on this same track.
	fat8IdSection = (uint8_t*)fat8DirList + fat8SectorSize * (fat8SectorsPerTrack - 4);
	if ((*fat8IdSection & 0xEF) != 0)
	{
		Verbose("Error: Unexpected first ID byte: " + to_string(*fat8IdSection));
		return false;
	}
	fat8IdSection++;
	if (*fat8IdSection == 0 || (*fat8IdSection > 0xF && *fat8IdSection < 0xFF))
	{
		Verbose("Error: Unexpected second ID byte: " + to_string(*fat8IdSection));
		return false;
	}
	fat8IdSection--;

	fat8ActiveAllocationTable = fat8IdSection + fat8SectorSize;
	if (*fat8ActiveAllocationTable < 0xFE || *(fat8ActiveAllocationTable + 1) < 0xFE)
	{
		Verbose("Error: Unexpected first bytes in allocation table");
		return false;
	}
	if (memcmp(fat8ActiveAllocationTable, fat8ActiveAllocationTable + fat8SectorSize, fat8SectorSize >> 1) != 0)
	{
		Verbose("Error: Mismatching allocation tables 0 vs 1");
		return false;
	}

	if (options.selectFAT >= fat8AllocationTables)
	{
		Verbose("Error: Can't select FAT " + to_string(options.selectFAT) + ", image has [0.."
			+ to_string(fat8AllocationTables - 1) + "]");
		return true;
	}
	fat8ActiveAllocationTable += options.selectFAT * fat8SectorSize;
	Verbose("FAT8 has tables [0.." + to_string(fat8AllocationTables - 1)
		+ "], using " + to_string(options.selectFAT));

	ScanFAT8Directory();

	if (options.idOnly)
		cout << logprefix << "FAT8\n";

	return true;
}
