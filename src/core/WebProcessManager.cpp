// Copyright (c) 2014-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "WebProcessManager.h"

#include <signal.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

#include "ApplicationDescription.h"
#include "LogManager.h"
#include "WebAppBase.h"
#include "WebAppManagerConfig.h"
#include "WebAppManagerUtils.h"
#include "WebAppManager.h"
#include "WebPageBase.h"

#include <ctype.h>
#include <string>
#include <sstream>
#include <WamString.h>

#include <glib.h>

WebProcessManager::WebProcessManager()
    : m_maximumNumberOfProcesses(1)
{
    readWebProcessPolicy();
}

std::list<const WebAppBase*> WebProcessManager::runningApps()
{
    return WebAppManager::instance()->runningApps();
}

std::list<const WebAppBase*> WebProcessManager::runningApps(uint32_t pid)
{
    return WebAppManager::instance()->runningApps(pid);
}

WebAppBase* WebProcessManager::findAppById(const std::string& appId)
{
    return WebAppManager::instance()->findAppById(appId);
}

WebAppBase* WebProcessManager::getContainerApp()
{
    return WebAppManager::instance()->getContainerApp();
}

bool WebProcessManager::webProcessInfoMapReady()
{
    uint32_t count = 0;
    for (const auto& it : m_webProcessInfoMap) {
        if (it.second.proxyID != 0)
            count++;
    }

    return count == m_maximumNumberOfProcesses;
}

uint32_t WebProcessManager::getWebProcessProxyID(const ApplicationDescription *desc) const
{
    if (!desc)
        return 0;

    std::string key = getProcessKey(desc);
    auto it = m_webProcessInfoMap.find(key);

    if (it == m_webProcessInfoMap.end() || !it->second.proxyID) {
        return getInitialWebViewProxyID();
    }

    return it->second.proxyID;
}

uint32_t WebProcessManager::getWebProcessProxyID(uint32_t pid) const
{
    for (std::map<std::string, WebProcessInfo>::const_iterator it = m_webProcessInfoMap.begin(); it != m_webProcessInfoMap.end(); it++) {
        if (it->second.webProcessPid == pid)
            return it->second.proxyID;
    }
    return 0;
}

std::string WebProcessManager::getWebProcessMemSize(uint32_t pid) const
{
    std::string filePath = std::string("/proc/") + std::to_string(pid) + std::string("/status");
    FILE *fd = fopen(filePath.c_str(), "r");
    std::string vmrss;
    char line[128];

    if (!fd)
        return vmrss;

    while (fgets(line, 128, fd) != NULL) {
        if(!strncmp(line, "VmRSS:", 6)) {
            vmrss = strdup(&line[8]);
            break;
        }
    }

    fclose(fd);
    // TODO : Implement simplified() method
    // http://doc.qt.io/qt-5/qstring.html#simplified
    return vmrss;//.simplified();
}

void WebProcessManager::readWebProcessPolicy()
{
#if 0
    std::string webProcessConfigurationPath = WebAppManager::instance()->config()->getWebProcessConfigPath();

    QFile file(webProcessConfigurationPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    std::string jsonStr = file.readAll();
    file.close();

    QJsonDocument webProcessEnvironment = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (webProcessEnvironment.isNull()) {
        LOG_ERROR(MSGID_WEBPROCESSENV_READ_FAIL, 1, PMLOGKS("CONTENT", jsonStr.c_str()), "");
        return;
    }

    bool createProcessForEachApp = webProcessEnvironment.object().value("createProcessForEachApp").toBool();
    if (createProcessForEachApp)
        m_maximumNumberOfProcesses = UINT_MAX;
    else {
        QJsonArray webProcessArray = webProcessEnvironment.object().value("webProcessList").toArray();
        Q_FOREACH (const QJsonValue &value, webProcessArray) {
            QJsonObject obj = value.toObject();
            if (!obj.value("id").isUndefined()) {
                std::string id = obj.value("id").toString();

                m_webProcessGroupAppIDList.append(id);
                setWebProcessCacheProperty(obj, id);
            }
            else if (!obj.value("trustLevel").isUndefined()) {
                std::string trustLevel = obj.value("trustLevel").toString();

                m_webProcessGroupTrustLevelList.append(trustLevel);
                setWebProcessCacheProperty(obj, trustLevel);
            }
        }
        m_maximumNumberOfProcesses = (m_webProcessGroupTrustLevelList.size() + m_webProcessGroupAppIDList.size());
    }

    LOG_INFO(MSGID_SET_WEBPROCESS_ENVIRONMENT, 3, PMLOGKFV("MAXIMUM_WEBPROCESS_NUMBER", "%u", m_maximumNumberOfProcesses),
            PMLOGKFV("GROUP_TRUSTLEVELS_COUNT", "%d", m_webProcessGroupTrustLevelList.size()),
            PMLOGKFV("GROUP_APP_IDS_COUNT", "%d", m_webProcessGroupAppIDList.size()), "");
#endif
}

void WebProcessManager::setWebProcessCacheProperty(QJsonObject object, std::string key)
{
    WebProcessManager::WebProcessInfo info(0, 0);
    std::string memoryCacheStr, codeCacheStr;
    if (!object.value("memoryCache").isUndefined()) {
        memoryCacheStr = object.value("memoryCache").toString().toStdString();
        WamString::findAndReplaceAll(memoryCacheStr, std::string("MB"), std::string());

        bool isDigit = std::all_of(memoryCacheStr.begin(), memoryCacheStr.end(), ::isdigit);
        if (isDigit)
            info.memoryCacheSize = stoi(memoryCacheStr);
    }
    if (!object.value("codeCache").isUndefined()) {
        codeCacheStr = object.value("codeCache").toString().toStdString();;
        WamString::findAndReplaceAll(codeCacheStr, std::string("MB"), std::string());

        bool isDigit = std::all_of(codeCacheStr.begin(), codeCacheStr.end(), ::isdigit);
        if (isDigit)
            info.memoryCacheSize = stoi(codeCacheStr);
    }
    m_webProcessInfoMap.insert(make_pair(key, info));
}

std::string WebProcessManager::getProcessKey(const ApplicationDescription* desc) const
{
    if (!desc)
        return std::string();

    std::string key;
    std::list<std::string> idList, trustLevelList;
    if (m_maximumNumberOfProcesses == 1)
        key = "system";
    else if (m_maximumNumberOfProcesses == UINT_MAX) {
        if (desc->trustLevel() == "default" || desc->trustLevel() == "trusted")
            key = "system";
        else
            key = desc->id().c_str();
    }
    else {
        for (int i = 0; i < m_webProcessGroupAppIDList.size(); i++) {
            std::string appId = m_webProcessGroupAppIDList.at(i);
            std::size_t found = appId.find("*"); 
            if (found != std::string::npos) {
                appId.erase(found);

                // Splitting string
                char delimiter = ',';
                std::string token;
                std::istringstream tokenStream(appId);
                while (std::getline(tokenStream, token, delimiter)) {
                    idList.push_back(token);
                }

                for (std::string id : idList) {
                    // returns offset 0 if 'id' is found at starting
                    if(desc->id().find(id) == 0)
                        key = m_webProcessGroupAppIDList.at(i);                        
                }
            } else {
                // Splitting string
                char delimiter = ',';
                std::string token;
                std::istringstream tokenStream(appId);
                while (std::getline(tokenStream, token, delimiter)) {
                    idList.push_back(token);
                }

                for (std::string id : idList) {
                    if (!id.compare(desc->id()))
                        return m_webProcessGroupAppIDList.at(i);
                }
            }
        }
        if (!key.empty())
            return key;

        for (int i = 0; i < m_webProcessGroupTrustLevelList.size(); i++) {
            std::string trustLevel = m_webProcessGroupTrustLevelList.at(i);

            // Splitting string
            char delimiter = ',';
            std::string token;
            std::istringstream tokenStream(trustLevel);
            while (std::getline(tokenStream, token, delimiter)) {
                trustLevelList.push_back(token);
            }

            for (std::string trust : trustLevelList) {
                if (!trust.compare(desc->trustLevel().c_str())) {
                    return m_webProcessGroupTrustLevelList.at(i);
                }
            }
        }
        key = "system";
    }
    return key;
}

void WebProcessManager::killWebProcess(uint32_t pid)
{
    for(auto it = m_webProcessInfoMap.begin(); it != m_webProcessInfoMap.end(); it++) {
        if (it->second.webProcessPid == pid) {
            it->second.requestKill = false;
            break;
        }
    }

    LOG_INFO(MSGID_KILL_WEBPROCESS, 1, PMLOGKFV("PID", "%u", pid), "");
    int ret = kill(pid, SIGKILL);
    if (ret == -1)
        LOG_ERROR(MSGID_KILL_WEBPROCESS_FAILED, 1, PMLOGKS("ERROR", strerror(errno)), "SystemCall failed");
}

void WebProcessManager::requestKillWebProcess(uint32_t pid)
{
    for (auto it = m_webProcessInfoMap.begin(); it != m_webProcessInfoMap.end(); it++) {
        if (it->second.webProcessPid == pid) {
            LOG_INFO(MSGID_KILL_WEBPROCESS_DELAYED, 1, PMLOGKFV("PID", "%u", pid), "");
            it->second.requestKill = true;
            return;
        }
    }
}
