#pragma once

#if defined(USE_LITTLEFS)

#include "app/FileSystem/FileSystem.h"
#include "app/app.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS64.h>
#include <esp_partition.h>

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

        // never format as a side effect of a failed mount - that would
        // silently erase the journal after a transient read error
        bool ok = LittleFS64.begin(false, _partitionLabel, _blockSize);

        // a blank partition (first boot after flashing) has nothing to lose
        if (!ok && _formatOnFail && partitionIsBlank())
        {
            _log("LittleFS partition is blank, formatting\n");
            ok = LittleFS64.begin(true, _partitionLabel, _blockSize);
        }

        if (!ok)
        {
            _log("LittleFS Init Failed\n");
            _mounted = false;

            // there is a partition with something on it that is not a
            // readable LittleFS: the user decides whether to erase it
            _needsFormat = _formatOnFail && partitionExists();
            return false;
        }

        _mounted = true;
        _needsFormat = false;

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

    bool needsFormat() override
    {
        return _needsFormat;
    }

    // erase the partition and mount the empty file system
    // only called after the user confirmed it
    bool format() override
    {
        if (_mounted)
        {
            LittleFS64.end();
            _mounted = false;
        }

        _log("LittleFS formatting partition=%s\n", _partitionLabel);
        if (!LittleFS64.begin(true, _partitionLabel, _blockSize))
        {
            _log("LittleFS format failed\n");
            return false;
        }

        _mounted = true;
        _needsFormat = false;
        ensureReadyFile();

        return true;
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
    const esp_partition_t *findPartition()
    {
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, _partitionLabel);
    }

    bool partitionExists()
    {
        return findPartition() != nullptr;
    }

    // LittleFS keeps its superblock in the first two blocks. When both are
    // still erased flash (all 0xFF) nothing was ever written here.
    bool partitionIsBlank()
    {
        const esp_partition_t *partition = findPartition();
        if (!partition)
            return false;

        size_t length = (size_t)_blockSize * 2;
        if (length > partition->size)
            length = partition->size;

        static uint8_t chunk[256];
        for (size_t offset = 0; offset < length; offset += sizeof(chunk))
        {
            size_t size = length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);

            // a read error is not proof of a blank partition
            if (esp_partition_read(partition, offset, chunk, size) != ESP_OK)
                return false;

            for (size_t i = 0; i < size; i++)
            {
                if (chunk[i] != 0xFF)
                    return false;
            }
        }

        return true;
    }

    bool _needsFormat = false;
    const char *_partitionLabel;
    uint32_t _blockSize;
    bool _formatOnFail;
    bool _mounted;
};

#endif
