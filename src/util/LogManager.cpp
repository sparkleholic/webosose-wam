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

#include "LogManager.h"

#if defined(DISABLE_LOGMANAGER) || !defined(HAS_PMLOG)
#include <stdarg.h>
#endif

static bool m_debugEventsEnable = false;
static bool m_debugBundleMessagesEnable = false;
static bool m_debugMouseMoveEnable = false;

void LogManager::setLogControl(const std::string& keys, const std::string& value)
{
    LOG_DEBUG("[LogManager::setLogControl] keys : %s, value : %s", keys.c_str(), value.c_str());

    if (keys == "all") {
        if (value == "on") {
            m_debugEventsEnable = true;
            m_debugBundleMessagesEnable = true;
        }
        else if (value == "off") {
            m_debugEventsEnable = false;
            m_debugBundleMessagesEnable = false;
        }
    }
    else if (keys == "event") {
        if (value == "on")
            m_debugEventsEnable = true;
        else if (value == "off")
            m_debugEventsEnable = false;
    }
    else if (keys == "bundleMessage") {
        if (value == "on")
            m_debugBundleMessagesEnable = true;
        else if (value == "off")
            m_debugBundleMessagesEnable = false;
    }
    else if (keys == "mouseMove") {
        if (value == "on")
            m_debugMouseMoveEnable = true;
        else if (value == "off")
            m_debugMouseMoveEnable = false;
    }
}

bool LogManager::getDebugEventsEnabled()
{
    return m_debugEventsEnable;
}

bool LogManager::getDebugBundleMessagesEnabled()
{
    return m_debugBundleMessagesEnable;
}

bool LogManager::getDebugMouseMoveEnabled()
{
    return m_debugMouseMoveEnable;
}

#if defined(DISABLE_LOGMANAGER) || !defined(HAS_PMLOG)
void FakePmLog(FILE* file, ...)
{
    va_list ap;
    va_start(ap, file);
    int count = va_arg(ap, int);
    for (int i = 0; i < count; i++) {
        const char* literal_key = va_arg(ap, const char*);
        fprintf(file, "%s=", literal_key);
        const char* fmt = va_arg(ap, const char*);
        vfprintf(file, fmt, ap);
        // Consume argument used in vfprintf
        (void) va_arg(ap, const char*);
    }
    const char* trailing_fmt = va_arg(ap, const char*);
    vfprintf(file, trailing_fmt, ap);
    va_end(ap);
}
#endif
