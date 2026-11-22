#include "sqlite_fdvfs.h"
#include "sqlite3.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#ifndef SQLITE_DEFAULT_SECTOR_SIZE
# define SQLITE_DEFAULT_SECTOR_SIZE 512
#endif

#ifndef SQLITE_FDVFS_MAX_PATH
#define SQLITE_FDVFS_MAX_PATH 512
#endif

#ifndef SQLITE_FDVFS_NAME
#define SQLITE_FDVFS_NAME "fdvfs"
#endif

// sqlite vfs context
typedef struct ndk_vfs ndk_vfs;
struct ndk_vfs
{
  sqlite3_vfs vfs;
  sqlite3_vfs* vfsDefault;
  const struct sqlite3_io_methods *pMethods;
};

// sqlite vfs file
typedef struct ndk_file ndk_file;
struct ndk_file
{
  const sqlite3_io_methods *pMethod;
  int fd;
};

// sqlite3_vfs.xOpen - open database file.
static int ndkOpen(sqlite3_vfs *pVfs, const char *zPath, sqlite3_file *pFile, int flags, int *pOutFlags)
{
  const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  ndk_file *ndkFile = (ndk_file*) pFile;

  ndkFile->fd = -1;
  ndkFile->pMethod = NULL;
  if (!zPath ||
      (flags & SQLITE_OPEN_DELETEONCLOSE) ||
      !(flags & SQLITE_OPEN_READONLY) ||
      (flags & SQLITE_OPEN_READWRITE) ||
      (flags & SQLITE_OPEN_CREATE) ||
      !(flags & SQLITE_OPEN_MAIN_DB)) {
    return SQLITE_PERM;
  }

  const char* lastsep = strrchr(zPath, '/');
  ndkFile->fd = atoi(lastsep ? lastsep + 1 : zPath);
  if(ndkFile->fd <= 0)
    return SQLITE_CANTOPEN;

  ndkFile->pMethod = ndk->pMethods;
  if(pOutFlags)
    *pOutFlags = flags;

  return SQLITE_OK;
}

// sqlite3_vfs.xDelete - not implemented. Assets in .apk are read only
static int ndkDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{
  return SQLITE_ERROR;
}

// sqlite3_vfs.xAccess - tests if file exists and/or can be read.
static int ndkAccess(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut)
{
  //const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  *pResOut = 0;
  switch (flags) {
  case SQLITE_ACCESS_EXISTS:
  case SQLITE_ACCESS_READ:
    *pResOut = 1;
    break;
  }
  return SQLITE_OK;
}

// sqlite3_vfs.xFullPathname - just return copy of input path
static int ndkFullPathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut)
{
  if (!zPath)
    return SQLITE_ERROR;

  int pos = 0;
  while(zPath[pos] && (pos < nOut)) {
    zOut[pos] = zPath[pos];
    ++pos;
  }
  if(pos >= nOut)
    return SQLITE_ERROR;
  zOut[pos] = '\0';
  return SQLITE_OK;
}

// sqlite3_vfs.xRandomness - call redirected to default VFS.
static int ndkRandomness(sqlite3_vfs *pVfs, int nBuf, char *zBuf)
{
  const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  return ndk->vfsDefault->xRandomness(ndk->vfsDefault, nBuf, zBuf);
}

// sqlite3_vfs.xSleep - call redirected to default VFS.
static int ndkSleep(sqlite3_vfs *pVfs, int microseconds)
{
  const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  return ndk->vfsDefault->xSleep(ndk->vfsDefault, microseconds);
}

//sqlite3_vfs.xCurrentTime - call redirected to default VFS.
static int ndkCurrentTime(sqlite3_vfs *pVfs, double *prNow)
{
  const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  return ndk->vfsDefault->xCurrentTime(ndk->vfsDefault, prNow);
}

// sqlite3_vfs.xGetLastError - not implemented (no additional information)
static int ndkGetLastError(sqlite3_vfs *NotUsed1, int NotUsed2, char *NotUsed3)
{
  return 0;
}

// sqlite3_vfs.xCurrentTimeInt64 - call redirected to default VFS.
static int ndkCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *piNow)
{
  const ndk_vfs* ndk = (ndk_vfs*) pVfs;
  return ndk->vfsDefault->xCurrentTimeInt64(ndk->vfsDefault, piNow);
}

// sqlite3_file.xClose - closing file opened in sqlite3_vfs.xOpen function.
static int ndkFileClose(sqlite3_file *pFile)
{
  ndk_file* file = (ndk_file*) pFile;
  //if(file->strm)
  //  fclose(file->strm);
  //file->strm = NULL;
  file->fd = -1;
  return SQLITE_OK;
}

//  sqlite3_file.xRead - database read from asset memory.
static int ndkFileRead(sqlite3_file *pFile, void *pBuf, int amt, sqlite3_int64 offset)
{
  const ndk_file* file = (ndk_file*) pFile;
  if(file->fd < 0)
    return SQLITE_IOERR_READ;

  // pread() doesn't use file offset, so safe if fd is shared by multiple connections (lseek + read is not)
  ssize_t got = pread(file->fd, pBuf, amt, offset);
  if(got == amt)
    return SQLITE_OK;
  if(got >= 0) {  // SQLite requires unread part of buffer be zero-filled
    memset(&((char*) pBuf)[got], 0, amt - got);
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_IOERR_READ;
}

// sqlite3_file.xWrite - not implemented (.apk is read-only)
static int ndkFileWrite(sqlite3_file *, const void *, int, sqlite3_int64)
{
  return SQLITE_IOERR_WRITE;
}

// sqlite3_file.xTruncate - not implemented (.apk is read-only)
static int ndkFileTruncate(sqlite3_file *, sqlite3_int64)
{
  return SQLITE_IOERR_TRUNCATE;
}

// sqlite3_file.xSync - not implemented (.apk is read-only)
static int ndkFileSync(sqlite3_file *, int flags)
{
  return SQLITE_IOERR_FSYNC;
}

// sqlite3_file.xFileSize - get database file size.
static int ndkFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize)
{
  const ndk_file* file = (ndk_file*) pFile;
  if(file->fd < 0) return SQLITE_ERROR;
  *pSize = lseek(file->fd, 0, SEEK_END);
  //long pos = ftell(file);
  //fseek(file->strm, 0, SEEK_END);
  //*pSize = ftell(file->strm);
  //fseek(file, pos, SEEK_SET);
  return SQLITE_OK; //size >= 0 && size < LONG_MAX ? (size_t)size : SIZE_MAX;  // seems we get LONG_MAX for directory on Linux!
}

// sqlite3_file.xLock - not implemented (.apk is read-only)
static int ndkFileLock(sqlite3_file *, int)
{
  return SQLITE_OK;
}

// sqlite3_file.xUnlock - not implemented (.apk is read-only)
static int ndkFileUnlock(sqlite3_file *, int)
{
  return SQLITE_OK;
}

// sqlite3_file.xCheckReservedLock - not implemented (.apk is read-only)
static int ndkFileCheckReservedLock(sqlite3_file *, int *pResOut)
{
  *pResOut = 0;
  return SQLITE_OK;
}

// sqlite3_file.xFileControl - not implemented (no special codes needed for now)
static int ndkFileControl(sqlite3_file *, int, void *)
{
  return SQLITE_NOTFOUND;
}

// sqlite3_file.xSectorSize - use same value as in os_unix.c
static int ndkFileSectorSize(sqlite3_file *)
{
  return SQLITE_DEFAULT_SECTOR_SIZE;
}

// sqlite3_file.xDeviceCharacteristics - not implemented (.apk is read-only)
static int ndkFileDeviceCharacteristics(sqlite3_file *)
{
  return 0;
}

// Register into SQLite. For more information see sqlite3ndk.h
int sqlite3_fdvfs_init(const char* vfsName, int makeDflt, const char *osVfs)
{
  static ndk_vfs ndkVfs;

  // Check if there was successful call to sqlite3_ndk_init before
  //if (ndkVfs.vfsDefault) return ndkVfs.mgr == assetMgr ? SQLITE_OK : SQLITE_ERROR

  // Find os VFS. Used to redirect xRandomness, xSleep, xCurrentTime, ndkCurrentTimeInt64 calls
  ndkVfs.vfsDefault = sqlite3_vfs_find(osVfs);
  if (ndkVfs.vfsDefault == NULL)
    return SQLITE_ERROR;

  // vfsFile
  static const sqlite3_io_methods ndkFileMethods = {
    1,
    ndkFileClose,
    ndkFileRead,
    ndkFileWrite,
    ndkFileTruncate,
    ndkFileSync,
    ndkFileSize,
    ndkFileLock,
    ndkFileUnlock,
    ndkFileCheckReservedLock,
    ndkFileControl,
    ndkFileSectorSize,
    ndkFileDeviceCharacteristics
  };

  // pMethods will be used in ndkOpen
  ndkVfs.pMethods = &ndkFileMethods;

  // vfs
  ndkVfs.vfs.iVersion = 3;
  ndkVfs.vfs.szOsFile = sizeof(ndk_file);
  ndkVfs.vfs.mxPathname = SQLITE_FDVFS_MAX_PATH;
  ndkVfs.vfs.pNext = 0;
  ndkVfs.vfs.zName = vfsName ? vfsName : SQLITE_FDVFS_NAME;
  ndkVfs.vfs.pAppData = 0;
  ndkVfs.vfs.xOpen = ndkOpen;
  ndkVfs.vfs.xDelete = ndkDelete;
  ndkVfs.vfs.xAccess = ndkAccess;
  ndkVfs.vfs.xFullPathname = ndkFullPathname;
  ndkVfs.vfs.xDlOpen = 0;
  ndkVfs.vfs.xDlError = 0;
  ndkVfs.vfs.xDlSym = 0;
  ndkVfs.vfs.xDlClose = 0;
  ndkVfs.vfs.xRandomness = ndkRandomness;
  ndkVfs.vfs.xSleep = ndkSleep;
  ndkVfs.vfs.xCurrentTime = ndkCurrentTime;
  ndkVfs.vfs.xGetLastError = ndkGetLastError;
  ndkVfs.vfs.xCurrentTimeInt64 = ndkCurrentTimeInt64;
  ndkVfs.vfs.xSetSystemCall = 0;
  ndkVfs.vfs.xGetSystemCall = 0;
  ndkVfs.vfs.xNextSystemCall = 0;

  return sqlite3_vfs_register(&ndkVfs.vfs, makeDflt);
}
