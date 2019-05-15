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

#ifndef DEVICEINFOIMPL_H
#define DEVICEINFOIMPL_H

#include "DeviceInfo.h"

class DeviceInfoImpl : public DeviceInfo {
public:
    DeviceInfoImpl();

    void initialize() override;

private:
    int m_screenWidth;
    int m_screenHeight;

    float m_screenDensity;

    std::string m_modelName;
    std::string m_platformVersion;

    // platform versions are <major>.<minor>.<dot>
    int m_platformVersionMajor;
    int m_platformVersionMinor;
    int m_platformVersionDot;

    bool m_3DSupport;
    std::string m_otaId;
    std::string m_hardwareVersion;
    std::string m_firmwareVersion;

#ifdef HAS_LUNA_SERVICE
    bool getInfoFromLunaPrefs(const char* key, std::string& value);
#endif
    void initDisplayInfo();
    void initPlatformInfo();
    void gatherInfo();
};

#endif /* DEVICEINFOIMPL_H */
