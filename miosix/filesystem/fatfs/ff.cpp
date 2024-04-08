/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem Module  R0.15 w/patch3                      /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 2022, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/
/---------------------------------------------------------------------------
/
/ Feb 26,'06 R0.00  Prototype.
/
/ Apr 29,'06 R0.01  First stable version.
/
/ Jun 01,'06 R0.02  Added FAT12 support.
/                   Removed unbuffered mode.
/                   Fixed a problem on small (<32M) partition.
/ Jun 10,'06 R0.02a Added a configuration option (_FS_MINIMUM).
/
/ Sep 22,'06 R0.03  Added f_rename().
/                   Changed option _FS_MINIMUM to _FS_MINIMIZE.
/ Dec 11,'06 R0.03a Improved cluster scan algorithm to write files fast.
/                   Fixed f_mkdir() creates incorrect directory on FAT32.
/
/ Feb 04,'07 R0.04  Supported multiple drive system.
/                   Changed some interfaces for multiple drive system.
/                   Changed f_mountdrv() to f_mount().
/                   Added f_mkfs().
/ Apr 01,'07 R0.04a Supported multiple partitions on a physical drive.
/                   Added a capability of extending file size to f_lseek().
/                   Added minimization level 3.
/                   Fixed an endian sensitive code in f_mkfs().
/ May 05,'07 R0.04b Added a configuration option _USE_NTFLAG.
/                   Added FSINFO support.
/                   Fixed DBCS name can result FR_INVALID_NAME.
/                   Fixed short seek (<= csize) collapses the file object.
/
/ Aug 25,'07 R0.05  Changed arguments of f_read(), f_write() and f_mkfs().
/                   Fixed f_mkfs() on FAT32 creates incorrect FSINFO.
/                   Fixed f_mkdir() on FAT32 creates incorrect directory.
/ Feb 03,'08 R0.05a Added f_truncate() and f_utime().
/                   Fixed off by one error at FAT sub-type determination.
/                   Fixed btr in f_read() can be mistruncated.
/                   Fixed cached sector is not flushed when create and close without write.
/
/ Apr 01,'08 R0.06  Added fputc(), fputs(), fprintf() and fgets().
/                   Improved performance of f_lseek() on moving to the same or following cluster.
/
/ Apr 01,'09 R0.07  Merged Tiny-FatFs as a configuration option. (_FS_TINY)
/                   Added long file name feature.
/                   Added multiple code page feature.
/                   Added re-entrancy for multitask operation.
/                   Added auto cluster size selection to f_mkfs().
/                   Added rewind option to f_readdir().
/                   Changed result code of critical errors.
/                   Renamed string functions to avoid name collision.
/ Apr 14,'09 R0.07a Separated out OS dependent code on reentrant cfg.
/                   Added multiple sector size feature.
/ Jun 21,'09 R0.07c Fixed f_unlink() can return FR_OK on error.
/                   Fixed wrong cache control in f_lseek().
/                   Added relative path feature.
/                   Added f_chdir() and f_chdrive().
/                   Added proper case conversion to extended character.
/ Nov 03,'09 R0.07e Separated out configuration options from ff.h to ffconf.h.
/                   Fixed f_unlink() fails to remove a sub-directory on _FS_RPATH.
/                   Fixed name matching error on the 13 character boundary.
/                   Added a configuration option, _LFN_UNICODE.
/                   Changed f_readdir() to return the SFN with always upper case on non-LFN cfg.
/
/ May 15,'10 R0.08  Added a memory configuration option. (_USE_LFN = 3)
/                   Added file lock feature. (_FS_SHARE)
/                   Added fast seek feature. (_USE_FASTSEEK)
/                   Changed some types on the API, XCHAR->TCHAR.
/                   Changed .fname in the FILINFO structure on Unicode cfg.
/                   String functions support UTF-8 encoding files on Unicode cfg.
/ Aug 16,'10 R0.08a Added f_getcwd().
/                   Added sector erase feature. (_USE_ERASE)
/                   Moved file lock semaphore table from fs object to the bss.
/                   Fixed a wrong directory entry is created on non-LFN cfg when the given name contains ';'.
/                   Fixed f_mkfs() creates wrong FAT32 volume.
/ Jan 15,'11 R0.08b Fast seek feature is also applied to f_read() and f_write().
/                   f_lseek() reports required table size on creating CLMP.
/                   Extended format syntax of f_printf().
/                   Ignores duplicated directory separators in given path name.
/
/ Sep 06,'11 R0.09  f_mkfs() supports multiple partition to complete the multiple partition feature.
/                   Added f_fdisk().
/ Aug 27,'12 R0.09a Changed f_open() and f_opendir() reject null object pointer to avoid crash.
/                   Changed option name _FS_SHARE to _FS_LOCK.
/                   Fixed assertion failure due to OS/2 EA on FAT12/16 volume.
/ Jan 24,'13 R0.09b Added f_setlabel() and f_getlabel().
/
/ Oct 02,'13 R0.10  Added selection of character encoding on the file. (_STRF_ENCODE)
/                   Added f_closedir().
/                   Added forced full FAT scan for f_getfree(). (_FS_NOFSINFO)
/                   Added forced mount feature with changes of f_mount().
/                   Improved behavior of volume auto detection.
/                   Improved write throughput of f_puts() and f_printf().
/                   Changed argument of f_chdrive(), f_mkfs(), disk_read() and disk_write().
/                   Fixed f_write() can be truncated when the file size is close to 4GB.
/                   Fixed f_open(), f_mkdir() and f_setlabel() can return incorrect error code.
/----------------------------------------------------------------------------*/

#include "ff.h"			/* Declarations of FatFs API */
#include "diskio.h"		/* Declarations of disk I/O functions */

// Added by TFT -- begin
#include <string.h>
#include <stdlib.h>
#include <util/unicode.h>
#include <interfaces/atomic_ops.h>
#include "config/miosix_settings.h"

#ifdef WITH_FILESYSTEM

/**
 * FAT32 does not have the concept of inodes, but we need them.
 * This code thus uses the sector # containing the directory entry and the
 * index within the sector where the directory entry is located as inode.
 * This code has the following limitations:
 * - If _FS_RPATH is defined and INODE() is applied to the '.' and '..'
 *   entries, it returns inconsistent results. That's because for example the
 *   inode of '..' must be the same of the '.' inode of the parent directory,
 *   but these are in two different directories, so the sector # are different.
 *   This has been fixed by disabling _FS_RPATH and filling those entries
 *   manually.
 * - It assumes that one sector is 512 byte, so that 16 directory entries fit
 *   in one sector.
 * - If there are more than 2^32/16 sectors (filesystems > 128GByte) inodes
 *   are no longer unique!
 * This code also guarantees not to ever return inode values of 0 and 1, as
 * zero is an invalid inode, and 1 is reserved by the Fat32Fs code for the
 * '.' entry of the root directory of the filesystem. If the algorithm for
 * some reason, such as a >128GB filesystem would return 0 or 1, it always
 * returns 2.
 */
#define INODE(x) ((x->sect<<4 | x->index % 16)<3) ? 2 : (x->sect<<4 | x->index % 16)
// Added by TFT -- end


/*--------------------------------------------------------------------------

   Module Private Definitions

---------------------------------------------------------------------------*/

#if _FATFS != 80286	/* Revision ID */
#error Wrong include file (ff.h).
#endif


/* Definitions on sector size */
#if _MAX_SS != 512 && _MAX_SS != 1024 && _MAX_SS != 2048 && _MAX_SS != 4096
#error Wrong sector size.
#endif
#if _MAX_SS != 512
#define	SS(fs)	((fs)->ssize)	/* Variable sector size */
#else
#define	SS(fs)	512U			/* Fixed sector size */
#endif


/* Limits and boundaries */
#define MAX_DIR		0x200000		/* Max size of FAT directory */
#define MAX_DIR_EX	0x10000000		/* Max size of exFAT directory */
#define MAX_FAT12	0xFF5			/* Max FAT12 clusters (differs from specs, but right for real DOS/Windows behavior) */
#define MAX_FAT16	0xFFF5			/* Max FAT16 clusters (differs from specs, but right for real DOS/Windows behavior) */
#define MAX_FAT32	0x0FFFFFF5		/* Max FAT32 clusters (not specified, practical limit) */
#define MAX_EXFAT	0x7FFFFFFD		/* Max exFAT clusters (differs from specs, implementation limit) */

/* Reentrancy related */
#if _FS_REENTRANT
#if _USE_LFN == 1
#error Static LFN work area cannot be used at thread-safe configuration.
#endif
#define	ENTER_FF(fs)		{ if (!lock_fs(fs)) return FR_TIMEOUT; }
#define	LEAVE_FF(fs, res)	{ unlock_fs(fs, res); return res; }
#else
#define	ENTER_FF(fs)
#define LEAVE_FF(fs, res)	return res
#endif

#define	ABORT(fs, res)		{ fp->err = (BYTE)(res); LEAVE_FF(fs, res); }






/* DBCS code ranges and SBCS extend character conversion table */

#if _CODE_PAGE == 932	/* Japanese Shift-JIS */
#define _DF1S	0x81	/* DBC 1st byte range 1 start */
#define _DF1E	0x9F	/* DBC 1st byte range 1 end */
#define _DF2S	0xE0	/* DBC 1st byte range 2 start */
#define _DF2E	0xFC	/* DBC 1st byte range 2 end */
#define _DS1S	0x40	/* DBC 2nd byte range 1 start */
#define _DS1E	0x7E	/* DBC 2nd byte range 1 end */
#define _DS2S	0x80	/* DBC 2nd byte range 2 start */
#define _DS2E	0xFC	/* DBC 2nd byte range 2 end */

#elif _CODE_PAGE == 936	/* Simplified Chinese GBK */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x40
#define _DS1E	0x7E
#define _DS2S	0x80
#define _DS2E	0xFE

#elif _CODE_PAGE == 949	/* Korean */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x41
#define _DS1E	0x5A
#define _DS2S	0x61
#define _DS2E	0x7A
#define _DS3S	0x81
#define _DS3E	0xFE

#elif _CODE_PAGE == 950	/* Traditional Chinese Big5 */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x40
#define _DS1E	0x7E
#define _DS2S	0xA1
#define _DS2E	0xFE

#elif _CODE_PAGE == 437	/* U.S. (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x90,0x41,0x8E,0x41,0x8F,0x80,0x45,0x45,0x45,0x49,0x49,0x49,0x8E,0x8F,0x90,0x92,0x92,0x4F,0x99,0x4F,0x55,0x55,0x59,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 720	/* Arabic (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x45,0x41,0x84,0x41,0x86,0x43,0x45,0x45,0x45,0x49,0x49,0x8D,0x8E,0x8F,0x90,0x92,0x92,0x93,0x94,0x95,0x49,0x49,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 737	/* Greek (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x92,0x92,0x93,0x94,0x95,0x96,0x97,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87, \
				0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0xAA,0x92,0x93,0x94,0x95,0x96,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0x97,0xEA,0xEB,0xEC,0xE4,0xED,0xEE,0xE7,0xE8,0xF1,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 775	/* Baltic (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x91,0xA0,0x8E,0x95,0x8F,0x80,0xAD,0xED,0x8A,0x8A,0xA1,0x8D,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0x95,0x96,0x97,0x97,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xE0,0xA3,0xA3,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xB5,0xB6,0xB7,0xB8,0xBD,0xBE,0xC6,0xC7,0xA5,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE3,0xE8,0xE8,0xEA,0xEA,0xEE,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 850	/* Multilingual Latin 1 (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0xDE,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x59,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
				0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE7,0xE9,0xEA,0xEB,0xED,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 852	/* Latin 2 (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xDE,0x8F,0x80,0x9D,0xD3,0x8A,0x8A,0xD7,0x8D,0x8E,0x8F,0x90,0x91,0x91,0xE2,0x99,0x95,0x95,0x97,0x97,0x99,0x9A,0x9B,0x9B,0x9D,0x9E,0x9F, \
				0xB5,0xD6,0xE0,0xE9,0xA4,0xA4,0xA6,0xA6,0xA8,0xA8,0xAA,0x8D,0xAC,0xB8,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBD,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC6,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD2,0xD3,0xD2,0xD5,0xD6,0xD7,0xB7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE3,0xD5,0xE6,0xE6,0xE8,0xE9,0xE8,0xEB,0xED,0xED,0xDD,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xEB,0xFC,0xFC,0xFE,0xFF}

#elif _CODE_PAGE == 855	/* Cyrillic (OEM) */
#define _DF1S	0
#define _EXCVT {0x81,0x81,0x83,0x83,0x85,0x85,0x87,0x87,0x89,0x89,0x8B,0x8B,0x8D,0x8D,0x8F,0x8F,0x91,0x91,0x93,0x93,0x95,0x95,0x97,0x97,0x99,0x99,0x9B,0x9B,0x9D,0x9D,0x9F,0x9F, \
				0xA1,0xA1,0xA3,0xA3,0xA5,0xA5,0xA7,0xA7,0xA9,0xA9,0xAB,0xAB,0xAD,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB6,0xB6,0xB8,0xB8,0xB9,0xBA,0xBB,0xBC,0xBE,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD3,0xD3,0xD5,0xD5,0xD7,0xD7,0xDD,0xD9,0xDA,0xDB,0xDC,0xDD,0xE0,0xDF, \
				0xE0,0xE2,0xE2,0xE4,0xE4,0xE6,0xE6,0xE8,0xE8,0xEA,0xEA,0xEC,0xEC,0xEE,0xEE,0xEF,0xF0,0xF2,0xF2,0xF4,0xF4,0xF6,0xF6,0xF8,0xF8,0xFA,0xFA,0xFC,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 857	/* Turkish (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0x98,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x98,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9E, \
				0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA6,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xDE,0x59,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 858	/* Multilingual Latin 1 + Euro (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0xDE,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x59,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
				0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE7,0xE9,0xEA,0xEB,0xED,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 862	/* Hebrew (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 866	/* Russian (OEM) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0x90,0x91,0x92,0x93,0x9d,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,0xF0,0xF0,0xF2,0xF2,0xF4,0xF4,0xF6,0xF6,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 874	/* Thai (OEM, Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 1250 /* Central Europe (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x8D,0x8E,0x8F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xA3,0xB4,0xB5,0xB6,0xB7,0xB8,0xA5,0xAA,0xBB,0xBC,0xBD,0xBC,0xAF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xFF}

#elif _CODE_PAGE == 1251 /* Cyrillic (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x82,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x80,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x8D,0x8E,0x8F, \
				0xA0,0xA2,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB2,0xA5,0xB5,0xB6,0xB7,0xA8,0xB9,0xAA,0xBB,0xA3,0xBD,0xBD,0xAF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF}

#elif _CODE_PAGE == 1252 /* Latin 1 (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0xAd,0x9B,0x8C,0x9D,0xAE,0x9F, \
				0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0x9F}

#elif _CODE_PAGE == 1253 /* Greek (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xA2,0xB8,0xB9,0xBA, \
				0xE0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xF2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xFB,0xBC,0xFD,0xBF,0xFF}

#elif _CODE_PAGE == 1254 /* Turkish (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x9D,0x9E,0x9F, \
				0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0x9F}

#elif _CODE_PAGE == 1255 /* Hebrew (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 1256 /* Arabic (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x8C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0x41,0xE1,0x41,0xE3,0xE4,0xE5,0xE6,0x43,0x45,0x45,0x45,0x45,0xEC,0xED,0x49,0x49,0xF0,0xF1,0xF2,0xF3,0x4F,0xF5,0xF6,0xF7,0xF8,0x55,0xFA,0x55,0x55,0xFD,0xFE,0xFF}

#elif _CODE_PAGE == 1257 /* Baltic (Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
				0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xA8,0xB9,0xAA,0xBB,0xBC,0xBD,0xBE,0xAF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xFF}

#elif _CODE_PAGE == 1258 /* Vietnam (OEM, Windows) */
#define _DF1S	0
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0xAC,0x9D,0x9E,0x9F, \
				0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
				0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xEC,0xCD,0xCE,0xCF,0xD0,0xD1,0xF2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xFE,0x9F}

#elif _CODE_PAGE == 1	/* ASCII (for only non-LFN cfg) */
#if _USE_LFN
#error Cannot use LFN feature without valid code page.
#endif
#define _DF1S	0

#else
#error Unknown code page

#endif


/* Character code support macros */
#define IsUpper(c)		(((c)>='A')&&((c)<='Z'))
#define IsLower(c)		(((c)>='a')&&((c)<='z'))
#define IsDigit(c)		(((c)>='0')&&((c)<='9'))
#define IsSeparator(c)	((c) == '/' || (c) == '\\')
#define IsTerminator(c)	((UINT)(c) < (_USE_LFN ? ' ' : '!'))
#define IsSurrogate(c)	((c) >= 0xD800 && (c) <= 0xDFFF)
#define IsSurrogateH(c)	((c) >= 0xD800 && (c) <= 0xDBFF)
#define IsSurrogateL(c)	((c) >= 0xDC00 && (c) <= 0xDFFF)

/* Additional file access control and file status flags for internal use */
#define FA_SEEKEND	0x20	/* Seek to end of the file on file open */
#define FA_MODIFIED	0x40	/* File has been modified */
#define FA_DIRTY	0x80	/* FIL.buf[] needs to be written-back */


/* Additional file attribute bits for internal use */
#define AM_VOL		0x08	/* Volume label */
#define AM_LFN		0x0F	/* LFN entry */
#define AM_MASK		0x3F	/* Mask of defined bits in FAT */
#define AM_MASKX	0x37	/* Mask of defined bits in exFAT */

#if _DF1S		/* Code page is DBCS */

#ifdef _DF2S	/* Two 1st byte areas */
#define IsDBCS1(c)	(((BYTE)(c) >= _DF1S && (BYTE)(c) <= _DF1E) || ((BYTE)(c) >= _DF2S && (BYTE)(c) <= _DF2E))
#else			/* One 1st byte area */
#define IsDBCS1(c)	((BYTE)(c) >= _DF1S && (BYTE)(c) <= _DF1E)
#endif

#ifdef _DS3S	/* Three 2nd byte areas */
#define IsDBCS2(c)	(((BYTE)(c) >= _DS1S && (BYTE)(c) <= _DS1E) || ((BYTE)(c) >= _DS2S && (BYTE)(c) <= _DS2E) || ((BYTE)(c) >= _DS3S && (BYTE)(c) <= _DS3E))
#else			/* Two 2nd byte areas */
#define IsDBCS2(c)	(((BYTE)(c) >= _DS1S && (BYTE)(c) <= _DS1E) || ((BYTE)(c) >= _DS2S && (BYTE)(c) <= _DS2E))
#endif

#else			/* Code page is SBCS */

#define IsDBCS1(c)	0
#define IsDBCS2(c)	0

#endif /* _DF1S */


/* Name status flags */
#define NS			11		/* Index of name status byte in fn[] */
#define NS_LOSS		0x01	/* Out of 8.3 format */
#define NS_LFN		0x02	/* Force to create LFN entry */
#define NS_LAST		0x04	/* Last segment */
#define NS_BODY		0x08	/* Lower case flag (body) */
#define NS_EXT		0x10	/* Lower case flag (ext) */
#define NS_DOT		0x20	/* Dot entry */
#define NS_NOLFN	0x40	/* Do not find LFN */
#define NS_NONAME	0x80	/* Not followed */


/* FAT sub-type boundaries */
#define MIN_FAT16	4086U	/* Minimum number of clusters for FAT16 */
#define	MIN_FAT32	65526U	/* Minimum number of clusters for FAT32 */


/* exFAT directory entry types */
#define	ET_BITMAP	0x81	/* Allocation bitmap */
#define	ET_UPCASE	0x82	/* Up-case table */
#define	ET_VLABEL	0x83	/* Volume label */
#define	ET_FILEDIR	0x85	/* File and directory */
#define	ET_STREAM	0xC0	/* Stream extension */
#define	ET_FILENAME	0xC1	/* Name extension */


/* FatFs refers the members in the FAT structures as byte array instead of
/ structure member because the structure is not binary compatible between
/ different platforms */

#define BS_jmpBoot			0	/* Jump instruction (3) */
#define BS_OEMName			3	/* OEM name (8) */
#define BPB_BytsPerSec		11	/* Sector size [byte] (2) */
#define BPB_SecPerClus		13	/* Cluster size [sector] (1) */
#define BPB_RsvdSecCnt		14	/* Size of reserved area [sector] (2) */
#define BPB_NumFATs			16	/* Number of FAT copies (1) */
#define BPB_RootEntCnt		17	/* Number of root directory entries for FAT12/16 (2) */
#define BPB_TotSec16		19	/* Volume size [sector] (2) */
#define BPB_Media			21	/* Media descriptor (1) */
#define BPB_FATSz16			22	/* FAT size [sector] (2) */
#define BPB_SecPerTrk		24	/* Track size [sector] (2) */
#define BPB_NumHeads		26	/* Number of heads (2) */
#define BPB_HiddSec			28	/* Number of special hidden sectors (4) */
#define BPB_TotSec32		32	/* Volume size [sector] (4) */
#define BS_DrvNum			36	/* Physical drive number (2) */
#define BS_NTres			37	/* WindowsNT error flag (BYTE) */
#define BS_BootSig			38	/* Extended boot signature (1) */
#define BS_VolID			39	/* Volume serial number (4) */
#define BS_VolLab			43	/* Volume label (8) */
#define BS_FilSysType		54	/* File system type (1) */
#define BS_BootCode			62	/* Boot code (448-byte) */
#define BS_55AA				510	/* Signature word (WORD) */

#define BPB_FATSz32			36	/* FAT32: FAT size [sector] (4) */
#define BPB_ExtFlags32		40	/* FAT32: Extended flags (2) */
#define BPB_FSVer32			42	/* FAT32: File system version (2) */
#define BPB_RootClus32		44	/* FAT32: Root directory first cluster (4) */
#define BPB_FSInfo32		48	/* FAT32: Offset of FSINFO sector (2) */
#define BPB_BkBootSec32		50	/* FAT32: Offset of backup boot sector (2) */
#define BS_DrvNum32			64	/* FAT32: Physical drive number (2) */
#define BS_NTres32			65	/* FAT32: Error flag (BYTE) */
#define BS_BootSig32		66	/* FAT32: Extended boot signature (1) */
#define BS_VolID32			67	/* FAT32: Volume serial number (4) */
#define BS_VolLab32			71	/* FAT32: Volume label (8) */
#define BS_FilSysType32		82	/* FAT32: File system type (1) */
#define BS_BootCode32		90		/* FAT32: Boot code (420-byte) */

#define BPB_ZeroedEx		11		/* exFAT: MBZ field (53-byte) */
#define BPB_VolOfsEx		64		/* exFAT: Volume offset from top of the drive [sector] (QWORD) */
#define BPB_TotSecEx		72		/* exFAT: Volume size [sector] (QWORD) */
#define BPB_FatOfsEx		80		/* exFAT: FAT offset from top of the volume [sector] (DWORD) */
#define BPB_FatSzEx			84		/* exFAT: FAT size [sector] (DWORD) */
#define BPB_DataOfsEx		88		/* exFAT: Data offset from top of the volume [sector] (DWORD) */
#define BPB_NumClusEx		92		/* exFAT: Number of clusters (DWORD) */
#define BPB_RootClusEx		96		/* exFAT: Root directory start cluster (DWORD) */
#define BPB_VolIDEx			100		/* exFAT: Volume serial number (DWORD) */
#define BPB_FSVerEx			104		/* exFAT: Filesystem version (WORD) */
#define BPB_VolFlagEx		106		/* exFAT: Volume flags (WORD) */
#define BPB_BytsPerSecEx	108		/* exFAT: Log2 of sector size in unit of byte (BYTE) */
#define BPB_SecPerClusEx	109		/* exFAT: Log2 of cluster size in unit of sector (BYTE) */
#define BPB_NumFATsEx		110		/* exFAT: Number of FATs (BYTE) */
#define BPB_DrvNumEx		111		/* exFAT: Physical drive number for int13h (BYTE) */
#define BPB_PercInUseEx		112		/* exFAT: Percent in use (BYTE) */
#define BPB_RsvdEx			113		/* exFAT: Reserved (7-byte) */
#define BS_BootCodeEx		120		/* exFAT: Boot code (390-byte) */


#define	FSI_LeadSig			0	/* FSI: Leading signature (4) */
#define	FSI_StrucSig		484	/* FSI: Structure signature (4) */
#define	FSI_Free_Count		488	/* FSI: Number of free clusters (4) */
#define	FSI_Nxt_Free		492	/* FSI: Last allocated cluster (4) */
#define MBR_Table			446	/* MBR: Partition table offset (2) */
#define	SZ_PTE				16	/* MBR: Size of a partition table entry */
#define BS_55AA				510	/* Boot sector signature (2) */

#define	DIR_Name			0	/* Short file name (11) */
#define	DIR_Attr			11	/* Attribute (1) */
#define	DIR_NTres			12	/* NT flag (1) */
#define DIR_CrtTimeTenth	13	/* Created time sub-second (1) */
#define	DIR_CrtTime			14	/* Created time (2) */
#define	DIR_CrtDate			16	/* Created date (2) */
#define DIR_LstAccDate		18	/* Last accessed date (2) */
#define	DIR_FstClusHI		20	/* Higher 16-bit of first cluster (2) */
#define	DIR_WrtTime			22	/* Modified time (2) */
#define	DIR_WrtDate			24	/* Modified date (2) */
#define DIR_ModTime			22	/* Modified time (DWORD) */
#define	DIR_FstClusLO		26	/* Lower 16-bit of first cluster (2) */
#define	DIR_FileSize		28	/* File size (4) */
#define	LDIR_Ord			0	/* LFN entry order and LLE flag (1) */
#define	LDIR_Attr			11	/* LFN attribute (1) */
#define	LDIR_Type			12	/* LFN type (1) */
#define	LDIR_Chksum			13	/* Sum of corresponding SFN entry */
#define	LDIR_FstClusLO		26	/* Filled by zero (0) */
#define	SZ_DIR				32		/* Size of a directory entry */
#define	LLE					0x40	/* Last long entry flag in LDIR_Ord */
#define	DDE					0xE5	/* Deleted directory entry mark in DIR_Name[0] */
#define	NDDE				0x05	/* Replacement of the character collides with DDE */

#define XDIR_Type			0		/* exFAT: Type of exFAT directory entry (BYTE) */
#define XDIR_NumLabel		1		/* exFAT: Number of volume label characters (BYTE) */
#define XDIR_Label			2		/* exFAT: Volume label (11-WORD) */
#define XDIR_CaseSum		4		/* exFAT: Sum of case conversion table (DWORD) */
#define XDIR_NumSec			1		/* exFAT: Number of secondary entries (BYTE) */
#define XDIR_SetSum			2		/* exFAT: Sum of the set of directory entries (WORD) */
#define XDIR_Attr			4		/* exFAT: File attribute (WORD) */
#define XDIR_CrtTime		8		/* exFAT: Created time (DWORD) */
#define XDIR_ModTime		12		/* exFAT: Modified time (DWORD) */
#define XDIR_AccTime		16		/* exFAT: Last accessed time (DWORD) */
#define XDIR_CrtTimeTenth	20		/* exFAT: Created time subsecond (BYTE) */
#define XDIR_ModTime10		21		/* exFAT: Modified time subsecond (BYTE) */
#define XDIR_CrtTZ			22		/* exFAT: Created timezone (BYTE) */
#define XDIR_ModTZ			23		/* exFAT: Modified timezone (BYTE) */
#define XDIR_AccTZ			24		/* exFAT: Last accessed timezone (BYTE) */
#define XDIR_GenFlags		33		/* exFAT: General secondary flags (BYTE) */
#define XDIR_NumName		35		/* exFAT: Number of file name characters (BYTE) */
#define XDIR_NameHash		36		/* exFAT: Hash of file name (WORD) */
#define XDIR_ValidFileSize	40		/* exFAT: Valid file size (QWORD) */
#define XDIR_FstClus		52		/* exFAT: First cluster of the file data (DWORD) */
#define XDIR_FileSize		56		/* exFAT: File/Directory size (QWORD) */


#define MBR_Table			446		/* MBR: Offset of partition table in the MBR */
#define SZ_PTE				16		/* MBR: Size of a partition table entry */
#define PTE_Boot			0		/* MBR PTE: Boot indicator */
#define PTE_StHead			1		/* MBR PTE: Start head */
#define PTE_StSec			2		/* MBR PTE: Start sector */
#define PTE_StCyl			3		/* MBR PTE: Start cylinder */
#define PTE_System			4		/* MBR PTE: System ID */
#define PTE_EdHead			5		/* MBR PTE: End head */
#define PTE_EdSec			6		/* MBR PTE: End sector */
#define PTE_EdCyl			7		/* MBR PTE: End cylinder */
#define PTE_StLba			8		/* MBR PTE: Start in LBA */
#define PTE_SizLba			12		/* MBR PTE: Size in LBA */

#define GPTH_Sign			0		/* GPT HDR: Signature (8-byte) */
#define GPTH_Rev			8		/* GPT HDR: Revision (DWORD) */
#define GPTH_Size			12		/* GPT HDR: Header size (DWORD) */
#define GPTH_Bcc			16		/* GPT HDR: Header BCC (DWORD) */
#define GPTH_CurLba			24		/* GPT HDR: This header LBA (QWORD) */
#define GPTH_BakLba			32		/* GPT HDR: Another header LBA (QWORD) */
#define GPTH_FstLba			40		/* GPT HDR: First LBA for partition data (QWORD) */
#define GPTH_LstLba			48		/* GPT HDR: Last LBA for partition data (QWORD) */
#define GPTH_DskGuid		56		/* GPT HDR: Disk GUID (16-byte) */
#define GPTH_PtOfs			72		/* GPT HDR: Partition table LBA (QWORD) */
#define GPTH_PtNum			80		/* GPT HDR: Number of table entries (DWORD) */
#define GPTH_PteSize		84		/* GPT HDR: Size of table entry (DWORD) */
#define GPTH_PtBcc			88		/* GPT HDR: Partition table BCC (DWORD) */
#define SZ_GPTE				128		/* GPT PTE: Size of partition table entry */
#define GPTE_PtGuid			0		/* GPT PTE: Partition type GUID (16-byte) */
#define GPTE_UpGuid			16		/* GPT PTE: Partition unique GUID (16-byte) */
#define GPTE_FstLba			32		/* GPT PTE: First LBA of partition (QWORD) */
#define GPTE_LstLba			40		/* GPT PTE: Last LBA of partition (QWORD) */
#define GPTE_Flags			48		/* GPT PTE: Partition flags (QWORD) */
#define GPTE_Name			56		/* GPT PTE: Partition name */

/*------------------------------------------------------------*/
/* Module private work area                                   */
/*------------------------------------------------------------*/
/* Note that uninitialized variables with static duration are
/  zeroed/nulled at start-up. If not, the compiler or start-up
/  routine is out of ANSI-C standard.
*/

/*--------------------------------*/
/* File/Volume controls           */
/*--------------------------------*/

#if _VOLUMES
//static
//FATFS *FatFs[_VOLUMES];	/* Pointer to the file system objects (logical drives) */
#else
#error Number of volumes must not be 0.
#endif

static /*WORD*/ int Fsid;				/* File system mount ID */

#if _FS_RPATH != 0
BYTE CurrVol;				/* Current drive set by f_chdrive() */
#endif

#if _FS_RPATH && _VOLUMES >= 2
static BYTE CurrVol;			/* Current drive */
#endif

#ifdef _FS_LOCK
//static
//FILESEM	Files[_FS_LOCK];	/* Open object lock semaphores */
#if _FS_REENTRANT
static volatile BYTE SysLock;		/* System lock flag to protect Files[] (0:no mutex, 1:unlocked, 2:locked) */
static volatile BYTE SysLockVolume;	/* Volume id who is locking Files[] */
#endif
#endif

#if _STR_VOLUME_ID
#ifdef _VOLUME_STRS
static const char *const VolumeStr[_VOLUMES] = {_VOLUME_STRS};	/* Pre-defined volume ID */
#endif
#endif

#if _LBA64
#if _MIN_GPT > 0x100000000
#error Wrong _MIN_GPT setting
#endif
static const BYTE GUID_MS_Basic[16] = {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
#endif

/*--------------------------------*/
/* LFN/Directory working buffer   */
/*--------------------------------*/

#if _USE_LFN == 0		/* Non-LFN configuration */
#if _FS_EXFAT
#error LFN must be enabled when enable exFAT
#endif
#define DEF_NAMBUF
#define INIT_NAMBUF(fs)
#define FREE_NAMBUF()
#define LEAVE_MKFS(res)	return res

#else					/* LFN configurations */
#if _MAX_LFN < 12 || _MAX_LFN > 255
#error Wrong setting of _MAX_LFN
#endif
#if _LFN_BUF < _SFN_BUF || _SFN_BUF < 12
#error Wrong setting of _LFN_BUF or _SFN_BUF
#endif
#if _LFN_UNICODE < 0 || _LFN_UNICODE > 3
#error Wrong setting of _LFN_UNICODE
#endif

#define MAXDIRB(nc)	((nc + 44U) / 15 * SZ_DIR)	/* exFAT: Size of directory entry block scratchpad buffer needed for the name length */

#if _USE_LFN == 1		/* LFN enabled with static working buffer */
#if _FS_EXFAT
static BYTE	DirBuf[MAXDIRB(_MAX_LFN)];	/* Directory entry block scratchpad buffer */
#endif
static WCHAR LfnBuf[_MAX_LFN + 1];		/* LFN working buffer */
#define DEF_NAMBUF
#define INIT_NAMBUF(fs)
#define FREE_NAMBUF()
#define LEAVE_MKFS(res)	return res

#elif _USE_LFN == 2 	/* LFN enabled with dynamic working buffer on the stack */
#if _FS_EXFAT
#define DEF_NAMEBUF		BYTE sfn[12]; WCHAR lbuf[_MAX_LFN+1]	/* LFN working buffer and directory entry block scratchpad buffer */
#define INIT_NAMBUF(fs)	{ (fs)->lfnbuf = lbuf; (fs)->dirbuf = dbuf; }
#define INIT_BUF(dobj)		{ (dobj).fn = sfn; (dobj).lfn = lbuf; }
#define	FREE_BUF()
#define FREE_NAMBUF()
#else
#define DEF_NAMEBUF		WCHAR lbuf[_MAX_LFN+1];	/* LFN working buffer */
#define INIT_NAMBUF(fs)	{ (fs)->lfnbuf = lbuf; }
#define FREE_NAMBUF()
#endif
#define LEAVE_MKFS(res)	return res

#elif _USE_LFN == 3 	/* LFN enabled with dynamic working buffer on the heap */
#if _FS_EXFAT
#define	DEF_NAMEBUF			BYTE sfn[12]; WCHAR *lfn
	/* Pointer to LFN working buffer and directory entry block scratchpad buffer */
#define INIT_NAMBUF(fs)	{ lfn = ff_memalloc((_MAX_LFN+1)*2 + MAXDIRB(_MAX_LFN)); if (!lfn) LEAVE_FF(fs, FR_NOT_ENOUGH_CORE); (fs)->lfnbuf = lfn; (fs)->dirbuf = (BYTE*)(lfn+_MAX_LFN+1); }

#define INIT_BUF(dobj)		{ lfn = ff_memalloc((_MAX_LFN + 1) * 2); \
							  if (!lfn) LEAVE_FF((dobj).fs, FR_NOT_ENOUGH_CORE); \
							  (dobj).lfn = lfn;	(dobj).fn = sfn; }
#define	FREE_BUF()			ff_memfree(lfn)

#else
#define DEF_NAMEBUF		WCHAR *lfn;	/* Pointer to LFN working buffer */
#define INIT_NAMBUF(fs)	{ lfn = _memalloc((_MAX_LFN+1)*2); if (!lfn) LEAVE_FF(fs, FR_NOT_ENOUGH_CORE); (fs)->lfnbuf = lfn; }
#define FREE_NAMBUF()	_memfree(lfn)
#endif
#define LEAVE_MKFS(res)	{ if (!work) _memfree(buf); return res; }
#define MAX_MALLOC	0x8000	/* Must be >=_MAX_SS */

#else
#error Wrong setting of _USE_LFN

#endif	/* _USE_LFN == 1 */
#endif	/* _USE_LFN == 0 */


#ifdef _EXCVT
static const BYTE ExCvt[] = _EXCVT;	/* Upper conversion table for extended characters */
#endif






/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Load/Store multi-byte word in the FAT structure                       */
/*-----------------------------------------------------------------------*/

static WORD ld_word (const BYTE* ptr)	/*	 Load a 2-byte little-endian word */
{
	WORD rv;

	rv = ptr[1];
	rv = rv << 8 | ptr[0];
	return rv;
}

static DWORD ld_dword (const BYTE* ptr)	/* Load a 4-byte little-endian word */
{
	DWORD rv;

	rv = ptr[3];
	rv = rv << 8 | ptr[2];
	rv = rv << 8 | ptr[1];
	rv = rv << 8 | ptr[0];
	return rv;
}

#if _FS_EXFAT
static QWORD ld_qword (const BYTE* ptr)	/* Load an 8-byte little-endian word */
{
	QWORD rv;

	rv = ptr[7];
	rv = rv << 8 | ptr[6];
	rv = rv << 8 | ptr[5];
	rv = rv << 8 | ptr[4];
	rv = rv << 8 | ptr[3];
	rv = rv << 8 | ptr[2];
	rv = rv << 8 | ptr[1];
	rv = rv << 8 | ptr[0];
	return rv;
}
#endif

#if !_FS_READONLY
static void st_word (BYTE* ptr, WORD val)	/* Store a 2-byte word in little-endian */
{
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val;
}

static void st_dword (BYTE* ptr, DWORD val)	/* Store a 4-byte word in little-endian */
{
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val;
}

#if _FS_EXFAT
static void st_qword (BYTE* ptr, QWORD val)	/* Store an 8-byte word in little-endian */
{
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val; val >>= 8;
	*ptr++ = (BYTE)val;
}
#endif
#endif	// _FS_READONLY


/*-----------------------------------------------------------------------*/
/* String functions                                                      */
/*-----------------------------------------------------------------------*/

// Added by TFT -- begin

//Using newlib's version of memcpy, memset, memcmp and strchr which are
//performance optimized, while these are size optimized ones.
#define mem_cpy memcpy
#define mem_set memset
#define mem_cmp memcmp

/* Copy memory to memory */
// static
// void mem_cpy (void* dst, const void* src, UINT cnt) {
// 	BYTE *d = (BYTE*)dst;
// 	const BYTE *s = (const BYTE*)src;
// 
// #if _WORD_ACCESS == 1
// 	while (cnt >= sizeof (int)) {
// 		*(int*)d = *(int*)s;
// 		d += sizeof (int); s += sizeof (int);
// 		cnt -= sizeof (int);
// 	}
// #endif
// 	while (cnt--)
// 		*d++ = *s++;
//}

/* Fill memory */
// static
// void mem_set (void* dst, int val, UINT cnt) {
// 	BYTE *d = (BYTE*)dst;
// 
// 	while (cnt--)
// 		*d++ = (BYTE)val;
// }

/* Compare memory to memory */
// static
// int mem_cmp (const void* dst, const void* src, UINT cnt) {
// 	const BYTE *d = (const BYTE *)dst, *s = (const BYTE *)src;
// 	int r = 0;
// 
// 	while (cnt-- && (r = *d++ - *s++) == 0) ;
// 	return r;
// }

/* Check if chr is contained in the string */
static int chk_chr (const char* str, int chr) {
	//while (*str && *str != chr) str++;
	//return *str;
    char *result=strchr(str,chr);
    if(result) return *result;
    else return 0;
}

// /* Test if the byte is DBC 1st byte */
// static int IsDBCS1 (BYTE c)
// {
// #if _CODE_PAGE == 0		/* Variable code page */
// 	if (DbcTbl && c >= DbcTbl[0]) {
// 		if (c <= DbcTbl[1]) return 1;					/* 1st byte range 1 */
// 		if (c >= DbcTbl[2] && c <= DbcTbl[3]) return 1;	/* 1st byte range 2 */
// 	}
// #elif _CODE_PAGE >= 900	/* DBCS fixed code page */
// 	if (c >= DbcTbl[0]) {
// 		if (c <= DbcTbl[1]) return 1;
// 		if (c >= DbcTbl[2] && c <= DbcTbl[3]) return 1;
// 	}
// #else						/* SBCS fixed code page */
// 	if (c != 0) return 0;	/* Always false */
// #endif
// 	return 0;
// }


// /* Test if the byte is DBC 2nd byte */
// static int dbc_2nd (BYTE c)
// {
// #if _CODE_PAGE == 0		/* Variable code page */
// 	if (DbcTbl && c >= DbcTbl[4]) {
// 		if (c <= DbcTbl[5]) return 1;					/* 2nd byte range 1 */
// 		if (c >= DbcTbl[6] && c <= DbcTbl[7]) return 1;	/* 2nd byte range 2 */
// 		if (c >= DbcTbl[8] && c <= DbcTbl[9]) return 1;	/* 2nd byte range 3 */
// 	}
// #elif _CODE_PAGE >= 900	/* DBCS fixed code page */
// 	if (c >= DbcTbl[4]) {
// 		if (c <= DbcTbl[5]) return 1;
// 		if (c >= DbcTbl[6] && c <= DbcTbl[7]) return 1;
// 		if (c >= DbcTbl[8] && c <= DbcTbl[9]) return 1;
// 	}
// #else						/* SBCS fixed code page */
// 	if (c != 0) return 0;	/* Always false */
// #endif
// 	return 0;
// }

// Added by TFT -- end

#if _USE_LFN
/* Store a Unicode char in defined API encoding */
static UINT put_utf (	/* Returns number of encoding units written (0:buffer overflow or wrong encoding) */
	DWORD chr,	/* UTF-16 encoded character (Surrogate pair if >=0x10000) */
	TCHAR* buf,	/* Output buffer */
	UINT szb	/* Size of the buffer */
)
{
#if _LFN_UNICODE == 1	/* UTF-16 output */
	WCHAR hs, wc;

	hs = (WCHAR)(chr >> 16);
	wc = (WCHAR)chr;
	if (hs == 0) {	/* Single encoding unit? */
		if (szb < 1 || IsSurrogate(wc)) return 0;	/* Buffer overflow or wrong code? */
		*buf = wc;
		return 1;
	}
	if (szb < 2 || !IsSurrogateH(hs) || !IsSurrogateL(wc)) return 0;	/* Buffer overflow or wrong surrogate? */
	*buf++ = hs;
	*buf++ = wc;
	return 2;

#elif _LFN_UNICODE == 2	/* UTF-8 output */
	DWORD hc;

	if (chr < 0x80) {	/* Single byte code? */
		if (szb < 1) return 0;	/* Buffer overflow? */
		*buf = (TCHAR)chr;
		return 1;
	}
	if (chr < 0x800) {	/* 2-byte sequence? */
		if (szb < 2) return 0;	/* Buffer overflow? */
		*buf++ = (TCHAR)(0xC0 | (chr >> 6 & 0x1F));
		*buf++ = (TCHAR)(0x80 | (chr >> 0 & 0x3F));
		return 2;
	}
	if (chr < 0x10000) {	/* 3-byte sequence? */
		if (szb < 3 || IsSurrogate(chr)) return 0;	/* Buffer overflow or wrong code? */
		*buf++ = (TCHAR)(0xE0 | (chr >> 12 & 0x0F));
		*buf++ = (TCHAR)(0x80 | (chr >> 6 & 0x3F));
		*buf++ = (TCHAR)(0x80 | (chr >> 0 & 0x3F));
		return 3;
	}
	/* 4-byte sequence */
	if (szb < 4) return 0;	/* Buffer overflow? */
	hc = ((chr & 0xFFFF0000) - 0xD8000000) >> 6;	/* Get high 10 bits */
	chr = (chr & 0xFFFF) - 0xDC00;					/* Get low 10 bits */
	if (hc >= 0x100000 || chr >= 0x400) return 0;	/* Wrong surrogate? */
	chr = (hc | chr) + 0x10000;
	*buf++ = (TCHAR)(0xF0 | (chr >> 18 & 0x07));
	*buf++ = (TCHAR)(0x80 | (chr >> 12 & 0x3F));
	*buf++ = (TCHAR)(0x80 | (chr >> 6 & 0x3F));
	*buf++ = (TCHAR)(0x80 | (chr >> 0 & 0x3F));
	return 4;

#elif _LFN_UNICODE == 3	/* UTF-32 output */
	DWORD hc;

	if (szb < 1) return 0;	/* Buffer overflow? */
	if (chr >= 0x10000) {	/* Out of BMP? */
		hc = ((chr & 0xFFFF0000) - 0xD8000000) >> 6;	/* Get high 10 bits */
		chr = (chr & 0xFFFF) - 0xDC00;					/* Get low 10 bits */
		if (hc >= 0x100000 || chr >= 0x400) return 0;	/* Wrong surrogate? */
		chr = (hc | chr) + 0x10000;
	}
	*buf++ = (TCHAR)chr;
	return 1;

#else						/* ANSI/OEM output */
	WCHAR wc;

	//wc = ff_uni2oem(chr, CODEPAGE);
	wc = ff_convert(chr, 0);
	if (wc >= 0x100) {	/* Is this a DBC? */
		if (szb < 2) return 0;
		*buf++ = (char)(wc >> 8);	/* Store DBC 1st byte */
		*buf++ = (TCHAR)wc;			/* Store DBC 2nd byte */
		return 2;
	}
	if (wc == 0 || szb < 1) return 0;	/* Invalid char or buffer overflow? */
	*buf++ = (TCHAR)wc;					/* Store the character */
	return 1;
#endif
}
#endif	/* _USE_LFN */
/*-----------------------------------------------------------------------*/
/* Request/Release grant to access the volume                            */
/*-----------------------------------------------------------------------*/
#if _FS_REENTRANT
static int lock_volume (	/* 1:Ok, 0:timeout */
	FATFS* fs,				/* Filesystem object to lock */
	int syslock				/* System lock required */
)
{
	int rv;


#if _FS_LOCK
	rv = _mutex_take(fs->ldrv);	/* Lock the volume */
	if (rv && syslock) {			/* System lock reqiered? */
		rv = _mutex_take(_VOLUMES);	/* Lock the system */
		if (rv) {
			SysLockVolume = fs->ldrv;
			SysLock = 2;				/* System lock succeeded */
		} else {
			_mutex_give(fs->ldrv);	/* Failed system lock */
		}
	}
#else
	rv = syslock ? _mutex_take(fs->ldrv) : _mutex_take(fs->ldrv);	/* Lock the volume (this is to prevent compiler warning) */
#endif
	return rv;
}


static void unlock_volume (
	FATFS* fs,		/* Filesystem object */
	FRESULT res		/* Result code to be returned */
)
{
	if (fs && res != FR_NOT_ENABLED && res != FR_INVALID_DRIVE && res != FR_TIMEOUT) {
#if _FS_LOCK
		if (SysLock == 2 && SysLockVolume == fs->ldrv) {	/* Unlock system if it has been locked by this task */
			SysLock = 1;
			_mutex_give(_VOLUMES);
		}
#endif
		_mutex_give(fs->ldrv);	/* Unlock the volume */
	}
}

static int lock_fs (
	FATFS* fs		/* File system object */
)
{
	return ff_req_grant(fs->sobj);
}


static void unlock_fs (
	FATFS* fs,		/* File system object */
	FRESULT res		/* Result code to be returned */
)
{
	if (fs &&
		res != FR_NOT_ENABLED &&
		res != FR_INVALID_DRIVE &&
		res != FR_INVALID_OBJECT &&
		res != FR_TIMEOUT) {
		ff_rel_grant(fs->sobj);
	}
}
#endif




/*-----------------------------------------------------------------------*/
/* File lock control functions                                           */
/*-----------------------------------------------------------------------*/
#ifdef _FS_LOCK

static FRESULT chk_share (	/* Check if the file can be accessed */
	DIR_* dp,		/* Directory object pointing the file to be checked */
	int acc			/* Desired access type (0:Read mode open, 1:Write mode open, 2:Delete or rename) */
)
{
	UINT i, be;

	/* Search open object table for the object */
	be = 0;
	for (i = 0; i < _FS_LOCK; i++) {
		if (dp->obj.fs->Files[i].fs) {	/* Existing entry */
			if (dp->obj.fs->Files[i].fs == dp->obj.fs &&	 	/* Check if the object matches with an open object */
				dp->obj.fs->Files[i].clu == dp->obj.sclust &&
				dp->obj.fs->Files[i].idx == dp->dptr) break;
		} else {			/* Blank entry */
			be = 1;
		}
	}
	if (i == _FS_LOCK) {	/* The object has not been opened */
		return (!be && acc != 2) ? FR_TOO_MANY_OPEN_FILES : FR_OK;	/* Is there a blank entry for new object? */
	}

	/* The object was opened. Reject any open against writing file and all write mode open */
	return (acc != 0 || dp->obj.fs->Files[i].ctr == 0x100) ? FR_LOCKED : FR_OK;
}


// TODO: Since this is a void function it uses the static Files, therefore maybe a signature change is in need to use it
// static int enq_share (void)	/* Check if an entry is available for a new object */
// {
// 	UINT i;

// 	for (i = 0; i < _FS_LOCK && dp->obj.fs->Files[i].fs; i++) ;	/* Find a free entry */
// 	return (i == _FS_LOCK) ? 0 : 1;
// }


static UINT inc_share (	/* Increment object open counter and returns its index (0:Internal error) */
	DIR_* dp,	/* Directory object pointing the file to register or increment */
	int acc		/* Desired access (0:Read, 1:Write, 2:Delete/Rename) */
)
{
	UINT i;


	for (i = 0; i < _FS_LOCK; i++) {	/* Find the object */
		if (dp->obj.fs->Files[i].fs == dp->obj.fs
		 && dp->obj.fs->Files[i].clu == dp->obj.sclust
		 && dp->obj.fs->Files[i].idx == dp->dptr) break;
	}

	if (i == _FS_LOCK) {			/* Not opened. Register it as new. */
		for (i = 0; i < _FS_LOCK && dp->obj.fs->Files[i].fs; i++) ;	/* Find a free entry */
		if (i == _FS_LOCK) return 0;	/* No free entry to register (int err) */

		// CHANGES: dp->obj.fs->Files and not Files static data structure
		dp->obj.fs->Files[i].fs = dp->obj.fs;
		dp->obj.fs->Files[i].clu = dp->obj.sclust;
		dp->obj.fs->Files[i].idx = dp->dptr;
		dp->obj.fs->Files[i].ctr = 0;
	}

	if (acc >= 1 && dp->obj.fs->Files[i].ctr) return 0;	/* Access violation (int err) */

	dp->obj.fs->Files[i].ctr = acc ? 0x100 : dp->obj.fs->Files[i].ctr + 1;	/* Set semaphore value */

	return i + 1;	/* Index number origin from 1 */
}


static FRESULT dec_share (	/* Decrement object open counter */
	FATFS*fs,
	UINT i			/* Semaphore index (1..) */
)
{
	UINT n;
	FRESULT res;


	if (--i < _FS_LOCK) {	/* Index number origin from 0 */
		n = fs->Files[i].ctr;
		if (n == 0x100) n = 0;	/* If write mode open, delete the object semaphore */
		if (n > 0) n--;			/* Decrement read mode open count */
		fs->Files[i].ctr = n;
		if (!n) {			/* Delete the object semaphore if open count becomes zero */
			fs->Files[i].fs = 0;	/* Free the entry <<<If this memory write operation is not in atomic, _FS_REENTRANT == 1 and _VOLUMES > 1, there is a potential error in this process >>> */
		}
		res = FR_OK;
	} else {
		res = FR_INT_ERR;		/* Invalid index number */
	}
	return res;
}


static void clear_share (	/* Clear all lock entries of the volume */
	FATFS* fs
)
{
	UINT i;

	for (i = 0; i < _FS_LOCK; i++) {
		if (fs->Files[i].fs == fs) fs->Files[i].fs = 0;
	}
}

static FRESULT chk_lock (	/* Check if the file can be accessed */
	DIR_* dp,		/* Directory object pointing the file to be checked */
	int acc			/* Desired access (0:Read, 1:Write, 2:Delete/Rename) */
)
{
	UINT i, be;

	/* Search file semaphore table */
	for (i = be = 0; i < miosix::FATFS_MAX_OPEN_FILES; i++) {
		if (dp->fs->Files[i].fs) {	/* Existing entry */
			if (dp->obj.fs->Files[i].fs == dp->obj.fs &&	 	/* Check if the object matched with an open object */
				dp->obj.fs->Files[i].clu == dp->obj.sclust &&
				dp->obj.fs->Files[i].idx == dp->index) break;
		} else {			/* Blank entry */
			be = 1;
		}
	}
	if (i == miosix::FATFS_MAX_OPEN_FILES)	/* The object is not opened */
		return (be || acc == 2) ? FR_OK : FR_TOO_MANY_OPEN_FILES;	/* Is there a blank entry for new object? */

	/* The object has been opened. Reject any open against writing file and all write mode open */
	return (acc || dp->obj.fs->Files[i].ctr == 0x100) ? FR_LOCKED : FR_OK;
}


static int enq_lock (FATFS *fs)	/* Check if an entry is available for a new object */
{
	UINT i;

	for (i = 0; i < miosix::FATFS_MAX_OPEN_FILES && fs->Files[i].fs; i++) ;
	return (i == miosix::FATFS_MAX_OPEN_FILES) ? 0 : 1;
}


static UINT inc_lock (	/* Increment object open counter and returns its index (0:Internal error) */
	DIR_* dp,	/* Directory object pointing the file to register or increment */
	int acc		/* Desired access (0:Read, 1:Write, 2:Delete/Rename) */
)
{
	UINT i;


	for (i = 0; i < miosix::FATFS_MAX_OPEN_FILES; i++) {	/* Find the object */
		if (dp->obj.fs->Files[i].fs == dp->obj.fs &&
			dp->obj.fs->Files[i].clu == dp->obj.sclust &&
			dp->obj.fs->Files[i].idx == dp->index) break;
	}

	if (i == miosix::FATFS_MAX_OPEN_FILES) {				/* Not opened. Register it as new. */
		for (i = 0; i < _FS_LOCK && dp->obj.fs->Files[i].fs; i++) ;
		if (i == _FS_LOCK) return 0;	/* No free entry to register (int err) */
		dp->obj.fs->Files[i].clu = dp->obj.sclust;
		dp->obj.fs->Files[i].fs = dp->obj.fs;
		dp->obj.fs->Files[i].idx = dp->index;
		dp->obj.fs->Files[i].ctr = 0;
	}

	if (acc && dp->obj.fs->Files[i].ctr) return 0;	/* Access violation (int err) */

	dp->obj.fs->Files[i].ctr = acc ? 0x100 : dp->obj.fs->Files[i].ctr + 1;	/* Set semaphore value */

	return i + 1;
}


static FRESULT dec_lock (	/* Decrement object open counter */
    FATFS *fs,
	UINT i			/* Semaphore index (1..) */
)
{
	WORD n;
	FRESULT res;


	if (--i < miosix::FATFS_MAX_OPEN_FILES) {	/* Shift index number origin from 0 */
		n = fs->Files[i].ctr;
		if (n == 0x100) n = 0;		/* If write mode open, delete the entry */
		if (n) n--;					/* Decrement read mode open count */
		fs->Files[i].ctr = n;
		if (!n) fs->Files[i].fs = 0;	/* Delete the entry if open count gets zero */
		res = FR_OK;
	} else {
		res = FR_INT_ERR;			/* Invalid index nunber */
	}
	return res;
}


static void clear_lock (	/* Clear lock entries of the volume */
	FATFS *fs
)
{
	UINT i;

	for (i = 0; i < miosix::FATFS_MAX_OPEN_FILES; i++) {
		if (fs->Files[i].fs == fs) fs->Files[i].fs = 0;
	}
}
#endif




/*-----------------------------------------------------------------------*/
/* Move/Flush disk access window in the file system object               */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
static FRESULT sync_window (	/* Returns FR_OK or FR_DISK_ERR */
	FATFS* fs			/* Filesystem object */
)
{
	FRESULT res = FR_OK;


	if (fs->wflag) {	/* Is the disk access window dirty? */
		if (disk_write(fs->drv, fs->win, fs->winsect, 1) == RES_OK) {	/* Write it back into the volume */
			fs->wflag = 0;	/* Clear window dirty flag */
			if (fs->winsect - fs->fatbase < fs->fsize) {	/* Is it in the 1st FAT? */
				if (fs->n_fats == 2) disk_write(fs->drv, fs->win, fs->winsect + fs->fsize, 1);	/* Reflect it to 2nd FAT if needed */
			}
		} else {
			res = FR_DISK_ERR;
		}
	}
	return res;
}
#endif


static FRESULT move_window (
	FATFS* fs,		/* File system object */
	LBA_t sect		/* Sector LBA to make appearance in the fs->win[] */
)
{
	FRESULT res = FR_OK;


	if (sect != fs->winsect) {	/* Window offset changed? */
#if !_FS_READONLY
		res = sync_window(fs);		/* Flush the window */
#endif
		if (res == FR_OK) {			/* Fill sector window with new data */
			if (disk_read(fs->drv, fs->win, sect, 1) != RES_OK) {
				sect = (LBA_t)0 - 1;	/* Invalidate window if read data is not valid */
				res = FR_DISK_ERR;
			}
			fs->winsect = sect;
		}
	}
	return res;
}




/*-----------------------------------------------------------------------*/
/* Synchronize file system and strage device                             */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
static FRESULT sync_fs (	/* FR_OK: successful, FR_DISK_ERR: failed */
	FATFS* fs		/* File system object */
)
{
	FRESULT res;


	res = sync_window(fs);
	if (res == FR_OK) {
		if (fs->fs_type == FS_FAT32 && fs->fsi_flag == 1) {	/* FAT32: Update FSInfo sector if needed */
			/* Create FSInfo structure */
			memset(fs->win, 0, sizeof fs->win);
			st_word(fs->win + BS_55AA, 0xAA55);					/* Boot signature */
			st_dword(fs->win + FSI_LeadSig, 0x41615252);		/* Leading signature */
			st_dword(fs->win + FSI_StrucSig, 0x61417272);		/* Structure signature */
			st_dword(fs->win + FSI_Free_Count, fs->free_clust);	/* Number of free clusters */
			st_dword(fs->win + FSI_Nxt_Free, fs->last_clust);	/* Last allocated culuster */
			fs->winsect = fs->volbase + 1;						/* Write it into the FSInfo sector (Next to VBR) */
			disk_write(fs->drv, fs->win, fs->winsect, 1);
			fs->fsi_flag = 0;
		}
		/* Make sure that no pending write process in the lower layer */
		if (disk_ioctl(fs->drv, CTRL_SYNC, 0) != RES_OK) res = FR_DISK_ERR;
	}

	return res;
}
#endif




/*-----------------------------------------------------------------------*/
/* Get sector# from cluster#                                             */
/*-----------------------------------------------------------------------*/


LBA_t clust2sect (	/* !=0: Sector number, 0: Failed - invalid cluster# */
	FATFS* fs,		/* File system object */
	DWORD clst		/* Cluster# to be converted */
)
{
	clst -= 2;
	if (clst >= (fs->n_fatent - 2)) return 0;		/* Invalid cluster# */
	return clst * (LBA_t)fs->csize + clst;
}




/*-----------------------------------------------------------------------*/
/* FAT access - Read value of a FAT entry                                */
/*-----------------------------------------------------------------------*/


DWORD get_fat (	/* 0xFFFFFFFF:Disk error, 1:Internal error, Else:Cluster status */
	FATFS* fs,	/* File system object */
	DWORD clst	/* Cluster# to get the link information */
)
{
	UINT wc, bc;
	BYTE *p;


	if (clst < 2 || clst >= fs->n_fatent)	/* Check range */
		return 1;

	switch (fs->fs_type) {
	case FS_FAT12 :
		bc = (UINT)clst; bc += bc / 2;
		if (move_window(fs, fs->fatbase + (bc / SS(fs)))) break;
		wc = fs->win[bc % SS(fs)]; bc++;
		if (move_window(fs, fs->fatbase + (bc / SS(fs)))) break;
		wc |= fs->win[bc % SS(fs)] << 8;
		return clst & 1 ? wc >> 4 : (wc & 0xFFF);

	case FS_FAT16 :
		if (move_window(fs, fs->fatbase + (clst / (SS(fs) / 2)))) break;
		p = &fs->win[clst * 2 % SS(fs)];
		return LD_WORD(p);

	case FS_FAT32 :
		if (move_window(fs, fs->fatbase + (clst / (SS(fs) / 4)))) break;
		p = &fs->win[clst * 4 % SS(fs)];
		return LD_DWORD(p) & 0x0FFFFFFF;
	
	#if _FS_EXFAT
		case FS_EXFAT :
			if ((obj->objsize != 0 && obj->sclust != 0) || obj->stat == 0) {	/* Object except root dir must have valid data length */
				DWORD cofs = clst - obj->sclust;	/* Offset from start cluster */
				DWORD clen = (DWORD)((LBA_t)((obj->objsize - 1) / SS(fs)) / fs->csize);	/* Number of clusters - 1 */

				if (obj->stat == 2 && cofs <= clen) {	/* Is it a contiguous chain? */
					val = (cofs == clen) ? 0x7FFFFFFF : clst + 1;	/* No data on the FAT, generate the value */
					break;
				}
				if (obj->stat == 3 && cofs < obj->n_cont) {	/* Is it in the 1st fragment? */
					val = clst + 1; 	/* Generate the value */
					break;
				}
				if (obj->stat != 2) {	/* Get value from FAT if FAT chain is valid */
					if (obj->n_frag != 0) {	/* Is it on the growing edge? */
						val = 0x7FFFFFFF;	/* Generate EOC */
					} else {
						if (move_window(fs, fs->fatbase + (clst / (SS(fs) / 4))) != FR_OK) break;
						val = ld_dword(fs->win + clst * 4 % SS(fs)) & 0x7FFFFFFF;
					}
					break;
				}
			}
			return 1;	/* Internal error */
	#endif
	}

	return 0xFFFFFFFF;	/* An error occurred at the disk I/O layer */
}




/*-----------------------------------------------------------------------*/
/* FAT access - Change value of a FAT entry                              */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY

FRESULT put_fat (
	FATFS* fs,	/* File system object */
	DWORD clst,	/* Cluster# to be changed in range of 2 to fs->n_fatent - 1 */
	DWORD val	/* New value to mark the cluster */
)
{
	UINT bc;
	BYTE *p;
	FRESULT res = FR_INT_ERR;


	if (clst < 2 || clst >= fs->n_fatent)	/* Check range */
		return FR_INT_ERR;

	switch (fs->fs_type) {
	case FS_FAT12:
		bc = (UINT)clst; bc += bc / 2;	/* bc: byte offset of the entry */
		res = move_window(fs, fs->fatbase + (bc / SS(fs)));
		if (res != FR_OK) break;
		p = fs->win + bc++ % SS(fs);
		*p = (clst & 1) ? ((*p & 0x0F) | ((BYTE)val << 4)) : (BYTE)val;	/* Update 1st byte */
		fs->wflag = 1;
		res = move_window(fs, fs->fatbase + (bc / SS(fs)));
		if (res != FR_OK) break;
		p = fs->win + bc % SS(fs);
		*p = (clst & 1) ? (BYTE)(val >> 4) : ((*p & 0xF0) | ((BYTE)(val >> 8) & 0x0F));	/* Update 2nd byte */
		fs->wflag = 1;
		break;

	case FS_FAT16:
		res = move_window(fs, fs->fatbase + (clst / (SS(fs) / 2)));
		if (res != FR_OK) break;
		st_word(fs->win + clst * 2 % SS(fs), (WORD)val);	/* Simple WORD array */
		fs->wflag = 1;
		break;

	case FS_FAT32 :
#if _FS_EXFAT
		case FS_EXFAT:
#endif
		res = move_window(fs, fs->fatbase + (clst / (SS(fs) / 4)));
			if (res != FR_OK) break;
			if (!_FS_EXFAT || fs->fs_type != FS_EXFAT) {
				val = (val & 0x0FFFFFFF) | (ld_dword(fs->win + clst * 4 % SS(fs)) & 0xF0000000);
			}
			st_dword(fs->win + clst * 4 % SS(fs), val);
			fs->wflag = 1;
			break;

	default :
		res = FR_INT_ERR;
	}
	fs->wflag = 1;
	return res;
}
#endif /* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* exFAT: Accessing FAT and Allocation Bitmap                            */
/*-----------------------------------------------------------------------*/

/*--------------------------------------*/
/* Find a contiguous free cluster block */
/*--------------------------------------*/
#if _FS_EXFAT && !_FS_READONLY
static DWORD find_bitmap (	/* 0:Not found, 2..:Cluster block found, 0xFFFFFFFF:Disk error */
	FATFS* fs,	/* Filesystem object */
	DWORD clst,	/* Cluster number to scan from */
	DWORD ncl	/* Number of contiguous clusters to find (1..) */
)
{
	BYTE bm, bv;
	UINT i;
	DWORD val, scl, ctr;


	clst -= 2;	/* The first bit in the bitmap corresponds to cluster #2 */
	if (clst >= fs->n_fatent - 2) clst = 0;
	scl = val = clst; ctr = 0;
	for (;;) {
		if (move_window(fs, fs->bitbase + val / 8 / SS(fs)) != FR_OK) return 0xFFFFFFFF;
		i = val / 8 % SS(fs); bm = 1 << (val % 8);
		do {
			do {
				bv = fs->win[i] & bm; bm <<= 1;		/* Get bit value */
				if (++val >= fs->n_fatent - 2) {	/* Next cluster (with wrap-around) */
					val = 0; bm = 0; i = SS(fs);
				}
				if (bv == 0) {	/* Is it a free cluster? */
					if (++ctr == ncl) return scl + 2;	/* Check if run length is sufficient for required */
				} else {
					scl = val; ctr = 0;		/* Encountered a cluster in-use, restart to scan */
				}
				if (val == clst) return 0;	/* All cluster scanned? */
			} while (bm != 0);
			bm = 1;
		} while (++i < SS(fs));
	}
}


/*----------------------------------------*/
/* Set/Clear a block of allocation bitmap */
/*----------------------------------------*/

static FRESULT change_bitmap (
	FATFS* fs,	/* Filesystem object */
	DWORD clst,	/* Cluster number to change from */
	DWORD ncl,	/* Number of clusters to be changed */
	int bv		/* bit value to be set (0 or 1) */
)
{
	BYTE bm;
	UINT i;
	LBA_t sect;


	clst -= 2;	/* The first bit corresponds to cluster #2 */
	sect = fs->bitbase + clst / 8 / SS(fs);	/* Sector address */
	i = clst / 8 % SS(fs);					/* Byte offset in the sector */
	bm = 1 << (clst % 8);					/* Bit mask in the byte */
	for (;;) {
		if (move_window(fs, sect++) != FR_OK) return FR_DISK_ERR;
		do {
			do {
				if (bv == (int)((fs->win[i] & bm) != 0)) return FR_INT_ERR;	/* Is the bit expected value? */
				fs->win[i] ^= bm;	/* Flip the bit */
				fs->wflag = 1;
				if (--ncl == 0) return FR_OK;	/* All bits processed? */
			} while (bm <<= 1);		/* Next bit */
			bm = 1;
		} while (++i < SS(fs));		/* Next byte */
		i = 0;
	}
}


/*---------------------------------------------*/
/* Fill the first fragment of the FAT chain    */
/*---------------------------------------------*/

static FRESULT fill_first_frag (
	FFOBJID* obj	/* Pointer to the corresponding object */
)
{
	FRESULT res;
	DWORD cl, n;


	if (obj->stat == 3) {	/* Has the object been changed 'fragmented' in this session? */
		for (cl = obj->sclust, n = obj->n_cont; n; cl++, n--) {	/* Create cluster chain on the FAT */
			res = put_fat(obj->fs, cl, cl + 1);
			if (res != FR_OK) return res;
		}
		obj->stat = 0;	/* Change status 'FAT chain is valid' */
	}
	return FR_OK;
}


/*---------------------------------------------*/
/* Fill the last fragment of the FAT chain     */
/*---------------------------------------------*/

static FRESULT fill_last_frag (
	FFOBJID* obj,	/* Pointer to the corresponding object */
	DWORD lcl,		/* Last cluster of the fragment */
	DWORD term		/* Value to set the last FAT entry */
)
{
	FRESULT res;


	while (obj->n_frag > 0) {	/* Create the chain of last fragment */
		res = put_fat(obj->fs, lcl - obj->n_frag + 1, (obj->n_frag > 1) ? lcl - obj->n_frag + 2 : term);
		if (res != FR_OK) return res;
		obj->n_frag--;
	}
	return FR_OK;
}

#endif	/* _FS_EXFAT && !_FS_READONLY */

/*-----------------------------------------------------------------------*/
/* FAT handling - Remove a cluster chain                                 */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
static FRESULT remove_chain (	/* FR_OK(0):succeeded, !=0:error */
	FFOBJID* obj,		/* Corresponding object */
	DWORD clst,			/* Cluster to remove a chain from */
	DWORD pclst			/* Previous cluster of clst (0 if entire chain) */
)
{
	FRESULT res = FR_OK;
	DWORD nxt;
	FATFS *fs = obj->fs;
#if _FS_EXFAT || _USE_TRIM
	DWORD scl = clst, ecl = clst;
#endif
#if _USE_TRIM
	LBA_t rt[2];
#endif

	if (clst < 2 || clst >= fs->n_fatent) return FR_INT_ERR;	/* Check if in valid range */

	/* Mark the previous cluster 'EOC' on the FAT if it exists */
	if (pclst != 0 && (!_FS_EXFAT || fs->fs_type != FS_EXFAT || obj->stat != 2)) {
		res = put_fat(fs, pclst, 0xFFFFFFFF);
		if (res != FR_OK) return res;
	}

	/* Remove the chain */
	do {
		nxt = get_fat(obj->fs, clst);			/* Get cluster status */
		if (nxt == 0) break;				/* Empty cluster? */
		if (nxt == 1) return FR_INT_ERR;	/* Internal error? */
		if (nxt == 0xFFFFFFFF) return FR_DISK_ERR;	/* Disk error? */
		if (!_FS_EXFAT || fs->fs_type != FS_EXFAT) {
			res = put_fat(fs, clst, 0);		/* Mark the cluster 'free' on the FAT */
			if (res != FR_OK) return res;
		}
		if (fs->free_clust < fs->n_fatent - 2) {	/* Update FSINFO */
			fs->free_clust++;
			fs->fsi_flag |= 1;
		}
#if _FS_EXFAT || _USE_TRIM
		if (ecl + 1 == nxt) {	/* Is next cluster contiguous? */
			ecl = nxt;
		} else {				/* End of contiguous cluster block */
#if _FS_EXFAT
			if (fs->fs_type == FS_EXFAT) {
				res = change_bitmap(fs, scl, ecl - scl + 1, 0);	/* Mark the cluster block 'free' on the bitmap */
				if (res != FR_OK) return res;
			}
#endif
#if _USE_TRIM
			rt[0] = clust2sect(fs, scl);					/* Start of data area to be freed */
			rt[1] = clust2sect(fs, ecl) + fs->csize - 1;	/* End of data area to be freed */
			disk_ioctl(fs->pdrv, CTRL_TRIM, rt);		/* Inform storage device that the data in the block may be erased */
#endif
			scl = ecl = nxt;
		}
#endif
		clst = nxt;					/* Next cluster */
	} while (clst < fs->n_fatent);	/* Repeat while not the last link */

#if _FS_EXFAT
	/* Some post processes for chain status */
	if (fs->fs_type == FS_EXFAT) {
		if (pclst == 0) {	/* Has the entire chain been removed? */
			obj->stat = 0;		/* Change the chain status 'initial' */
		} else {
			if (obj->stat == 0) {	/* Is it a fragmented chain from the beginning of this session? */
				clst = obj->sclust;		/* Follow the chain to check if it gets contiguous */
				while (clst != pclst) {
					nxt = get_fat(obj, clst);
					if (nxt < 2) return FR_INT_ERR;
					if (nxt == 0xFFFFFFFF) return FR_DISK_ERR;
					if (nxt != clst + 1) break;	/* Not contiguous? */
					clst++;
				}
				if (clst == pclst) {	/* Has the chain got contiguous again? */
					obj->stat = 2;		/* Change the chain status 'contiguous' */
				}
			} else {
				if (obj->stat == 3 && pclst >= obj->sclust && pclst <= obj->sclust + obj->n_cont) {	/* Was the chain fragmented in this session and got contiguous again? */
					obj->stat = 2;	/* Change the chain status 'contiguous' */
				}
			}
		}
	}
#endif
	return FR_OK;
}




/*-----------------------------------------------------------------------*/
/* FAT handling - Stretch or Create a cluster chain                      */
/*-----------------------------------------------------------------------*/
static DWORD create_chain (	/* 0:No free cluster, 1:Internal error, 0xFFFFFFFF:Disk error, >=2:New cluster# */
	FFOBJID* obj,		/* Corresponding object */
	DWORD clst			/* Cluster# to stretch, 0:Create a new chain */
)
{
	DWORD cs, ncl, scl;
	FRESULT res;
	FATFS *fs = obj->fs;


	if (clst == 0) {	/* Create a new chain */
		scl = fs->last_clust;				/* Suggested cluster to start to find */
		if (scl == 0 || scl >= fs->n_fatent) scl = 1;
	}
	else {				/* Stretch a chain */
		cs = get_fat(obj->fs, clst);			/* Check the cluster status */
		if (cs < 2) return 1;				/* Test for insanity */
		if (cs == 0xFFFFFFFF) return cs;	/* Test for disk error */
		if (cs < fs->n_fatent) return cs;	/* It is already followed by next cluster */
		scl = clst;							/* Cluster to start to find */
	}
	if (fs->free_clust == 0) return 0;		/* No free cluster */

#if _FS_EXFAT
	if (fs->fs_type == FS_EXFAT) {	/* On the exFAT volume */
		ncl = find_bitmap(fs, scl, 1);				/* Find a free cluster */
		if (ncl == 0 || ncl == 0xFFFFFFFF) return ncl;	/* No free cluster or hard error? */
		res = change_bitmap(fs, ncl, 1, 1);			/* Mark the cluster 'in use' */
		if (res == FR_INT_ERR) return 1;
		if (res == FR_DISK_ERR) return 0xFFFFFFFF;
		if (clst == 0) {							/* Is it a new chain? */
			obj->stat = 2;							/* Set status 'contiguous' */
		} else {									/* It is a stretched chain */
			if (obj->stat == 2 && ncl != scl + 1) {	/* Is the chain got fragmented? */
				obj->n_cont = scl - obj->sclust;	/* Set size of the contiguous part */
				obj->stat = 3;						/* Change status 'just fragmented' */
			}
		}
		if (obj->stat != 2) {	/* Is the file non-contiguous? */
			if (ncl == clst + 1) {	/* Is the cluster next to previous one? */
				obj->n_frag = obj->n_frag ? obj->n_frag + 1 : 2;	/* Increment size of last framgent */
			} else {				/* New fragment */
				if (obj->n_frag == 0) obj->n_frag = 1;
				res = fill_last_frag(obj, clst, ncl);	/* Fill last fragment on the FAT and link it to new one */
				if (res == FR_OK) obj->n_frag = 1;
			}
		}
	} else
#endif
	{	/* On the FAT/FAT32 volume */
		ncl = 0;
		if (scl == clst) {						/* Stretching an existing chain? */
			ncl = scl + 1;						/* Test if next cluster is free */
			if (ncl >= fs->n_fatent) ncl = 2;
			cs = get_fat(obj->fs, ncl);				/* Get next cluster status */
			if (cs == 1 || cs == 0xFFFFFFFF) return cs;	/* Test for error */
			if (cs != 0) {						/* Not free? */
				cs = fs->last_clust;				/* Start at suggested cluster if it is valid */
				if (cs >= 2 && cs < fs->n_fatent) scl = cs;
				ncl = 0;
			}
		}
		if (ncl == 0) {	/* The new cluster cannot be contiguous and find another fragment */
			ncl = scl;	/* Start cluster */
			for (;;) {
				ncl++;							/* Next cluster */
				if (ncl >= fs->n_fatent) {		/* Check wrap-around */
					ncl = 2;
					if (ncl > scl) return 0;	/* No free cluster found? */
				}
				cs = get_fat(obj->fs, ncl);			/* Get the cluster status */
				if (cs == 0) break;				/* Found a free cluster? */
				if (cs == 1 || cs == 0xFFFFFFFF) return cs;	/* Test for error */
				if (ncl == scl) return 0;		/* No free cluster found? */
			}
		}
		res = put_fat(fs, ncl, 0xFFFFFFFF);		/* Mark the new cluster 'EOC' */
		if (res == FR_OK && clst != 0) {
			res = put_fat(fs, clst, ncl);		/* Link it from the previous one if needed */
		}
	}

	if (res == FR_OK) {			/* Update FSINFO if function succeeded. */
		fs->last_clust = ncl;
		if (fs->free_clust <= fs->n_fatent - 2) fs->free_clust--;
		fs->fsi_flag |= 1;
	} else {
		ncl = (res == FR_DISK_ERR) ? 0xFFFFFFFF : 1;	/* Failed. Generate error status */
	}

	return ncl;		/* Return new cluster number or error status */
}

#endif /* !_FS_READONLY */ 


/*-----------------------------------------------------------------------*/
/* FAT-LFN: Calculate checksum of an SFN entry                           */
/*-----------------------------------------------------------------------*/
#if _USE_LFN
static BYTE sum_sfn (
	const BYTE* dir		/* Pointer to the SFN entry */
)
{
	BYTE sum = 0;
	UINT n = 11;

	do {
		sum = (sum >> 1) + (sum << 7) + *dir++;
	} while (--n);
	return sum;
}
#endif	/* _USE_LFN */


/*-----------------------------------------------------------------------*/
/* FAT handling - Convert offset into cluster with link map table        */
/*-----------------------------------------------------------------------*/

#if _USE_FASTSEEK
static DWORD clmt_clust (	/* <2:Error, >=2:Cluster number */
	FIL* fp,		/* Pointer to the file object */
	FSIZE_t ofs		/* File offset to be converted to cluster# */
)
{
	DWORD cl, ncl, *tbl;
	FATFS *fs = fp->obj.fs;


	tbl = fp->cltbl + 1;	/* Top of CLMT */
	cl = (DWORD)(ofs / SS(fs) / fs->csize);	/* Cluster order from top of the file */
	for (;;) {
		ncl = *tbl++;			/* Number of cluters in the fragment */
		if (ncl == 0) return 0;	/* End of table? (error) */
		if (cl < ncl) break;	/* In this fragment? */
		cl -= ncl; tbl++;		/* Next fragment */
	}
	return cl + *tbl;	/* Return the cluster number */
}
#endif	/* _USE_FASTSEEK */


/*-----------------------------------------------------------------------*/
/* LFN handling - Test/Pick/Fit an LFN segment from/to directory entry   */
/*-----------------------------------------------------------------------*/
#if _USE_LFN
static const BYTE LfnOfs[] = {1,3,5,7,9,14,16,18,20,22,24,28,30};	/* Offset of LFN characters in the directory entry */

static int cmp_lfn (		/* 1:matched, 0:not matched */
	const WCHAR* lfnbuf,	/* Pointer to the LFN working buffer to be compared */
	BYTE* dir				/* Pointer to the directory entry containing the part of LFN */
)
{
	UINT i, s;
	WCHAR wc, uc;


	if (ld_word(dir + LDIR_FstClusLO) != 0) return 0;	/* Check LDIR_FstClusLO */

	i = ((dir[LDIR_Ord] & 0x3F) - 1) * 13;	/* Offset in the LFN buffer */

	for (wc = 1, s = 0; s < 13; s++) {		/* Process all characters in the entry */
		uc = ld_word(dir + LfnOfs[s]);		/* Pick an LFN character */
		if (wc != 0) {
			if (i >= _MAX_LFN + 1 || ff_wtoupper(uc) != ff_wtoupper(lfnbuf[i++])) {	/* Compare it */
				return 0;					/* Not matched */
			}
			wc = uc;
		} else {
			if (uc != 0xFFFF) return 0;		/* Check filler */
		}
	}

	if ((dir[LDIR_Ord] & LLE) && wc && lfnbuf[i]) return 0;	/* Last segment matched but different length */

	return 1;		/* The part of LFN matched */
}

/*-----------------------------------------------------*/
/* FAT-LFN: Pick a part of file name from an LFN entry */
/*-----------------------------------------------------*/
#if _FS_MINIMIZE <= 1 || _FS_RPATH >= 2 || _USE_LABEL || _FS_EXFAT

static int pick_lfn (			/* 1:Succeeded, 0:Buffer overflow */
	WCHAR* lfnbuf,		/* Pointer to the Unicode-LFN buffer */
	BYTE* dir			/* Pointer to the directory entry */
)
{
	UINT i, s;
	WCHAR wc, uc;


	i = ((dir[LDIR_Ord] & 0x3F) - 1) * 13;	/* Offset in the LFN buffer */

	s = 0; wc = 1;
	do {
		uc = LD_WORD(dir+LfnOfs[s]);		/* Pick an LFN character from the entry */
		if (wc) {	/* Last character has not been processed */
			if (i >= _MAX_LFN) return 0;	/* Buffer overflow? */
			lfnbuf[i++] = wc = uc;			/* Store it */
		} else {
			if (uc != 0xFFFF) return 0;		/* Check filler */
		}
	} while (++s < 13);						/* Read all character in the entry */

	if (dir[LDIR_Ord] & LLE) {				/* Put terminator if it is the last LFN part */
		if (i >= _MAX_LFN) return 0;		/* Buffer overflow? */
		lfnbuf[i] = 0;
	}

	return 1;
}
#endif


/*-----------------------------------------*/
/* FAT-LFN: Create an entry of LFN entries */
/*-----------------------------------------*/

#if !_FS_READONLY

static void put_lfn (
	const WCHAR* lfn,	/* Pointer to the LFN */
	BYTE* dir,			/* Pointer to the LFN entry to be created */
	BYTE ord,			/* LFN order (1-20) */
	BYTE sum			/* Checksum of the corresponding SFN */
)
{
	UINT i, s;
	WCHAR wc;


	dir[LDIR_Chksum] = sum;			/* Set checksum */
	dir[LDIR_Attr] = AM_LFN;		/* Set attribute. LFN entry */
	dir[LDIR_Type] = 0;
	st_word(dir + LDIR_FstClusLO, 0);

	i = (ord - 1) * 13;				/* Get offset in the LFN working buffer */
	s = wc = 0;
	do {
		if (wc != 0xFFFF) wc = lfn[i++];	/* Get an effective character */
		st_word(dir + LfnOfs[s], wc);		/* Put it */
		if (wc == 0) wc = 0xFFFF;			/* Padding characters for following items */
	} while (++s < 13);
	if (wc == 0xFFFF || !lfn[i]) ord |= LLE;	/* Last LFN part is the start of LFN sequence */
	dir[LDIR_Ord] = ord;			/* Set the LFN order */
}

#endif	/* !_FS_READONLY */

// TODO: Is it still needed?
#if !_FS_READONLY
static void fit_lfn (
	const WCHAR* lfnbuf,	/* Pointer to the LFN buffer */
	BYTE* dir,				/* Pointer to the directory entry */
	BYTE ord,				/* LFN order (1-20) */
	BYTE sum				/* SFN sum */
)
{
	UINT i, s;
	WCHAR wc;


	dir[LDIR_Chksum] = sum;			/* Set check sum */
	dir[LDIR_Attr] = AM_LFN;		/* Set attribute. LFN entry */
	dir[LDIR_Type] = 0;
	ST_WORD(dir+LDIR_FstClusLO, 0);

	i = (ord - 1) * 13;				/* Get offset in the LFN buffer */
	s = wc = 0;
	do {
		if (wc != 0xFFFF) wc = lfnbuf[i++];	/* Get an effective character */
		ST_WORD(dir+LfnOfs[s], wc);	/* Put it */
		if (!wc) wc = 0xFFFF;		/* Padding characters following last character */
	} while (++s < 13);
	if (wc == 0xFFFF || !lfnbuf[i]) ord |= LLE;	/* Bottom LFN part is the start of LFN sequence */
	dir[LDIR_Ord] = ord;			/* Set the LFN order */
}

#endif
#endif	/* _USE_LFN */



/*-----------------------------------------------------------------------*/
/* Directory handling - Fill a cluster with zeros                        */
/*-----------------------------------------------------------------------*/

#if !_FS_READONLY
static FRESULT dir_clear (	/* Returns FR_OK or FR_DISK_ERR */
	FATFS *fs,		/* Filesystem object */
	DWORD clst		/* Directory table to clear */
)
{
	LBA_t sect;
	UINT n, szb;
	BYTE *ibuf;


	if (sync_window(fs) != FR_OK) return FR_DISK_ERR;	/* Flush disk access window */
	sect = clust2sect(fs, clst);		/* Top of the cluster */
	fs->winsect = sect;				/* Set window to top of the cluster */
	memset(fs->win, 0, sizeof fs->win);	/* Clear window buffer */
#if _USE_LFN == 3		/* Quick table clear by using multi-secter write */
	/* Allocate a temporary buffer */
	for (szb = ((DWORD)fs->csize * SS(fs) >= MAX_MALLOC) ? MAX_MALLOC : fs->csize * SS(fs), ibuf = 0; szb > SS(fs) && (ibuf = _memalloc(szb)) == 0; szb /= 2) ;
	if (szb > SS(fs)) {		/* Buffer allocated? */
		memset(ibuf, 0, szb);
		szb /= SS(fs);		/* Bytes -> Sectors */
		for (n = 0; n < fs->csize && disk_write(fs->drv, ibuf, sect + n, szb) == RES_OK; n += szb) ;	/* Fill the cluster with 0 */
		_memfree(ibuf);
	} else
#endif
	{
		ibuf = fs->win; szb = 1;	/* Use window buffer (many single-sector writes may take a time) */
		for (n = 0; n < fs->csize && disk_write(fs->drv, ibuf, sect + n, szb) == RES_OK; n += szb) ;	/* Fill the cluster with 0 */
	}
	return (n == fs->csize) ? FR_OK : FR_DISK_ERR;
}
#endif	/* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* Directory handling - Set directory index                              */
/*-----------------------------------------------------------------------*/

static FRESULT dir_sdi (	/* FR_OK(0):succeeded, !=0:error */
	DIR_* dp,		/* Pointer to directory object */
	DWORD idx		/* Offset of directory table */
)
{
	DWORD csz, clst;
	FATFS *fs = dp->obj.fs;


	if (idx >= (DWORD)((_FS_EXFAT && fs->fs_type == FS_EXFAT) ? MAX_DIR_EX : MAX_DIR) || idx % SZ_DIR) {	/* Check range of offset and alignment */
		return FR_INT_ERR;
	}
	dp->dptr = idx;				/* Set current offset */
	clst = dp->obj.sclust;		/* Table start cluster (0:root) */
	if (clst == 0 && fs->fs_type >= FS_FAT32) {	/* Replace cluster# 0 with root cluster# */
		clst = (DWORD)fs->dirbase;
		if (_FS_EXFAT) dp->obj.stat = 0;	/* exFAT: Root dir has an FAT chain */
	}

	if (clst == 0) {	/* Static table (root-directory on the FAT volume) */
		if (idx / SZ_DIR >= fs->n_rootdir) return FR_INT_ERR;	/* Is index out of range? */
		dp->sect = fs->dirbase;

	} else {			/* Dynamic table (sub-directory or root-directory on the FAT32/exFAT volume) */
		csz = (DWORD)fs->csize * SS(fs);	/* Bytes per cluster */
		while (idx >= csz) {				/* Follow cluster chain */
			clst = get_fat(dp->obj.fs, clst);				/* Get next cluster */
			if (clst == 0xFFFFFFFF) return FR_DISK_ERR;	/* Disk error */
			if (clst < 2 || clst >= fs->n_fatent) return FR_INT_ERR;	/* Reached to end of table or internal error */
			idx -= csz;
		}
		dp->sect = clust2sect(fs, clst);
	}
	dp->clust = clst;					/* Current cluster# */
	if (dp->sect == 0) return FR_INT_ERR;
	dp->sect += idx / SS(fs);			/* Sector# of the directory entry */
	dp->dir = fs->win + (idx % SS(fs));	/* Pointer to the entry in the win[] */

	return FR_OK;	/* Seek succeeded */
}



/*-----------------------------------------------------------------------*/
/* Directory handling - Move directory table index next                  */
/*-----------------------------------------------------------------------*/

static FRESULT dir_next (	/* FR_OK(0):succeeded, FR_NO_FILE:End of table, FR_DENIED:Could not stretch */
	DIR_* dp,		/* Pointer to the directory object */
	int stretch		/* 0: Do not stretch table, 1: Stretch table if needed */
)
{
	DWORD ofs, clst;
	FATFS *fs = dp->obj.fs;
	WORD i;


	//ofs = dp->dptr + SZ_DIR;	/* Next entry */
	i = dp->index + 1 ;
	if (i >= (DWORD)((_FS_EXFAT && fs->fs_type == FS_EXFAT) ? MAX_DIR_EX : MAX_DIR)) dp->sect = 0;	/* Disable it if the offset reached the max value */
	if (dp->sect == 0) return FR_NO_FILE;	/* Report EOT if it has been disabled */

	if (i % SS(fs) == 0) {	/* Sector changed? */
		dp->sect++;				/* Next sector */

		if (dp->clust == 0) {	/* Static table */
			if (i / SZ_DIR >= fs->n_rootdir) {	/* Report EOT if it reached end of static table */
				dp->sect = 0; return FR_NO_FILE;
			}
		}
		else {					/* Dynamic table */
			if ((i / SS(fs) & (fs->csize - 1)) == 0) {	/* Cluster changed? */
				clst = get_fat(dp->obj.fs, dp->clust);		/* Get next cluster */
				if (clst <= 1) return FR_INT_ERR;			/* Internal error */
				if (clst == 0xFFFFFFFF) return FR_DISK_ERR;	/* Disk error */
				if (clst >= fs->n_fatent) {					/* It reached end of dynamic table */
#if !_FS_READONLY
					if (!stretch) {						/* If no stretch, report EOT */
						dp->sect = 0; return FR_NO_FILE;
					}
					clst = create_chain(&dp->obj, dp->clust);	/* Allocate a cluster */
					if (clst == 0) return FR_DENIED;			/* No free cluster */
					if (clst == 1) return FR_INT_ERR;			/* Internal error */
					if (clst == 0xFFFFFFFF) return FR_DISK_ERR;	/* Disk error */
					if (dir_clear(fs, clst) != FR_OK) return FR_DISK_ERR;	/* Clean up the stretched table */
					if (_FS_EXFAT) dp->obj.stat |= 4;			/* exFAT: The directory has been stretched */
#else
					if (!stretch) dp->sect = 0;					/* (this line is to suppress compiler warning) */
					dp->sect = 0; return FR_NO_FILE;			/* Report EOT */
#endif
				}
				dp->clust = clst;		/* Initialize data for new cluster */
				dp->sect = clust2sect(fs, clst);
			}
		}
	}
	dp->index = i;						/* Current entry */
	//dp->dir = fs->win + ofs % SS(fs);	/* Pointer to the entry in the win[] */
	dp->dir = dp->obj.fs->win + (i % (SS(dp->obj.fs) / SZ_DIR)) * SZ_DIR;	/* Current entry in the window */


	return FR_OK;
}



/*-----------------------------------------------------------------------*/
/* Directory handling - Find an object in the directory                  */
/*-----------------------------------------------------------------------*/

static FRESULT dir_find (	/* FR_OK(0):succeeded, !=0:error */
	DIR_* dp					/* Pointer to the directory object with the file name */
)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;
	BYTE c;
#if _USE_LFN
	BYTE a, ord, sum;
#endif

	res = dir_sdi(dp, 0);			/* Rewind directory object */
	if (res != FR_OK) return res;
#if _FS_EXFAT
	if (fs->fs_type == FS_EXFAT) {	/* On the exFAT volume */
		BYTE nc;
		UINT di, ni;
		WORD hash = xname_sum(fs->lfnbuf);		/* Hash value of the name to find */

		while ((res = DIR_READ_FILE(dp)) == FR_OK) {	/* Read an item */
#if _MAX_LFN < 255
			if (fs->dirbuf[XDIR_NumName] > _MAX_LFN) continue;		/* Skip comparison if inaccessible object name */
#endif
			if (ld_word(fs->dirbuf + XDIR_NameHash) != hash) continue;	/* Skip comparison if hash mismatched */
			for (nc = fs->dirbuf[XDIR_NumName], di = SZDIRE * 2, ni = 0; nc; nc--, di += 2, ni++) {	/* Compare the name */
				if ((di % SZDIRE) == 0) di += 2;
				if (ff_wtoupper(ld_word(fs->dirbuf + di)) != ff_wtoupper(fs->lfnbuf[ni])) break;
			}
			if (nc == 0 && !fs->lfnbuf[ni]) break;	/* Name matched? */
		}
		return res;
	}
#endif
	/* On the FAT/FAT32 volume */
#if _USE_LFN
	ord = sum = 0xFF; dp->blk_ofs = 0xFFFFFFFF;	/* Reset LFN sequence */
#endif
	do {
		res = move_window(fs, dp->sect);
		if (res != FR_OK) break;
		c = dp->dir[DIR_Name];
		if (c == 0) { res = FR_NO_FILE; break; }	/* Reached to end of table */
#if _USE_LFN		/* LFN configuration */
		dp->obj.attr = a = dp->dir[DIR_Attr] & AM_MASK;
		if (c == DDE || ((a & AM_VOL) && a != AM_LFN)) {	/* An entry without valid data */
			ord = 0xFF; dp->blk_ofs = 0xFFFFFFFF;	/* Reset LFN sequence */
		} else {
			if (a == AM_LFN) {			/* An LFN entry is found */
				if (!(dp->fn[NS] & NS_NOLFN)) {
					if (c & LLE) {		/* Is it start of LFN sequence? */
						sum = dp->dir[LDIR_Chksum];
						c &= (BYTE)~LLE; ord = c;	/* LFN start order */
						dp->blk_ofs = dp->dptr;	/* Start offset of LFN */
					}
					/* Check validity of the LFN entry and compare it with given name */
					ord = (c == ord && sum == dp->dir[LDIR_Chksum] && cmp_lfn(fs->lfnbuf, dp->dir)) ? ord - 1 : 0xFF;
				}
			} else {					/* An SFN entry is found */
				if (ord == 0 && sum == sum_sfn(dp->dir)) break;	/* LFN matched? */
				if (!(dp->fn[NS] & NS_LOSS) && !memcmp(dp->dir, dp->fn, 11)) break;	/* SFN matched? */
				ord = 0xFF; dp->blk_ofs = 0xFFFFFFFF;	/* Reset LFN sequence */
			}
		}
#else		/* Non LFN configuration */
		dp->obj.attr = dp->dir[DIR_Attr] & AM_MASK;
		if (!(dp->dir[DIR_Attr] & AM_VOL) && !memcmp(dp->dir, dp->fn, 11)) break;	/* Is it a valid entry? */
#endif
		res = dir_next(dp, 0);	/* Next entry */
	} while (res == FR_OK);

	return res;
}




/*-----------------------------------------------------------------------*/
/* Directory handling - Reserve directory entry                          */
/*-----------------------------------------------------------------------*/

#if !_FS_READONLY
static FRESULT dir_alloc (	/* FR_OK(0):succeeded, !=0:error */
	DIR_* dp,				/* Pointer to the directory object */
	UINT n_ent				/* Number of contiguous entries to allocate */
)
{
	FRESULT res;
	UINT n;
	FATFS *fs = dp->obj.fs;


	res = dir_sdi(dp, 0);
	if (res == FR_OK) {
		n = 0;
		do {
			res = move_window(fs, dp->sect);
			if (res != FR_OK) break;
#if _FS_EXFAT
			if ((fs->fs_type == FS_EXFAT) ? (int)((dp->dir[XDIR_Type] & 0x80) == 0) : (int)(dp->dir[DIR_Name] == DDE || dp->dir[DIR_Name] == 0)) {	/* Is the entry free? */
#else
			if (dp->dir[DIR_Name] == DDE || dp->dir[DIR_Name] == 0) {	/* Is the entry free? */
#endif
				if (++n == n_ent) break;	/* Is a block of contiguous free entries found? */
			} else {
				n = 0;				/* Not a free entry, restart to search */
			}
			res = dir_next(dp, 1);	/* Next entry with table stretch enabled */
		} while (res == FR_OK);
	}

	if (res == FR_NO_FILE) res = FR_DENIED;	/* No directory entry to allocate */
	return res;
}

#endif	/* !_FS_READONLY */




/*-----------------------------------------------------------------------*/
/* FAT: Directory handling - Load/Store start cluster number             */
/*-----------------------------------------------------------------------*/
static DWORD ld_clust (	/* Returns the top cluster value of the SFN entry */
	FATFS* fs,			/* Pointer to the fs object */
	const BYTE* dir		/* Pointer to the key entry */
)
{
	DWORD cl;

	cl = ld_word(dir + DIR_FstClusLO);
	if (fs->fs_type == FS_FAT32) {
		cl |= (DWORD)ld_word(dir + DIR_FstClusHI) << 16;
	}

	return cl;
}


#if !_FS_READONLY
static void st_clust (
	FATFS* fs,	/* Pointer to the fs object */
	BYTE* dir,	/* Pointer to the key entry */
	DWORD cl	/* Value to be set */
)
{
	st_word(dir + DIR_FstClusLO, (WORD)cl);
	if (fs->fs_type == FS_FAT32) {
		st_word(dir + DIR_FstClusHI, (WORD)(cl >> 16));
	}
}
#endif



/*-----------------------------------------------------------------------*/
/* Create numbered name                                                  */
/*-----------------------------------------------------------------------*/
#if _USE_LFN  && !_FS_READONLY
void gen_numname (
	BYTE* dst,			/* Pointer to generated SFN */
	const BYTE* src,	/* Pointer to source SFN to be modified */
	const WCHAR* lfn,	/* Pointer to LFN */
	WORD seq			/* Sequence number */
)
{
	BYTE ns[8], c;
	UINT i, j;
	WCHAR wc;
	DWORD sreg;


	memcpy(dst, src, 11);	/* Prepare the SFN to be modified */

	if (seq > 5) {	/* In case of many collisions, generate a hash number instead of sequential number */
		
		do seq = (seq >> 1) + (seq << 15) + (WORD)*lfn++; while (*lfn);
	}

	/* Make suffix (~ + hexadecimal) */
	i = 7;
	do {
		c = (BYTE)((seq % 16) + '0'); seq /= 16;
		if (c > '9') c += 7;
		ns[i--] = c;
	} while (i && seq);
	ns[i] = '~';

	/* Append the suffix to the SFN body */
	for (j = 0; j < i && dst[j] != ' '; j++) {	/* Find the offset to append */
		if (IsDBCS1(dst[j])) {	/* To avoid DBC break up */
			if (j == i - 1) break;
			j++;
		}
	}
	do {	/* Append the suffix */
		dst[j++] = (i < 8) ? ns[i++] : ' ';
	} while (j < 8);
}
#endif


/*-----------------------------------------------------------------------*/
/* exFAT: Checksum                                                       */
/*-----------------------------------------------------------------------*/
#if _FS_EXFAT

static WORD xdir_sum (	/* Get checksum of the directoly entry block */
	const BYTE* dir		/* Directory entry block to be calculated */
)
{
	UINT i, szblk;
	WORD sum;


	szblk = (dir[XDIR_NumSec] + 1) * SZ_DIR;	/* Number of bytes of the entry block */
	for (i = sum = 0; i < szblk; i++) {
		if (i == XDIR_SetSum) {	/* Skip 2-byte sum field */
			i++;
		} else {
			sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + dir[i];
		}
	}
	return sum;
}

static WORD xname_sum (	/* Get check sum (to be used as hash) of the file name */
	const WCHAR* name	/* File name to be calculated */
)
{
	WCHAR chr;
	WORD sum = 0;


	while ((chr = *name++) != 0) {
		chr = (WCHAR)_wtoupper(chr);		/* File name needs to be up-case converted */
		sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + (chr & 0xFF);
		sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + (chr >> 8);
	}
	return sum;
}


#if !_FS_READONLY && _USE_MKFS
static DWORD xsum32 (	/* Returns 32-bit checksum */
	BYTE  dat,			/* Byte to be calculated (byte-by-byte processing) */
	DWORD sum			/* Previous sum value */
)
{
	sum = ((sum & 1) ? 0x80000000 : 0) + (sum >> 1) + dat;
	return sum;
}
#endif





/*------------------------------------*/
/* exFAT: Get a directory entry block */
/*------------------------------------*/

static FRESULT load_xdir (	/* FR_INT_ERR: invalid entry block */
	DIR_* dp					/* Reading directory object pointing top of the entry block to load */
)
{
	FRESULT res;
	UINT i, sz_ent;
	BYTE *dirb = dp->obj.fs->dirbuf;	/* Pointer to the on-memory directory entry block 85+C0+C1s */


	/* Load file directory entry */
	res = move_window(dp->obj.fs, dp->sect);
	if (res != FR_OK) return res;
	if (dp->dir[XDIR_Type] != ET_FILEDIR) return FR_INT_ERR;	/* Invalid order */
	memcpy(dirb + 0 * SZ_DIR, dp->dir, SZ_DIR);
	sz_ent = (dirb[XDIR_NumSec] + 1) * SZ_DIR;
	if (sz_ent < 3 * SZ_DIR || sz_ent > 19 * SZ_DIR) return FR_INT_ERR;

	/* Load stream extension entry */
	res = dir_next(dp, 0);
	if (res == FR_NO_FILE) res = FR_INT_ERR;	/* It cannot be */
	if (res != FR_OK) return res;
	res = move_window(dp->obj.fs, dp->sect);
	if (res != FR_OK) return res;
	if (dp->dir[XDIR_Type] != ET_STREAM) return FR_INT_ERR;	/* Invalid order */
	memcpy(dirb + 1 * SZ_DIR, dp->dir, SZ_DIR);
	if (MAXDIRB(dirb[XDIR_NumName]) > sz_ent) return FR_INT_ERR;

	/* Load file name entries */
	i = 2 * SZ_DIR;	/* Name offset to load */
	do {
		res = dir_next(dp, 0);
		if (res == FR_NO_FILE) res = FR_INT_ERR;	/* It cannot be */
		if (res != FR_OK) return res;
		res = move_window(dp->obj.fs, dp->sect);
		if (res != FR_OK) return res;
		if (dp->dir[XDIR_Type] != ET_FILENAME) return FR_INT_ERR;	/* Invalid order */
		if (i < MAXDIRB(_MAX_LFN)) memcpy(dirb + i, dp->dir, SZ_DIR);
	} while ((i += SZ_DIR) < sz_ent);

	/* Sanity check (do it for only accessible object) */
	if (i <= MAXDIRB(_MAX_LFN)) {
		if (xdir_sum(dirb) != ld_word(dirb + XDIR_SetSum)) return FR_INT_ERR;
	}
	return FR_OK;
}


/*------------------------------------------------------------------*/
/* exFAT: Initialize object allocation info with loaded entry block */
/*------------------------------------------------------------------*/

static void init_alloc_info (
	FATFS* fs,		/* Filesystem object */
	FFOBJID* obj	/* Object allocation information to be initialized */
)
{
	obj->sclust = ld_dword(fs->dirbuf + XDIR_FstClus);		/* Start cluster */
	obj->objsize = ld_qword(fs->dirbuf + XDIR_FileSize);	/* Size */
	obj->stat = fs->dirbuf[XDIR_GenFlags] & 2;				/* Allocation status */
	obj->n_frag = 0;										/* No last fragment info */
}



#if !_FS_READONLY || _FS_RPATH != 0
/*------------------------------------------------*/
/* exFAT: Load the object's directory entry block */
/*------------------------------------------------*/

static FRESULT load_obj_xdir (
	DIR_* dp,			/* Blank directory object to be used to access containing directory */
	const FFOBJID* obj	/* Object with its containing directory information */
)
{
	FRESULT res;

	/* Open object containing directory */
	dp->obj.fs = obj->fs;
	dp->obj.sclust = obj->c_scl;
	dp->obj.stat = (BYTE)obj->c_size;
	dp->obj.objsize = obj->c_size & 0xFFFFFF00;
	dp->obj.n_frag = 0;
	dp->blk_ofs = obj->c_ofs;

	res = dir_sdi(dp, dp->blk_ofs);	/* Goto object's entry block */
	if (res == FR_OK) {
		res = load_xdir(dp);		/* Load the object's entry block */
	}
	return res;
}
#endif


#if !_FS_READONLY
/*----------------------------------------*/
/* exFAT: Store the directory entry block */
/*----------------------------------------*/

static FRESULT store_xdir (
	DIR_* dp				/* Pointer to the directory object */
)
{
	FRESULT res;
	UINT nent;
	BYTE *dirb = dp->obj.fs->dirbuf;	/* Pointer to the directory entry block 85+C0+C1s */

	/* Create set sum */
	st_word(dirb + XDIR_SetSum, xdir_sum(dirb));
	nent = dirb[XDIR_NumSec] + 1;

	/* Store the directory entry block to the directory */
	res = dir_sdi(dp, dp->blk_ofs);
	while (res == FR_OK) {
		res = move_window(dp->obj.fs, dp->sect);
		if (res != FR_OK) break;
		memcpy(dp->dir, dirb, SZ_DIR);
		dp->obj.fs->wflag = 1;
		if (--nent == 0) break;
		dirb += SZ_DIR;
		res = dir_next(dp, 0);
	}
	return (res == FR_OK || res == FR_DISK_ERR) ? res : FR_INT_ERR;
}

/*-------------------------------------------*/
/* exFAT: Create a new directory entry block */
/*-------------------------------------------*/

static void create_xdir (
	BYTE* dirb,			/* Pointer to the directory entry block buffer */
	const WCHAR* lfn	/* Pointer to the object name */
)
{
	UINT i;
	BYTE nc1, nlen;
	WCHAR wc;


	/* Create file-directory and stream-extension entry */
	memset(dirb, 0, 2 * SZ_DIR);
	dirb[0 * SZ_DIR + XDIR_Type] = ET_FILEDIR;
	dirb[1 * SZ_DIR + XDIR_Type] = ET_STREAM;

	/* Create file-name entries */
	i = SZ_DIR * 2;	/* Top of file_name entries */
	nlen = nc1 = 0; wc = 1;
	do {
		dirb[i++] = ET_FILENAME; dirb[i++] = 0;
		do {	/* Fill name field */
			if (wc != 0 && (wc = lfn[nlen]) != 0) nlen++;	/* Get a character if exist */
			st_word(dirb + i, wc); 	/* Store it */
			i += 2;
		} while (i % SZ_DIR != 0);
		nc1++;
	} while (lfn[nlen]);	/* Fill next entry if any char follows */

	dirb[XDIR_NumName] = nlen;		/* Set name length */
	dirb[XDIR_NumSec] = 1 + nc1;	/* Set secondary count (C0 + C1s) */
	st_word(dirb + XDIR_NameHash, xname_sum(lfn));	/* Set name hash */
}

#endif	/* !_FS_READONLY */
#endif	/* _FS_EXFAT */


/*-----------------------------------------------------------------------*/
/* Directory handling - Find an object in the directory                  */
/*-----------------------------------------------------------------------*/

#if _FS_MINIMIZE <= 1 || _FS_RPATH >= 2 || _USE_LABEL || _FS_EXFAT
#define DIR_READ_FILE(dp) dir_read(dp, 0)
#define DIR_READ_LABEL(dp) dir_read(dp, 1)

static FRESULT dir_read (
	DIR_* dp,		/* Pointer to the directory object */
	int vol			/* Filtered by 0:file/directory or 1:volume label */
)
{
	FRESULT res = FR_NO_FILE;
	FATFS *fs = dp->obj.fs;
	BYTE attr, b;
#if _USE_LFN
	BYTE ord = 0xFF, sum = 0xFF;
#endif

	while (dp->sect) {
		res = move_window(fs, dp->sect);
		if (res != FR_OK) break;
		b = dp->dir[DIR_Name];	/* Test for the entry type */
		if (b == 0) {
			res = FR_NO_FILE; break; /* Reached to end of the directory */
		}
#if _FS_EXFAT
		if (fs->fs_type == FS_EXFAT) {	/* On the exFAT volume */
			if (_USE_LABEL && vol) {
				if (b == ET_VLABEL) break;	/* Volume label entry? */
			} else {
				if (b == ET_FILEDIR) {		/* Start of the file entry block? */
					dp->blk_ofs = dp->dptr;	/* Get location of the block */
					res = load_xdir(dp);	/* Load the entry block */
					if (res == FR_OK) {
						dp->obj.attr = fs->dirbuf[XDIR_Attr] & AM_MASK;	/* Get attribute */
					}
					break;
				}
			}
		} else
#endif
		{	/* On the FAT/FAT32 volume */
			dp->obj.attr = attr = dp->dir[DIR_Attr] & AM_MASK;	/* Get attribute */
#if _USE_LFN		/* LFN configuration */
			if (b == DDE || b == '.' || (int)((attr & ~AM_ARC) == AM_VOL) != vol) {	/* An entry without valid data */
				ord = 0xFF;
			} else {
				if (attr == AM_LFN) {	/* An LFN entry is found */
					if (b & LLE) {		/* Is it start of an LFN sequence? */
						sum = dp->dir[LDIR_Chksum];
						b &= (BYTE)~LLE; ord = b;
						dp->blk_ofs = dp->dptr;
					}
					/* Check LFN validity and capture it */
					ord = (b == ord && sum == dp->dir[LDIR_Chksum] && pick_lfn(fs->lfnbuf, dp->dir)) ? ord - 1 : 0xFF;
				} else {				/* An SFN entry is found */
					if (ord != 0 || sum != sum_sfn(dp->dir)) {	/* Is there a valid LFN? */
						dp->blk_ofs = 0xFFFFFFFF;	/* It has no LFN. */
					}
					break;
				}
			}
#else		/* Non LFN configuration */
			if (b != DDE && b != '.' && attr != AM_LFN && (int)((attr & ~AM_ARC) == AM_VOL) == vol) {	/* Is it a valid entry? */
				break;
			}
#endif
		}
		res = dir_next(dp, 0);		/* Next entry */
		if (res != FR_OK) break;
	}

	if (res != FR_OK) dp->sect = 0;		/* Terminate the read operation on error or EOT */
	return res;
}

#endif	/* _FS_MINIMIZE <= 1 || _USE_LABEL || _FS_RPATH >= 2 */





/*-----------------------------------------------------------------------*/
/* Register an object to the directory                                   */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
static FRESULT dir_register (	/* FR_OK:Successful, FR_DENIED:No free entry or too many SFN collision, FR_DISK_ERR:Disk error */
	DIR_* dp				/* Target directory with object name to be created */
)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;
#if _USE_LFN	/* LFN configuration */
	WORD n, len, n_ent;
	BYTE sn[12], sum;


	if (dp->fn[NS] & (NS_DOT | NS_NONAME)) return FR_INVALID_NAME;	/* Check name validity */
	for (len = 0; fs->lfnbuf[len]; len++) ;	/* Get lfn length */

#if _FS_EXFAT
	if (fs->fs_type == FS_EXFAT) {	/* On the exFAT volume */
		n_ent = (len + 14) / 15 + 2;	/* Number of entries to allocate (85+C0+C1s) */
		res = dir_alloc(dp, n_ent);		/* Allocate directory entries */
		if (res != FR_OK) return res;
		dp->blk_ofs = dp->dptr - SZ_DIR * (n_ent - 1);	/* Set the allocated entry block offset */

		if (dp->obj.stat & 4) {			/* Has the directory been stretched by new allocation? */
			dp->obj.stat &= ~4;
			res = fill_first_frag(&dp->obj);	/* Fill the first fragment on the FAT if needed */
			if (res != FR_OK) return res;
			res = fill_last_frag(&dp->obj, dp->clust, 0xFFFFFFFF);	/* Fill the last fragment on the FAT if needed */
			if (res != FR_OK) return res;
			if (dp->obj.sclust != 0) {		/* Is it a sub-directory? */
				DIR_ dj;

				res = load_obj_xdir(&dj, &dp->obj);	/* Load the object status */
				if (res != FR_OK) return res;
				dp->obj.objsize += (DWORD)fs->csize * SS(fs);		/* Increase the directory size by cluster size */
				st_qword(fs->dirbuf + XDIR_FileSize, dp->obj.objsize);
				st_qword(fs->dirbuf + XDIR_ValidFileSize, dp->obj.objsize);
				fs->dirbuf[XDIR_GenFlags] = dp->obj.stat | 1;		/* Update the allocation status */
				res = store_xdir(&dj);				/* Store the object status */
				if (res != FR_OK) return res;
			}
		}

		create_xdir(fs->dirbuf, fs->lfnbuf);	/* Create on-memory directory block to be written later */
		return FR_OK;
	}
#endif

	/* On the FAT/FAT32 volume */
	memcpy(sn, dp->fn, 12);
	if (sn[NS] & NS_LOSS) {			/* When LFN is out of 8.3 format, generate a numbered name */
		dp->fn[NS] = NS_NOLFN;		/* Find only SFN */
		for (n = 1; n < 100; n++) {
			gen_numname(dp->fn, sn, fs->lfnbuf, n);	/* Generate a numbered name */
			res = dir_find(dp);				/* Check if the name collides with existing SFN */
			if (res != FR_OK) break;
		}
		if (n == 100) return FR_DENIED;		/* Abort if too many collisions */
		if (res != FR_NO_FILE) return res;	/* Abort if the result is other than 'not collided' */
		dp->fn[NS] = sn[NS];
	}

	/* Create an SFN with/without LFNs. */
	n_ent = (sn[NS] & NS_LFN) ? (len + 12) / 13 + 1 : 1;	/* Number of entries to allocate */
	res = dir_alloc(dp, n_ent);		/* Allocate entries */
	if (res == FR_OK && --n_ent) {	/* Set LFN entry if needed */
		res = dir_sdi(dp, dp->dptr - n_ent * SZ_DIR);
		if (res == FR_OK) {
			sum = sum_sfn(dp->fn);	/* Checksum value of the SFN tied to the LFN */
			do {					/* Store LFN entries in bottom first */
				res = move_window(fs, dp->sect);
				if (res != FR_OK) break;
				put_lfn(fs->lfnbuf, dp->dir, (BYTE)n_ent, sum);
				fs->wflag = 1;
				res = dir_next(dp, 0);	/* Next entry */
			} while (res == FR_OK && --n_ent);
		}
	}

#else	/* Non LFN configuration */
	res = dir_alloc(dp, 1);		/* Allocate an entry for SFN */

#endif

	/* Set SFN entry */
	if (res == FR_OK) {
		res = move_window(fs, dp->sect);
		if (res == FR_OK) {
			memset(dp->dir, 0, SZ_DIR);	/* Clean the entry */
			memcpy(dp->dir + DIR_Name, dp->fn, 11);	/* Put SFN */
#if _USE_LFN
			dp->dir[DIR_NTres] = dp->fn[NS] & (NS_BODY | NS_EXT);	/* Put NT flag */
#endif
			fs->wflag = 1;
		}
	}

	return res;
}

#endif /* !_FS_READONLY */




/*-----------------------------------------------------------------------*/
/* Remove an object from the directory                                   */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY && _FS_MINIMIZE == 0
static FRESULT dir_remove (	/* FR_OK:Succeeded, FR_DISK_ERR:A disk error */
	DIR_* dp					/* Directory object pointing the entry to be removed */
)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;
#if _USE_LFN		/* LFN configuration */
	DWORD last = dp->dptr;

	res = (dp->blk_ofs == 0xFFFFFFFF) ? FR_OK : dir_sdi(dp, dp->blk_ofs);	/* Goto top of the entry block if LFN is exist */
	if (res == FR_OK) {
		do {
			res = move_window(fs, dp->sect);
			if (res != FR_OK) break;
			if (_FS_EXFAT && fs->fs_type == FS_EXFAT) {	/* On the exFAT volume */
				dp->dir[XDIR_Type] &= 0x7F;	/* Clear the entry InUse flag. */
			} else {										/* On the FAT/FAT32 volume */
				dp->dir[DIR_Name] = DDE;	/* Mark the entry 'deleted'. */
			}
			fs->wflag = 1;
			if (dp->dptr >= last) break;	/* If reached last entry then all entries of the object has been deleted. */
			res = dir_next(dp, 0);	/* Next entry */
		} while (res == FR_OK);
		if (res == FR_NO_FILE) res = FR_INT_ERR;
	}

#else			/* Non LFN configuration */

	res = move_window(fs, dp->sect);
	if (res == FR_OK) {
		dp->dir[DIR_Name] = DDE;	/* Mark the entry 'deleted'.*/
		fs->wflag = 1;
	}
#endif

	return res;
}

#endif /* !_FS_READONLY && _FS_MINIMIZE == 0 */


/*-----------------------------------------------------------------------*/
/* Pattern matching                                                      */
/*-----------------------------------------------------------------------*/
#if _USE_FIND && _FS_MINIMIZE <= 1

#define FIND_RECURS	4	/* Maximum number of wildcard terms in the pattern to limit recursion */


static DWORD get_achar (	/* Get a character and advance ptr */
	const TCHAR** ptr		/* Pointer to pointer to the ANSI/OEM or Unicode string */
)
{
	DWORD chr;


#if _USE_LFN && _LFN_UNICODE >= 1	/* Unicode input */
	//chr = tchar2uni(ptr);
	chr = ff_convert(*ptr, 1);
	if (chr == 0xFFFFFFFF) chr = 0;		/* Wrong UTF encoding is recognized as end of the string */
	chr = ff_wtoupper(chr);

#else									/* ANSI/OEM input */
	chr = (BYTE)*(*ptr)++;				/* Get a byte */
	if (IsLower(chr)) chr -= 0x20;		/* To upper ASCII char */
#if _CODE_PAGE == 0
	if (ExCvt && chr >= 0x80) chr = ExCvt[chr - 0x80];	/* To upper SBCS extended char */
#elif _CODE_PAGE < 900
	if (chr >= 0x80) chr = ExCvt[chr - 0x80];	/* To upper SBCS extended char */
#endif
#if _CODE_PAGE == 0 || _CODE_PAGE >= 900
	if (IsDBCS1((BYTE)chr)) {	/* Get DBC 2nd byte if needed */
		chr = IsDBCS2((BYTE)**ptr) ? chr << 8 | (BYTE)*(*ptr)++ : 0;
	}
#endif

#endif
	return chr;
}


static int pattern_match (	/* 0:mismatched, 1:matched */
	const TCHAR* pat,	/* Matching pattern */
	const TCHAR* nam,	/* String to be tested */
	UINT skip,			/* Number of pre-skip chars (number of ?s, b8:infinite (* specified)) */
	UINT recur			/* Recursion count */
)
{
	const TCHAR *pptr;
	const TCHAR *nptr;
	DWORD pchr, nchr;
	UINT sk;


	while ((skip & 0xFF) != 0) {		/* Pre-skip name chars */
		if (!get_achar(&nam)) return 0;	/* Branch mismatched if less name chars */
		skip--;
	}
	if (*pat == 0 && skip) return 1;	/* Matched? (short circuit) */

	do {
		pptr = pat; nptr = nam;			/* Top of pattern and name to match */
		for (;;) {
			if (*pptr == '\?' || *pptr == '*') {	/* Wildcard term? */
				if (recur == 0) return 0;	/* Too many wildcard terms? */
				sk = 0;
				do {	/* Analyze the wildcard term */
					if (*pptr++ == '\?') {
						sk++;
					} else {
						sk |= 0x100;
					}
				} while (*pptr == '\?' || *pptr == '*');
				if (pattern_match(pptr, nptr, sk, recur - 1)) return 1;	/* Test new branch (recursive call) */
				nchr = *nptr; break;	/* Branch mismatched */
			}
			pchr = get_achar(&pptr);	/* Get a pattern char */
			nchr = get_achar(&nptr);	/* Get a name char */
			if (pchr != nchr) break;	/* Branch mismatched? */
			if (pchr == 0) return 1;	/* Branch matched? (matched at end of both strings) */
		}
		get_achar(&nam);			/* nam++ */
	} while (skip && nchr);		/* Retry until end of name if infinite search is specified */

	return 0;
}

#endif /* _USE_FIND && _FS_MINIMIZE <= 1 */


/*-----------------------------------------------------------------------*/
/* Pick a segment and create the object name in directory form           */
/*-----------------------------------------------------------------------*/

static FRESULT create_name (	/* FR_OK: successful, FR_INVALID_NAME: could not create */
	DIR_* dp,			/* Pointer to the directory object */
	const /*TCHAR*/char **path	/* Pointer to pointer to the segment in the path string */
)
{
#if _USE_LFN	/* LFN configuration */
	BYTE b, cf;
	WCHAR *lfn;
    char32_t w;
	UINT i, ni, si, di;
	const /*TCHAR*/char *p;
	DWORD uc;

	/* Create LFN into LFN working buffer */
	//p = *path; lfn = dp->obj.fs->lfnbuf; di = 0;
	//for (;;) {
//		uc = tchar2uni(&p);			/* Get a character */
	//	if (uc == 0xFFFFFFFF) return FR_INVALID_NAME;		/* Invalid code or UTF decode error */
	//	if (uc >= 0x10000) lfn[di++] = (WCHAR)(uc >> 16);	/* Store high surrogate if needed */
	//	wc = (WCHAR)uc;
	//	if (wc < ' ' || IsSeparator(wc)) break;	/* Break if end of the path or a separator is found */
	//	if (wc < 0x80 && strchr("*:<>|\"\?\x7F", (int)wc)) return FR_INVALID_NAME;	/* Reject illegal characters for LFN */
	//	if (di >= _MAX_LFN) return FR_INVALID_NAME;	/* Reject too long name */
	//	lfn[di++] = wc;				/* Store the Unicode character */
	//}
	//if (wc < ' ') {				/* Stopped at end of the path? */
	//	cf = NS_LAST;			/* Last segment */
	//} else {					/* Stopped at a separator */
	//	while (IsSeparator(*p)) p++;	/* Skip duplicated separators if exist */
	//		cf = 0;					/* Next segment may follow */
	//	if (IsTerminator(*p)) cf = NS_LAST;	/* Ignore terminating separator */
	//}
	//*path = p;					/* Return pointer to the next segment */
	

#if _FS_RPATH != 0
	if ((di == 1 && lfn[di - 1] == '.') ||
		(di == 2 && lfn[di - 1] == '.' && lfn[di - 2] == '.')) {	/* Is this segment a dot name? */
		lfn[di] = 0;
		for (i = 0; i < 11; i++) {	/* Create dot name for SFN entry */
			dp->fn[i] = (i < di) ? '.' : ' ';
		}
		dp->fn[i] = cf | NS_DOT;	/* This is a dot entry */
		return FR_OK;
	}
#endif
	while (di) {					/* Snip off trailing spaces and dots if exist */
		w = lfn[di - 1];
		if (w != ' ' && w != '.') break;
		di--;
	}
	lfn[di] = 0;							/* LFN is created into the working buffer */
	if (di == 0) return FR_INVALID_NAME;	/* Reject null name */

	/* Create SFN in directory form */
	for (si = 0; lfn[si] == ' '; si++) ;	/* Remove leading spaces */
	if (si > 0 || lfn[si] == '.') cf |= NS_LOSS | NS_LFN;	/* Is there any leading space or dot? */
	while (di > 0 && lfn[di - 1] != '.') di--;	/* Find last dot (di<=si: no extension) */

	memset(dp->fn, ' ', 11);
	i = b = 0; ni = 8;
	for (;;) {
		w = lfn[si++];					/* Get an LFN character */
		if (w == 0) break;				/* Break on end of the LFN */
		if (w == ' ' || (w == '.' && si != di)) {	/* Remove embedded spaces and dots */
			cf |= NS_LOSS | NS_LFN;
			continue;
		}

		if (i >= ni || si == di) {		/* End of field? */
			if (ni == 11) {				/* Name extension overflow? */
				cf |= NS_LOSS | NS_LFN;
				break;
			}
			if (si != di) cf |= NS_LOSS | NS_LFN;	/* Name body overflow? */
			if (si > di) break;						/* No name extension? */
			si = di; i = 8; ni = 11; b <<= 2;		/* Enter name extension */
			continue;
		}

		if (w >= 0x80) {	/* Is this an extended character? */
			cf |= NS_LFN;	/* LFN entry needs to be created */
#if _CODE_PAGE == 0
			if (ExCvt) {	/* In SBCS cfg */
				//w = _uni2oem(w, CODEPAGE);			/* Unicode ==> ANSI/OEM code */
				w = ff_convert(w, 0);
				if (w & 0x80) w = ExCvt[w & 0x7F];	/* Convert extended character to upper (SBCS) */
			} else {		/* In DBCS cfg */
				//w = _uni2oem(_wtoupper(w), CODEPAGE);	/* Unicode ==> Up-convert ==> ANSI/OEM code */
				w = ff_convert(ff_wtoupper(w), 0);
			}
#elif _CODE_PAGE < 900	/* In SBCS cfg */
			//w = _uni2oem(w, CODEPAGE);			/* Unicode ==> ANSI/OEM code */
			w = ff_convert(w, 0);
			if (w & 0x80) w = ExCvt[w & 0x7F];	/* Convert extended character to upper (SBCS) */
#else						/* In DBCS cfg */
			//w = _uni2oem(_wtoupper(w), CODEPAGE);	/* Unicode ==> Up-convert ==> ANSI/OEM code */
			w = ff_convert(ff_wtoupper(w), 0);
#endif
		}

		if (w >= 0x100) {				/* Is this a DBC? */
			if (i >= ni - 1) {			/* Field overflow? */
				cf |= NS_LOSS | NS_LFN;
				i = ni; continue;		/* Next field */
			}
			dp->fn[i++] = (BYTE)(w >> 8);	/* Put 1st byte */
		} else {						/* SBC */
			if (w == 0 || strchr("+,;=[]", (int)w)) {	/* Replace illegal characters for SFN */
				w = '_'; cf |= NS_LOSS | NS_LFN;/* Lossy conversion */
			} else {
				if (IsUpper(w)) {		/* ASCII upper case? */
					b |= 2;
				}
				if (IsLower(w)) {		/* ASCII lower case? */
					b |= 1; w -= 0x20;
				}
			}
		}
		dp->fn[i++] = (BYTE)w;
	}

	if (dp->fn[0] == DDE) dp->fn[0] = NDDE;	/* If the first character collides with DDE, replace it with NDDE */

	if (ni == 8) b <<= 2;				/* Shift capital flags if no extension */
	if ((b & 0x0C) == 0x0C || (b & 0x03) == 0x03) cf |= NS_LFN;	/* LFN entry needs to be created if composite capitals */
	if (!(cf & NS_LFN)) {				/* When LFN is in 8.3 format without extended character, NT flags are created */
		if (b & 0x01) cf |= NS_EXT;		/* NT flag (Extension has small capital letters only) */
		if (b & 0x04) cf |= NS_BODY;	/* NT flag (Body has small capital letters only) */
	}

	dp->fn[NS] = cf;	/* SFN is created into dp->fn[] */

	return FR_OK;


#else	/* _USE_LFN : Non-LFN configuration */
	BYTE c, d;
	BYTE *sfn;
	UINT ni, si, i;
	const char *p;

	/* Create file name in directory form */
	p = *path; sfn = dp->fn;
	memset(sfn, ' ', 11);
	si = i = 0; ni = 8;
#if _FS_RPATH != 0
	if (p[si] == '.') { /* Is this a dot entry? */
		for (;;) {
			c = (BYTE)p[si++];
			if (c != '.' || si >= 3) break;
			sfn[i++] = c;
		}
		if (!IsSeparator(c) && c > ' ') return FR_INVALID_NAME;
		*path = p + si;					/* Return pointer to the next segment */
		sfn[NS] = (c <= ' ') ? NS_LAST | NS_DOT : NS_DOT;	/* Set last segment flag if end of the path */
		return FR_OK;
	}
#endif
	for (;;) {
		c = (BYTE)p[si++];				/* Get a byte */
		if (c <= ' ') break; 			/* Break if end of the path name */
		if (IsSeparator(c)) {			/* Break if a separator is found */
			while (IsSeparator(p[si])) si++;	/* Skip duplicated separator if exist */
			break;
		}
		if (c == '.' || i >= ni) {		/* End of body or field overflow? */
			if (ni == 11 || c != '.') return FR_INVALID_NAME;	/* Field overflow or invalid dot? */
			i = 8; ni = 11;				/* Enter file extension field */
			continue;
		}
#if _CODE_PAGE == 0
		if (ExCvt && c >= 0x80) {		/* Is SBC extended character? */
			c = ExCvt[c & 0x7F];		/* To upper SBC extended character */
		}
#elif _CODE_PAGE < 900
		if (c >= 0x80) {				/* Is SBC extended character? */
			c = ExCvt[c & 0x7F];		/* To upper SBC extended character */
		}
#endif
		if (IsDBCS1(c)) {				/* Check if it is a DBC 1st byte */
			d = (BYTE)p[si++];			/* Get 2nd byte */
			if (!IsDBCS2(d) || i >= ni - 1) return FR_INVALID_NAME;	/* Reject invalid DBC */
			sfn[i++] = c;
			sfn[i++] = d;
		} else {						/* SBC */
			if (strchr("*+,:;<=>[]|\"\?\x7F", (int)c)) return FR_INVALID_NAME;	/* Reject illegal chrs for SFN */
			if (IsLower(c)) c -= 0x20;	/* To upper */
			sfn[i++] = c;
		}
	}
	*path = &p[si];						/* Return pointer to the next segment */
	if (i == 0) return FR_INVALID_NAME;	/* Reject nul string */

	if (sfn[0] == DDE) sfn[0] = NDDE;	/* If the first character collides with DDE, replace it with NDDE */
	sfn[NS] = (c <= ' ' || p[si] <= ' ') ? NS_LAST : 0;	/* Set last segment flag if end of the path */

	return FR_OK;
#endif /* _USE_LFN */
}




/*-----------------------------------------------------------------------*/
/* Get file information from directory entry                             */
/*-----------------------------------------------------------------------*/
#if _FS_MINIMIZE <= 1 || _FS_RPATH >= 2
static void get_fileinfo (
	DIR_* dp,			/* Pointer to the directory object */
	FILINFO* fno		/* Pointer to the file information to be filled */
)
{
	UINT si, di;
	UINT i;
#if _USE_LFN
	BYTE lcf;
	WCHAR wc, hs;
	FATFS *fs = dp->obj.fs;
	UINT nw;
#else
	TCHAR c;
#endif


	fno->fname[0] = 0;			/* Invaidate file info */
	if (dp->sect == 0) return;	/* Exit if read pointer has reached end of directory */

#if _USE_LFN		/* LFN configuration */
#if _FS_EXFAT
	if (fs->fs_type == FS_EXFAT) {	/* exFAT volume */
		UINT nc = 0;

		si = SZ_DIR * 2; di = 0;	/* 1st C1 entry in the entry block */
		hs = 0;
		while (nc < fs->dirbuf[XDIR_NumName]) {
			if (si >= MAXDIRB(_MAX_LFN)) {	/* Truncated directory block? */
				di = 0; break;
			}
			if ((si % SZ_DIR) == 0) si += 2;	/* Skip entry type field */
			wc = ld_word(fs->dirbuf + si); si += 2; nc++;	/* Get a character */
			if (hs == 0 && IsSurrogate(wc)) {	/* Is it a surrogate? */
				hs = wc; continue;				/* Get low surrogate */
			}
			
			//nw = put_utf((DWORD)hs << 16 | wc, &fno->fname[di], _LFN_BUF - di);	/* Store it in API encoding */
			std::pair<miosix::Unicode::error,int> result;
            result = miosix::Unicode::putUtf8(&fno->fname[di], (DWORD)hs << 16, _LFN_BUF - di);
            if(result.first != miosix::Unicode::OK) { di = 0; break; }	/* No LFN if buffer overflow */
			
			di += nw;
			hs = 0;
		}
		if (hs != 0) di = 0;					/* Broken surrogate pair? */
		if (di == 0) fno->fname[di++] = '\?';	/* Inaccessible object name? */
		fno->fname[di] = 0;						/* Terminate the name */
		fno->altname[0] = 0;					/* exFAT does not support SFN */

		fno->fattrib = fs->dirbuf[XDIR_Attr] & AM_MASKX;		/* Attribute */
		fno->fsize = (fno->fattrib & AM_DIR) ? 0 : ld_qword(fs->dirbuf + XDIR_FileSize);	/* Size */
		fno->ftime = ld_word(fs->dirbuf + XDIR_ModTime + 0);	/* Time */
		fno->fdate = ld_word(fs->dirbuf + XDIR_ModTime + 2);	/* Date */
		return;
	} else
#endif
	{	/* FAT/FAT32 volume */
		if (dp->blk_ofs != 0xFFFFFFFF) {	/* Get LFN if available */
			si = di = 0;
			hs = 0;
			while (fs->lfnbuf[si] != 0) {
				wc = fs->lfnbuf[si++];		/* Get an LFN character (UTF-16) */
				if (hs == 0 && IsSurrogate(wc)) {	/* Is it a surrogate? */
					hs = wc; continue;		/* Get low surrogate */
				}

				// std::pair<miosix::Unicode::error,int> result;
                // result = miosix::Unicode::putUtf8(&fno->fname[di], (DWORD)hs << 16, _LFN_BUF - di);
                // if(result.first != miosix::Unicode::OK) { di = 0; break; }	/* No LFN if buffer overflow */
				
				nw = put_utf((DWORD)hs << 16 | wc, &fno->fname[di], _LFN_BUF - di);	/* Store it in API encoding */
				if (nw == 0) {				/* Buffer overflow or wrong char? */
					di = 0; break;
				}				
				di += nw;
				hs = 0;
			}
			if (hs != 0) di = 0;	/* Broken surrogate pair? */
			fno->fname[di] = 0;		/* Terminate the LFN (null string means LFN is invalid) */
		}
	}

	si = di = 0;
	while (si < 11) {		/* Get SFN from SFN entry */
		wc = dp->dir[si++];			/* Get a char */
		if (wc == ' ') continue;	/* Skip padding spaces */
		if (wc == NDDE) wc = DDE;	/* Restore replaced DDE character */
		if (si == 9 && di < _SFN_BUF) fno->altname[di++] = '.';	/* Insert a . if extension is exist */
#if _LFN_UNICODE >= 1	/* Unicode output */
		if (IsDBCS1((BYTE)wc) && si != 8 && si != 11 && IsDBCS2(dp->dir[si])) {	/* Make a DBC if needed */
			wc = wc << 8 | dp->dir[si++];
		}
		//wc = ff_oem2uni(wc, CODEPAGE);		/* ANSI/OEM -> Unicode */
		wc = ff_convert(wc, 1);
		if (wc == 0) {				/* Wrong char in the current code page? */
			di = 0; break;
		}
		nw = put_utf(wc, &fno->altname[di], _SFN_BUF - di);	/* Store it in API encoding */
		if (nw == 0) {				/* Buffer overflow? */
			di = 0; break;
		}
		di += nw;
#else					/* ANSI/OEM output */
		fno->altname[di++] = (TCHAR)wc;	/* Store it without any conversion */
#endif
	}
	fno->altname[di] = 0;	/* Terminate the SFN  (null string means SFN is invalid) */

	if (fno->fname[0] == 0) {	/* If LFN is invalid, altname[] needs to be copied to fname[] */
		if (di == 0) {	/* If LFN and SFN both are invalid, this object is inaccessible */
			fno->fname[di++] = '\?';
		} else {
			for (si = di = 0, lcf = NS_BODY; fno->altname[si]; si++, di++) {	/* Copy altname[] to fname[] with case information */
				wc = (WCHAR)fno->altname[si];
				if (wc == '.') lcf = NS_EXT;
				if (IsUpper(wc) && (dp->dir[DIR_NTres] & lcf)) wc += 0x20;
				fno->fname[di] = (TCHAR)wc;
			}
		}
		fno->fname[di] = 0;	/* Terminate the LFN */
		if (!dp->dir[DIR_NTres]) fno->altname[0] = 0;	/* Altname is not needed if neither LFN nor case info is exist. */
	}

#else	/* Non-LFN configuration */
	si = di = 0;
	while (si < 11) {		/* Copy name body and extension */
		c = (TCHAR)dp->dir[si++];
		if (c == ' ') continue;		/* Skip padding spaces */
		if (c == NDDE) c = DDE;	/* Restore replaced DDE character */
		if (si == 9) fno->fname[di++] = '.';/* Insert a . if extension is exist */
		fno->fname[di++] = c;
	}
	fno->fname[di] = 0;		/* Terminate the SFN */
#endif

	fno->fattrib = dp->dir[DIR_Attr] & AM_MASK;			/* Attribute */
	fno->fsize = ld_dword(dp->dir + DIR_FileSize);		/* Size */
	fno->ftime = ld_word(dp->dir + DIR_ModTime + 0);	/* Time */
	fno->fdate = ld_word(dp->dir + DIR_ModTime + 2);	/* Date */
	fno->inode=INODE(dp);


	#if _USE_LFN
	if (fno->lfname) {
		WCHAR w, *lfn;
        char *pp;

		i = 0; pp = fno->lfname;
		if (dp->sect && fno->lfsize && dp->lfn_idx != 0xFFFF) {	/* Get LFN if available */
			lfn = dp->lfn;
			while ((w = *lfn++) != 0) {		/* Get an LFN character */
#if !_LFN_UNICODE
                #error "unsupported"
				w = ff_convert(w, 0);		/* Unicode -> OEM */
				if (!w) { i = 0; break; }	/* No LFN if it could not be converted */
				if (_DF1S && w >= 0x100)	/* Put 1st byte if it is a DBC (always false on SBCS cfg) */
					pp[i++] = (TCHAR)(w >> 8);
#endif
                std::pair<miosix::Unicode::error,int> result;
                result = miosix::Unicode::putUtf8(&pp[i],w,fno->lfsize - 1 - i);
                if(result.first != miosix::Unicode::OK) { i = 0; break; }	/* No LFN if buffer overflow */
				i += result.second;
			}
		}
		pp[i] = 0;	/* Terminate LFN string by a \0 */
        
        //By TFT: unlike plain FatFs we always want to fill lfname with an
        //utf8-encoded file name. If there is no lfn (or is too long or
        //otherwise broken), then take the sfn (which is utf16) and copy it to
        //lfname, converting it to utf8 in the process
        if(pp[0]==0)
        {
            lfn = fno->fname;
            i = 0;
            while ((w = *lfn++) != 0)
            {
                std::pair<miosix::Unicode::error,int> result;
                result = miosix::Unicode::putUtf8(&pp[i],w,fno->lfsize - 1 - i);
                if(result.first != miosix::Unicode::OK) { i = 0; break; }	/* No LFN if buffer overflow */
				i += result.second;
            }
            pp[i] = 0;	/* Terminate LFN string by a \0 */
        }
	}
#endif
}
#endif /* _FS_MINIMIZE <= 1 || _FS_RPATH >= 2 */



/*-----------------------------------------------------------------------*/
/* Pattern matching                                                      */
/*-----------------------------------------------------------------------*/
#if _USE_FIND && _FS_MINIMIZE <= 1
#define FIND_RECURS	4	/* Maximum number of wildcard terms in the pattern to limit recursion */


static DWORD get_achar (	/* Get a character and advance ptr */
	const TCHAR** ptr		/* Pointer to pointer to the ANSI/OEM or Unicode string */
)
{
	DWORD chr;


#if _USE_LFN && _LFN_UNICODE >= 1	/* Unicode input */
	//chr = tchar2uni(ptr);
	chr = ff_convert(*ptr, 1);
	if (chr == 0xFFFFFFFF) chr = 0;		/* Wrong UTF encoding is recognized as end of the string */
	chr = ff_wtoupper(chr);

#else									/* ANSI/OEM input */
	chr = (BYTE)*(*ptr)++;				/* Get a byte */
	if (IsLower(chr)) chr -= 0x20;		/* To upper ASCII char */
#if _CODE_PAGE == 0
	if (ExCvt && chr >= 0x80) chr = ExCvt[chr - 0x80];	/* To upper SBCS extended char */
#elif _CODE_PAGE < 900
	if (chr >= 0x80) chr = ExCvt[chr - 0x80];	/* To upper SBCS extended char */
#endif
#if _CODE_PAGE == 0 || _CODE_PAGE >= 900
	if (IsDBCS1((BYTE)chr)) {	/* Get DBC 2nd byte if needed */
		chr = dbc_2nd((BYTE)**ptr) ? chr << 8 | (BYTE)*(*ptr)++ : 0;
	}
#endif

#endif
	return chr;
}


static int pattern_match (	/* 0:mismatched, 1:matched */
	const TCHAR* pat,	/* Matching pattern */
	const TCHAR* nam,	/* String to be tested */
	UINT skip,			/* Number of pre-skip chars (number of ?s, b8:infinite (* specified)) */
	UINT recur			/* Recursion count */
)
{
	const TCHAR *pptr;
	const TCHAR *nptr;
	DWORD pchr, nchr;
	UINT sk;


	while ((skip & 0xFF) != 0) {		/* Pre-skip name chars */
		if (!get_achar(&nam)) return 0;	/* Branch mismatched if less name chars */
		skip--;
	}
	if (*pat == 0 && skip) return 1;	/* Matched? (short circuit) */

	do {
		pptr = pat; nptr = nam;			/* Top of pattern and name to match */
		for (;;) {
			if (*pptr == '\?' || *pptr == '*') {	/* Wildcard term? */
				if (recur == 0) return 0;	/* Too many wildcard terms? */
				sk = 0;
				do {	/* Analyze the wildcard term */
					if (*pptr++ == '\?') {
						sk++;
					} else {
						sk |= 0x100;
					}
				} while (*pptr == '\?' || *pptr == '*');
				if (pattern_match(pptr, nptr, sk, recur - 1)) return 1;	/* Test new branch (recursive call) */
				nchr = *nptr; break;	/* Branch mismatched */
			}
			pchr = get_achar(&pptr);	/* Get a pattern char */
			nchr = get_achar(&nptr);	/* Get a name char */
			if (pchr != nchr) break;	/* Branch mismatched? */
			if (pchr == 0) return 1;	/* Branch matched? (matched at end of both strings) */
		}
		get_achar(&nam);			/* nam++ */
	} while (skip && nchr);		/* Retry until end of name if infinite search is specified */

	return 0;
}

#endif /* _USE_FIND && _FS_MINIMIZE <= 1 */





/*-----------------------------------------------------------------------*/
/* Get logical drive number from path name                               */
/*-----------------------------------------------------------------------*/

// TODO: Such code was commented due to Miosix managing such aspect?

// static int get_ldnumber (	/* Returns logical drive number (-1:invalid drive number or null pointer) */
// 	const TCHAR** path		/* Pointer to pointer to the path name */
// )
// {
// 	const TCHAR *tp;
// 	const TCHAR *tt;
// 	TCHAR tc;
// 	int i;
// 	int vol = -1;
// #if _STR_VOLUME_ID		/* Find string volume ID */
// 	const char *sp;
// 	char c;
// #endif

// 	tt = tp = *path;
// 	if (!tp) return vol;	/* Invalid path name? */
// 	do {					/* Find a colon in the path */
// 		tc = *tt++;
// 	} while (!IsTerminator(tc) && tc != ':');

// 	if (tc == ':') {	/* DOS/Windows style volume ID? */
// 		i = _VOLUMES;
// 		if (IsDigit(*tp) && tp + 2 == tt) {	/* Is there a numeric volume ID + colon? */
// 			i = (int)*tp - '0';	/* Get the LD number */
// 		}
// #if _STR_VOLUME_ID == 1	/* Arbitrary string is enabled */
// 		else {
// 			i = 0;
// 			do {
// 				sp = VolumeStr[i]; tp = *path;	/* This string volume ID and path name */
// 				do {	/* Compare the volume ID with path name */
// 					c = *sp++; tc = *tp++;
// 					if (IsLower(c)) c -= 0x20;
// 					if (IsLower(tc)) tc -= 0x20;
// 				} while (c && (TCHAR)c == tc);
// 			} while ((c || tp != tt) && ++i < _VOLUMES);	/* Repeat for each id until pattern match */
// 		}
// #endif
// 		if (i < _VOLUMES) {	/* If a volume ID is found, get the drive number and strip it */
// 			vol = i;		/* Drive number */
// 			*path = tt;		/* Snip the drive prefix off */
// 		}
// 		return vol;
// 	}
// #if _STR_VOLUME_ID == 2		/* Unix style volume ID is enabled */
// 	if (*tp == '/') {			/* Is there a volume ID? */
// 		while (*(tp + 1) == '/') tp++;	/* Skip duplicated separator */
// 		i = 0;
// 		do {
// 			tt = tp; sp = VolumeStr[i]; /* Path name and this string volume ID */
// 			do {	/* Compare the volume ID with path name */
// 				c = *sp++; tc = *(++tt);
// 				if (IsLower(c)) c -= 0x20;
// 				if (IsLower(tc)) tc -= 0x20;
// 			} while (c && (TCHAR)c == tc);
// 		} while ((c || (tc != '/' && !IsTerminator(tc))) && ++i < _VOLUMES);	/* Repeat for each ID until pattern match */
// 		if (i < _VOLUMES) {	/* If a volume ID is found, get the drive number and strip it */
// 			vol = i;		/* Drive number */
// 			*path = tt;		/* Snip the drive prefix off */
// 		}
// 		return vol;
// 	}
// #endif
// 	/* No drive prefix is found */
// #if _FS_RPATH != 0
// 	vol = CurrVol;	/* Default drive is current drive */
// #else
// 	vol = 0;		/* Default drive is 0 */
// #endif
// 	return vol;		/* Return the default drive */
// }




/*-----------------------------------------------------------------------*/
/* Follow a file path                                                    */
/*-----------------------------------------------------------------------*/

static FRESULT follow_path (	/* FR_OK(0): successful, !=0: error code */
	DIR_* dp,					/* Directory object to return last directory and found object */
	const TCHAR* /*char*/ path			/* Full-path string to find a file or directory */
)
{
	FRESULT res;
	BYTE ns;
	FATFS *fs = dp->obj.fs;


#if _FS_RPATH != 0
	if (!IsSeparator(*path) && (_STR_VOLUME_ID != 2 || !IsTerminator(*path))) {	/* Without heading separator */
		dp->obj.sclust = fs->cdir;			/* Start at the current directory */
	} else
#endif
	{										/* With heading separator */
		while (IsSeparator(*path)) path++;	/* Strip separators */
		dp->obj.sclust = 0;					/* Start from the root directory */
	}
#if _FS_EXFAT
	dp->obj.n_frag = 0;	/* Invalidate last fragment counter of the object */
#if _FS_RPATH != 0
	if (fs->fs_type == FS_EXFAT && dp->obj.sclust) {	/* exFAT: Retrieve the sub-directory's status */
		DIR_ dj;

		dp->obj.c_scl = fs->cdc_scl;
		dp->obj.c_size = fs->cdc_size;
		dp->obj.c_ofs = fs->cdc_ofs;
		res = load_obj_xdir(&dj, &dp->obj);
		if (res != FR_OK) return res;
		dp->obj.objsize = ld_dword(fs->dirbuf + XDIR_FileSize);
		dp->obj.stat = fs->dirbuf[XDIR_GenFlags] & 2;
	}
#endif
#endif

	if ((UINT)*path < ' ') {				/* Null path name is the origin directory itself */
		dp->fn[NS] = NS_NONAME;
		res = dir_sdi(dp, 0);

	} else {								/* Follow path */
		for (;;) {
			res = create_name(dp, &path);	/* Get a segment name of the path */
			if (res != FR_OK) break;
			res = dir_find(dp);				/* Find an object with the segment name */
			ns = dp->fn[NS];
			if (res != FR_OK) {				/* Failed to find the object */
				if (res == FR_NO_FILE) {	/* Object is not found */
					if (_FS_RPATH && (ns & NS_DOT)) {	/* If dot entry is not exist, stay there */
						if (!(ns & NS_LAST)) continue;	/* Continue to follow if not last segment */
						dp->fn[NS] = NS_NONAME;
						res = FR_OK;
					} else {							/* Could not find the object */
						if (!(ns & NS_LAST)) res = FR_NO_PATH;	/* Adjust error code if not last segment */
					}
				}
				break;
			}
			if (ns & NS_LAST) break;		/* Last segment matched. Function completed. */
			/* Get into the sub-directory */
			if (!(dp->obj.attr & AM_DIR)) {	/* It is not a sub-directory and cannot follow */
				res = FR_NO_PATH; break;
			}
#if _FS_EXFAT
			if (fs->fs_type == FS_EXFAT) {	/* Save containing directory information for next dir */
				dp->obj.c_scl = dp->obj.sclust;
				dp->obj.c_size = ((DWORD)dp->obj.objsize & 0xFFFFFF00) | dp->obj.stat;
				dp->obj.c_ofs = dp->blk_ofs;
				init_alloc_info(fs, &dp->obj);	/* Open next directory */
			} else
#endif
			{
				dp->obj.sclust = ld_clust(fs, fs->win + dp->dptr % SS(fs));	/* Open next directory */
			}
		}
	}

	return res;
}


/*-----------------------------------------------------------------------*/
/* GPT support functions                                                 */
/*-----------------------------------------------------------------------*/

#if _LBA64

/* Calculate CRC32 in byte-by-byte */

static DWORD crc32 (	/* Returns next CRC value */
	DWORD crc,			/* Current CRC value */
	BYTE d				/* A byte to be processed */
)
{
	BYTE b;


	for (b = 1; b; b <<= 1) {
		crc ^= (d & b) ? 1 : 0;
		crc = (crc & 1) ? crc >> 1 ^ 0xEDB88320 : crc >> 1;
	}
	return crc;
}


/* Check validity of GPT header */

static int test_gpt_header (	/* 0:Invalid, 1:Valid */
	const BYTE* gpth			/* Pointer to the GPT header */
)
{
	UINT i;
	DWORD bcc, hlen;


	if (memcmp(gpth + GPTH_Sign, "EFI PART" "\0\0\1", 12)) return 0;	/* Check signature and version (1.0) */
	hlen = ld_dword(gpth + GPTH_Size);						/* Check header size */
	if (hlen < 92 || hlen > _MIN_SS) return 0;
	for (i = 0, bcc = 0xFFFFFFFF; i < hlen; i++) {			/* Check header BCC */
		bcc = crc32(bcc, i - GPTH_Bcc < 4 ? 0 : gpth[i]);
	}
	if (~bcc != ld_dword(gpth + GPTH_Bcc)) return 0;
	if (ld_dword(gpth + GPTH_PteSize) != SZ_GPTE) return 0;	/* Table entry size (must be SZ_GPTE bytes) */
	if (ld_dword(gpth + GPTH_PtNum) > 128) return 0;		/* Table size (must be 128 entries or less) */

	return 1;
}

#if !_FS_READONLY && _USE_MKFS

/* Generate random value */
static DWORD make_rand (
	DWORD seed,		/* Seed value */
	BYTE *buff,		/* Output buffer */
	UINT n			/* Data length */
)
{
	UINT r;


	if (seed == 0) seed = 1;
	do {
		for (r = 0; r < 8; r++) seed = seed & 1 ? seed >> 1 ^ 0xA3000000 : seed >> 1;	/* Shift 8 bits the 32-bit LFSR */
		*buff++ = (BYTE)seed;
	} while (--n);
	return seed;
}

#endif
#endif




/*-----------------------------------------------------------------------*/
/* Load a sector and check if it is an FAT VBR                           */
/*-----------------------------------------------------------------------*/

/* Check what the sector is */

static UINT check_fs (	/* 0:FAT/FAT32 VBR, 1:exFAT VBR, 2:Not FAT and valid BS, 3:Not FAT and invalid BS, 4:Disk error */
	FATFS* fs,			/* Filesystem object */
	LBA_t sect			/* Sector to load and check if it is an FAT-VBR or not */
)
{
	WORD w, sign;
	BYTE b;


	fs->wflag = 0; fs->winsect = (LBA_t)0 - 1;		/* Invaidate window */
	if (move_window(fs, sect) != FR_OK) return 4;	/* Load the boot sector */
	sign = ld_word(fs->win + BS_55AA);
#if _FS_EXFAT
	if (sign == 0xAA55 && !memcmp(fs->win + BS_jmpBoot, "\xEB\x76\x90" "EXFAT   ", 11)) return 1;	/* It is an exFAT VBR */
#endif
	b = fs->win[BS_jmpBoot];
	if (b == 0xEB || b == 0xE9 || b == 0xE8) {	/* Valid JumpBoot code? (short jump, near jump or near call) */
		if (sign == 0xAA55 && !memcmp(fs->win + BS_FilSysType32, "FAT32   ", 8)) {
			return 0;	/* It is an FAT32 VBR */
		}
		/* FAT volumes formatted with early MS-DOS lack BS_55AA and BS_FilSysType, so FAT VBR needs to be identified without them. */
		w = ld_word(fs->win + BPB_BytsPerSec);
		b = fs->win[BPB_SecPerClus];
		if ((w & (w - 1)) == 0 && w >= _MIN_SS && w <= _MAX_SS	/* Properness of sector size (512-4096 and 2^n) */
			&& b != 0 && (b & (b - 1)) == 0				/* Properness of cluster size (2^n) */
			&& ld_word(fs->win + BPB_RsvdSecCnt) != 0	/* Properness of reserved sectors (MNBZ) */
			&& (UINT)fs->win[BPB_NumFATs] - 1 <= 1		/* Properness of FATs (1 or 2) */
			&& ld_word(fs->win + BPB_RootEntCnt) != 0	/* Properness of root dir entries (MNBZ) */
			&& (ld_word(fs->win + BPB_TotSec16) >= 128 || ld_dword(fs->win + BPB_TotSec32) >= 0x10000)	/* Properness of volume sectors (>=128) */
			&& ld_word(fs->win + BPB_FATSz16) != 0) {	/* Properness of FAT size (MNBZ) */
				return 0;	/* It can be presumed an FAT VBR */
		}
	}
	return sign == 0xAA55 ? 2 : 3;	/* Not an FAT VBR (valid or invalid BS) */
}


/*-----------------------------------------------------------------------*/
/* Find logical drive and check if the volume is mounted                 */
/*-----------------------------------------------------------------------*/

/* (It supports only generic partitioning rules, MBR, GPT and SFD) */

// TODO: Some calls use old parameter wmode, change accordingly since now using part
static FRESULT find_volume (	/* Returns BS status found in the hosting drive */
	FATFS* fs,		/* Filesystem object */
	UINT part		/* Partition to fined = 0:find as SFD and partitions, >0:forced partition number */
)
{
	UINT fmt, i;
	DWORD mbr_pt[4];

	/* 0:FAT/FAT32 VBR, 1:exFAT VBR, 2:Not FAT and valid BS, 3:Not FAT and invalid BS, 4:Disk error */
	fmt = check_fs(fs, 0);				/* Load sector 0 and check if it is an FAT VBR as SFD format */
	//if (fmt != 2 && (fmt >= 3 || part == 0)) return fmt;	/* Returns if it is an FAT VBR as auto scan, not a BS or disk error */
	if(fmt ==4) 
		return FR_DISK_ERR;
	if(fmt >=2)
		return FR_NO_FILESYSTEM;


	/* Sector 0 is not an FAT VBR or forced partition number wants a partition */

#if _LBA64
	if (fs->win[MBR_Table + PTE_System] == 0xEE) {	/* GPT protective MBR? */
		DWORD n_ent, v_ent, ofs;
		QWORD pt_lba;

		if (move_window(fs, 1) != FR_OK) return 4;	/* Load GPT header sector (next to MBR) */
		if (!test_gpt_header(fs->win)) return 3;	/* Check if GPT header is valid */
		n_ent = ld_dword(fs->win + GPTH_PtNum);		/* Number of entries */
		pt_lba = ld_qword(fs->win + GPTH_PtOfs);	/* Table location */
		for (v_ent = i = 0; i < n_ent; i++) {		/* Find FAT partition */
			if (move_window(fs, pt_lba + i * SZ_GPTE / SS(fs)) != FR_OK) return 4;	/* PT sector */
			ofs = i * SZ_GPTE % SS(fs);												/* Offset in the sector */
			if (!memcmp(fs->win + ofs + GPTE_PtGuid, GUID_MS_Basic, 16)) {	/* MS basic data partition? */
				v_ent++;
				fmt = check_fs(fs, ld_qword(fs->win + ofs + GPTE_FstLba));	/* Load VBR and check status */
				if (part == 0 && fmt <= 1) return fmt;			/* Auto search (valid FAT volume found first) */
				if (part != 0 && v_ent == part) return fmt;		/* Forced partition order (regardless of it is valid or not) */
			}
		}
		return 3;	/* Not found */
	}

#endif
	if (_MULTI_PARTITION && part > 4);	/* MBR has 4 partitions max */
	for (i = 0; i < 4; i++) {		/* Load partition offset in the MBR */
		mbr_pt[i] = ld_dword(fs->win + MBR_Table + i * SZ_PTE + PTE_StLba);
	}
	i = part ? part - 1 : 0;		/* Table index to find first */
	do {							/* Find an FAT volume */
		fmt = mbr_pt[i] ? check_fs(fs, mbr_pt[i]) : 3;	/* Check if the partition is FAT */
	} while (part == 0 && fmt >= 2 && ++i < 4);
	return fmt;
}


/*-----------------------------------------------------------------------*/
/* Determine logical drive number and mount the volume if needed         */
/*-----------------------------------------------------------------------*/

static FRESULT mount_volume (	/* FR_OK(0): successful, !=0: an error occurred */
	const TCHAR** path,			/* Pointer to pointer to the path name (drive number) */
	FATFS** rfs,				/* Pointer to pointer to the found filesystem object */
	BYTE mode					/* Desiered access mode to check write protection */
)
{
	int vol;
	FATFS *fs;
	DSTATUS stat;
	LBA_t bsect;
	DWORD tsect, sysect, fasize, nclst, szbfat;
	WORD nrsv;
	UINT fmt;


	/* Get logical drive number */
	*rfs = 0;
	vol = 0;//get_ldnumber(path);
	if (vol < 0) return FR_INVALID_DRIVE;

	/* Check if the filesystem object is valid or not */
	//fs = FatFs[vol];					/* Get pointer to the filesystem object */
	if (!fs) return FR_NOT_ENABLED;		/* Is the filesystem object available? */
#if _FS_REENTRANT
	if (!lock_volume(fs, 1)) return FR_TIMEOUT;	/* Lock the volume, and system if needed */
#endif
	*rfs = fs;							/* Return pointer to the filesystem object */

	mode &= (BYTE)~FA_READ;				/* Desired access mode, write access or not */
	if (fs->fs_type != 0) {				/* If the volume has been mounted */
		stat = disk_status(fs->pdrv);
		if (!(stat & STA_NOINIT)) {		/* and the physical drive is kept initialized */
			if (!_FS_READONLY && mode && (stat & STA_PROTECT)) {	/* Check write protection if needed */
				return FR_WRITE_PROTECTED;
			}
			return FR_OK;				/* The filesystem object is already valid */
		}
	}

	/* The filesystem object is not valid. */
	/* Following code attempts to mount the volume. (find an FAT volume, analyze the BPB and initialize the filesystem object) */

	fs->fs_type = 0;					/* Invalidate the filesystem object */
	stat = disk_initialize(fs->pdrv);	/* Initialize the volume hosting physical drive */
	if (stat & STA_NOINIT) { 			/* Check if the initialization succeeded */
		return FR_NOT_READY;			/* Failed to initialize due to no medium or hard error */
	}
	if (!_FS_READONLY && mode && (stat & STA_PROTECT)) { /* Check disk write protection if needed */
		return FR_WRITE_PROTECTED;
	}
#if _MAX_SS != _MIN_SS				/* Get sector size (multiple sector size cfg only) */
	if (disk_ioctl(fs->pdrv, GET_SECTOR_SIZE, &SS(fs)) != RES_OK) return FR_DISK_ERR;
	if (SS(fs) > _MAX_SS || SS(fs) < _MIN_SS || (SS(fs) & (SS(fs) - 1))) return FR_DISK_ERR;
#endif

	/* Find an FAT volume on the hosting drive */
	// TODO: Change to res...
	fmt = find_volume(fs, LD2PT(vol));
	if (fmt == 4) return FR_DISK_ERR;		/* An error occurred in the disk I/O layer */
	if (fmt >= 2) return FR_NO_FILESYSTEM;	/* No FAT volume is found */
	bsect = fs->winsect;					/* Volume offset in the hosting physical drive */

	/* An FAT volume is found (bsect). Following code initializes the filesystem object */

#if _FS_EXFAT
	if (fmt == 1) {
		QWORD maxlba;
		DWORD so, cv, bcl, i;

		for (i = BPB_ZeroedEx; i < BPB_ZeroedEx + 53 && fs->win[i] == 0; i++) ;	/* Check zero filler */
		if (i < BPB_ZeroedEx + 53) return FR_NO_FILESYSTEM;

		if (ld_word(fs->win + BPB_FSVerEx) != 0x100) return FR_NO_FILESYSTEM;	/* Check exFAT version (must be version 1.0) */

		if (1 << fs->win[BPB_BytsPerSecEx] != SS(fs)) {	/* (BPB_BytsPerSecEx must be equal to the physical sector size) */
			return FR_NO_FILESYSTEM;
		}

		maxlba = ld_qword(fs->win + BPB_TotSecEx) + bsect;	/* Last LBA of the volume + 1 */
		if (!_LBA64 && maxlba >= 0x100000000) return FR_NO_FILESYSTEM;	/* (It cannot be accessed in 32-bit LBA) */

		fs->fsize = ld_dword(fs->win + BPB_FatSzEx);	/* Number of sectors per FAT */

		fs->n_fats = fs->win[BPB_NumFATsEx];			/* Number of FATs */
		if (fs->n_fats != 1) return FR_NO_FILESYSTEM;	/* (Supports only 1 FAT) */

		fs->csize = 1 << fs->win[BPB_SecPerClusEx];		/* Cluster size */
		if (fs->csize == 0)	return FR_NO_FILESYSTEM;	/* (Must be 1..32768 sectors) */

		nclst = ld_dword(fs->win + BPB_NumClusEx);		/* Number of clusters */
		if (nclst > MAX_EXFAT) return FR_NO_FILESYSTEM;	/* (Too many clusters) */
		fs->n_fatent = nclst + 2;

		/* Boundaries and Limits */
		fs->volbase = bsect;
		fs->database = bsect + ld_dword(fs->win + BPB_DataOfsEx);
		fs->fatbase = bsect + ld_dword(fs->win + BPB_FatOfsEx);
		if (maxlba < (QWORD)fs->database + nclst * fs->csize) return FR_NO_FILESYSTEM;	/* (Volume size must not be smaller than the size required) */
		fs->dirbase = ld_dword(fs->win + BPB_RootClusEx);

		/* Get bitmap location and check if it is contiguous (implementation assumption) */
		so = i = 0;
		for (;;) {	/* Find the bitmap entry in the root directory (in only first cluster) */
			if (i == 0) {
				if (so >= fs->csize) return FR_NO_FILESYSTEM;	/* Not found? */
				if (move_window(fs, clust2sect(fs, (DWORD)fs->dirbase) + so) != FR_OK) return FR_DISK_ERR;
				so++;
			}
			if (fs->win[i] == ET_BITMAP) break;			/* Is it a bitmap entry? */
			i = (i + SZ_DIR) % SS(fs);	/* Next entry */
		}
		bcl = ld_dword(fs->win + i + 20);				/* Bitmap cluster */
		if (bcl < 2 || bcl >= fs->n_fatent) return FR_NO_FILESYSTEM;	/* (Wrong cluster#) */
		fs->bitbase = fs->database + fs->csize * (bcl - 2);	/* Bitmap sector */
		for (;;) {	/* Check if bitmap is contiguous */
			if (move_window(fs, fs->fatbase + bcl / (SS(fs) / 4)) != FR_OK) return FR_DISK_ERR;
			cv = ld_dword(fs->win + bcl % (SS(fs) / 4) * 4);
			if (cv == 0xFFFFFFFF) break;				/* Last link? */
			if (cv != ++bcl) return FR_NO_FILESYSTEM;	/* Fragmented bitmap? */
		}

#if !_FS_READONLY
		fs->last_clst = fs->free_clust = 0xFFFFFFFF;		/* Initialize cluster allocation information */
#endif
		fmt = FS_EXFAT;			/* FAT sub-type */
	} else
#endif	/* _FS_EXFAT */
	{
		if (ld_word(fs->win + BPB_BytsPerSec) != SS(fs)) return FR_NO_FILESYSTEM;	/* (BPB_BytsPerSec must be equal to the physical sector size) */

		fasize = ld_word(fs->win + BPB_FATSz16);		/* Number of sectors per FAT */
		if (fasize == 0) fasize = ld_dword(fs->win + BPB_FATSz32);
		fs->fsize = fasize;

		fs->n_fats = fs->win[BPB_NumFATs];				/* Number of FATs */
		if (fs->n_fats != 1 && fs->n_fats != 2) return FR_NO_FILESYSTEM;	/* (Must be 1 or 2) */
		fasize *= fs->n_fats;							/* Number of sectors for FAT area */

		fs->csize = fs->win[BPB_SecPerClus];			/* Cluster size */
		if (fs->csize == 0 || (fs->csize & (fs->csize - 1))) return FR_NO_FILESYSTEM;	/* (Must be power of 2) */

		fs->n_rootdir = ld_word(fs->win + BPB_RootEntCnt);	/* Number of root directory entries */
		if (fs->n_rootdir % (SS(fs) / SZ_DIR)) return FR_NO_FILESYSTEM;	/* (Must be sector aligned) */

		tsect = ld_word(fs->win + BPB_TotSec16);		/* Number of sectors on the volume */
		if (tsect == 0) tsect = ld_dword(fs->win + BPB_TotSec32);

		nrsv = ld_word(fs->win + BPB_RsvdSecCnt);		/* Number of reserved sectors */
		if (nrsv == 0) return FR_NO_FILESYSTEM;			/* (Must not be 0) */

		/* Determine the FAT sub type */
		sysect = nrsv + fasize + fs->n_rootdir / (SS(fs) / SZ_DIR);	/* RSV + FAT + DIR */
		if (tsect < sysect) return FR_NO_FILESYSTEM;	/* (Invalid volume size) */
		nclst = (tsect - sysect) / fs->csize;			/* Number of clusters */
		if (nclst == 0) return FR_NO_FILESYSTEM;		/* (Invalid volume size) */
		fmt = 0;
		if (nclst <= MAX_FAT32) fmt = FS_FAT32;
		if (nclst <= MAX_FAT16) fmt = FS_FAT16;
		if (nclst <= MAX_FAT12) fmt = FS_FAT12;
		if (fmt == 0) return FR_NO_FILESYSTEM;

		/* Boundaries and Limits */
		fs->n_fatent = nclst + 2;						/* Number of FAT entries */
		fs->volbase = bsect;							/* Volume start sector */
		fs->fatbase = bsect + nrsv; 					/* FAT start sector */
		fs->database = bsect + sysect;					/* Data start sector */
		if (fmt == FS_FAT32) {
			if (ld_word(fs->win + BPB_FSVer32) != 0) return FR_NO_FILESYSTEM;	/* (Must be FAT32 revision 0.0) */
			if (fs->n_rootdir != 0) return FR_NO_FILESYSTEM;	/* (BPB_RootEntCnt must be 0) */
			fs->dirbase = ld_dword(fs->win + BPB_RootClus32);	/* Root directory start cluster */
			szbfat = fs->n_fatent * 4;					/* (Needed FAT size) */
		} else {
			if (fs->n_rootdir == 0)	return FR_NO_FILESYSTEM;	/* (BPB_RootEntCnt must not be 0) */
			fs->dirbase = fs->fatbase + fasize;			/* Root directory start sector */
			szbfat = (fmt == FS_FAT16) ?				/* (Needed FAT size) */
				fs->n_fatent * 2 : fs->n_fatent * 3 / 2 + (fs->n_fatent & 1);
		}
		if (fs->fsize < (szbfat + (SS(fs) - 1)) / SS(fs)) return FR_NO_FILESYSTEM;	/* (BPB_FATSz must not be less than the size needed) */

#if !_FS_READONLY
		/* Get FSInfo if available */
		fs->last_clust = fs->free_clust = 0xFFFFFFFF;		/* Initialize cluster allocation information */
		fs->fsi_flag = 0x80;
#if (_FS_NOFSINFO & 3) != 3
		if (fmt == FS_FAT32				/* Allow to update FSInfo only if BPB_FSInfo32 == 1 */
			&& ld_word(fs->win + BPB_FSInfo32) == 1
			&& move_window(fs, bsect + 1) == FR_OK)
		{
			fs->fsi_flag = 0;
			if (ld_word(fs->win + BS_55AA) == 0xAA55	/* Load FSInfo data if available */
				&& ld_dword(fs->win + FSI_LeadSig) == 0x41615252
				&& ld_dword(fs->win + FSI_StrucSig) == 0x61417272)
			{
#if (_FS_NOFSINFO & 1) == 0
				fs->free_clust = ld_dword(fs->win + FSI_Free_Count);
#endif
#if (_FS_NOFSINFO & 2) == 0
				fs->last_clust = ld_dword(fs->win + FSI_Nxt_Free);
#endif
			}
		}
#endif	/* (_FS_NOFSINFO & 3) != 3 */
#endif	/* !_FS_READONLY */
	}

	fs->fs_type = (BYTE)fmt;/* FAT sub-type (the filesystem object gets valid) */
	fs->id = ++Fsid;		/* Volume mount ID */
#if _USE_LFN == 1
	fs->lfnbuf = LfnBuf;	/* Static LFN working buffer */
#if _FS_EXFAT
	fs->dirbuf = DirBuf;	/* Static directory block scratchpad buuffer */
#endif
#endif
#ifdef _FS_LOCK			/* Clear file lock semaphores */
	clear_lock(fs);
#if _FS_RPATH != 0
	fs->cdir = 0;			/* Initialize current directory */
#endif
#if _FS_LOCK				/* Clear file lock semaphores */
	clear_share(fs);
#endif
	return FR_OK;
}



/*-----------------------------------------------------------------------*/
/* Check if the file/directory object is valid or not                    */
/*-----------------------------------------------------------------------*/

static FRESULT validate (	/* Returns FR_OK or FR_INVALID_OBJECT */
	FFOBJID* obj,			/* Pointer to the FFOBJID, the 1st member in the FIL/DIR structure, to check validity */
	FATFS** rfs				/* Pointer to pointer to the owner filesystem object to return */
)
{
	FRESULT res = FR_INVALID_OBJECT;


	if (obj && obj->fs && obj->fs->fs_type && obj->id == obj->fs->id) {	/* Test if the object is valid */
#if _FS_REENTRANT
		if (lock_volume(obj->fs, 0)) {	/* Take a grant to access the volume */
			if (!(disk_status(obj->fs->pdrv) & STA_NOINIT)) { /* Test if the hosting phsical drive is kept initialized */
				res = FR_OK;
			} else {
				unlock_volume(obj->fs, FR_OK);	/* Invalidated volume, abort to access */
			}
		} else {	/* Could not take */
			res = FR_TIMEOUT;
		}
#else
		if (!(disk_status(obj->fs->pdrv) & STA_NOINIT)) { /* Test if the hosting phsical drive is kept initialized */
			res = FR_OK;
		}
#endif
	}
	*rfs = (res == FR_OK) ? obj->fs : 0;	/* Return corresponding filesystem object if it is valid */
	return res;
}




/*--------------------------------------------------------------------------

   Public Functions

--------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------*/
/* Mount/Unmount a Logical Drive                                         */
/*-----------------------------------------------------------------------*/

FRESULT f_mount (
	FATFS* fs,			/* Pointer to the file system object (NULL:unmount)*/
	/*const TCHAR* path,*/	/* Logical drive number to be mounted/unmounted */
	BYTE opt,			/* 0:Do not mount (delayed mount), 1:Mount immediately */
    bool umount
)
{
	FATFS *cfs;
	int vol;
	FRESULT res;


	vol = 0;//get_ldnumber(&path);
	if (vol < 0) return FR_INVALID_DRIVE;
	cfs = fs;//FatFs[vol];					/* Pointer to fs object */

	if (/*cfs*/umount) {
#ifdef _FS_LOCK
		clear_lock(cfs);
#endif
#if _FS_REENTRANT						/* Discard sync object of the current volume */
		if (!ff_del_syncobj(cfs->sobj)) return FR_INT_ERR;
#endif
		cfs->fs_type = 0;				/* Clear old fs object */
	}

	if (/*fs*/!umount) {
		fs->fs_type = 0;				/* Clear new fs object */
        memset(fs->Files,0,sizeof(FATFS::Files));
#if _FS_REENTRANT						/* Create sync object for the new volume */
		if (!ff_cre_syncobj(vol, &fs->sobj)) return FR_INT_ERR;
#endif
	}
	//FatFs[vol] = fs;					/* Register new fs object */

	if (/*!fs*/umount || opt != 1) return FR_OK;	/* Do not mount now, it will be mounted later */

	res = find_volume(fs, /*&path,*/ 0);	/* Force mounted the volume */
	LEAVE_FF(fs, res);
}




/*-----------------------------------------------------------------------*/
/* Open or Create a File                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_open (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	FIL* fp,			/* Pointer to the blank file object */
	const /*TCHAR*/char *path,	/* Pointer to the file name */
	BYTE mode			/* Access mode and file open mode flags */
)
{
	FRESULT res;
	DIR_ dj;
	BYTE *dir;
	DEF_NAMEBUF;


	if (!fp) return FR_INVALID_OBJECT;
	fp->fs = 0;			/* Clear file object */

	/* Get logical drive number */
    dj.fs=fs;
#if !_FS_READONLY
	mode &= FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW;
	res = find_volume(dj.fs, /*&path,*/ (BYTE)(mode & ~FA_READ));
#else
	mode &= FA_READ;
	res = find_volume(dj.fs, &path, 0);
#endif
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);	/* Follow the file path */
		dir = dj.dir;
#if !_FS_READONLY	/* R/W configuration */
		if (res == FR_OK) {
			if (!dir)	/* Default directory itself */
				res = FR_INVALID_NAME;
#ifdef _FS_LOCK
			else
				res = chk_lock(&dj, (mode & ~FA_READ) ? 1 : 0);
#endif
		}
		/* Create or Open a file */
		if (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW)) {
			DWORD dw, cl;

			if (res != FR_OK) {					/* No file, create new */
				if (res == FR_NO_FILE)			/* There is no file to open, create a new entry */
#ifdef _FS_LOCK
					res = enq_lock(dj.fs) ? dir_register(&dj) : FR_TOO_MANY_OPEN_FILES;
#else
					res = dir_register(&dj);
#endif
				mode |= FA_CREATE_ALWAYS;		/* File is created */
				dir = dj.dir;					/* New entry */
			}
			else {								/* Any object is already existing */
				if (dir[DIR_Attr] & (AM_RDO | AM_DIR)) {	/* Cannot overwrite it (R/O or DIR) */
					res = FR_DENIED;
				} else {
					if (mode & FA_CREATE_NEW)	/* Cannot create as new file */
						res = FR_EXIST;
				}
			}
			if (res == FR_OK && (mode & FA_CREATE_ALWAYS)) {	/* Truncate it if overwrite mode */
				dw = get_fattime();				/* Created time */
				ST_DWORD(dir+DIR_CrtTime, dw);
				dir[DIR_Attr] = 0;				/* Reset attribute */
				ST_DWORD(dir+DIR_FileSize, 0);	/* size = 0 */
				cl = ld_clust(dj.fs, dir);		/* Get start cluster */
				st_clust(dir, 0);				/* cluster = 0 */
				dj.fs->wflag = 1;
				if (cl) {						/* Remove the cluster chain if exist */
					dw = dj.fs->winsect;
					res = remove_chain(dj.fs, cl);
					if (res == FR_OK) {
						dj.fs->last_clust = cl - 1;	/* Reuse the cluster hole */
						res = move_window(dj.fs, dw);
					}
				}
			}
		}
		else {	/* Open an existing file */
			if (res == FR_OK) {					/* Follow succeeded */
				if (dir[DIR_Attr] & AM_DIR) {	/* It is a directory */
					res = FR_NO_FILE;
				} else {
					if ((mode & FA_WRITE) && (dir[DIR_Attr] & AM_RDO)) /* R/O violation */
						res = FR_DENIED;
				}
			}
		}
		if (res == FR_OK) {
			if (mode & FA_CREATE_ALWAYS)		/* Set file change flag if created or overwritten */
				mode |= FA__WRITTEN;
			fp->dir_sect = dj.fs->winsect;		/* Pointer to the directory entry */
			fp->dir_ptr = dir;
#ifdef _FS_LOCK
			fp->lockid = inc_lock(&dj, (mode & ~FA_READ) ? 1 : 0);
			if (!fp->lockid) res = FR_INT_ERR;
#endif
		}

#else				/* R/O configuration */
		if (res == FR_OK) {					/* Follow succeeded */
			dir = dj.dir;
			if (!dir) {						/* Current directory itself */
				res = FR_INVALID_NAME;
			} else {
				if (dir[DIR_Attr] & AM_DIR)	/* It is a directory */
					res = FR_NO_FILE;
			}
		}
#endif
		FREE_BUF();

		if (res == FR_OK) {
			fp->flag = mode;					/* File access mode */
			fp->err = 0;						/* Clear error flag */
			fp->sclust = ld_clust(dj.fs, dir);	/* File start cluster */
			fp->fsize = LD_DWORD(dir+DIR_FileSize);	/* File size */
			fp->fptr = 0;						/* File pointer */
			fp->dsect = 0;
#if _USE_FASTSEEK
			fp->cltbl = 0;						/* Normal seek mode */
#endif
			fp->fs = dj.fs;	 					/* Validate file object */
			fp->id = fp->fs->id;
		}
	}

	LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Read File                                                             */
/*-----------------------------------------------------------------------*/

FRESULT f_read (
	FIL* fp, 		/* Pointer to the file object */
	void* buff,		/* Pointer to data buffer */
	UINT btr,		/* Number of bytes to read */
	UINT* br		/* Pointer to number of bytes read */
)
{
	FRESULT res;
	DWORD clst, sect, remain;
	UINT rcnt, cc;
	BYTE csect, *rbuff = (BYTE*)buff;


	*br = 0;	/* Clear read byte counter */

	res = validate(fp);							/* Check validity */
	if (res != FR_OK) LEAVE_FF(fp->fs, res);
	if (fp->err)								/* Check error */
		LEAVE_FF(fp->fs, (FRESULT)fp->err);
	if (!(fp->flag & FA_READ)) 					/* Check access mode */
		LEAVE_FF(fp->fs, FR_DENIED);
	remain = fp->fsize - fp->fptr;
	if (btr > remain) btr = (UINT)remain;		/* Truncate btr by remaining bytes */

	for ( ;  btr;								/* Repeat until all data read */
		rbuff += rcnt, fp->fptr += rcnt, *br += rcnt, btr -= rcnt) {
		if ((fp->fptr % SS(fp->fs)) == 0) {		/* On the sector boundary? */
			csect = (BYTE)(fp->fptr / SS(fp->fs) & (fp->fs->csize - 1));	/* Sector offset in the cluster */
			if (!csect) {						/* On the cluster boundary? */
				if (fp->fptr == 0) {			/* On the top of the file? */
					clst = fp->sclust;			/* Follow from the origin */
				} else {						/* Middle or end of the file */
#if _USE_FASTSEEK
					if (fp->cltbl)
						clst = clmt_clust(fp, fp->fptr);	/* Get cluster# from the CLMT */
					else
#endif
						clst = get_fat(fp->fs, fp->clust);	/* Follow cluster chain on the FAT */
				}
				if (clst < 2) ABORT(fp->fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
				fp->clust = clst;				/* Update current cluster */
			}
			sect = clust2sect(fp->fs, fp->clust);	/* Get current sector */
			if (!sect) ABORT(fp->fs, FR_INT_ERR);
			sect += csect;
			cc = btr / SS(fp->fs);				/* When remaining bytes >= sector size, */
			if (cc) {							/* Read maximum contiguous sectors directly */
				if (csect + cc > fp->fs->csize)	/* Clip at cluster boundary */
					cc = fp->fs->csize - csect;
				if (disk_read(fp->fs->drv, rbuff, sect, cc))
					ABORT(fp->fs, FR_DISK_ERR);
#if !_FS_READONLY && _FS_MINIMIZE <= 2			/* Replace one of the read sectors with cached data if it contains a dirty sector */
#if _FS_TINY
				if (fp->fs->wflag && fp->fs->winsect - sect < cc)
					mem_cpy(rbuff + ((fp->fs->winsect - sect) * SS(fp->fs)), fp->fs->win, SS(fp->fs));
#else
				if ((fp->flag & FA__DIRTY) && fp->dsect - sect < cc)
					mem_cpy(rbuff + ((fp->dsect - sect) * SS(fp->fs)), fp->buf, SS(fp->fs));
#endif
#endif
				rcnt = SS(fp->fs) * cc;			/* Number of bytes transferred */
				continue;
			}
#if !_FS_TINY
			if (fp->dsect != sect) {			/* Load data sector if not in cache */
#if !_FS_READONLY
				if (fp->flag & FA__DIRTY) {		/* Write-back dirty sector cache */
					if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
						ABORT(fp->fs, FR_DISK_ERR);
					fp->flag &= ~FA__DIRTY;
				}
#endif
				if (disk_read(fp->fs->drv, fp->buf, sect, 1))	/* Fill sector cache */
					ABORT(fp->fs, FR_DISK_ERR);
			}
#endif
			fp->dsect = sect;
		}
		rcnt = SS(fp->fs) - ((UINT)fp->fptr % SS(fp->fs));	/* Get partial sector data from sector buffer */
		if (rcnt > btr) rcnt = btr;
#if _FS_TINY
		if (move_window(fp->fs, fp->dsect))		/* Move sector window */
			ABORT(fp->fs, FR_DISK_ERR);
		mem_cpy(rbuff, &fp->fs->win[fp->fptr % SS(fp->fs)], rcnt);	/* Pick partial sector */
#else
		mem_cpy(rbuff, &fp->buf[fp->fptr % SS(fp->fs)], rcnt);	/* Pick partial sector */
#endif
	}

	LEAVE_FF(fp->fs, FR_OK);
}




#if !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Write File                                                            */
/*-----------------------------------------------------------------------*/

FRESULT f_write (
	FIL* fp,			/* Pointer to the file object */
	const void *buff,	/* Pointer to the data to be written */
	UINT btw,			/* Number of bytes to write */
	UINT* bw			/* Pointer to number of bytes written */
)
{
	FRESULT res;
	DWORD clst, sect;
	UINT wcnt, cc;
	const BYTE *wbuff = (const BYTE*)buff;
	BYTE csect;


	*bw = 0;	/* Clear write byte counter */

	res = validate(fp);						/* Check validity */
	if (res != FR_OK) LEAVE_FF(fp->fs, res);
	if (fp->err)							/* Check error */
		LEAVE_FF(fp->fs, (FRESULT)fp->err);
	if (!(fp->flag & FA_WRITE))				/* Check access mode */
		LEAVE_FF(fp->fs, FR_DENIED);
	if (fp->fptr + btw < fp->fptr) btw = 0;	/* File size cannot reach 4GB */

	for ( ;  btw;							/* Repeat until all data written */
		wbuff += wcnt, fp->fptr += wcnt, *bw += wcnt, btw -= wcnt) {
		if ((fp->fptr % SS(fp->fs)) == 0) {	/* On the sector boundary? */
			csect = (BYTE)(fp->fptr / SS(fp->fs) & (fp->fs->csize - 1));	/* Sector offset in the cluster */
			if (!csect) {					/* On the cluster boundary? */
				if (fp->fptr == 0) {		/* On the top of the file? */
					clst = fp->sclust;		/* Follow from the origin */
					if (clst == 0)			/* When no cluster is allocated, */
						fp->sclust = clst = create_chain(fp->fs, 0);	/* Create a new cluster chain */
				} else {					/* Middle or end of the file */
#if _USE_FASTSEEK
					if (fp->cltbl)
						clst = clmt_clust(fp, fp->fptr);	/* Get cluster# from the CLMT */
					else
#endif
						clst = create_chain(fp->fs, fp->clust);	/* Follow or stretch cluster chain on the FAT */
				}
				if (clst == 0) break;		/* Could not allocate a new cluster (disk full) */
				if (clst == 1) ABORT(fp->fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
				fp->clust = clst;			/* Update current cluster */
			}
#if _FS_TINY
			if (fp->fs->winsect == fp->dsect && sync_window(fp->fs))	/* Write-back sector cache */
				ABORT(fp->fs, FR_DISK_ERR);
#else
			if (fp->flag & FA__DIRTY) {		/* Write-back sector cache */
				if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
					ABORT(fp->fs, FR_DISK_ERR);
				fp->flag &= ~FA__DIRTY;
			}
#endif
			sect = clust2sect(fp->fs, fp->clust);	/* Get current sector */
			if (!sect) ABORT(fp->fs, FR_INT_ERR);
			sect += csect;
			cc = btw / SS(fp->fs);			/* When remaining bytes >= sector size, */
			if (cc) {						/* Write maximum contiguous sectors directly */
				if (csect + cc > fp->fs->csize)	/* Clip at cluster boundary */
					cc = fp->fs->csize - csect;
				if (disk_write(fp->fs->drv, wbuff, sect, cc))
					ABORT(fp->fs, FR_DISK_ERR);
#if _FS_MINIMIZE <= 2
#if _FS_TINY
				if (fp->fs->winsect - sect < cc) {	/* Refill sector cache if it gets invalidated by the direct write */
					mem_cpy(fp->fs->win, wbuff + ((fp->fs->winsect - sect) * SS(fp->fs)), SS(fp->fs));
					fp->fs->wflag = 0;
				}
#else
				if (fp->dsect - sect < cc) { /* Refill sector cache if it gets invalidated by the direct write */
					mem_cpy(fp->buf, wbuff + ((fp->dsect - sect) * SS(fp->fs)), SS(fp->fs));
					fp->flag &= ~FA__DIRTY;
				}
#endif
#endif
				wcnt = SS(fp->fs) * cc;		/* Number of bytes transferred */
				continue;
			}
#if _FS_TINY
			if (fp->fptr >= fp->fsize) {	/* Avoid silly cache filling at growing edge */
				if (sync_window(fp->fs)) ABORT(fp->fs, FR_DISK_ERR);
				fp->fs->winsect = sect;
			}
#else
			if (fp->dsect != sect) {		/* Fill sector cache with file data */
				if (fp->fptr < fp->fsize &&
					disk_read(fp->fs->drv, fp->buf, sect, 1))
						ABORT(fp->fs, FR_DISK_ERR);
			}
#endif
			fp->dsect = sect;
		}
		wcnt = SS(fp->fs) - ((UINT)fp->fptr % SS(fp->fs));/* Put partial sector into file I/O buffer */
		if (wcnt > btw) wcnt = btw;
#if _FS_TINY
		if (move_window(fp->fs, fp->dsect))	/* Move sector window */
			ABORT(fp->fs, FR_DISK_ERR);
		mem_cpy(&fp->fs->win[fp->fptr % SS(fp->fs)], wbuff, wcnt);	/* Fit partial sector */
		fp->fs->wflag = 1;
#else
		mem_cpy(&fp->buf[fp->fptr % SS(fp->fs)], wbuff, wcnt);	/* Fit partial sector */
		fp->flag |= FA__DIRTY;
#endif
	}

	if (fp->fptr > fp->fsize) fp->fsize = fp->fptr;	/* Update file size if needed */
	fp->flag |= FA__WRITTEN;						/* Set file change flag */

	LEAVE_FF(fp->fs, FR_OK);
}




/*-----------------------------------------------------------------------*/
/* Synchronize the File                                                  */
/*-----------------------------------------------------------------------*/

FRESULT f_sync (
	FIL* fp		/* Pointer to the file object */
)
{
	FRESULT res;
	DWORD tm;
	BYTE *dir;


	res = validate(fp);					/* Check validity of the object */
	if (res == FR_OK) {
		if (fp->flag & FA__WRITTEN) {	/* Has the file been written? */
			/* Write-back dirty buffer */
#if !_FS_TINY
			if (fp->flag & FA__DIRTY) {
				if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
					LEAVE_FF(fp->fs, FR_DISK_ERR);
				fp->flag &= ~FA__DIRTY;
			}
#endif
			/* Update the directory entry */
			res = move_window(fp->fs, fp->dir_sect);
			if (res == FR_OK) {
				dir = fp->dir_ptr;
				dir[DIR_Attr] |= AM_ARC;					/* Set archive bit */
				ST_DWORD(dir+DIR_FileSize, fp->fsize);		/* Update file size */
				st_clust(dir, fp->sclust);					/* Update start cluster */
				tm = get_fattime();							/* Update updated time */
				ST_DWORD(dir+DIR_WrtTime, tm);
				ST_WORD(dir+DIR_LstAccDate, 0);
				fp->flag &= ~FA__WRITTEN;
				fp->fs->wflag = 1;
				res = sync_fs(fp->fs);
			}
		}
	}

	LEAVE_FF(fp->fs, res);
}

#endif /* !_FS_READONLY */




/*-----------------------------------------------------------------------*/
/* Close File                                                            */
/*-----------------------------------------------------------------------*/

FRESULT f_close (
	FIL *fp		/* Pointer to the file object to be closed */
)
{
	FRESULT res;


#if _FS_READONLY
	res = validate(fp);
	{
#if _FS_REENTRANT
		FATFS *fs = 0;
		if (res == FR_OK) fs = fp->fs;	/* Get corresponding file system object */
#endif
		if (res == FR_OK) fp->fs = 0;	/* Invalidate file object */
		LEAVE_FF(fs, res);
	}
#else
	res = f_sync(fp);					/* Flush cached data */
#ifdef _FS_LOCK
	if (res == FR_OK) {					/* Decrement open counter */
#if _FS_REENTRANT
		res = validate(fp);
		if (res == FR_OK) {
			res = dec_lock(fp->fs,fp->lockid);
			unlock_fs(fp->fs, FR_OK);
		}
#else
		res = dec_lock(fp->fs,fp->lockid);
#endif
	}
#endif
	if (res == FR_OK) fp->fs = 0;		/* Invalidate file object */
	return res;
#endif
}




/*-----------------------------------------------------------------------*/
/* Change Current Directory or Current Drive, Get Current Directory      */
/*-----------------------------------------------------------------------*/

#if _FS_RPATH >= 1
#if _VOLUMES >= 2
FRESULT f_chdrive (
	const TCHAR* path		/* Drive number */
)
{
	int vol;


	vol = get_ldnumber(&path);
	if (vol < 0) return FR_INVALID_DRIVE;

	CurrVol = (BYTE)vol;

	return FR_OK;
}
#endif


FRESULT f_chdir (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const TCHAR* path	/* Pointer to the directory path */
)
{
	FRESULT res;
	DIR_ dj;
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, &path, 0);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);		/* Follow the path */
		FREE_BUF();
		if (res == FR_OK) {					/* Follow completed */
			if (!dj.dir) {
				dj.fs->cdir = dj.sclust;	/* Start directory itself */
			} else {
				if (dj.dir[DIR_Attr] & AM_DIR)	/* Reached to the directory */
					dj.fs->cdir = ld_clust(dj.fs, dj.dir);
				else
					res = FR_NO_PATH;		/* Reached but a file */
			}
		}
		if (res == FR_NO_FILE) res = FR_NO_PATH;
	}

	LEAVE_FF(dj.fs, res);
}


#if _FS_RPATH >= 2
FRESULT f_getcwd (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	TCHAR* buff,	/* Pointer to the directory path */
	UINT len		/* Size of path */
)
{
	FRESULT res;
	DIR_ dj;
	UINT i, n;
	DWORD ccl;
	TCHAR *tp;
	FILINFO fno;
	DEF_NAMEBUF;


	*buff = 0;
	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, (const TCHAR**)&buff, 0);	/* Get current volume */
	if (res == FR_OK) {
		INIT_BUF(dj);
		i = len;			/* Bottom of buffer (directory stack base) */
		dj.sclust = dj.fs->cdir;			/* Start to follow upper directory from current directory */
		while ((ccl = dj.sclust) != 0) {	/* Repeat while current directory is a sub-directory */
			res = dir_sdi(&dj, 1);			/* Get parent directory */
			if (res != FR_OK) break;
			res = dir_read(&dj, 0);
			if (res != FR_OK) break;
			dj.sclust = ld_clust(dj.fs, dj.dir);	/* Goto parent directory */
			res = dir_sdi(&dj, 0);
			if (res != FR_OK) break;
			do {							/* Find the entry links to the child directory */
				res = dir_read(&dj, 0);
				if (res != FR_OK) break;
				if (ccl == ld_clust(dj.fs, dj.dir)) break;	/* Found the entry */
				res = dir_next(&dj, 0);	
			} while (res == FR_OK);
			if (res == FR_NO_FILE) res = FR_INT_ERR;/* It cannot be 'not found'. */
			if (res != FR_OK) break;
#if _USE_LFN
			fno.lfname = buff;
			fno.lfsize = i;
#endif
			get_fileinfo(&dj, &fno);		/* Get the directory name and push it to the buffer */
			tp = fno.fname;
#if _USE_LFN
			if (*buff) tp = buff;
#endif
			for (n = 0; tp[n]; n++) ;
			if (i < n + 3) {
				res = FR_NOT_ENOUGH_CORE; break;
			}
			while (n) buff[--i] = tp[--n];
			buff[--i] = '/';
		}
		tp = buff;
		if (res == FR_OK) {
#if _VOLUMES >= 2
			*tp++ = '0' + CurrVol;			/* Put drive number */
			*tp++ = ':';
#endif
			if (i == len) {					/* Root-directory */
				*tp++ = '/';
			} else {						/* Sub-directroy */
				do		/* Add stacked path str */
					*tp++ = buff[i++];
				while (i < len);
			}
		}
		*tp = 0;
		FREE_BUF();
	}

	LEAVE_FF(dj.fs, res);
}
#endif /* _FS_RPATH >= 2 */
#endif /* _FS_RPATH >= 1 */



#if _FS_MINIMIZE <= 2
/*-----------------------------------------------------------------------*/
/* Seek File R/W Pointer                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_lseek (
	FIL* fp,		/* Pointer to the file object */
	DWORD ofs		/* File pointer from top of file */
)
{
	FRESULT res;


	res = validate(fp);					/* Check validity of the object */
	if (res != FR_OK) LEAVE_FF(fp->fs, res);
	if (fp->err)						/* Check error */
		LEAVE_FF(fp->fs, (FRESULT)fp->err);

#if _USE_FASTSEEK
	if (fp->cltbl) {	/* Fast seek */
		DWORD cl, pcl, ncl, tcl, dsc, tlen, ulen, *tbl;

		if (ofs == CREATE_LINKMAP) {	/* Create CLMT */
			tbl = fp->cltbl;
			tlen = *tbl++; ulen = 2;	/* Given table size and required table size */
			cl = fp->sclust;			/* Top of the chain */
			if (cl) {
				do {
					/* Get a fragment */
					tcl = cl; ncl = 0; ulen += 2;	/* Top, length and used items */
					do {
						pcl = cl; ncl++;
						cl = get_fat(fp->fs, cl);
						if (cl <= 1) ABORT(fp->fs, FR_INT_ERR);
						if (cl == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
					} while (cl == pcl + 1);
					if (ulen <= tlen) {		/* Store the length and top of the fragment */
						*tbl++ = ncl; *tbl++ = tcl;
					}
				} while (cl < fp->fs->n_fatent);	/* Repeat until end of chain */
			}
			*fp->cltbl = ulen;	/* Number of items used */
			if (ulen <= tlen)
				*tbl = 0;		/* Terminate table */
			else
				res = FR_NOT_ENOUGH_CORE;	/* Given table size is smaller than required */

		} else {						/* Fast seek */
			if (ofs > fp->fsize)		/* Clip offset at the file size */
				ofs = fp->fsize;
			fp->fptr = ofs;				/* Set file pointer */
			if (ofs) {
				fp->clust = clmt_clust(fp, ofs - 1);
				dsc = clust2sect(fp->fs, fp->clust);
				if (!dsc) ABORT(fp->fs, FR_INT_ERR);
				dsc += (ofs - 1) / SS(fp->fs) & (fp->fs->csize - 1);
				if (fp->fptr % SS(fp->fs) && dsc != fp->dsect) {	/* Refill sector cache if needed */
#if !_FS_TINY
#if !_FS_READONLY
					if (fp->flag & FA__DIRTY) {		/* Write-back dirty sector cache */
						if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
							ABORT(fp->fs, FR_DISK_ERR);
						fp->flag &= ~FA__DIRTY;
					}
#endif
					if (disk_read(fp->fs->drv, fp->buf, dsc, 1))	/* Load current sector */
						ABORT(fp->fs, FR_DISK_ERR);
#endif
					fp->dsect = dsc;
				}
			}
		}
	} else
#endif

	/* Normal Seek */
	{
		DWORD clst, bcs, nsect, ifptr;

		if (ofs > fp->fsize					/* In read-only mode, clip offset with the file size */
#if !_FS_READONLY
			 && !(fp->flag & FA_WRITE)
#endif
			) ofs = fp->fsize;

		ifptr = fp->fptr;
		fp->fptr = nsect = 0;
		if (ofs) {
			bcs = (DWORD)fp->fs->csize * SS(fp->fs);	/* Cluster size (byte) */
			if (ifptr > 0 &&
				(ofs - 1) / bcs >= (ifptr - 1) / bcs) {	/* When seek to same or following cluster, */
				fp->fptr = (ifptr - 1) & ~(bcs - 1);	/* start from the current cluster */
				ofs -= fp->fptr;
				clst = fp->clust;
			} else {									/* When seek to back cluster, */
				clst = fp->sclust;						/* start from the first cluster */
#if !_FS_READONLY
				if (clst == 0) {						/* If no cluster chain, create a new chain */
					clst = create_chain(fp->fs, 0);
					if (clst == 1) ABORT(fp->fs, FR_INT_ERR);
					if (clst == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
					fp->sclust = clst;
				}
#endif
				fp->clust = clst;
			}
			if (clst != 0) {
				while (ofs > bcs) {						/* Cluster following loop */
#if !_FS_READONLY
					if (fp->flag & FA_WRITE) {			/* Check if in write mode or not */
						clst = create_chain(fp->fs, clst);	/* Force stretch if in write mode */
						if (clst == 0) {				/* When disk gets full, clip file size */
							ofs = bcs; break;
						}
					} else
#endif
						clst = get_fat(fp->fs, clst);	/* Follow cluster chain if not in write mode */
					if (clst == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
					if (clst <= 1 || clst >= fp->fs->n_fatent) ABORT(fp->fs, FR_INT_ERR);
					fp->clust = clst;
					fp->fptr += bcs;
					ofs -= bcs;
				}
				fp->fptr += ofs;
				if (ofs % SS(fp->fs)) {
					nsect = clust2sect(fp->fs, clst);	/* Current sector */
					if (!nsect) ABORT(fp->fs, FR_INT_ERR);
					nsect += ofs / SS(fp->fs);
				}
			}
		}
		if (fp->fptr % SS(fp->fs) && nsect != fp->dsect) {	/* Fill sector cache if needed */
#if !_FS_TINY
#if !_FS_READONLY
			if (fp->flag & FA__DIRTY) {			/* Write-back dirty sector cache */
				if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
					ABORT(fp->fs, FR_DISK_ERR);
				fp->flag &= ~FA__DIRTY;
			}
#endif
			if (disk_read(fp->fs->drv, fp->buf, nsect, 1))	/* Fill sector cache */
				ABORT(fp->fs, FR_DISK_ERR);
#endif
			fp->dsect = nsect;
		}
#if !_FS_READONLY
		if (fp->fptr > fp->fsize) {			/* Set file change flag if the file size is extended */
			fp->fsize = fp->fptr;
			fp->flag |= FA__WRITTEN;
		}
#endif
	}

	LEAVE_FF(fp->fs, res);
}



#if _FS_MINIMIZE <= 1
/*-----------------------------------------------------------------------*/
/* Create a Directory Object                                             */
/*-----------------------------------------------------------------------*/

FRESULT f_opendir (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	DIR_* dp,			/* Pointer to directory object to create */
	const /*TCHAR*/char *path	/* Pointer to the directory path */
)
{
	FRESULT res;
	//FATFS* fs;
	DEF_NAMEBUF;


	if (!dp) return FR_INVALID_OBJECT;

	/* Get logical drive number */
	res = find_volume(fs, /*&path,*/ 0);
	if (res == FR_OK) {
		dp->fs = fs;
		INIT_BUF(*dp);
		res = follow_path(dp, path);			/* Follow the path to the directory */
		FREE_BUF();
		if (res == FR_OK) {						/* Follow completed */
			if (dp->dir) {						/* It is not the origin directory itself */
				if (dp->dir[DIR_Attr] & AM_DIR)	/* The object is a sub directory */
					dp->sclust = ld_clust(fs, dp->dir);
				else							/* The object is a file */
					res = FR_NO_PATH;
			}
			if (res == FR_OK) {
				dp->id = fs->id;
				res = dir_sdi(dp, 0);			/* Rewind directory */
#ifdef _FS_LOCK
				if (res == FR_OK) {
					if (dp->sclust) {
						dp->lockid = inc_lock(dp, 0);	/* Lock the sub directory */
						if (!dp->lockid)
							res = FR_TOO_MANY_OPEN_FILES;
					} else {
						dp->lockid = 0;	/* Root directory need not to be locked */
					}
				}
#endif
			}
		}
		if (res == FR_NO_FILE) res = FR_NO_PATH;
	}
	if (res != FR_OK) dp->fs = 0;		/* Invalidate the directory object if function faild */

	LEAVE_FF(fs, res);
}




/*-----------------------------------------------------------------------*/
/* Close Directory                                                       */
/*-----------------------------------------------------------------------*/

FRESULT f_closedir (
	DIR_ *dp		/* Pointer to the directory object to be closed */
)
{
	FRESULT res;


	res = validate(dp);
#ifdef _FS_LOCK
	if (res == FR_OK) {				/* Decrement open counter */
		if (dp->lockid)
			res = dec_lock(dp->fs,dp->lockid);
#if _FS_REENTRANT
		unlock_fs(dp->fs, FR_OK);
#endif
	}
#endif
	if (res == FR_OK) dp->fs = 0;	/* Invalidate directory object */
	return res;
}




/*-----------------------------------------------------------------------*/
/* Read Directory Entries in Sequence                                    */
/*-----------------------------------------------------------------------*/

FRESULT f_readdir (
	DIR_* dp,			/* Pointer to the open directory object */
	FILINFO* fno		/* Pointer to file information to return */
)
{
	FRESULT res;
	DEF_NAMEBUF;


	res = validate(dp);						/* Check validity of the object */
	if (res == FR_OK) {
		if (!fno) {
			res = dir_sdi(dp, 0);			/* Rewind the directory object */
		} else {
			INIT_BUF2(*dp,dp->fs);
			res = dir_read(dp, 0);			/* Read an item */
			if (res == FR_NO_FILE) {		/* Reached end of directory */
				dp->sect = 0;
				res = FR_OK;
			}
			if (res == FR_OK) {				/* A valid entry is found */
				get_fileinfo(dp, fno);		/* Get the object information */
				res = dir_next(dp, 0);		/* Increment index for next */
				if (res == FR_NO_FILE) {
					dp->sect = 0;
					res = FR_OK;
				}
			}
			FREE_BUF();
		}
	}

	LEAVE_FF(dp->fs, res);
}



#if _FS_MINIMIZE == 0
/*-----------------------------------------------------------------------*/
/* Get File Status                                                       */
/*-----------------------------------------------------------------------*/

FRESULT f_stat (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path,	/* Pointer to the file path */
	FILINFO* fno		/* Pointer to file information to return */
)
{
	FRESULT res;
	DIR_ dj;
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, /*&path,*/ 0);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);	/* Follow the file path */
		if (res == FR_OK) {				/* Follow completed */
			if (dj.dir) {		/* Found an object */
				if (fno) get_fileinfo(&dj, fno);
			} else {			/* It is root directory */
				res = FR_INVALID_NAME;
			}
		}
		FREE_BUF();
	}

	LEAVE_FF(dj.fs, res);
}



#if !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Get Number of Free Clusters                                           */
/*-----------------------------------------------------------------------*/

FRESULT f_getfree (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	/*const TCHAR* path,*/	/* Path name of the logical drive number */
	DWORD* nclst		/* Pointer to a variable to return number of free clusters */
	/*FATFS** fatfs*/		/* Pointer to return pointer to corresponding file system object */
)
{
	FRESULT res;
	//FATFS *fs;
	DWORD n, clst, sect, stat;
	UINT i;
	BYTE fat, *p;


	/* Get logical drive number */
	res = find_volume(fs/*fatfs*/, /*&path,*/ 0);
	//fs = *fatfs;
	if (res == FR_OK) {
		/* If free_clust is valid, return it without full cluster scan */
		if (fs->free_clust <= fs->n_fatent - 2) {
			*nclst = fs->free_clust;
		} else {
			/* Get number of free clusters */
			fat = fs->fs_type;
			n = 0;
			if (fat == FS_FAT12) {
				clst = 2;
				do {
					stat = get_fat(fs, clst);
					if (stat == 0xFFFFFFFF) { res = FR_DISK_ERR; break; }
					if (stat == 1) { res = FR_INT_ERR; break; }
					if (stat == 0) n++;
				} while (++clst < fs->n_fatent);
			} else {
				clst = fs->n_fatent;
				sect = fs->fatbase;
				i = 0; p = 0;
				do {
					if (!i) {
						res = move_window(fs, sect++);
						if (res != FR_OK) break;
						p = fs->win;
						i = SS(fs);
					}
					if (fat == FS_FAT16) {
						if (LD_WORD(p) == 0) n++;
						p += 2; i -= 2;
					} else {
						if ((LD_DWORD(p) & 0x0FFFFFFF) == 0) n++;
						p += 4; i -= 4;
					}
				} while (--clst);
			}
			fs->free_clust = n;
			fs->fsi_flag |= 1;
			*nclst = n;
		}
	}
	LEAVE_FF(fs, res);
}




/*-----------------------------------------------------------------------*/
/* Truncate File                                                         */
/*-----------------------------------------------------------------------*/

FRESULT f_truncate (
	FIL* fp		/* Pointer to the file object */
)
{
	FRESULT res;
	DWORD ncl;


	res = validate(fp);						/* Check validity of the object */
	if (res == FR_OK) {
		if (fp->err) {						/* Check error */
			res = (FRESULT)fp->err;
		} else {
			if (!(fp->flag & FA_WRITE))		/* Check access mode */
				res = FR_DENIED;
		}
	}
	if (res == FR_OK) {
		if (fp->fsize > fp->fptr) {
			fp->fsize = fp->fptr;	/* Set file size to current R/W point */
			fp->flag |= FA__WRITTEN;
			if (fp->fptr == 0) {	/* When set file size to zero, remove entire cluster chain */
				res = remove_chain(fp->fs, fp->sclust);
				fp->sclust = 0;
			} else {				/* When truncate a part of the file, remove remaining clusters */
				ncl = get_fat(fp->fs, fp->clust);
				res = FR_OK;
				if (ncl == 0xFFFFFFFF) res = FR_DISK_ERR;
				if (ncl == 1) res = FR_INT_ERR;
				if (res == FR_OK && ncl < fp->fs->n_fatent) {
					res = put_fat(fp->fs, fp->clust, 0x0FFFFFFF);
					if (res == FR_OK) res = remove_chain(fp->fs, ncl);
				}
			}
#if !_FS_TINY
			if (res == FR_OK && (fp->flag & FA__DIRTY)) {
				if (disk_write(fp->fs->drv, fp->buf, fp->dsect, 1))
					res = FR_DISK_ERR;
				else
					fp->flag &= ~FA__DIRTY;
			}
#endif
		}
		if (res != FR_OK) fp->err = (FRESULT)res;
	}

	LEAVE_FF(fp->fs, res);
}




/*-----------------------------------------------------------------------*/
/* Delete a File or Directory                                            */
/*-----------------------------------------------------------------------*/

FRESULT f_unlink (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path		/* Pointer to the file or directory path */
)
{
	FRESULT res;
	DIR_ dj, sdj;
	BYTE *dir;
	DWORD dclst;
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, /*&path,*/ 1);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);		/* Follow the file path */
		if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
			res = FR_INVALID_NAME;			/* Cannot remove dot entry */
#ifdef _FS_LOCK
		if (res == FR_OK) res = chk_lock(&dj, 2);	/* Cannot remove open file */
#endif
		if (res == FR_OK) {					/* The object is accessible */
			dir = dj.dir;
			if (!dir) {
				res = FR_INVALID_NAME;		/* Cannot remove the start directory */
			} else {
				if (dir[DIR_Attr] & AM_RDO)
					res = FR_DENIED;		/* Cannot remove R/O object */
			}
			dclst = ld_clust(dj.fs, dir);
			if (res == FR_OK && (dir[DIR_Attr] & AM_DIR)) {	/* Is it a sub-dir? */
				if (dclst < 2) {
					res = FR_INT_ERR;
				} else {
					mem_cpy(&sdj, &dj, sizeof (DIR_));	/* Check if the sub-directory is empty or not */
					sdj.sclust = dclst;
					res = dir_sdi(&sdj, 2);		/* Exclude dot entries */
					if (res == FR_OK) {
						res = dir_read(&sdj, 0);	/* Read an item */
						if (res == FR_OK		/* Not empty directory */
#if _FS_RPATH
						|| dclst == dj.fs->cdir	/* Current directory */
#endif
						) res = FR_DENIED;
						if (res == FR_NO_FILE) res = FR_OK;	/* Empty */
					}
				}
			}
			if (res == FR_OK) {
				res = dir_remove(&dj);		/* Remove the directory entry */
				if (res == FR_OK) {
					if (dclst)				/* Remove the cluster chain if exist */
						res = remove_chain(dj.fs, dclst);
					if (res == FR_OK) res = sync_fs(dj.fs);
				}
			}
		}
		FREE_BUF();
	}

	LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Create a Directory                                                    */
/*-----------------------------------------------------------------------*/

FRESULT f_mkdir (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path		/* Pointer to the directory path */
)
{
	FRESULT res;
	DIR_ dj;
	BYTE *dir, n;
	DWORD dsc, dcl, pcl, tm = get_fattime();
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, /*&path,*/ 1);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);			/* Follow the file path */
		if (res == FR_OK) res = FR_EXIST;		/* Any object with same name is already existing */
		if (_FS_RPATH && res == FR_NO_FILE && (dj.fn[NS] & NS_DOT))
			res = FR_INVALID_NAME;
		if (res == FR_NO_FILE) {				/* Can create a new directory */
			dcl = create_chain(dj.fs, 0);		/* Allocate a cluster for the new directory table */
			res = FR_OK;
			if (dcl == 0) res = FR_DENIED;		/* No space to allocate a new cluster */
			if (dcl == 1) res = FR_INT_ERR;
			if (dcl == 0xFFFFFFFF) res = FR_DISK_ERR;
			if (res == FR_OK)					/* Flush FAT */
				res = sync_window(dj.fs);
			if (res == FR_OK) {					/* Initialize the new directory table */
				dsc = clust2sect(dj.fs, dcl);
				dir = dj.fs->win;
				mem_set(dir, 0, SS(dj.fs));
				mem_set(dir+DIR_Name, ' ', 11);	/* Create "." entry */
				dir[DIR_Name] = '.';
				dir[DIR_Attr] = AM_DIR;
				ST_DWORD(dir+DIR_WrtTime, tm);
				st_clust(dir, dcl);
				mem_cpy(dir+SZ_DIR, dir, SZ_DIR); 	/* Create ".." entry */
				dir[SZ_DIR+1] = '.'; pcl = dj.sclust;
				if (dj.fs->fs_type == FS_FAT32 && pcl == dj.fs->dirbase)
					pcl = 0;
				st_clust(dir+SZ_DIR, pcl);
				for (n = dj.fs->csize; n; n--) {	/* Write dot entries and clear following sectors */
					dj.fs->winsect = dsc++;
					dj.fs->wflag = 1;
					res = sync_window(dj.fs);
					if (res != FR_OK) break;
					mem_set(dir, 0, SS(dj.fs));
				}
			}
			if (res == FR_OK) res = dir_register(&dj);	/* Register the object to the directoy */
			if (res != FR_OK) {
				remove_chain(dj.fs, dcl);			/* Could not register, remove cluster chain */
			} else {
				dir = dj.dir;
				dir[DIR_Attr] = AM_DIR;				/* Attribute */
				ST_DWORD(dir+DIR_WrtTime, tm);		/* Created time */
				st_clust(dir, dcl);					/* Table start cluster */
				dj.fs->wflag = 1;
				res = sync_fs(dj.fs);
			}
		}
		FREE_BUF();
	}

	LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Change Attribute                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_chmod (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path,	/* Pointer to the file path */
	BYTE value,			/* Attribute bits */
	BYTE mask			/* Attribute mask to change */
)
{
	FRESULT res;
	DIR_ dj;
	BYTE *dir;
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, /*&path,*/ 1);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);		/* Follow the file path */
		FREE_BUF();
		if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
			res = FR_INVALID_NAME;
		if (res == FR_OK) {
			dir = dj.dir;
			if (!dir) {						/* Is it a root directory? */
				res = FR_INVALID_NAME;
			} else {						/* File or sub directory */
				mask &= AM_RDO|AM_HID|AM_SYS|AM_ARC;	/* Valid attribute mask */
				dir[DIR_Attr] = (value & mask) | (dir[DIR_Attr] & (BYTE)~mask);	/* Apply attribute change */
				dj.fs->wflag = 1;
				res = sync_fs(dj.fs);
			}
		}
	}

	LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Change Timestamp                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_utime (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path,	/* Pointer to the file/directory name */
	const FILINFO* fno	/* Pointer to the time stamp to be set */
)
{
	FRESULT res;
	DIR_ dj;
	BYTE *dir;
	DEF_NAMEBUF;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, /*&path,*/ 1);
	if (res == FR_OK) {
		INIT_BUF(dj);
		res = follow_path(&dj, path);	/* Follow the file path */
		FREE_BUF();
		if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
			res = FR_INVALID_NAME;
		if (res == FR_OK) {
			dir = dj.dir;
			if (!dir) {					/* Root directory */
				res = FR_INVALID_NAME;
			} else {					/* File or sub-directory */
				ST_WORD(dir+DIR_WrtTime, fno->ftime);
				ST_WORD(dir+DIR_WrtDate, fno->fdate);
				dj.fs->wflag = 1;
				res = sync_fs(dj.fs);
			}
		}
	}

	LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Rename File/Directory                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_rename (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const /*TCHAR*/char *path_old,	/* Pointer to the old name */
	const /*TCHAR*/char *path_new	/* Pointer to the new name */
)
{
	FRESULT res;
	DIR_ djo, djn;
	BYTE buf[21], *dir;
	DWORD dw;
	DEF_NAMEBUF;


	/* Get logical drive number of the source object */
    djo.fs=fs;
	res = find_volume(djo.fs, /*&path_old,*/ 1);
	if (res == FR_OK) {
		djn.fs = djo.fs;
		INIT_BUF(djo);
		res = follow_path(&djo, path_old);		/* Check old object */
		if (_FS_RPATH && res == FR_OK && (djo.fn[NS] & NS_DOT))
			res = FR_INVALID_NAME;
#ifdef _FS_LOCK
		if (res == FR_OK) res = chk_lock(&djo, 2);
#endif
		if (res == FR_OK) {						/* Old object is found */
			if (!djo.dir) {						/* Is root dir? */
				res = FR_NO_FILE;
			} else {
				mem_cpy(buf, djo.dir+DIR_Attr, 21);		/* Save the object information except for name */
				mem_cpy(&djn, &djo, sizeof (DIR_));		/* Check new object */
				res = follow_path(&djn, path_new);
				if (res == FR_OK) res = FR_EXIST;		/* The new object name is already existing */
				if (res == FR_NO_FILE) { 				/* Is it a valid path and no name collision? */
/* Start critical section that any interruption can cause a cross-link */
					res = dir_register(&djn);			/* Register the new entry */
					if (res == FR_OK) {
						dir = djn.dir;					/* Copy object information except for name */
						mem_cpy(dir+13, buf+2, 19);
						dir[DIR_Attr] = buf[0] | AM_ARC;
						djo.fs->wflag = 1;
						if (djo.sclust != djn.sclust && (dir[DIR_Attr] & AM_DIR)) {		/* Update .. entry in the directory if needed */
							dw = clust2sect(djo.fs, ld_clust(djo.fs, dir));
							if (!dw) {
								res = FR_INT_ERR;
							} else {
								res = move_window(djo.fs, dw);
								dir = djo.fs->win+SZ_DIR;	/* .. entry */
								if (res == FR_OK && dir[1] == '.') {
									dw = (djo.fs->fs_type == FS_FAT32 && djn.sclust == djo.fs->dirbase) ? 0 : djn.sclust;
									st_clust(dir, dw);
									djo.fs->wflag = 1;
								}
							}
						}
						if (res == FR_OK) {
							res = dir_remove(&djo);		/* Remove old entry */
							if (res == FR_OK)
								res = sync_fs(djo.fs);
						}
					}
/* End critical section */
				}
			}
		}
		FREE_BUF();
	}

	LEAVE_FF(djo.fs, res);
}

#endif /* !_FS_READONLY */
#endif /* _FS_MINIMIZE == 0 */
#endif /* _FS_MINIMIZE <= 1 */
#endif /* _FS_MINIMIZE <= 2 */



#if _USE_LABEL
/*-----------------------------------------------------------------------*/
/* Get volume label                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_getlabel (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const TCHAR* path,	/* Path name of the logical drive number */
	TCHAR* label,		/* Pointer to a buffer to return the volume label */
	DWORD* sn			/* Pointer to a variable to return the volume serial number */
)
{
	FRESULT res;
	DIR_ dj;
	UINT i, j;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, &path, 0);

	/* Get volume label */
	if (res == FR_OK && label) {
		dj.sclust = 0;					/* Open root directory */
		res = dir_sdi(&dj, 0);
		if (res == FR_OK) {
			res = dir_read(&dj, 1);		/* Get an entry with AM_VOL */
			if (res == FR_OK) {			/* A volume label is exist */
#if _LFN_UNICODE
				WCHAR w;
				i = j = 0;
				do {
					w = (i < 11) ? dj.dir[i++] : ' ';
					if (IsDBCS1(w) && i < 11 && IsDBCS2(dj.dir[i]))
						w = w << 8 | dj.dir[i++];
					label[j++] = ff_convert(w, 1);	/* OEM -> Unicode */
				} while (j < 11);
#else
				mem_cpy(label, dj.dir, 11);
#endif
				j = 11;
				do {
					label[j] = 0;
					if (!j) break;
				} while (label[--j] == ' ');
			}
			if (res == FR_NO_FILE) {	/* No label, return nul string */
				label[0] = 0;
				res = FR_OK;
			}
		}
	}

	/* Get volume serial number */
	if (res == FR_OK && sn) {
		res = move_window(dj.fs, dj.fs->volbase);
		if (res == FR_OK) {
			i = dj.fs->fs_type == FS_FAT32 ? BS_VolID32 : BS_VolID;
			*sn = LD_DWORD(&dj.fs->win[i]);
		}
	}

	LEAVE_FF(dj.fs, res);
}



#if !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Set volume label                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_setlabel (
    FATFS *fs,          /* By TFT: added to get rid of static variables */
	const TCHAR* label	/* Pointer to the volume label to set */
)
{
	FRESULT res;
	DIR_ dj;
	BYTE vn[11];
	UINT i, j, sl;
	WCHAR w;
	DWORD tm;


	/* Get logical drive number */
    dj.fs=fs;
	res = find_volume(dj.fs, &label, 1);
	if (res) LEAVE_FF(dj.fs, res);

	/* Create a volume label in directory form */
	vn[0] = 0;
	for (sl = 0; label[sl]; sl++) ;				/* Get name length */
	for ( ; sl && label[sl-1] == ' '; sl--) ;	/* Remove trailing spaces */
	if (sl) {	/* Create volume label in directory form */
		i = j = 0;
		do {
#if _LFN_UNICODE
			w = ff_convert(ff_wtoupper(label[i++]), 0);
#else
			w = (BYTE)label[i++];
			if (IsDBCS1(w))
				w = (j < 10 && i < sl && IsDBCS2(label[i])) ? w << 8 | (BYTE)label[i++] : 0;
#if _USE_LFN
			w = ff_convert(ff_wtoupper(ff_convert(w, 1)), 0);
#else
			if (IsLower(w)) w -= 0x20;			/* To upper ASCII characters */
#ifdef _EXCVT
			if (w >= 0x80) w = ExCvt[w - 0x80];	/* To upper extended characters (SBCS cfg) */
#else
			if (!_DF1S && w >= 0x80) w = 0;		/* Reject extended characters (ASCII cfg) */
#endif
#endif
#endif
			if (!w || chk_chr("\"*+,.:;<=>\?[]|\x7F", w) || j >= (UINT)((w >= 0x100) ? 10 : 11)) /* Reject invalid characters for volume label */
				LEAVE_FF(dj.fs, FR_INVALID_NAME);
			if (w >= 0x100) vn[j++] = (BYTE)(w >> 8);
			vn[j++] = (BYTE)w;
		} while (i < sl);
		while (j < 11) vn[j++] = ' ';
	}

	/* Set volume label */
	dj.sclust = 0;					/* Open root directory */
	res = dir_sdi(&dj, 0);
	if (res == FR_OK) {
		res = dir_read(&dj, 1);		/* Get an entry with AM_VOL */
		if (res == FR_OK) {			/* A volume label is found */
			if (vn[0]) {
				mem_cpy(dj.dir, vn, 11);	/* Change the volume label name */
				tm = get_fattime();
				ST_DWORD(dj.dir+DIR_WrtTime, tm);
			} else {
				dj.dir[0] = DDE;			/* Remove the volume label */
			}
			dj.fs->wflag = 1;
			res = sync_fs(dj.fs);
		} else {					/* No volume label is found or error */
			if (res == FR_NO_FILE) {
				res = FR_OK;
				if (vn[0]) {				/* Create volume label as new */
					res = dir_alloc(&dj, 1);	/* Allocate an entry for volume label */
					if (res == FR_OK) {
						mem_set(dj.dir, 0, SZ_DIR);	/* Set volume label */
						mem_cpy(dj.dir, vn, 11);
						dj.dir[DIR_Attr] = AM_VOL;
						tm = get_fattime();
						ST_DWORD(dj.dir+DIR_WrtTime, tm);
						dj.fs->wflag = 1;
						res = sync_fs(dj.fs);
					}
				}
			}
		}
	}

	LEAVE_FF(dj.fs, res);
}

#endif /* !_FS_READONLY */
#endif /* _USE_LABEL */



/*-----------------------------------------------------------------------*/
/* Forward data to the stream directly (available on only tiny cfg)      */
/*-----------------------------------------------------------------------*/
#if _USE_FORWARD && _FS_TINY

FRESULT f_forward (
	FIL* fp, 						/* Pointer to the file object */
	UINT (*func)(const BYTE*,UINT),	/* Pointer to the streaming function */
	UINT btf,						/* Number of bytes to forward */
	UINT* bf						/* Pointer to number of bytes forwarded */
)
{
	FRESULT res;
	DWORD remain, clst, sect;
	UINT rcnt;
	BYTE csect;


	*bf = 0;	/* Clear transfer byte counter */

	res = validate(fp);								/* Check validity of the object */
	if (res != FR_OK) LEAVE_FF(fp->fs, res);
	if (fp->err)									/* Check error */
		LEAVE_FF(fp->fs, (FRESULT)fp->err);
	if (!(fp->flag & FA_READ))						/* Check access mode */
		LEAVE_FF(fp->fs, FR_DENIED);

	remain = fp->fsize - fp->fptr;
	if (btf > remain) btf = (UINT)remain;			/* Truncate btf by remaining bytes */

	for ( ;  btf && (*func)(0, 0);					/* Repeat until all data transferred or stream becomes busy */
		fp->fptr += rcnt, *bf += rcnt, btf -= rcnt) {
		csect = (BYTE)(fp->fptr / SS(fp->fs) & (fp->fs->csize - 1));	/* Sector offset in the cluster */
		if ((fp->fptr % SS(fp->fs)) == 0) {			/* On the sector boundary? */
			if (!csect) {							/* On the cluster boundary? */
				clst = (fp->fptr == 0) ?			/* On the top of the file? */
					fp->sclust : get_fat(fp->fs, fp->clust);
				if (clst <= 1) ABORT(fp->fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF) ABORT(fp->fs, FR_DISK_ERR);
				fp->clust = clst;					/* Update current cluster */
			}
		}
		sect = clust2sect(fp->fs, fp->clust);		/* Get current data sector */
		if (!sect) ABORT(fp->fs, FR_INT_ERR);
		sect += csect;
		if (move_window(fp->fs, sect))				/* Move sector window */
			ABORT(fp->fs, FR_DISK_ERR);
		fp->dsect = sect;
		rcnt = SS(fp->fs) - (WORD)(fp->fptr % SS(fp->fs));	/* Forward data from sector window */
		if (rcnt > btf) rcnt = btf;
		rcnt = (*func)(&fp->fs->win[(WORD)fp->fptr % SS(fp->fs)], rcnt);
		if (!rcnt) ABORT(fp->fs, FR_INT_ERR);
	}

	LEAVE_FF(fp->fs, FR_OK);
}
#endif /* _USE_FORWARD */



#if _USE_MKFS && !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Create File System on the Drive                                       */
/*-----------------------------------------------------------------------*/
#define N_ROOTDIR	512		/* Number of root directory entries for FAT12/16 */
#define N_FATS		1		/* Number of FAT copies (1 or 2) */


FRESULT f_mkfs (
	const TCHAR* path,	/* Logical drive number */
	BYTE sfd,			/* Partitioning rule 0:FDISK, 1:SFD */
	UINT au				/* Allocation unit [bytes] */
)
{
	static const WORD vst[] = { 1024,   512,  256,  128,   64,    32,   16,    8,    4,    2,   0};
	static const WORD cst[] = {32768, 16384, 8192, 4096, 2048, 16384, 8192, 4096, 2048, 1024, 512};
	int vol;
	BYTE fmt, md, sys, *tbl, pdrv, part;
	DWORD n_clst, vs, n, wsect;
	UINT i;
	DWORD b_vol, b_fat, b_dir, b_data;	/* LBA */
	DWORD n_vol, n_rsv, n_fat, n_dir;	/* Size */
	FATFS *fs;
	DSTATUS stat;


	/* Check mounted drive and clear work area */
	vol = get_ldnumber(&path);
	if (vol < 0) return FR_INVALID_DRIVE;
	if (sfd > 1) return FR_INVALID_PARAMETER;
	if (au & (au - 1)) return FR_INVALID_PARAMETER;
	fs = FatFs[vol];
	if (!fs) return FR_NOT_ENABLED;
	fs->fs_type = 0;
	pdrv = LD2PD(vol);	/* Physical drive */
	part = LD2PT(vol);	/* Partition (0:auto detect, 1-4:get from partition table)*/

	/* Get disk statics */
	stat = RES_OK;//disk_initialize(pdrv);
	if (stat & STA_NOINIT) return FR_NOT_READY;
	if (stat & STA_PROTECT) return FR_WRITE_PROTECTED;
#if _MAX_SS != 512					/* Get disk sector size */
	if (disk_ioctl(pdrv, GET_SECTOR_SIZE, &SS(fs)) != RES_OK || SS(fs) > _MAX_SS)
		return FR_DISK_ERR;
#endif
	if (_MULTI_PARTITION && part) {
		/* Get partition information from partition table in the MBR */
		if (disk_read(pdrv, fs->win, 0, 1)) return FR_DISK_ERR;
		if (LD_WORD(fs->win+BS_55AA) != 0xAA55) return FR_MKFS_ABORTED;
		tbl = &fs->win[MBR_Table + (part - 1) * SZ_PTE];
		if (!tbl[4]) return FR_MKFS_ABORTED;	/* No partition? */
		b_vol = LD_DWORD(tbl+8);	/* Volume start sector */
		n_vol = LD_DWORD(tbl+12);	/* Volume size */
	} else {
		/* Create a partition in this function */
		if (disk_ioctl(pdrv, GET_SECTOR_COUNT, &n_vol) != RES_OK || n_vol < 128)
			return FR_DISK_ERR;
		b_vol = (sfd) ? 0 : 63;		/* Volume start sector */
		n_vol -= b_vol;				/* Volume size */
	}

	if (!au) {				/* AU auto selection */
		vs = n_vol / (2000 / (SS(fs) / 512));
		for (i = 0; vs < vst[i]; i++) ;
		au = cst[i];
	}
	au /= SS(fs);		/* Number of sectors per cluster */
	if (au == 0) au = 1;
	if (au > 128) au = 128;

	/* Pre-compute number of clusters and FAT sub-type */
	n_clst = n_vol / au;
	fmt = FS_FAT12;
	if (n_clst >= MIN_FAT16) fmt = FS_FAT16;
	if (n_clst >= MIN_FAT32) fmt = FS_FAT32;

	/* Determine offset and size of FAT structure */
	if (fmt == FS_FAT32) {
		n_fat = ((n_clst * 4) + 8 + SS(fs) - 1) / SS(fs);
		n_rsv = 32;
		n_dir = 0;
	} else {
		n_fat = (fmt == FS_FAT12) ? (n_clst * 3 + 1) / 2 + 3 : (n_clst * 2) + 4;
		n_fat = (n_fat + SS(fs) - 1) / SS(fs);
		n_rsv = 1;
		n_dir = (DWORD)N_ROOTDIR * SZ_DIR / SS(fs);
	}
	b_fat = b_vol + n_rsv;				/* FAT area start sector */
	b_dir = b_fat + n_fat * N_FATS;		/* Directory area start sector */
	b_data = b_dir + n_dir;				/* Data area start sector */
	if (n_vol < b_data + au - b_vol) return FR_MKFS_ABORTED;	/* Too small volume */

	/* Align data start sector to erase block boundary (for flash memory media) */
	if (disk_ioctl(pdrv, GET_BLOCK_SIZE, &n) != RES_OK || !n || n > 32768) n = 1;
	n = (b_data + n - 1) & ~(n - 1);	/* Next nearest erase block from current data start */
	n = (n - b_data) / N_FATS;
	if (fmt == FS_FAT32) {		/* FAT32: Move FAT offset */
		n_rsv += n;
		b_fat += n;
	} else {					/* FAT12/16: Expand FAT size */
		n_fat += n;
	}

	/* Determine number of clusters and final check of validity of the FAT sub-type */
	n_clst = (n_vol - n_rsv - n_fat * N_FATS - n_dir) / au;
	if (   (fmt == FS_FAT16 && n_clst < MIN_FAT16)
		|| (fmt == FS_FAT32 && n_clst < MIN_FAT32))
		return FR_MKFS_ABORTED;

	/* Determine system ID in the partition table */
	if (fmt == FS_FAT32) {
		sys = 0x0C;		/* FAT32X */
	} else {
		if (fmt == FS_FAT12 && n_vol < 0x10000) {
			sys = 0x01;	/* FAT12(<65536) */
		} else {
			sys = (n_vol < 0x10000) ? 0x04 : 0x06;	/* FAT16(<65536) : FAT12/16(>=65536) */
		}
	}

	if (_MULTI_PARTITION && part) {
		/* Update system ID in the partition table */
		tbl = &fs->win[MBR_Table + (part - 1) * SZ_PTE];
		tbl[4] = sys;
		if (disk_write(pdrv, fs->win, 0, 1))	/* Write it to teh MBR */
			return FR_DISK_ERR;
		md = 0xF8;
	} else {
		if (sfd) {	/* No partition table (SFD) */
			md = 0xF0;
		} else {	/* Create partition table (FDISK) */
			mem_set(fs->win, 0, SS(fs));
			tbl = fs->win+MBR_Table;	/* Create partition table for single partition in the drive */
			tbl[1] = 1;						/* Partition start head */
			tbl[2] = 1;						/* Partition start sector */
			tbl[3] = 0;						/* Partition start cylinder */
			tbl[4] = sys;					/* System type */
			tbl[5] = 254;					/* Partition end head */
			n = (b_vol + n_vol) / 63 / 255;
			tbl[6] = (BYTE)(n >> 2 | 63);	/* Partition end sector */
			tbl[7] = (BYTE)n;				/* End cylinder */
			ST_DWORD(tbl+8, 63);			/* Partition start in LBA */
			ST_DWORD(tbl+12, n_vol);		/* Partition size in LBA */
			ST_WORD(fs->win+BS_55AA, 0xAA55);	/* MBR signature */
			if (disk_write(pdrv, fs->win, 0, 1))	/* Write it to the MBR */
				return FR_DISK_ERR;
			md = 0xF8;
		}
	}

	/* Create BPB in the VBR */
	tbl = fs->win;							/* Clear sector */
	mem_set(tbl, 0, SS(fs));
	mem_cpy(tbl, "\xEB\xFE\x90" "MSDOS5.0", 11);/* Boot jump code, OEM name */
	i = SS(fs);								/* Sector size */
	ST_WORD(tbl+BPB_BytsPerSec, i);
	tbl[BPB_SecPerClus] = (BYTE)au;			/* Sectors per cluster */
	ST_WORD(tbl+BPB_RsvdSecCnt, n_rsv);		/* Reserved sectors */
	tbl[BPB_NumFATs] = N_FATS;				/* Number of FATs */
	i = (fmt == FS_FAT32) ? 0 : N_ROOTDIR;	/* Number of root directory entries */
	ST_WORD(tbl+BPB_RootEntCnt, i);
	if (n_vol < 0x10000) {					/* Number of total sectors */
		ST_WORD(tbl+BPB_TotSec16, n_vol);
	} else {
		ST_DWORD(tbl+BPB_TotSec32, n_vol);
	}
	tbl[BPB_Media] = md;					/* Media descriptor */
	ST_WORD(tbl+BPB_SecPerTrk, 63);			/* Number of sectors per track */
	ST_WORD(tbl+BPB_NumHeads, 255);			/* Number of heads */
	ST_DWORD(tbl+BPB_HiddSec, b_vol);		/* Hidden sectors */
	n = get_fattime();						/* Use current time as VSN */
	if (fmt == FS_FAT32) {
		ST_DWORD(tbl+BS_VolID32, n);		/* VSN */
		ST_DWORD(tbl+BPB_FATSz32, n_fat);	/* Number of sectors per FAT */
		ST_DWORD(tbl+BPB_RootClus, 2);		/* Root directory start cluster (2) */
		ST_WORD(tbl+BPB_FSInfo, 1);			/* FSINFO record offset (VBR+1) */
		ST_WORD(tbl+BPB_BkBootSec, 6);		/* Backup boot record offset (VBR+6) */
		tbl[BS_DrvNum32] = 0x80;			/* Drive number */
		tbl[BS_BootSig32] = 0x29;			/* Extended boot signature */
		mem_cpy(tbl+BS_VolLab32, "NO NAME    " "FAT32   ", 19);	/* Volume label, FAT signature */
	} else {
		ST_DWORD(tbl+BS_VolID, n);			/* VSN */
		ST_WORD(tbl+BPB_FATSz16, n_fat);	/* Number of sectors per FAT */
		tbl[BS_DrvNum] = 0x80;				/* Drive number */
		tbl[BS_BootSig] = 0x29;				/* Extended boot signature */
		mem_cpy(tbl+BS_VolLab, "NO NAME    " "FAT     ", 19);	/* Volume label, FAT signature */
	}
	ST_WORD(tbl+BS_55AA, 0xAA55);			/* Signature (Offset is fixed here regardless of sector size) */
	if (disk_write(pdrv, tbl, b_vol, 1))	/* Write it to the VBR sector */
		return FR_DISK_ERR;
	if (fmt == FS_FAT32)					/* Write backup VBR if needed (VBR+6) */
		disk_write(pdrv, tbl, b_vol + 6, 1);

	/* Initialize FAT area */
	wsect = b_fat;
	for (i = 0; i < N_FATS; i++) {		/* Initialize each FAT copy */
		mem_set(tbl, 0, SS(fs));			/* 1st sector of the FAT  */
		n = md;								/* Media descriptor byte */
		if (fmt != FS_FAT32) {
			n |= (fmt == FS_FAT12) ? 0x00FFFF00 : 0xFFFFFF00;
			ST_DWORD(tbl+0, n);				/* Reserve cluster #0-1 (FAT12/16) */
		} else {
			n |= 0xFFFFFF00;
			ST_DWORD(tbl+0, n);				/* Reserve cluster #0-1 (FAT32) */
			ST_DWORD(tbl+4, 0xFFFFFFFF);
			ST_DWORD(tbl+8, 0x0FFFFFFF);	/* Reserve cluster #2 for root directory */
		}
		if (disk_write(pdrv, tbl, wsect++, 1))
			return FR_DISK_ERR;
		mem_set(tbl, 0, SS(fs));			/* Fill following FAT entries with zero */
		for (n = 1; n < n_fat; n++) {		/* This loop may take a time on FAT32 volume due to many single sector writes */
			if (disk_write(pdrv, tbl, wsect++, 1))
				return FR_DISK_ERR;
		}
	}

	/* Initialize root directory */
	i = (fmt == FS_FAT32) ? au : n_dir;
	do {
		if (disk_write(pdrv, tbl, wsect++, 1))
			return FR_DISK_ERR;
	} while (--i);

#if _USE_ERASE	/* Erase data area if needed */
	{
		DWORD eb[2];

		eb[0] = wsect; eb[1] = wsect + (n_clst - ((fmt == FS_FAT32) ? 1 : 0)) * au - 1;
		disk_ioctl(pdrv, CTRL_ERASE_SECTOR, eb);
	}
#endif

	/* Create FSINFO if needed */
	if (fmt == FS_FAT32) {
		ST_DWORD(tbl+FSI_LeadSig, 0x41615252);
		ST_DWORD(tbl+FSI_StrucSig, 0x61417272);
		ST_DWORD(tbl+FSI_Free_Count, n_clst - 1);	/* Number of free clusters */
		ST_DWORD(tbl+FSI_Nxt_Free, 2);				/* Last allocated cluster# */
		ST_WORD(tbl+BS_55AA, 0xAA55);
		disk_write(pdrv, tbl, b_vol + 1, 1);	/* Write original (VBR+1) */
		disk_write(pdrv, tbl, b_vol + 7, 1);	/* Write backup (VBR+7) */
	}

	return (disk_ioctl(pdrv, CTRL_SYNC, 0) == RES_OK) ? FR_OK : FR_DISK_ERR;
}



#if _MULTI_PARTITION
/*-----------------------------------------------------------------------*/
/* Divide Physical Drive                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_fdisk (
	BYTE pdrv,			/* Physical drive number */
	const DWORD szt[],	/* Pointer to the size table for each partitions */
	void* work			/* Pointer to the working buffer */
)
{
	UINT i, n, sz_cyl, tot_cyl, b_cyl, e_cyl, p_cyl;
	BYTE s_hd, e_hd, *p, *buf = (BYTE*)work;
	DSTATUS stat;
	DWORD sz_disk, sz_part, s_part;


	stat = RES_OK;//disk_initialize(pdrv);
	if (stat & STA_NOINIT) return FR_NOT_READY;
	if (stat & STA_PROTECT) return FR_WRITE_PROTECTED;
	if (disk_ioctl(pdrv, GET_SECTOR_COUNT, &sz_disk)) return FR_DISK_ERR;

	/* Determine CHS in the table regardless of the drive geometry */
	for (n = 16; n < 256 && sz_disk / n / 63 > 1024; n *= 2) ;
	if (n == 256) n--;
	e_hd = n - 1;
	sz_cyl = 63 * n;
	tot_cyl = sz_disk / sz_cyl;

	/* Create partition table */
	mem_set(buf, 0, _MAX_SS);
	p = buf + MBR_Table; b_cyl = 0;
	for (i = 0; i < 4; i++, p += SZ_PTE) {
		p_cyl = (szt[i] <= 100U) ? (DWORD)tot_cyl * szt[i] / 100 : szt[i] / sz_cyl;
		if (!p_cyl) continue;
		s_part = (DWORD)sz_cyl * b_cyl;
		sz_part = (DWORD)sz_cyl * p_cyl;
		if (i == 0) {	/* Exclude first track of cylinder 0 */
			s_hd = 1;
			s_part += 63; sz_part -= 63;
		} else {
			s_hd = 0;
		}
		e_cyl = b_cyl + p_cyl - 1;
		if (e_cyl >= tot_cyl) return FR_INVALID_PARAMETER;

		/* Set partition table */
		p[1] = s_hd;						/* Start head */
		p[2] = (BYTE)((b_cyl >> 2) + 1);	/* Start sector */
		p[3] = (BYTE)b_cyl;					/* Start cylinder */
		p[4] = 0x06;						/* System type (temporary setting) */
		p[5] = e_hd;						/* End head */
		p[6] = (BYTE)((e_cyl >> 2) + 63);	/* End sector */
		p[7] = (BYTE)e_cyl;					/* End cylinder */
		ST_DWORD(p + 8, s_part);			/* Start sector in LBA */
		ST_DWORD(p + 12, sz_part);			/* Partition size */

		/* Next partition */
		b_cyl += p_cyl;
	}
	ST_WORD(p, 0xAA55);

	/* Write it to the MBR */
	return (disk_write(pdrv, buf, 0, 1) || disk_ioctl(pdrv, CTRL_SYNC, 0)) ? FR_DISK_ERR : FR_OK;
}


#endif /* _MULTI_PARTITION */
#endif /* _USE_MKFS && !_FS_READONLY */




#if _USE_STRFUNC
/*-----------------------------------------------------------------------*/
/* Get a string from the file                                            */
/*-----------------------------------------------------------------------*/

TCHAR* f_gets (
	TCHAR* buff,	/* Pointer to the string buffer to read */
	int len,		/* Size of string buffer (characters) */
	FIL* fp			/* Pointer to the file object */
)
{
	int n = 0;
	TCHAR c, *p = buff;
	BYTE s[2];
	UINT rc;


	while (n < len - 1) {	/* Read characters until buffer gets filled */
#if _LFN_UNICODE
#if _STRF_ENCODE == 3		/* Read a character in UTF-8 */
		f_read(fp, s, 1, &rc);
		if (rc != 1) break;
		c = s[0];
		if (c >= 0x80) {
			if (c < 0xC0) continue;	/* Skip stray trailer */
			if (c < 0xE0) {			/* Two-byte sequence */
				f_read(fp, s, 1, &rc);
				if (rc != 1) break;
				c = (c & 0x1F) << 6 | (s[0] & 0x3F);
				if (c < 0x80) c = '?';
			} else {
				if (c < 0xF0) {		/* Three-byte sequence */
					f_read(fp, s, 2, &rc);
					if (rc != 2) break;
					c = c << 12 | (s[0] & 0x3F) << 6 | (s[1] & 0x3F);
					if (c < 0x800) c = '?';
				} else {			/* Reject four-byte sequence */
					c = '?';
				}
			}
		}
#elif _STRF_ENCODE == 2		/* Read a character in UTF-16BE */
		f_read(fp, s, 2, &rc);
		if (rc != 2) break;
		c = s[1] + (s[0] << 8);
#elif _STRF_ENCODE == 1		/* Read a character in UTF-16LE */
		f_read(fp, s, 2, &rc);
		if (rc != 2) break;
		c = s[0] + (s[1] << 8);
#else						/* Read a character in ANSI/OEM */
		f_read(fp, s, 1, &rc);
		if (rc != 1) break;
		c = s[0];
		if (IsDBCS1(c)) {
			f_read(fp, s, 1, &rc);
			if (rc != 1) break;
			c = (c << 8) + s[0];
		}
		c = ff_convert(c, 1);	/* OEM -> Unicode */
		if (!c) c = '?';
#endif
#else						/* Read a character without conversion */
		f_read(fp, s, 1, &rc);
		if (rc != 1) break;
		c = s[0];
#endif
		if (_USE_STRFUNC == 2 && c == '\r') continue;	/* Strip '\r' */
		*p++ = c;
		n++;
		if (c == '\n') break;		/* Break on EOL */
	}
	*p = 0;
	return n ? buff : 0;			/* When no data read (eof or error), return with error. */
}



#if !_FS_READONLY
#include <stdarg.h>
/*-----------------------------------------------------------------------*/
/* Put a character to the file                                           */
/*-----------------------------------------------------------------------*/

typedef struct {
	FIL* fp;
	int idx, nchr;
	BYTE buf[64];
} putbuff;


static
void putc_bfd (
	putbuff* pb,
	TCHAR c
)
{
	UINT bw;
	int i;


	if (_USE_STRFUNC == 2 && c == '\n')	 /* LF -> CRLF conversion */
		putc_bfd(pb, '\r');

	i = pb->idx;	/* Buffer write index (-1:error) */
	if (i < 0) return;

#if _LFN_UNICODE
#if _STRF_ENCODE == 3			/* Write a character in UTF-8 */
	if (c < 0x80) {				/* 7-bit */
		pb->buf[i++] = (BYTE)c;
	} else {
		if (c < 0x800) {		/* 11-bit */
			pb->buf[i++] = (BYTE)(0xC0 | c >> 6);
		} else {				/* 16-bit */
			pb->buf[i++] = (BYTE)(0xE0 | c >> 12);
			pb->buf[i++] = (BYTE)(0x80 | (c >> 6 & 0x3F));
		}
		pb->buf[i++] = (BYTE)(0x80 | (c & 0x3F));
	}
#elif _STRF_ENCODE == 2			/* Write a character in UTF-16BE */
	pb->buf[i++] = (BYTE)(c >> 8);
	pb->buf[i++] = (BYTE)c;
#elif _STRF_ENCODE == 1			/* Write a character in UTF-16LE */
	pb->buf[i++] = (BYTE)c;
	pb->buf[i++] = (BYTE)(c >> 8);
#else							/* Write a character in ANSI/OEM */
	c = ff_convert(c, 0);	/* Unicode -> OEM */
	if (!c) c = '?';
	if (c >= 0x100)
		pb->buf[i++] = (BYTE)(c >> 8);
	pb->buf[i++] = (BYTE)c;
#endif
#else							/* Write a character without conversion */
	pb->buf[i++] = (BYTE)c;
#endif

	if (i >= (int)(sizeof pb->buf) - 3) {	/* Write buffered characters to the file */
		f_write(pb->fp, pb->buf, (UINT)i, &bw);
		i = (bw == (UINT)i) ? 0 : -1;
	}
	pb->idx = i;
	pb->nchr++;
}



int f_putc (
	TCHAR c,	/* A character to be output */
	FIL* fp		/* Pointer to the file object */
)
{
	putbuff pb;
	UINT nw;


	pb.fp = fp;			/* Initialize output buffer */
	pb.nchr = pb.idx = 0;

	putc_bfd(&pb, c);	/* Put a character */

	if (   pb.idx >= 0	/* Flush buffered characters to the file */
		&& f_write(pb.fp, pb.buf, (UINT)pb.idx, &nw) == FR_OK
		&& (UINT)pb.idx == nw) return pb.nchr;
	return EOF;
}




/*-----------------------------------------------------------------------*/
/* Put a string to the file                                              */
/*-----------------------------------------------------------------------*/

int f_puts (
	const TCHAR* str,	/* Pointer to the string to be output */
	FIL* fp				/* Pointer to the file object */
)
{
	putbuff pb;
	UINT nw;


	pb.fp = fp;				/* Initialize output buffer */
	pb.nchr = pb.idx = 0;

	while (*str)			/* Put the string */
		putc_bfd(&pb, *str++);

	if (   pb.idx >= 0		/* Flush buffered characters to the file */
		&& f_write(pb.fp, pb.buf, (UINT)pb.idx, &nw) == FR_OK
		&& (UINT)pb.idx == nw) return pb.nchr;
	return EOF;
}




/*-----------------------------------------------------------------------*/
/* Put a formatted string to the file                                    */
/*-----------------------------------------------------------------------*/

int f_printf (
	FIL* fp,			/* Pointer to the file object */
	const TCHAR* fmt,	/* Pointer to the format string */
	...					/* Optional arguments... */
)
{
	va_list arp;
	BYTE f, r;
	UINT nw, i, j, w;
	DWORD v;
	TCHAR c, d, s[16], *p;
	putbuff pb;


	pb.fp = fp;				/* Initialize output buffer */
	pb.nchr = pb.idx = 0;

	va_start(arp, fmt);

	for (;;) {
		c = *fmt++;
		if (c == 0) break;			/* End of string */
		if (c != '%') {				/* Non escape character */
			putc_bfd(&pb, c);
			continue;
		}
		w = f = 0;
		c = *fmt++;
		if (c == '0') {				/* Flag: '0' padding */
			f = 1; c = *fmt++;
		} else {
			if (c == '-') {			/* Flag: left justified */
				f = 2; c = *fmt++;
			}
		}
		while (IsDigit(c)) {		/* Precision */
			w = w * 10 + c - '0';
			c = *fmt++;
		}
		if (c == 'l' || c == 'L') {	/* Prefix: Size is long int */
			f |= 4; c = *fmt++;
		}
		if (!c) break;
		d = c;
		if (IsLower(d)) d -= 0x20;
		switch (d) {				/* Type is... */
		case 'S' :					/* String */
			p = va_arg(arp, TCHAR*);
			for (j = 0; p[j]; j++) ;
			if (!(f & 2)) {
				while (j++ < w) putc_bfd(&pb, ' ');
			}
			while (*p) putc_bfd(&pb, *p++);
			while (j++ < w) putc_bfd(&pb, ' ');
			continue;
		case 'C' :					/* Character */
			putc_bfd(&pb, (TCHAR)va_arg(arp, int)); continue;
		case 'B' :					/* Binary */
			r = 2; break;
		case 'O' :					/* Octal */
			r = 8; break;
		case 'D' :					/* Signed decimal */
		case 'U' :					/* Unsigned decimal */
			r = 10; break;
		case 'X' :					/* Hexdecimal */
			r = 16; break;
		default:					/* Unknown type (pass-through) */
			putc_bfd(&pb, c); continue;
		}

		/* Get an argument and put it in numeral */
		v = (f & 4) ? (DWORD)va_arg(arp, long) : ((d == 'D') ? (DWORD)(long)va_arg(arp, int) : (DWORD)va_arg(arp, unsigned int));
		if (d == 'D' && (v & 0x80000000)) {
			v = 0 - v;
			f |= 8;
		}
		i = 0;
		do {
			d = (TCHAR)(v % r); v /= r;
			if (d > 9) d += (c == 'x') ? 0x27 : 0x07;
			s[i++] = d + '0';
		} while (v && i < sizeof s / sizeof s[0]);
		if (f & 8) s[i++] = '-';
		j = i; d = (f & 1) ? '0' : ' ';
		while (!(f & 2) && j++ < w) putc_bfd(&pb, d);
		do putc_bfd(&pb, s[--i]); while (i);
		while (j++ < w) putc_bfd(&pb, d);
	}

	va_end(arp);

	if (   pb.idx >= 0		/* Flush buffered characters to the file */
		&& f_write(pb.fp, pb.buf, (UINT)pb.idx, &nw) == FR_OK
		&& (UINT)pb.idx == nw) return pb.nchr;
	return EOF;
}

#endif /* !_FS_READONLY */
#endif /* _USE_STRFUNC */

#endif //WITH_FILESYSTEM
