#pragma once

//
#include <Arduino.h>
#include <FS.h>

using fs::File;

class FileSystem {
public:
    virtual bool begin() = 0;
    virtual void end() {};
    virtual File open(const char* path, const char* mode) = 0;
    virtual bool exists(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;

    // storage capacity in bytes, 0 when the backend can't tell
    virtual uint64_t totalBytes() { return 0; }
    virtual uint64_t usedBytes() { return 0; }
    virtual ~FileSystem() = default;
};