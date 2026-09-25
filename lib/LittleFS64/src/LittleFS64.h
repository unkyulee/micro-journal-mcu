#pragma once

#include <FS.h>

// LittleFS on an ESP32 data partition with a configurable block size
//
// The LittleFS that ships with the ESP32 core is fixed to the 4KB flash
// sector. This one talks to the partition directly so a littlefs block can
// span several sectors - 64KB blocks are erased with a single block erase
// command and keep the allocation tables small.
//
// It is an ordinary fs::FS, so File, open(), exists() etc. work as usual.
// Paths are passed to littlefs as is, there is no mount point prefix.
// All access is serialized with a mutex, it is safe to use from both cores.

namespace fs
{

class LittleFS64FS : public FS
{
public:
    LittleFS64FS();

    // blockSize must be a multiple of the 4KB flash sector and divide the
    // partition size. A partition that can't be mounted - blank, another
    // file system on it, or a different block size - is formatted when
    // formatOnFail is set.
    bool begin(bool formatOnFail = false,
               const char *partitionLabel = "storage",
               uint32_t blockSize = 65536);
    void end();

    // erase everything, only after begin() located the partition
    bool format();

    bool mounted();
    size_t totalBytes();
    size_t usedBytes();
    uint32_t blockSize();
};

} // namespace fs

extern fs::LittleFS64FS LittleFS64;
