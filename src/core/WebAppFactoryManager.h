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

#ifndef WEBAPPFACTORYMANAGER_H
#define WEBAPPFACTORYMANAGER_H

#include <map>
#include <list>

#include "WebAppFactoryInterface.h"

class WebAppFactoryManager {
public:
    static WebAppFactoryManager* instance();
    WebAppBase* createWebApp(std::string winType, ApplicationDescription* desc = 0, std::string appType = "");
    WebAppBase* createWebApp(std::string winType, WebPageBase* page, ApplicationDescription* desc = 0, std::string appType = "");
    WebPageBase* createWebPage(std::string winType, std::string url, ApplicationDescription* desc, std::string appType = "", std::string launchParams = "");
    WebAppFactoryInterface* getPluggable(std::string appType);
    WebAppFactoryInterface* loadPluggable(std::string appType = "");

private:
    static WebAppFactoryManager* m_instance;
    WebAppFactoryManager();
    std::map<std::string, WebAppFactoryInterface*> m_interfaces;
    std::string m_webAppFactoryPluginPath;
    std::list<std::string> m_factoryEnv;
    bool m_loadPluggableOnDemand;
};

#endif /* WEBAPPFACTORY_H */
