#pragma once
#include <SD.h>
#include <sys/stat.h>
#include <errno.h>

// A File/exists query opens and stats the same FAT path several times. Cache
// inventory needs metadata only, so perform one VFS lookup without opening it.
struct V5SdMetadata {
  uint64_t size = 0, modified = 0;
  bool directory = false;
  enum Result { Found, Missing, Error };
  static Result read(const String &path, V5SdMetadata &out) {
    const char *mount = SD.mountpoint();
    if (!mount) return Error;
    const String full = String(mount) + path;
    struct stat info{};
    if (::stat(full.c_str(), &info) != 0)
      return errno == ENOENT ? Missing : Error;
    out.size = info.st_size;
    out.modified = info.st_mtime;
    out.directory = S_ISDIR(info.st_mode);
    return Found;
  }
};
