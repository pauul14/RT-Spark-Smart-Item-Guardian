/*
 * ffconf.h - FatFs R0.12c configuration for the Smart Item Guardian microSD event log.
 * Derived from ST's ffconf_template.h (STM32CubeF4 1.28.1). Minimal, write-capable,
 * 8.3 names, no mkfs: the firmware NEVER formats a card, it only creates/appends
 * its own directory and file on an existing FAT volume. Single user (health task),
 * so no re-entrancy layer is needed.
 */
#ifndef _FFCONF
#define _FFCONF 68300

#define _FS_READONLY    0
#define _FS_MINIMIZE    0
#define _USE_STRFUNC    0
#define _USE_FIND       0
#define _USE_MKFS       0      /* never format the user's card */
#define _USE_FASTSEEK   0
#define _USE_EXPAND     0
#define _USE_CHMOD      0
#define _USE_LABEL      0
#define _USE_FORWARD    0

#define _CODE_PAGE      437
#define _USE_LFN        0      /* 8.3 names: SIGUARD/EVENTS.CSV */
#define _MAX_LFN        255
#define _LFN_UNICODE    0
#define _STRF_ENCODE    0
#define _FS_RPATH       0

#define _VOLUMES        1
#define _STR_VOLUME_ID  0
#define _VOLUME_STRS    "SD"
#define _MULTI_PARTITION 0
#define _MIN_SS         512
#define _MAX_SS         512
#define _USE_TRIM       0
#define _FS_NOFSINFO    0

#define _FS_TINY        1      /* share the volume window buffer: saves 512 B per file */
#define _FS_EXFAT       0      /* FAT12/16/32 only (SD/SDHC); exFAT (SDXC) is reported as unsupported */
#define _FS_NORTC       0      /* timestamps from the RTC via get_fattime() */
#define _NORTC_MON      1
#define _NORTC_MDAY     1
#define _NORTC_YEAR     2026

#define _FS_LOCK        0
#define _FS_REENTRANT   0
#define _FS_TIMEOUT     1000
#define _SYNC_t         void *

#endif /* _FFCONF */
