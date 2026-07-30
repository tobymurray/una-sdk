/**
 ******************************************************************************
 * @file    RecordingFile.cpp
 * @brief   Format-agnostic durable-file lifecycle for periodic recordings.
 ******************************************************************************
 */

#include "SDK/Activity/RecordingFile.hpp"

#include "SDK/Interfaces/IFileSystem.hpp"

#include <cstdio>
#include <cstring>

#define LOG_MODULE_PRX      "Activity::RecordingFile"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace SDK::Activity {

RecordingFile::RecordingFile(const SDK::Kernel& kernel, const char* pathToDir,
                              const char* extension, std::time_t flushIntervalSec)
    : mKernel(kernel), mPath(pathToDir), mExtension(extension), mFlushIntervalSec(flushIntervalSec)
{
}

bool RecordingFile::create(std::time_t utc)
{
    mLastFlushUtc = utc;

    char buff[256]{};
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif

    int len = snprintf(buff, sizeof(buff), "%s/%04u%02u/", mPath,
                       localTime.tm_year + 1900, localTime.tm_mon + 1);
    if (len <= 0 || !mKernel.fs.mkdir(buff)) {
        LOG_ERROR("Failed to create dir [%s]\n", buff);
        return false;
    }

    snprintf(&buff[len], sizeof(buff) - len, "activity_%04u%02u%02uT%02u%02u%02u.%s",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        localTime.tm_hour, localTime.tm_min, localTime.tm_sec, mExtension);

    mFile = mKernel.fs.file(buff);
    if (!mFile || !mFile->open(true, true)) {
        LOG_ERROR("Failed to create file [%s]\n", buff);
        mFile.reset();
        return false;
    }

    return true;
}

bool RecordingFile::dueForFlush(std::time_t now) const
{
    return now - mLastFlushUtc >= mFlushIntervalSec;
}

bool RecordingFile::reopenWithExtension(const char* newExtension)
{
    if (!mFile) {
        return false;
    }
    const size_t pathLen = std::strlen(mFile->getPath());
    const size_t stemLen = pathLen - std::strlen(mExtension);

    char buff[256]{};
    snprintf(buff, sizeof(buff), "%.*s%s", static_cast<int>(stemLen), mFile->getPath(), newExtension);
    mFile->setPath(buff);

    if (!mFile->open(true, true)) {
        LOG_ERROR("Failed to open [%s]\n", buff);
        mFile.reset();
        return false;
    }
    return true;
}

void RecordingFile::discard()
{
    if (!mFile) {
        return;
    }
    if (mFile->isOpen()) {
        mFile->close();
    }
    mFile->remove();
    mFile.reset();
}

}  // namespace SDK::Activity
