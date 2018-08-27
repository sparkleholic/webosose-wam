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

#include "DeviceInfo.h"

bool DeviceInfo::getDisplayWidth(int &value)
{
    bool ret = false;
    std::string valueStr;

    ret = getDeviceInfo("DisplayWidth", valueStr);
    value = std::stoi(valueStr); // TODO : This variable is unused

    return ret;
}

void DeviceInfo::setDisplayWidth(int value)
{
    m_deviceInfo.insert(make_pair("DisplayWidth", std::to_string(value)));
}

bool DeviceInfo::getDisplayHeight(int &value)
{
    bool ret = false;
    std::string valueStr;

    ret = getDeviceInfo("DisplayHeight", valueStr);
    value = std::stoi(valueStr); // TODO : This variable is unused

    return ret;
}

void DeviceInfo::setDisplayHeight(int value)
{
    m_deviceInfo.insert(make_pair("DisplayHeight", std::to_string(value)));
}

bool DeviceInfo::getSystemLanguage(std::string &value)
{
    return getDeviceInfo("SystemLanguage", value);
}

void DeviceInfo::setSystemLanguage(std::string value)
{
    m_deviceInfo.insert(make_pair("SystemLanguage", value));
}

bool DeviceInfo::getDeviceInfo(std::string name, std::string &value)
{
    auto search = m_deviceInfo.find(name);
    if (search != m_deviceInfo.end()) {
        value = m_deviceInfo[name];
        return true;
    }

    return false;
}

void DeviceInfo::setDeviceInfo(std::string name, std::string value)
{
    m_deviceInfo.insert(make_pair(name, value));
}
