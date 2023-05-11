/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "QuarantineUtil.hpp"

#include <Poco/Path.h>
#include <Poco/URI.h>
#include "ClientSession.hpp"
#include "COOLWSD.hpp"
#include "DocumentBroker.hpp"
#include "FileUtil.hpp"

#include <chrono>
#include <common/Common.hpp>
#include <common/StringVector.hpp>
#include <common/Log.hpp>

std::string Quarantine::QuarantinePath;

namespace
{

std::string createQuarantinedFilename(DocumentBroker& docBroker)
{
    std::string docKey;
    Poco::URI::encode(docBroker.getDocKey(), "?#/", docKey);
    return '_' + std::to_string(docBroker.getPid()) + '_' + docKey + '_';
}

} // namespace

Quarantine::Quarantine(DocumentBroker& docBroker)
    : _docKey(docBroker.getDocKey())
    , _quarantinedFilenamePrefix(createQuarantinedFilename(docBroker))
    , _maxSizeBytes(COOLWSD::getConfigValue<std::size_t>("quarantine_files.limit_dir_size_mb", 0) *
                    1024 * 1024)
    , _maxAgeSecs(COOLWSD::getConfigValue<std::size_t>("quarantine_files.expiry_min", 30) * 60)
{
}

void Quarantine::initialize(const std::string& path)
{
    if (!COOLWSD::getConfigValue<bool>("quarantine_files[@enable]", false) ||
        !QuarantinePath.empty())
        return;

    std::vector<std::string> files;
    Poco::File(path).list(files);
    COOLWSD::QuarantineMap.clear();

    std::vector<StringToken> tokens;
    std::string decoded;

    std::sort(files.begin(), files.end());
    for (const auto& file : files)
    {
        StringVector::tokenize(file.c_str(), file.size(), '_', tokens);
        Poco::URI::decode(file.substr(tokens[2]._index), decoded);
        COOLWSD::QuarantineMap[decoded].emplace_back(path + file);

        tokens.clear();
        decoded.clear();
    }

    QuarantinePath = path;
}

void Quarantine::removeQuarantine()
{
    if (!isQuarantineEnabled())
        return;

    FileUtil::removeFile(QuarantinePath, true);
}

// returns quarantine directory size in bytes
// files with hardlink count of more than 1 is not counted
// because they are originally stored in jails
std::size_t Quarantine::quarantineSize()
{
    if (!isQuarantineEnabled())
        return 0;

    std::vector<std::string> files;
    Poco::File(QuarantinePath).list(files);
    std::size_t size = 0;
    for (const auto& file : files)
    {
        FileUtil::Stat f(QuarantinePath + file);

        if (f.hardLinkCount() == 1)
            size += f.size();
    }
    return size;
}

void Quarantine::makeQuarantineSpace()
{
    if (!isQuarantineEnabled())
        return;

    std::vector<std::string> files;
    Poco::File(QuarantinePath).list(files);

    std::sort(files.begin(), files.end());

    const auto timeNow = std::chrono::system_clock::now();
    const auto ts =
        std::chrono::duration_cast<std::chrono::seconds>(timeNow.time_since_epoch()).count();

    std::size_t currentSize = quarantineSize();
    auto index = files.begin();
    while (index != files.end() && !files.empty())
    {
        FileUtil::Stat file(QuarantinePath + *index);
        const auto modifyTime = std::chrono::duration_cast<std::chrono::seconds>(
                                    file.modifiedTimepoint().time_since_epoch())
                                    .count();
        bool isExpired = static_cast<std::size_t>(ts - modifyTime) > _maxAgeSecs;

        if ((file.hardLinkCount() == 1) && (isExpired || (currentSize >= _maxSizeBytes)))
        {
            currentSize -= file.size();
            FileUtil::removeFile(QuarantinePath + *index, true);
            files.erase(index);
        }
        else
            index++;
    }
}

void Quarantine::clearOldQuarantineVersions()
{
    if (!isQuarantineEnabled())
        return;

    std::size_t maxVersionCount = std::max<size_t>(
        COOLWSD::getConfigValue<std::size_t>("quarantine_files.max_versions_to_maintain", 2), 1);
    std::string decoded;
    Poco::URI::decode(_docKey, decoded);
    while (COOLWSD::QuarantineMap[decoded].size() > maxVersionCount)
    {
        FileUtil::removeFile(COOLWSD::QuarantineMap[decoded][0]);
        COOLWSD::QuarantineMap[decoded].erase(COOLWSD::QuarantineMap[decoded].begin());
    }
}

bool Quarantine::quarantineFile(const std::string& docPath)
{
    if (!isQuarantineEnabled())
        return false;

    const auto timeNow = std::chrono::system_clock::now();
    const auto ts =
        std::chrono::duration_cast<std::chrono::seconds>(timeNow.time_since_epoch()).count();

    const std::string linkedFilePath =
        QuarantinePath + std::to_string(ts) + _quarantinedFilenamePrefix + _docName;

    auto& fileList = COOLWSD::QuarantineMap[_docKey];
    if (!fileList.empty())
    {
        FileUtil::Stat sourceStat(docPath);
        FileUtil::Stat lastFileStat(fileList[fileList.size() - 1]);

        if (lastFileStat.isIdenticalTo(sourceStat))
        {
            LOG_INF("Quarantining of file ["
                    << docPath << "] to [" << linkedFilePath
                    << "] is skipped because this file version is already quarantined");
            return false;
        }
    }

    makeQuarantineSpace();

    if (FileUtil::linkOrCopyFile(docPath, linkedFilePath))
    {
        fileList.emplace_back(linkedFilePath);
        clearOldQuarantineVersions();
        makeQuarantineSpace();

        LOG_INF("Quarantined [" << docPath << "] to [" << linkedFilePath << ']');
        return true;
    }

    LOG_ERR("Quarantining of file [" << docPath << "] to [" << linkedFilePath << "] failed");
    return false;
}

void Quarantine::removeQuarantinedFiles()
{
    for (const auto& file : COOLWSD::QuarantineMap[_docKey])
    {
        FileUtil::removeFile(file);
    }

    COOLWSD::QuarantineMap.erase(_docKey);
}
