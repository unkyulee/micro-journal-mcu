#pragma once

#if defined(USE_LITTLEFS)

#include "app/FileSystem/FileSystem.h"
#include "app/app.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS64.h>

// ESP32 internal flash formatted as LittleFS with 64KB blocks
// power loss safe: an interrupted write never corrupts the file system
class FileSystemLittleFS : public FileSystem
{
public:
    FileSystemLittleFS(
        const char *partitionLabel = "storage",
        uint32_t blockSize = 64 * 1024,
        bool formatOnFail = true)
        : _partitionLabel(partitionLabel),
          _blockSize(blockSize),
          _formatOnFail(formatOnFail),
          _mounted(false)
    {
    }

    bool begin() override
    {
        if (_mounted)
        {
            return true;
        }

        _log("LittleFS Init: partition=%s block=%u\n",
             _partitionLabel,
             (unsigned int)_blockSize);

        // a partition without LittleFS on it (first boot, data left by an
        // older file system or another block size) is formatted when
        // formatOnFail is set
        bool ok = LittleFS64.begin(_formatOnFail, _partitionLabel, _blockSize);

        if (!ok)
        {
            _log("LittleFS Init Failed\n");
            _mounted = false;
            return false;
        }

        _mounted = true;

        _log("LittleFS Init OK: total=%u used=%u\n",
             (unsigned int)LittleFS64.totalBytes(),
             (unsigned int)LittleFS64.usedBytes());

        ensureReadyFile();

        return true;
    }

    void end() override
    {
        if (!_mounted)
        {
            return;
        }

        LittleFS64.end();
        _mounted = false;

        _log("LittleFS End\n");
    }

    bool mounted() const
    {
        return _mounted;
    }

    bool ensureReadyFile()
    {
        if (!_mounted)
        {
            return false;
        }

        if (LittleFS64.exists("/ready.txt"))
        {
            return true;
        }

        File file = LittleFS64.open("/ready.txt", FILE_WRITE);

        if (!file)
        {
            _log("LittleFS ready.txt create failed\n");
            return false;
        }

        file.println("ESP32-S3 LittleFS storage ready");
        file.close();

        _log("LittleFS ready.txt created\n");

        return true;
    }

    File open(const char *path, const char *mode) override
    {
        if (!_mounted)
        {
            _log("LittleFS open failed, not mounted: %s\n", path);
            return File();
        }

        return LittleFS64.open(path, mode);
    }

    bool exists(const char *path) override
    {
        if (!_mounted)
        {
            return false;
        }

        return LittleFS64.exists(path);
    }

    bool remove(const char *path) override
    {
        if (!_mounted)
        {
            return false;
        }

        return LittleFS64.remove(path);
    }

    bool rename(const char *pathFrom, const char *pathTo) override
    {
        if (!_mounted)
        {
            return false;
        }

        return LittleFS64.rename(pathFrom, pathTo);
    }

    uint64_t totalBytes() override
    {
        if (!_mounted)
        {
            return 0;
        }

        return LittleFS64.totalBytes();
    }

    uint64_t usedBytes() override
    {
        if (!_mounted)
        {
            return 0;
        }

        return LittleFS64.usedBytes();
    }

private:
    const char *_partitionLabel;
    uint32_t _blockSize;
    bool _formatOnFail;
    bool _mounted;
};

#endif
