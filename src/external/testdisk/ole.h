/*
 * ole.h - OLE2/CFB structure definitions for TestDisk validators
 *
 * Copyright (C) 2006 Christophe GRENIER <grenier@cgsecurity.org>
 *
 * Adapted for disk-recover: Removed FRAMAC annotations, added MSVC compat.
 */
#ifndef _OLE_H
#define _OLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "td_config.h"
#include "types.h"

#define SPECIAL_BLOCK	(-3)
#define END_OF_CHAIN	(-2)
#define UNUSED		(-1)

#define NO_ENTRY		0
#define STORAGE			1
#define STREAM			2
#define ROOT			5
#define SHORT_BLOCK		3

#define FAT_START		0x4c
#define OUR_BLK_SIZE	512
#define DIRS_PER_BLK	4

#ifdef _MSC_VER
__pragma(pack(push, 1))
struct OLE_HDR
{
	char		magic[8];				/*0*/
	char		clsid[16];				/*8*/
	uint16_t	uMinorVersion;			/*24*/
	uint16_t	uDllVersion;			/*26*/
	uint16_t	uByteOrder;				/*28*/
	uint16_t	uSectorShift;			/*30*/
	uint16_t	uMiniSectorShift;		/*32*/
	uint16_t	reserved;				/*34*/
	uint32_t	reserved1;				/*36*/
	uint32_t	csectDir;			/*40*/
	uint32_t	num_FAT_blocks;			/*44*/
	uint32_t	root_start_block;		/*48*/
	uint32_t	dfsignature;			/*52*/
	uint32_t	miniSectorCutoff;		/*56*/
	uint32_t	MiniFat_block;			/*60*/
	uint32_t	csectMiniFat;			/*64*/
	uint32_t	FAT_next_block;			/*68*/
	uint32_t	num_extra_FAT_blocks;	/*72*/
};
__pragma(pack(pop))

__pragma(pack(push, 1))
struct OLE_DIR
{
	char		name[64];	// 0
	uint16_t	namsiz;		// 64
	char		type;		// 66
	char		bflags;		// 67: 0 or 1
	uint32_t	prev_dirent;	// 68
	uint32_t	next_dirent;	// 72
	uint32_t	sidChild;	// 76
	char		clsid[16];	// 80
	uint32_t	userFlags;	// 96
	int32_t		secs1;		// 100
	int32_t		days1;		// 104
	int32_t		secs2;		// 108
	int32_t		days2;		// 112
	uint32_t	start_block;	// 116
	uint32_t	size;		// 120
	int16_t		reserved;	// 124
	int16_t		padding;	// 126
};
__pragma(pack(pop))
#else
struct OLE_HDR
{
	char		magic[8];
	char		clsid[16];
	uint16_t	uMinorVersion;
	uint16_t	uDllVersion;
	uint16_t	uByteOrder;
	uint16_t	uSectorShift;
	uint16_t	uMiniSectorShift;
	uint16_t	reserved;
	uint32_t	reserved1;
	uint32_t	csectDir;
	uint32_t	num_FAT_blocks;
	uint32_t	root_start_block;
	uint32_t	dfsignature;
	uint32_t	miniSectorCutoff;
	uint32_t	MiniFat_block;
	uint32_t	csectMiniFat;
	uint32_t	FAT_next_block;
	uint32_t	num_extra_FAT_blocks;
} __attribute__ ((gcc_struct, __packed__));

struct OLE_DIR
{
	char		name[64];
	uint16_t	namsiz;
	char		type;
	char		bflags;
	uint32_t	prev_dirent;
	uint32_t	next_dirent;
	uint32_t	sidChild;
	char		clsid[16];
	uint32_t	userFlags;
	int32_t		secs1;
	int32_t		days1;
	int32_t		secs2;
	int32_t		days2;
	uint32_t	start_block;
	uint32_t	size;
	int16_t		reserved;
	int16_t		padding;
} __attribute__ ((gcc_struct, __packed__));
#endif

struct DIRECTORY
{
	char	name[64];
	int32_t		type;
	int32_t		level;
	int32_t		start_block;
	int32_t		size;
	int32_t		next;
	int32_t		prev;
	int32_t		dir;
	int32_t		s1;
	int32_t		s2;
	int32_t		d1;
	int32_t		d2;
};

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _OLE_H */
