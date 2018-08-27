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

#include "WebAppManagerConfig.h"

#include <unistd.h>

WebAppManagerConfig::WebAppManagerConfig()
    : m_suspendDelayTime(0)
    , m_devModeEnabled(false)
    , m_inspectorEnabled(false)
    , m_containerAppEnabled(true)
    , m_dynamicPluggableLoadEnabled(false)
    , m_postWebProcessCreatedDisabled(false)
    , m_checkLaunchTimeEnabled(false)
    , m_useSystemAppOptimization(false)
    , m_launchOptimizationEnabled(false)
{
    initConfiguration();
}

void WebAppManagerConfig::initConfiguration()
{
    m_webAppFactoryPluginTypes = getenv("WEBAPPFACTORY");

    m_webAppFactoryPluginPath = getenv("WEBAPPFACTORY_PLUGIN_PATH");
    if (m_webAppFactoryPluginPath.empty()) {
        m_webAppFactoryPluginPath = "/usr/lib/webappmanager/plugins";
    }

    std::string suspendDelay = getenv("WAM_SUSPEND_DELAY_IN_MS");
    m_suspendDelayTime = std::max(stoi(suspendDelay), 1);

    m_webProcessConfigPath = getenv("WEBPROCESS_CONFIGURATION_PATH");
    if (m_webProcessConfigPath.empty())
        m_webProcessConfigPath = "/etc/wam/com.webos.wam.json";

    m_errorPageUrl = getenv("WAM_ERROR_PAGE");

    if (strcmp(getenv("DISABLE_CONTAINER"), "1") == 0)
        m_containerAppEnabled = false;

    if (strcmp(getenv("LOAD_DYNAMIC_PLUGGABLE"), "1") == 0)
        m_dynamicPluggableLoadEnabled = true;

    if (strcmp(getenv("POST_WEBPROCESS_CREATED_DISABLED"), "1") == 0)
        m_postWebProcessCreatedDisabled =  true;

    if (strcmp(getenv("LAUNCH_TIME_CHECK"), "1") == 0)
        m_checkLaunchTimeEnabled = true;

    if (strcmp(getenv("USE_SYSTEM_APP_OPTIMIZATION"), "1") == 0)
        m_useSystemAppOptimization = true;

    if (strcmp(getenv("ENABLE_LAUNCH_OPTIMIZATION"), "1") == 0)
        m_launchOptimizationEnabled = true;

    m_userScriptPath = getenv("USER_SCRIPT_PATH");
    if (m_userScriptPath.empty())
        m_userScriptPath = "webOSUserScripts/userScript.js";

    m_name = getenv("WAM_NAME");
}

QVariant WebAppManagerConfig::getConfiguration(std::string name)
{
    QVariant value(0);

    auto search = m_configuration.find(name);
    if (search != m_configuration.end()) {
        value = m_configuration[name];
    }

    return value;
}

void WebAppManagerConfig::setConfiguration(std::string name, QVariant value)
{
    m_configuration.insert(make_pair(name, value));
}

void WebAppManagerConfig::postInitConfiguration()
{
    if (access("/var/luna/preferences/debug_system_apps", F_OK) == 0) {
        m_inspectorEnabled = true;
    }

    if (access("/var/luna/preferences/devmode_enabled", F_OK) == 0) {
        m_devModeEnabled = true;
        m_telluriumNubPath = getenv("TELLURIUM_NUB_PATH");
    }
}
