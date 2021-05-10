// Copyright (c) 2013-2018 LG Electronics, Inc.
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

#include <memory>

#include <sstream>
#include <boost/filesystem.hpp>

#include "ApplicationDescription.h"
#include "JsonHelper.h"
#include "LogManager.h"
#include "WebAppManagerConfig.h"
#include "WebAppManager.h"
#include "WebPageBase.h"
#include "WebPageObserver.h"
#include "WebProcessManager.h"

#define CONSOLE_DEBUG(AAA) evaluateJavaScript("console.debug('" + AAA + "');")

namespace fs = boost::filesystem;

WebPageBase::WebPageBase()
    : m_appDesc(nullptr)
    , m_suspendAtLoad(false)
    , m_isClosing(false)
    , m_isLoadErrorPageFinish(false)
    , m_isLoadErrorPageStart(false)
    , m_didErrorPageLoadedFromNetErrorHelper(false)
    , m_enableBackgroundRun(false)
    , m_loadErrorPolicy("default")
    , m_cleaningResources(false)
    , m_isPreload(false)
{
}

WebPageBase::WebPageBase(const Url& url, std::shared_ptr<ApplicationDescription> desc, const std::string& params)
    : m_appDesc(desc)
    , m_appId(desc->id())
    , m_suspendAtLoad(false)
    , m_isClosing(false)
    , m_isLoadErrorPageFinish(false)
    , m_isLoadErrorPageStart(false)
    , m_didErrorPageLoadedFromNetErrorHelper(false)
    , m_enableBackgroundRun(false)
    , m_defaultUrl(url)
    , m_launchParams(params)
    , m_loadErrorPolicy("default")
    , m_cleaningResources(false)
    , m_isPreload(false)
{
}

WebPageBase::~WebPageBase()
{
    LOG_INFO(MSGID_WEBPAGE_CLOSED, 1, PMLOGKS("APP_ID", appId().c_str()), "");
}

std::string WebPageBase::launchParams() const
{
    return m_launchParams;
}

void WebPageBase::setLaunchParams(const std::string& params)
{
    m_launchParams = params;
}

void WebPageBase::setApplicationDescription(std::shared_ptr<ApplicationDescription> desc)
{
    m_appDesc = desc;
    setPageProperties();
}

std::string WebPageBase::getIdentifier() const
{
    // If appId is ContainerAppId then it should be ""? Why not just container appid?
    // I think there shouldn't be any chance to be returned container appid even for container base app

    if(appId().empty() || appId() == WebAppManager::instance()->getContainerAppId())
        return {};
    return m_appId;
}

void WebPageBase::load()
{
    LOG_INFO(MSGID_WEBPAGE_LOAD, 2, PMLOGKS("APP_ID", appId().c_str()),
             PMLOGKFV("PID", "%d", getWebProcessPID()),
             "m_launchParams:%s", m_launchParams.c_str());
    /* this function is main load of WebPage : load default url */
    setupLaunchEvent();
    if (!doDeeplinking(m_launchParams)) {
        LOG_INFO(MSGID_WEBPAGE_LOAD, 2, PMLOGKS("APP_ID", appId().c_str()),
                 PMLOGKFV("PID", "%d", getWebProcessPID()), "loadDefaultUrl()");
        loadDefaultUrl();
    }
}

void WebPageBase::setupLaunchEvent()
{
    std::stringstream launchEventJS;
    std::string params = launchParams().empty() ? "{}" : launchParams();
    launchEventJS
            << "(function() {"
            << "    var launchEvent = new CustomEvent('webOSLaunch', { detail: " << params << " });"
            << "    if(document.readyState === 'complete') {"
            << "        setTimeout(function() {"
            << "            document.dispatchEvent(launchEvent);"
            << "        }, 1);"
            << "    } else {"
            << "        document.addEventListener('DOMContentLoaded', function() {"
            << "            setTimeout(function() {"
            << "                document.dispatchEvent(launchEvent);"
            << "            }, 1);"
            << "        });"
            << "    }"
            << "})();";
    addUserScript(launchEventJS.str());
}

void WebPageBase::sendLocaleChangeEvent(const std::string&)
{
    evaluateJavaScript(
        "setTimeout(function () {"
        "    var localeEvent=new CustomEvent('webOSLocaleChange');"
        "    document.dispatchEvent(localeEvent);"
        "}, 1);"
    );
}

void WebPageBase::cleanResources()
{
    setCleaningResources(true);
}

bool WebPageBase::relaunch(const std::string& launchParams, const std::string& launchingAppId)
{
    resumeWebPagePaintingAndJSExecution();

    // for common webapp relaunch scenario
    // 1. For hosted webapp deeplinking : reload default page
    // 2-1. check progress; to send webOSRelaunch event, then page loading progress should be 100
    // 2-2. Update launchParams
    // 2-3. send webOSRelaunch event

    if(doHostedWebAppRelaunch(launchParams)) {
        LOG_DEBUG("[%s] Hosted webapp; handled", appId().c_str());
        return true;
    }

    if (!hasBeenShown()){
        LOG_INFO(MSGID_WEBPAGE_RELAUNCH, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "In Loading(%d%%), Can not handle relaunch now, return false", progress());
        return false;
    }

    setLaunchParams(launchParams);

    // WebPageBase::relaunch handles setting the stageArgs for the launch/relaunch events
    sendRelaunchEvent();
    return true;
}

bool WebPageBase::doHostedWebAppRelaunch(const std::string& launchParams)
{
    /* hosted webapp deeplinking spec
    // legacy case
    "deeplinkingParams":"{ \
        \"contentTarget\" : \"https://www.youtube.com/tv?v=$CONTENTID\" \
    }"
    // webOS4.0 spec
    "deeplinkingParams":"{ \
        \"handledBy\" : \"platform\" || \"app\" || \"default\", \
        \"contentTarget\" : \"https://www.youtube.com/tv?v=$CONTENTID\" \
    }"
    To support backward compatibility, should cover the case not having "handledBy"
    */
    // check deeplinking relaunch condition
    Json::Value obj;
    readJsonFromString(launchParams, obj);
    if (url().scheme() ==  "file"
        || m_defaultUrl.scheme() != "file"
        || !obj.isObject() /* no launchParams, { }, and this should be check with object().isEmpty()*/
        || obj["contentTarget"].isNull()
        || (m_appDesc && !m_appDesc->handlesDeeplinking())) {
        LOG_INFO(MSGID_WEBPAGE_RELAUNCH, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),
            "%s; NOT enough deeplinking condition; return false", __func__);
        return false;
    }

    // Do deeplinking relaunch
    setLaunchParams(launchParams);
    return doDeeplinking(launchParams);
}

// TODO: Optimization: Consider use previously parsed Json::Object here instead of std::string
bool WebPageBase::doDeeplinking(const std::string& launchParams)
{
    Json::Value obj;
    readJsonFromString(launchParams, obj);
    if (!obj.isObject() || obj["contentTarget"].isNull())
        return false;

    std::string handledBy = obj["handledBy"].isNull() ? "default" : obj["handledBy"].asString();
    if (handledBy == "platform") {
        std::string targetUrl = obj["contentTarget"].asString();
        LOG_INFO(MSGID_DEEPLINKING, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),
            PMLOGKS("handledBy", handledBy.c_str()),
            "%s; load target URL:%s", __func__, targetUrl.c_str());
        // load the target URL directly
        loadUrl(targetUrl);
        return true;
    } else if(handledBy == "app") {
        // If "handledBy" == "app" return false
        // then it will be handled just like common relaunch case, checking progress
        return false;
    } else {
        // handledBy == "default" or "other values"
        LOG_INFO(MSGID_DEEPLINKING, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),
            PMLOGKS("handledBy", handledBy.c_str()), "%s; loadDefaultUrl", __func__);
        loadDefaultUrl();
        return true;
    }
}

void WebPageBase::sendRelaunchEvent()
{
    setVisible(true);
    LOG_INFO(MSGID_SEND_RELAUNCHEVENT, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "");
    // Send the relaunch event on the next tick after javascript is loaded
    // This is a workaround for a problem where WebKit can't free the page
    // if we don't use a timeout here.

    std::stringstream jss;
    std::string detail = launchParams().empty() ? "{}" : launchParams();
    jss << "setTimeout(function () {"
        << "    console.log('[WAM] fires webOSRelaunch event');"
        << "    var launchEvent=new CustomEvent('webOSRelaunch', { detail: " << detail << " });"
        << "    document.dispatchEvent(launchEvent);"
        << "}, 1);";
    evaluateJavaScript(jss.str());
}

void WebPageBase::handleLoadStarted()
{
    m_suspendAtLoad = true;
    m_didErrorPageLoadedFromNetErrorHelper = false;
}

void WebPageBase::handleLoadFinished()
{
    LOG_INFO(MSGID_WEBPAGE_LOAD_FINISHED, 2, PMLOGKS("APP_ID", appId().c_str()),
             PMLOGKFV("PID", "%d", getWebProcessPID()),
             "m_suspendAtLoad : %s", m_suspendAtLoad ? "true; suspend in this time" : "false");

    if (appId() == WebAppManager::instance()->getContainerAppId())
        WebAppManager::instance()->setContainerAppLaunched(true);

    FOR_EACH_OBSERVER(WebPageObserver, m_observers, webPageLoadFinished());

    // if there was an attempt made to suspend while this page was loading, then
    // we flag m_suspendAtLoad = true, and suspend it after it is loaded. This is
    // to prevent application load from failing.
    if(m_suspendAtLoad) {
        suspendWebPagePaintingAndJSExecution();
    }
    updateIsLoadErrorPageFinish();
}

void WebPageBase::handleLoadFailed(int errorCode)
{
    LOG_INFO(MSGID_WEBPAGE_LOAD_FAILED, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "");

    // errorCode 204 specifically states that the web browser not relocate
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
    // we can't handle unknown protcol like mailto.
    // Client want to not show error page with unknown protocol like chrome.
    if (!m_isPreload && errorCode != 204 && errorCode != 301)
        loadErrorPage(errorCode);
}

void WebPageBase::cleanResourcesFinished()
{
    WebAppManager::instance()->postRunningAppList();
    if (m_cleaningResources) {
        WebAppManager::instance()->removeWebAppFromWebProcessInfoMap(appId());
        delete this;
    }
}

void WebPageBase::handleForceDeleteWebPage()
{
    delete this;
}

bool WebPageBase::getSystemLanguage(std::string &value)
{
    return WebAppManager::instance()->getSystemLanguage(value);
}

bool WebPageBase::getDeviceInfo(const std::string& name, std::string& value)
{
    return WebAppManager::instance()->getDeviceInfo(name, value);
}

int WebPageBase::currentUiWidth()
{
    return WebAppManager::instance()->currentUiWidth();
}

int WebPageBase::currentUiHeight()
{
    return WebAppManager::instance()->currentUiHeight();
}

WebProcessManager* WebPageBase::getWebProcessManager()
{
    return WebAppManager::instance()->getWebProcessManager();
}

WebAppManagerConfig* WebPageBase::getWebAppManagerConfig()
{
    return WebAppManager::instance()->config();
}

bool WebPageBase::processCrashed()
{
    return WebAppManager::instance()->processCrashed(appId());
}

int WebPageBase::suspendDelay()
{
    return WebAppManager::instance()->getSuspendDelay();
}

std::string WebPageBase::telluriumNubPath()
{
    return getWebAppManagerConfig()->getTelluriumNubPath();
}

bool WebPageBase::hasLoadErrorPolicy(bool isHttpResponseError, int errorCode)
{
    if (m_loadErrorPolicy == "event") {
        std::stringstream jss;
        std::string genError = isHttpResponseError ? "false" : "true";
        jss <<"{"
            << "    console.log('[WAM3] create webOSLoadError event');"
            << "    var launchEvent=new CustomEvent('webOSLoadError', "
            << "        { detail : { genericError : " << genError << ", errorCode : " << errorCode << "}});"
            << "    document.dispatchEvent(launchEvent);"
            << "}";
        //App has load error policy, do not show platform load error page
        evaluateJavaScript(jss.str());
        return true;
    }
    return false;
}

void WebPageBase::applyPolicyForUrlResponse(bool isMainFrame, const std::string& urlStr, int status_code)
{
    Url url(urlStr);
    static const int s_httpErrorStatusCode = 400;
    if (url.scheme() != "file" &&  status_code >= s_httpErrorStatusCode) {
        if(!hasLoadErrorPolicy(true, status_code) && isMainFrame) {
            // If app does not have policy for load error and
            // this error response is from main frame document
            // then before open server error page, reset the body's background color to white
            setBackgroundColorOfBody("white");
        }
    }
}

void WebPageBase::postRunningAppList()
{
    WebAppManager::instance()->postRunningAppList();
}

void WebPageBase::postWebProcessCreated(uint32_t pid)
{
    WebAppManager::instance()->postWebProcessCreated(m_appId, pid);
}

void WebPageBase::setBackgroundColorOfBody(const std::string& color)
{
    // for error page only, set default background color to white by executing javascript
    std::stringstream jss;
    jss << "(function() {"
        << "    if(document.readyState === 'complete' || document.readyState === 'interactive') { "
        << "       if(document.body.style.backgroundColor)"
        << "           console.log('[Server Error] Already set document.body.style.backgroundColor');"
        << "       else {"
        << "           console.log('[Server Error] set background Color of body to " << color << "');"
        << "           document.body.style.backgroundColor = '" << color << "';"
        << "       }"
        << "     } else {"
        << "        document.addEventListener('DOMContentLoaded', function() {"
        << "           if(document.body.style.backgroundColor)"
        << "               console.log('[Server Error] Already set document.body.style.backgroundColor');"
        << "           else {"
        << "               console.log('[Server Error] set background Color of body to " << color << "');"
        << "               document.body.style.backgroundColor = '" << color << "';"
        << "           }"
        << "        });"
        << "    }"
        << "})();";
    evaluateJavaScript(jss.str());
}

std::string WebPageBase::defaultFont()
{
    std::string defaultFont = "LG Display-Regular";
    std::string language;
    getSystemLanguage(language);
    std::string country;
    getDeviceInfo("LocalCountry", country);

    // for the model
    if(country == "JPN")
        defaultFont = "LG Display_JP";
    else if(country == "HKG")
        defaultFont = "LG Display GP4_HK";
    // for the locale(language)
    else if(language == "ur-IN")
        defaultFont = "LG Display_Urdu";

    LOG_DEBUG("[%s] country : [%s], language : [%s], default font : [%s]", appId().c_str(),
              country.c_str(), language.c_str(), defaultFont.c_str());
    return defaultFont;
}

void WebPageBase::updateIsLoadErrorPageFinish()
{
    // ex)
    // Target error page URL : file:///usr/share/localization/webappmanager2/resources/ko/html/loaderror.html?errorCode=65&webkitErrorCode=65
    // WAM error page : file:///usr/share/localization/webappmanager2/loaderror.html

    m_isLoadErrorPageFinish = false;

    if (!url().isLocalFile()) return;

    fs::path urlPath(url().toLocalFile());
    fs::path urlFileName = urlPath.filename();
    fs::path urlDirPath = urlPath.parent_path();

    fs::path errPath(Url(getWebAppManagerConfig()->getErrorPageUrl()).toLocalFile());
    fs::path errFileName = errPath.filename();
    fs::path errDirPath = errPath.parent_path();

    if ((urlDirPath.string().find(errDirPath.string()) == 0) // urlDirPath starts with errDirPath
            && urlFileName == errFileName) {
        LOG_DEBUG("[%s] This is WAM ErrorPage; URL: %s ", appId().c_str(), url().toString().c_str());
        m_isLoadErrorPageFinish = true;
    }
}

void WebPageBase::setCustomUserScript()
{
    // 1. check app folder has userScripts
    // 2. check userscript.js there is, appfolder/webOSUserScripts/*.js
    auto userScriptFilePath = fs::path(m_appDesc->folderPath()) / getWebAppManagerConfig()->getUserScriptPath();

    if(!fs::exists(userScriptFilePath) || !fs::is_regular_file(fs::canonical(userScriptFilePath))) {
        LOG_DEBUG("WebPageBase: Couldn't set '%s' as user script cause it does not exist/is a regular file.",
                  userScriptFilePath.string().c_str());
        return;
    }

    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),
             "User Scripts exists : %s", userScriptFilePath.string().c_str());
    addUserScriptUrl(Url::fromLocalFile(userScriptFilePath.string()));
}

void WebPageBase::addObserver(WebPageObserver* observer)
{
    m_observers.addObserver(observer);
}

void WebPageBase::removeObserver(WebPageObserver* observer)
{
    m_observers.removeObserver(observer);
}

bool WebPageBase::isAccessibilityEnabled() const
{
     return WebAppManager::instance()->isAccessibilityEnabled();
}
