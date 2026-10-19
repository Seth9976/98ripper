// The Ange games "Kiss" and "Rose" have something called IZBIOS on them.
// Version numbers 1.01 and 1.02 are simultaneously present?? Developed by H.Izumino.
// Also found in Matrix's KO Seiki Beast Sanjuushi - Gaia no Fukkatsu.
//
// Kiss has some text right in the boot sector, from 0x10 to 0xFF. And some more after the entry
// code, from 0x19A to 0x1DA.
//
// At 0x1C00 (Kiss) or 0x3C00 (Rose), there's a disk metadata section:
// 8 chars: disk label
// dword: zeroes
// byte 0x26? or 0x0B?
// byte 0x75 - number of entries in following index list?.. no, only matches on first disk.
// bytes 0xFB 0x18 - perhaps disk size or geometry?
//
// Followed by a file index list:
// uint16_t sector number
// uint16_t file length in sectors
// Ends at the first 0000 0000 entry, or end of sector?
//
// Each file occupies at least one sector, extra space is wasted. The files don't appear to be
// implicitly compressed. There are no filenames.

// Each file entry (Kiss):
// uint16_t something? In early files, same as next value...
// uint16_t file byte size
// uint16_t something?
// followed by file data of the given size minus 2. (The size counts that previous word as well,
// but the observed contents don't jive with the actual data; there are obvious 4-char signatures
// at many file starts and extra two bytes before those would be weird.)

// In case of Rose, it seems files start without an extra header - MIZ1 signatures for example
// appear at exact 0x400 boundaries. Nothing indicates exact file size?.. Rose is the older game,
// so would be more primitive.

// Kiss A: Sector = 0x400 (1024) bytes
// File starts:
// - 2000 - sector 08 0x08
// - 4C00 - sector 19 0x13
// - 5000 - sector 20 0x14
// - 8000 - sector 32 0x20
// - 8400 - sector 33 0x21
// - 8800 - sector 34 0x22
// - 8C00 - sector 35 0x23
// - 9000 - sector 36 0x24
// - 9C00 - sector 39 0x27
// If a file starts with "IZBIOS" then it's not a real file.
