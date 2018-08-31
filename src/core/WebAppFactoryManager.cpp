// Copyright (c) 2008-2018 LG Electronics, Inc.
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

#include "WebAppFactoryManager.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>

#include <algorithm>
#include <experimental/filesystem>
#include <string>

#include "LogManager.h"
#include "WebAppBase.h"
#include "WebAppManagerConfig.h"
#include "WebAppManager.h"
#include "WebPageBase.h"

WebAppFactoryManager* WebAppFactoryManager::m_instance = NULL;

WebAppFactoryManager* WebAppFactoryManager::instance()
{
    if(!m_instance) {
        m_instance = new WebAppFactoryManager();
    }
    return m_instance;
}

WebAppFactoryManager::WebAppFactoryManager()
    : m_loadPluggableOnDemand(false)
{
    WebAppManagerConfig* webAppManagerConfig = WebAppManager::instance()->config();

    std::string factoryEnv = webAppManagerConfig->getWebAppFactoryPluginTypes();
    // TODO : Revisit
    //m_factoryEnv = factoryEnv.split(QLatin1Char(':'));
    m_factoryEnv.push_back("default");

    m_webAppFactoryPluginPath = webAppManagerConfig->getWebAppFactoryPluginPath();

    if (webAppManagerConfig->isDynamicPluggableLoadEnabled())
        m_loadPluggableOnDemand = true;

    if (!m_loadPluggableOnDemand)
        loadPluggable();
}

WebAppFactoryInterface* WebAppFactoryManager::getPluggable(std::string appType)
{
    auto iter = m_interfaces.find(appType);
    if (iter != m_interfaces.end())
        return iter->second;

    return loadPluggable(appType);
}

WebAppFactoryInterface* WebAppFactoryManager::loadPluggable(std::string appType)
{
    std::list<std::string>::iterator findIter = std::find(m_factoryEnv.begin(), m_factoryEnv.end(), appType);
    if (!appType.empty() && findIter != m_factoryEnv.end())
        return 0;

    WebAppFactoryInterface* interface;
    namespace fs = std::experimental::filesystem;
    for(auto& file : fs::directory_iterator(m_webAppFactoryPluginPath)) {
        if(fs::is_regular_file(file.symlink_status())) {
            std::string fileName = fs::absolute(file); 
#if 0
            QPluginLoader pluginLoader(pluginsDir.absoluteFilePath(fileName));
            QString key = pluginLoader.metaData().value("MetaData").toObject().value("Keys").toArray().at(0).toString();

            if (key.contains(appType) || !m_loadPluggableOnDemand) {
                QObject *plugin = pluginLoader.instance();

                if (plugin) {
                    interface = qobject_cast<WebAppFactoryInterface*>(plugin);
                    if (interface)
                        m_interfaces.insert(key, interface);
                    if (!appType.isEmpty())
                        return interface;
                } else {
                    LOG_WARNING(MSGID_PLUGIN_LOAD_FAIL, 1, PMLOGKS("ERROR", pluginLoader.errorString().toStdString().c_str()), "");
                    if (pluginLoader.isLoaded())
                        pluginLoader.unload();
                    if (!appType.isEmpty())
                        return 0;
                }
            }
#endif
        }
    }
    return 0;
}

WebAppBase* WebAppFactoryManager::createWebApp(std::string winType, ApplicationDescription* desc, std::string appType)
{
    WebAppFactoryInterface* interface = getPluggable(appType);
    if (interface)
        return interface->createWebApp(winType, desc);

    return NULL;
}

WebAppBase* WebAppFactoryManager::createWebApp(std::string winType, WebPageBase* page, ApplicationDescription* desc, std::string appType)
{
    WebAppFactoryInterface* interface = getPluggable(appType);  
    if (interface)
        return interface->createWebApp(winType, page, desc);

    return NULL;
}

WebPageBase* WebAppFactoryManager::createWebPage(std::string winType, std::string url, ApplicationDescription* desc, std::string appType, std::string launchParams)
{
    WebPageBase *page = NULL;

    WebAppFactoryInterface* interface = getPluggable(appType);
    if (interface) {
        page = interface->createWebPage(url, desc, launchParams);
    } else {
        // use default factory if cannot find appType.
        if (m_interfaces.find("default") != m_interfaces.end())
            page = m_interfaces["default"]->createWebPage(url, desc, launchParams);
    }

    if (page) page->init();
    return page;
}

