#include "LittleFS64.h"

#include <Arduino.h>
#include <FSImpl.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "littlefs/lfs.h"

using namespace fs;

namespace
{

// smallest unit the flash can erase
constexpr uint32_t FLASH_SECTOR_SIZE = 4096;

// read / program granularity. Small values keep metadata commits compact,
// the cache (one per open file plus two for the file system) batches them.
constexpr lfs_size_t READ_SIZE = 16;
constexpr lfs_size_t PROG_SIZE = 16;
constexpr lfs_size_t CACHE_SIZE = 512;

// move metadata to a fresh block after this many erases (wear leveling)
constexpr int32_t BLOCK_CYCLES = 500;

// buffers handed to the flash driver stay in internal RAM,
// PSRAM is not reachable while the flash cache is disabled
void *internalAlloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

class Lock
{
public:
    explicit Lock(SemaphoreHandle_t mutex) : _mutex(mutex) { xSemaphoreTakeRecursive(_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(_mutex); }

private:
    SemaphoreHandle_t _mutex;
};

// Arduino open modes -> littlefs flags, -1 when not understood
int modeFlags(const char *mode)
{
    bool plus = strchr(mode, '+') != nullptr;
    switch (mode[0])
    {
    case 'r':
        return plus ? LFS_O_RDWR : LFS_O_RDONLY;
    case 'w':
        return (plus ? LFS_O_RDWR : LFS_O_WRONLY) | LFS_O_CREAT | LFS_O_TRUNC;
    case 'a':
        return (plus ? LFS_O_RDWR : LFS_O_WRONLY) | LFS_O_CREAT | LFS_O_APPEND;
    }
    return -1;
}

String joinPath(const String &dir, const char *name)
{
    return dir.endsWith("/") ? dir + name : dir + "/" + name;
}

class LittleFS64Impl;

//
// FILE / DIRECTORY
//
class LittleFS64File : public FileImpl
{
public:
    LittleFS64File(LittleFS64Impl *fs, const char *path);
    ~LittleFS64File() override { close(); }

    bool openFile(int flags);
    bool openDir();

    size_t write(const uint8_t *buf, size_t size) override;
    size_t read(uint8_t *buf, size_t size) override;
    void flush() override;
    bool seek(uint32_t pos, SeekMode mode) override;
    size_t position() const override;
    size_t size() const override;
    bool setBufferSize(size_t size) override { return true; }
    void close() override;
    time_t getLastWrite() override { return 0; }
    const char *path() const override { return _path.c_str(); }
    const char *name() const override { return _path.c_str() + _nameOffset; }
    boolean isDirectory(void) override { return _isDir; }
    FileImplPtr openNextFile(const char *mode) override;
    boolean seekDir(long position) override;
    String getNextFileName(void) override;
    String getNextFileName(bool *isDir) override;
    void rewindDirectory(void) override;
    operator bool() override { return alive(); }

private:
    // still open and the file system hasn't been remounted since
    bool alive() const;
    bool nextEntry(lfs_info &info);

    LittleFS64Impl *_fs;
    String _path;
    size_t _nameOffset = 0;

    bool _open = false;
    bool _isDir = false;
    uint32_t _generation = 0;

    mutable lfs_file_t _file;
    mutable lfs_dir_t _dir;
    lfs_file_config _fileConfig;
    uint8_t *_cache = nullptr;
};

//
// FILE SYSTEM
//
class LittleFS64Impl : public FSImpl
{
public:
    LittleFS64Impl()
    {
        memset(&lfs, 0, sizeof(lfs));
        memset(&config, 0, sizeof(config));
        mutex = xSemaphoreCreateRecursiveMutex();
    }

    bool begin(bool formatOnFail, const char *label, uint32_t blockSize)
    {
        Lock lock(mutex);
        if (mounted)
            return true;

        partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
        if (!partition)
        {
            log_e("LittleFS64: partition '%s' not found", label);
            return false;
        }

        if (blockSize < FLASH_SECTOR_SIZE || blockSize % FLASH_SECTOR_SIZE != 0 ||
            partition->size % blockSize != 0 || partition->size / blockSize < 2)
        {
            log_e("LittleFS64: block size %u doesn't fit partition '%s' (%u bytes)",
                  (unsigned)blockSize, label, (unsigned)partition->size);
            partition = nullptr;
            return false;
        }

        // aligned blocks let the flash chip erase each one in a single command
        if (partition->address % blockSize != 0)
            log_w("LittleFS64: partition '%s' is not aligned to the %u byte block size", label, (unsigned)blockSize);

        if (!configure(blockSize))
            return false;

        int err = lfs_mount(&lfs, &config);
        if (err && formatOnFail)
        {
            log_w("LittleFS64: mount failed (%d), formatting '%s'", err, label);
            err = lfs_format(&lfs, &config);
            if (!err)
                err = lfs_mount(&lfs, &config);
        }

        if (err)
        {
            log_e("LittleFS64: mount failed (%d)", err);
            return false;
        }

        mounted = true;
        generation++;
        return true;
    }

    void end()
    {
        Lock lock(mutex);
        if (!mounted)
            return;

        // files still open become invalid, see LittleFS64File::alive
        lfs_unmount(&lfs);
        mounted = false;
        generation++;
    }

    bool format()
    {
        Lock lock(mutex);
        if (!partition)
            return false;

        bool wasMounted = mounted;
        if (wasMounted)
            end();

        int err = lfs_format(&lfs, &config);
        if (!err && wasMounted)
        {
            err = lfs_mount(&lfs, &config);
            if (!err)
            {
                mounted = true;
                generation++;
            }
        }

        return err == 0;
    }

    size_t totalBytes()
    {
        Lock lock(mutex);
        return mounted ? (size_t)config.block_count * config.block_size : 0;
    }

    size_t usedBytes()
    {
        Lock lock(mutex);
        if (!mounted)
            return 0;

        lfs_ssize_t blocks = lfs_fs_size(&lfs);
        return blocks < 0 ? 0 : (size_t)blocks * config.block_size;
    }

    //
    FileImplPtr open(const char *path, const char *mode, const bool create) override
    {
        Lock lock(mutex);
        if (!mounted || !path || !mode)
            return FileImplPtr();

        int flags = modeFlags(mode);
        if (flags < 0)
            return FileImplPtr();

        lfs_info info;
        bool found = lfs_stat(&lfs, path, &info) == 0;

        // directories open read only, for listing
        if (found && info.type == LFS_TYPE_DIR)
        {
            if (flags != LFS_O_RDONLY)
                return FileImplPtr();

            auto dir = std::make_shared<LittleFS64File>(this, path);
            return dir->openDir() ? dir : FileImplPtr();
        }

        // a missing file opened for reading is simply not there, no error
        if (!found && !(flags & LFS_O_CREAT))
            return FileImplPtr();

        if (!found && create)
            makeParents(path);

        auto file = std::make_shared<LittleFS64File>(this, path);
        return file->openFile(flags) ? file : FileImplPtr();
    }

    bool exists(const char *path) override
    {
        Lock lock(mutex);
        lfs_info info;
        return mounted && path && lfs_stat(&lfs, path, &info) == 0;
    }

    bool rename(const char *pathFrom, const char *pathTo) override
    {
        Lock lock(mutex);
        // an existing destination file is replaced
        return mounted && pathFrom && pathTo && lfs_rename(&lfs, pathFrom, pathTo) == 0;
    }

    bool remove(const char *path) override
    {
        Lock lock(mutex);
        lfs_info info;
        if (!mounted || !path || lfs_stat(&lfs, path, &info) != 0 || info.type != LFS_TYPE_REG)
            return false;

        return lfs_remove(&lfs, path) == 0;
    }

    bool mkdir(const char *path) override
    {
        Lock lock(mutex);
        return mounted && path && lfs_mkdir(&lfs, path) == 0;
    }

    bool rmdir(const char *path) override
    {
        Lock lock(mutex);
        lfs_info info;
        if (!mounted || !path || lfs_stat(&lfs, path, &info) != 0 || info.type != LFS_TYPE_DIR)
            return false;

        // littlefs only removes empty directories
        return lfs_remove(&lfs, path) == 0;
    }

    lfs_t lfs;
    lfs_config config;
    SemaphoreHandle_t mutex;
    bool mounted = false;
    uint32_t generation = 0;
    const esp_partition_t *partition = nullptr;

private:
    bool configure(uint32_t blockSize)
    {
        lfs_size_t blockCount = partition->size / blockSize;

        // one bit per block, littlefs wants a multiple of 8 bytes
        lfs_size_t lookaheadSize = ((blockCount + 63) / 64) * 8;

        if (!readBuffer)
            readBuffer = (uint8_t *)internalAlloc(CACHE_SIZE);
        if (!progBuffer)
            progBuffer = (uint8_t *)internalAlloc(CACHE_SIZE);
        if (lookaheadBuffer && lookaheadCapacity < lookaheadSize)
        {
            free(lookaheadBuffer);
            lookaheadBuffer = nullptr;
        }
        if (!lookaheadBuffer)
        {
            lookaheadBuffer = (uint8_t *)internalAlloc(lookaheadSize);
            lookaheadCapacity = lookaheadBuffer ? lookaheadSize : 0;
        }

        if (!readBuffer || !progBuffer || !lookaheadBuffer)
        {
            log_e("LittleFS64: out of memory");
            return false;
        }

        memset(&config, 0, sizeof(config));
        config.context = this;
        config.read = blockRead;
        config.prog = blockProg;
        config.erase = blockErase;
        config.sync = blockSync;

        config.read_size = READ_SIZE;
        config.prog_size = PROG_SIZE;
        config.block_size = blockSize;
        config.block_count = blockCount;
        config.block_cycles = BLOCK_CYCLES;
        config.cache_size = CACHE_SIZE;
        config.lookahead_size = lookaheadSize;

        config.read_buffer = readBuffer;
        config.prog_buffer = progBuffer;
        config.lookahead_buffer = lookaheadBuffer;

        return true;
    }

    // create the folders leading up to path
    void makeParents(const char *path)
    {
        String p(path);
        for (int slash = p.indexOf('/', 1); slash > 0; slash = p.indexOf('/', slash + 1))
            lfs_mkdir(&lfs, p.substring(0, slash).c_str());
    }

    //
    // BLOCK DEVICE - littlefs blocks map straight onto the partition
    //
    static const esp_partition_t *part(const lfs_config *c)
    {
        return static_cast<LittleFS64Impl *>(c->context)->partition;
    }

    static int blockRead(const lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
    {
        return esp_partition_read(part(c), block * c->block_size + off, buffer, size) == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
    }

    static int blockProg(const lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
    {
        return esp_partition_write(part(c), block * c->block_size + off, buffer, size) == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
    }

    static int blockErase(const lfs_config *c, lfs_block_t block)
    {
        return esp_partition_erase_range(part(c), block * c->block_size, c->block_size) == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
    }

    static int blockSync(const lfs_config *c)
    {
        // writes go straight to flash
        return LFS_ERR_OK;
    }

    uint8_t *readBuffer = nullptr;
    uint8_t *progBuffer = nullptr;
    uint8_t *lookaheadBuffer = nullptr;
    lfs_size_t lookaheadCapacity = 0;
};

//
// FILE / DIRECTORY IMPLEMENTATION
//
LittleFS64File::LittleFS64File(LittleFS64Impl *fs, const char *path) : _fs(fs), _path(path)
{
    int slash = _path.lastIndexOf('/');
    _nameOffset = slash < 0 ? 0 : slash + 1;
    memset(&_file, 0, sizeof(_file));
    memset(&_dir, 0, sizeof(_dir));
    memset(&_fileConfig, 0, sizeof(_fileConfig));
}

bool LittleFS64File::openFile(int flags)
{
    _cache = (uint8_t *)internalAlloc(CACHE_SIZE);
    if (!_cache)
        return false;

    _fileConfig.buffer = _cache;
    if (lfs_file_opencfg(&_fs->lfs, &_file, _path.c_str(), flags, &_fileConfig) != 0)
    {
        free(_cache);
        _cache = nullptr;
        return false;
    }

    _open = true;
    _isDir = false;
    _generation = _fs->generation;
    return true;
}

bool LittleFS64File::openDir()
{
    if (lfs_dir_open(&_fs->lfs, &_dir, _path.c_str()) != 0)
        return false;

    _open = true;
    _isDir = true;
    _generation = _fs->generation;
    return true;
}

bool LittleFS64File::alive() const
{
    return _open && _fs->mounted && _generation == _fs->generation;
}

size_t LittleFS64File::write(const uint8_t *buf, size_t size)
{
    Lock lock(_fs->mutex);
    if (!alive() || _isDir)
        return 0;

    lfs_ssize_t written = lfs_file_write(&_fs->lfs, &_file, buf, size);
    return written < 0 ? 0 : written;
}

size_t LittleFS64File::read(uint8_t *buf, size_t size)
{
    Lock lock(_fs->mutex);
    if (!alive() || _isDir)
        return 0;

    lfs_ssize_t got = lfs_file_read(&_fs->lfs, &_file, buf, size);
    return got < 0 ? 0 : got;
}

void LittleFS64File::flush()
{
    Lock lock(_fs->mutex);
    if (alive() && !_isDir)
        lfs_file_sync(&_fs->lfs, &_file);
}

bool LittleFS64File::seek(uint32_t pos, SeekMode mode)
{
    Lock lock(_fs->mutex);
    if (!alive() || _isDir)
        return false;

    int whence = mode == SeekCur ? LFS_SEEK_CUR : (mode == SeekEnd ? LFS_SEEK_END : LFS_SEEK_SET);
    return lfs_file_seek(&_fs->lfs, &_file, (lfs_soff_t)pos, whence) >= 0;
}

size_t LittleFS64File::position() const
{
    Lock lock(_fs->mutex);
    if (!alive() || _isDir)
        return 0;

    lfs_soff_t pos = lfs_file_tell(&_fs->lfs, &_file);
    return pos < 0 ? 0 : pos;
}

size_t LittleFS64File::size() const
{
    Lock lock(_fs->mutex);
    if (!alive() || _isDir)
        return 0;

    lfs_soff_t size = lfs_file_size(&_fs->lfs, &_file);
    return size < 0 ? 0 : size;
}

void LittleFS64File::close()
{
    Lock lock(_fs->mutex);
    if (!_open)
        return;

    // after a remount the handle belongs to a file system that is gone
    if (alive())
    {
        if (_isDir)
            lfs_dir_close(&_fs->lfs, &_dir);
        else
            lfs_file_close(&_fs->lfs, &_file);
    }

    if (_cache)
    {
        free(_cache);
        _cache = nullptr;
    }
    _open = false;
}

// next real entry, skipping "." and ".."
bool LittleFS64File::nextEntry(lfs_info &info)
{
    while (lfs_dir_read(&_fs->lfs, &_dir, &info) > 0)
    {
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
            return true;
    }
    return false;
}

FileImplPtr LittleFS64File::openNextFile(const char *mode)
{
    Lock lock(_fs->mutex);
    if (!alive() || !_isDir)
        return FileImplPtr();

    lfs_info info;
    if (!nextEntry(info))
        return FileImplPtr();

    return _fs->open(joinPath(_path, info.name).c_str(), mode, false);
}

boolean LittleFS64File::seekDir(long position)
{
    Lock lock(_fs->mutex);
    return alive() && _isDir && lfs_dir_seek(&_fs->lfs, &_dir, position) == 0;
}

String LittleFS64File::getNextFileName(void)
{
    bool isDir;
    return getNextFileName(&isDir);
}

String LittleFS64File::getNextFileName(bool *isDir)
{
    Lock lock(_fs->mutex);
    if (!alive() || !_isDir)
        return "";

    lfs_info info;
    if (!nextEntry(info))
        return "";

    if (isDir)
        *isDir = info.type == LFS_TYPE_DIR;
    return joinPath(_path, info.name);
}

void LittleFS64File::rewindDirectory(void)
{
    Lock lock(_fs->mutex);
    if (alive() && _isDir)
        lfs_dir_rewind(&_fs->lfs, &_dir);
}

LittleFS64Impl *impl(const FSImplPtr &ptr)
{
    return static_cast<LittleFS64Impl *>(ptr.get());
}

} // namespace

//
// PUBLIC API
//
LittleFS64FS::LittleFS64FS() : FS(FSImplPtr(new LittleFS64Impl())) {}

bool LittleFS64FS::begin(bool formatOnFail, const char *partitionLabel, uint32_t blockSize)
{
    return impl(_impl)->begin(formatOnFail, partitionLabel, blockSize);
}

void LittleFS64FS::end() { impl(_impl)->end(); }
bool LittleFS64FS::format() { return impl(_impl)->format(); }
bool LittleFS64FS::mounted() { return impl(_impl)->mounted; }
size_t LittleFS64FS::totalBytes() { return impl(_impl)->totalBytes(); }
size_t LittleFS64FS::usedBytes() { return impl(_impl)->usedBytes(); }
uint32_t LittleFS64FS::blockSize() { return impl(_impl)->config.block_size; }

fs::LittleFS64FS LittleFS64;
