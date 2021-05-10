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

#include "WebPageBlink.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <unistd.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "ApplicationDescription.h"
#include "BlinkWebProcessManager.h"
#include "BlinkWebView.h"
#include "LogManager.h"
#include "PalmSystemBlink.h"
#include "StringUtils.h"
#include "WebAppManagerConfig.h"
#include "WebAppManagerTracer.h"
#include "WebAppManagerUtils.h"
#include "WebPageObserver.h"
#include "WebPageBlinkObserver.h"

#define DBG(fmt, ...)                           \
    do {                                        \
        fprintf(stderr, "### [WebPageBlink] "); \
        fprintf(stderr, fmt, ##__VA_ARGS__);    \
    } while (0)

namespace fs = boost::filesystem;

/**
 * Hide dirty implementation details from
 * public API
 */

static const int kExecuteCloseCallbackTimeOutMs = 10000;

class WebPageBlinkPrivate {
public:
    WebPageBlinkPrivate(WebPageBlink * page)
        : q(page)
        , pageView(0)
        , m_palmSystem(0)
    {
    }

    ~WebPageBlinkPrivate()
    {
        delete pageView;
        delete m_palmSystem;
    }


public:
    WebPageBlink *q;
    BlinkWebView *pageView;
    PalmSystemBlink* m_palmSystem;
};


WebPageBlink::WebPageBlink(const Url& url, std::shared_ptr<ApplicationDescription> desc, const std::string& params)
    : WebPageBase(url, desc, params)
    , d(new WebPageBlinkPrivate(this))
    , m_isPaused(false)
    , m_isSuspended(false)
    , m_hasCustomPolicyForResponse(false)
    , m_hasBeenShown(false)
    , m_vkbHeight(0)
    , m_vkbWasOverlap(false)
    , m_hasCloseCallback(false)
    , m_trustLevel(desc->trustLevel())
    , m_observer(nullptr)
{
}

WebPageBlink::~WebPageBlink()
{
    if(m_domSuspendTimer.isRunning())
        m_domSuspendTimer.stop();

    delete d;
    d = NULL;
}

void WebPageBlink::init()
{
    d->pageView = createPageView();
    d->pageView->setDelegate(this);
    const std::string& policy_string = m_appDesc->firstFramePolicy();
    if (policy_string.empty()) {
        //for AGL
        d->pageView->SetFirstFramePolicy(webos::WebViewBase::FirstFramePolicy::kImmediate);
    } else if (policy_string == "contents") {
        d->pageView->SetFirstFramePolicy(webos::WebViewBase::FirstFramePolicy::kContents);
    } else if (policy_string == "immediate") {
        d->pageView->SetFirstFramePolicy(webos::WebViewBase::FirstFramePolicy::kImmediate);
    }

    d->pageView->Initialize(m_appDesc->id(),
                            m_appDesc->folderPath(),
                            m_appDesc->trustLevel(),
                            m_appDesc->v8SnapshotPath(),
                            m_appDesc->v8ExtraFlags(),
                            m_appDesc->useNativeScroll());
    setViewportSize();

    d->pageView->SetVisible(false);
    d->pageView->SetUserAgent(d->pageView->DefaultUserAgent() + " " + getWebAppManagerConfig()->getName());

    if(WebAppManagerUtils::getEnv("ENABLE_INSPECTOR") == "1")
        d->pageView->SetInspectable(true);

    std::string pluginPath = WebAppManagerUtils::getEnv("PRIVILEGED_PLUGIN_PATH");
    if (!pluginPath.empty()) {
        d->pageView->AddAvailablePluginDir(pluginPath);
    }

    d->pageView->SetAllowFakeBoldText(false);

    // FIXME: It should be permitted for backward compatibility for a limited list of legacy applications only.
    d->pageView->SetAllowRunningInsecureContent(true);
    d->pageView->SetAllowScriptsToCloseWindows(true);
    d->pageView->SetAllowUniversalAccessFromFileUrls(true);
    d->pageView->SetSuppressesIncrementalRendering(true);
    d->pageView->SetDisallowScrollbarsInMainFrame(true);
    d->pageView->SetDisallowScrollingInMainFrame(true);
    d->pageView->SetDoNotTrack(m_appDesc->doNotTrack());
    d->pageView->SetJavascriptCanOpenWindows(true);
    d->pageView->SetSupportsMultipleWindows(false);
    d->pageView->SetCSSNavigationEnabled(true);
    d->pageView->SetV8DateUseSystemLocaloffset(false);
    d->pageView->SetLocalStorageEnabled(true);
    d->pageView->SetShouldSuppressDialogs(true);
    setDisallowScrolling(m_appDesc->disallowScrollingInMainFrame());

    if (!std::isnan(m_appDesc->networkStableTimeout()) && (m_appDesc->networkStableTimeout() >= 0.0))
        d->pageView->SetNetworkStableTimeout(m_appDesc->networkStableTimeout());

    if (m_appDesc->trustLevel() == "trusted") {
        LOG_DEBUG("[%s] trustLevel : trusted; allow load local Resources", appId().c_str());
        d->pageView->SetAllowLocalResourceLoad(true);
    }
    d->pageView->AddUserStyleSheet("body { -webkit-user-select: none; } :focus { outline: none }");
    d->pageView->SetBackgroundColor(29, 29, 29, 0xFF);

    setDefaultFont(defaultFont());

    d->pageView->SetFontHinting(webos::WebViewBase::FontRenderParams::HINTING_SLIGHT);

    std::string language;
    getSystemLanguage(language);
    setPreferredLanguages(language);
    d->pageView->SetAppId(appId());
    d->pageView->SetSecurityOrigin(appId());
    updateHardwareResolution();
    updateBoardType();
    updateDatabaseIdentifier();
    updateMediaCodecCapability();
    setupStaticUserScripts();
    setCustomPluginIfNeeded();
    setCustomUserScript();
    d->pageView->SetAudioGuidanceOn(isAccessibilityEnabled());
    updateBackHistoryAPIDisabled();

    d->pageView->UpdatePreferences();

    loadExtension();
}

void* WebPageBlink::getWebContents()
{
    return (void*)d->pageView->GetWebContents();
}

void WebPageBlink::handleBrowserControlCommand(const std::string& command, const std::vector<std::string>& arguments)
{
    handleBrowserControlMessage(command, arguments);
}

void WebPageBlink::handleBrowserControlFunction(const std::string& command, const std::vector<std::string>& arguments, std::string* result)
{
    *result = handleBrowserControlMessage(command, arguments);
}

std::string WebPageBlink::handleBrowserControlMessage(const std::string& message, const std::vector<std::string>& params)
{
    if (!d->m_palmSystem)
        return {};

    auto res = d->m_palmSystem->handleBrowserControlMessage(message, params);
    return res;
}

bool WebPageBlink::canGoBack()
{
    return d->pageView->CanGoBack();
}

std::string WebPageBlink::title()
{
    return d->pageView->DocumentTitle();
}

void WebPageBlink::setFocus(bool focus)
{
    d->pageView->SetFocus(focus);
}

void WebPageBlink::loadDefaultUrl()
{
    d->pageView->LoadUrl(defaultUrl().toString());
}

int WebPageBlink::progress() const
{
    return d->pageView->progress();
}

bool WebPageBlink::hasBeenShown() const
{
    return m_hasBeenShown;
}

Url WebPageBlink::url() const
{
    return Url(d->pageView->GetUrl());
}

uint32_t WebPageBlink::getWebProcessProxyID()
{
    return 0;
}

void WebPageBlink::setPreferredLanguages(const std::string& language)
{
    if (d->m_palmSystem)
        d->m_palmSystem->setLocale(language);

#ifndef TARGET_DESKTOP
    // just set system language for accept-language for http header, navigator.language, navigator.languages
    // even window.languagechange event too
    d->pageView->SetAcceptLanguages(language);
    d->pageView->UpdatePreferences();
#endif
}

void WebPageBlink::setDefaultFont(const std::string& font)
{
    d->pageView->SetStandardFontFamily(font);
    d->pageView->SetFixedFontFamily(font);
    d->pageView->SetSerifFontFamily(font);
    d->pageView->SetSansSerifFontFamily(font);
    d->pageView->SetCursiveFontFamily(font);
    d->pageView->SetFantasyFontFamily(font);
}

void WebPageBlink::reloadDefaultPage()
{
    // When WebProcess is crashed
    // not only default page reloading,
    // need to set WebProcess setting (especially the options not using Setting or preference)

    loadDefaultUrl();
}

static fs::path genPathForLang(const std::string &localeStr)
{
    auto encodingBegin = localeStr.find('.');
    auto variantBegin = localeStr.find('@');
    auto size = std::min(encodingBegin, variantBegin);
    std::string lang(localeStr, 0, size);
    std::vector<std::string> tokens;
    boost::split(tokens, lang, boost::is_any_of("_"));
    return boost::join(tokens, "/");
}


void WebPageBlink::loadErrorPage(int errorCode)
{
    std::string errorpage = getWebAppManagerConfig()->getErrorPageUrl();
    if(!errorpage.empty()) {
        if(hasLoadErrorPolicy(false, errorCode)) {
            // has loadErrorPolicy, do not show error page
            LOG_DEBUG("[%s] has own policy for Error Page, do not load Error page; send webOSLoadError event; return",
                      appId().c_str());
            return;
        }

        // Break the provided URL down into it's component pieces
        // we always assume the error page will be a file:// url, because that's
        // the only thing that makes sense.
        Url errorUrl(errorpage);
        fs::path errPagePath(errorUrl.toLocalFile());
        fs::path fileName = errPagePath.filename();
        fs::path searchPath = fs::canonical(errPagePath);
        std::string errCode = std::to_string(errorCode);

        // search order:
        // searchPath/resources/<language>/<script>/<region>/html/fileName
        // searchPath/resources/<language>/<region>/html/fileName
        // searchPath/resources/<language>/html/fileName
        // searchPath/resources/html/fileName
        // searchPath/fileName

        // exception :
        // locale : zh-Hant-HK, zh-Hant-TW
        // searchPath/resources/zh/Hant/HK/html/fileName
        // searchPath/resources/zh/Hant/TW/html/fileName
        // es-ES has resources/es/ES/html but QLocale::bcp47Name() returns es not es-ES
        // fr-CA, pt-PT has its own localization folder and QLocale::bcp47Name() returns well

        std::string language;
        getSystemLanguage(language);
        bool found = false;
        for (auto l = genPathForLang(language); !found && !l.empty(); l = l.parent_path())
            found = fs::exists(searchPath / "resources" / l / "html" / fileName);
        found = found || fs::exists(searchPath / "resources" / "html" / fileName);
        found = found || fs::exists(searchPath / fileName);

        // finally found something!
        if(found) {
            // re-create it as a proper URL, so WebKit can understand it
            m_isLoadErrorPageStart = true;
            errorUrl = Url::fromLocalFile(errPagePath.string());
            // set query items for error code and hostname to URL
            std::unordered_map<std::string, std::string> query{{"errorCode", errCode}, {"hosname", m_loadFailedHostname}};
            errorUrl.setQuery(query);
            LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),
                     "LoadErrorPage : %s", errorUrl.toString().c_str());
            d->pageView->LoadUrl(errorUrl.toString());
        } else {
            LOG_ERROR(MSGID_ERROR_ERROR, 1, PMLOGKS("PATH", errorpage.c_str()), "Error loading error page");
        }
    }
}

void WebPageBlink::reload()
{
    d->pageView->Reload();
}

void WebPageBlink::loadUrl(const std::string& url)
{
    d->pageView->LoadUrl(url);
}

void WebPageBlink::setLaunchParams(const std::string& params)
{
    WebPageBase::setLaunchParams(params);
    if (d->m_palmSystem)
        d->m_palmSystem->setLaunchParams(params);
}

void WebPageBlink::setUseLaunchOptimization(bool enabled, int delayMs) {
    if (getWebAppManagerConfig()->isLaunchOptimizationEnabled())
        d->pageView->SetUseLaunchOptimization(enabled, delayMs);
}

void WebPageBlink::setUseSystemAppOptimization(bool enabled) {
    d->pageView->SetUseEnyoOptimization(enabled);
}

void WebPageBlink::setUseAccessibility(bool enabled)
{
    d->pageView->SetUseAccessibility(enabled);
}

void WebPageBlink::setAppPreloadHint(bool is_preload)
{
    d->pageView->SetAppPreloadHint(is_preload);
}

void WebPageBlink::suspendWebPageAll()
{
    LOG_INFO(MSGID_SUSPEND_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "%s", __func__);

    d->pageView->SetVisible(false);
    if (m_isSuspended || m_enableBackgroundRun)
        return;

    if (!(WebAppManagerUtils::getEnv("WAM_KEEP_RTC_CONNECTIONS_ON_SUSPEND") == "1")) {
        // On sending applications to background, disconnect RTC
        d->pageView->DropAllPeerConnections(webos::DROP_PEER_CONNECTION_REASON_PAGE_HIDDEN);
    }

    suspendWebPageMedia();

    // suspend painting
    // set visibility : hidden
    // set send to plugin about this visibility change
    // but NOT suspend DOM and JS Excution
    /* actually suspendWebPagePaintingAndJSExecution will do this again,
      * but this visibilitychange event and paint suspend should be done ASAP
      */
    d->pageView->SuspendPaintingAndSetVisibilityHidden();


    if (isClosing()) {
        // In app closing scenario, loading about:blank and executing onclose callback should be done
        // For that, WebPage should be resume
        // So, do not suspend here
        LOG_INFO(MSGID_SUSPEND_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "InClosing; Don't start DOMSuspendTimer");
        return;
    }

    m_isSuspended = true;
    if (shouldStopJSOnSuspend()) {
        m_domSuspendTimer.start(suspendDelay(), this,
                            &WebPageBlink::suspendWebPagePaintingAndJSExecution);
    }
    LOG_INFO(MSGID_SUSPEND_WEBPAGE, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), PMLOGKFV("DELAY", "%dms", suspendDelay()), "DomSuspendTimer Started");
}

void WebPageBlink::resumeWebPageAll()
{
    LOG_INFO(MSGID_RESUME_ALL, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "");
    // resume painting
    // Resume DOM and JS Excution
    // set visibility : visible (dispatch visibilitychange event)
    // set send to plugin about this visibility change
    if (shouldStopJSOnSuspend()) {
        resumeWebPagePaintingAndJSExecution();
    }
    resumeWebPageMedia();
    d->pageView->SetVisible(true);
}

void WebPageBlink::suspendWebPageMedia()
{
    if (m_isPaused || m_enableBackgroundRun) {
        LOG_INFO(MSGID_SUSPEND_MEDIA, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "%s; Already paused; return", __func__);
        return;
    }

    d->pageView->SuspendWebPageMedia();
    m_isPaused = true;

    LOG_INFO(MSGID_SUSPEND_MEDIA, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "");

}

void WebPageBlink::resumeWebPageMedia()
{
    if (!m_isPaused) {
        LOG_INFO(MSGID_RESUME_MEDIA, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "%s; Not paused; return", __func__);
        return;
    }

    //If there is a trouble while other app loading(loading fail or other unexpected cases)
    //Set use launching time optimization false.
    //This function call ensure that case.
    setUseLaunchOptimization(false);

    d->pageView->ResumeWebPageMedia();
    m_isPaused = false;

    LOG_INFO(MSGID_RESUME_MEDIA, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "");
}

void WebPageBlink::suspendWebPagePaintingAndJSExecution()
{
    LOG_INFO(MSGID_SUSPEND_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "%s; m_isSuspended : %s", __func__, m_isSuspended ? "true" : "false; will be returned");
    if (m_domSuspendTimer.isRunning()) {
        LOG_INFO(MSGID_SUSPEND_WEBPAGE_DELAYED, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "DomSuspendTimer Expired; suspend DOM");
        m_domSuspendTimer.stop();
    }

    if (m_enableBackgroundRun)
        return;

    if (!m_isSuspended)
        return;

    // if we haven't finished loading the page yet, wait until it is loaded before suspending
    bool isLoading = !hasBeenShown() && progress() < 100;
    if (isLoading) {
        LOG_INFO(MSGID_SUSPEND_WEBPAGE, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),  PMLOGKS("URL", qPrintable(url().toString())), "Currently loading, Do not suspend, return");
        m_suspendAtLoad = true;
    } else {
        d->pageView->SuspendPaintingAndSetVisibilityHidden();
        d->pageView->SuspendWebPageDOM();
        LOG_INFO(MSGID_SUSPEND_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "DONE");
    }
}

void WebPageBlink::resumeWebPagePaintingAndJSExecution()
{
    LOG_INFO(MSGID_RESUME_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "%s; m_isSuspended : %s ", __func__, m_isSuspended ? "true" : "false; nothing to resume");
    m_suspendAtLoad = false;
    if (m_isSuspended) {
        if (m_domSuspendTimer.isRunning()) {
            LOG_INFO(MSGID_SUSPEND_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "DomSuspendTimer canceled by Resume");
            m_domSuspendTimer.stop();
            d->pageView->ResumePaintingAndSetVisibilityVisible();
        } else {
            d->pageView->ResumeWebPageDOM();
            d->pageView->ResumePaintingAndSetVisibilityVisible();
            LOG_INFO(MSGID_RESUME_WEBPAGE, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "DONE");
        }
        m_isSuspended = false;
    }
}

// TODO: Refactor this
std::string WebPageBlink::escapeData(const std::string& value)
{
    std::string escapedValue(value);
    replaceSubstrings(escapedValue, "\\", "\\\\");
    replaceSubstrings(escapedValue, "'", "\\'");
    replaceSubstrings(escapedValue, "\n", "\\n");
    replaceSubstrings(escapedValue, "\r", "\\r");
    return escapedValue;
}

void WebPageBlink::reloadExtensionData()
{
    std::string eventJS =
       "if (typeof(webOSSystem) != 'undefined') {"
       "  webOSSystem.reloadInjectionData();"
       "};";
    LOG_INFO(MSGID_PALMSYSTEM, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "Reload");
    evaluateJavaScript(eventJS);
}

void WebPageBlink::updateExtensionData(const std::string& key, const std::string& value)
{
    if (!d->m_palmSystem->isInitialized()) {
        LOG_WARNING(MSGID_PALMSYSTEM, 2,
            PMLOGKS("APP_ID", appId().c_str()),
            PMLOGKFV("PID", "%d", getWebProcessPID()),
            "webOSSystem is not initialized. key:%s, value:%s", qPrintable(key), qPrintable(value));
        return;
    }
    std::stringstream eventJS;
    eventJS
       << "if (typeof(PalmSystem) != 'undefined') {"
       << "  webOSSystem.updateInjectionData('" << escapeData(key)
       << "', '" << escapeData(value) << "');"
       << "};";

    LOG_INFO(MSGID_PALMSYSTEM, 2, PMLOGKS("APP_ID", appId().c_str()),
             PMLOGKFV("PID", "%d", getWebProcessPID()),
             "Update; key:%s; value:%s", key.c_str(), value.c_str());
    evaluateJavaScript(eventJS.str());
}

void WebPageBlink::updatePageSettings()
{
    // When a container based app is launched
    // if there any properties different from container app then should update
    // ex, application description info
    if(!m_appDesc)
        return;

    if(m_appDesc->trustLevel() == "trusted") {
        LOG_DEBUG("[%s] trustLevel : trusted; allow load local Resources", appId().c_str());
        d->pageView->SetAllowLocalResourceLoad(true);
    }

    LOG_DEBUG("[%s] WebPageBlink::updatePageSettings(); update appId to chromium", appId().c_str());
    d->pageView->SetAppId(appId());
    d->pageView->SetTrustLevel(m_appDesc->trustLevel());
    d->pageView->SetAppPath(m_appDesc->folderPath());

    if (!std::isnan(m_appDesc->networkStableTimeout()) && (m_appDesc->networkStableTimeout() >= 0.0))
        d->pageView->SetNetworkStableTimeout(m_appDesc->networkStableTimeout());

    setCustomPluginIfNeeded();
    updateBackHistoryAPIDisabled();
    d->pageView->UpdatePreferences();
}

void WebPageBlink::handleDeviceInfoChanged(const std::string& deviceInfo)
{
    if (!d->m_palmSystem)
        return;

    if (deviceInfo == "LocalCountry" || deviceInfo == "SmartServiceCountry")
        d->m_palmSystem->setCountry();
}

void WebPageBlink::evaluateJavaScript(const std::string& jsCode)
{
    d->pageView->RunJavaScript(jsCode);
}

void WebPageBlink::evaluateJavaScriptInAllFrames(const std::string& script, const char *method)
{
    d->pageView->RunJavaScriptInAllFrames(script);
}

void WebPageBlink::cleanResources()
{
    WebPageBase::cleanResources();
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "StopLoading and load about:blank");
    d->pageView->StopLoading();
    d->pageView->LoadUrl(std::string("about:blank"));
}

void WebPageBlink::close()
{
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, webPageClosePageRequested());
}

void WebPageBlink::didFirstFrameFocused()
{
    LOG_DEBUG("[%s] render process frame focused for the first time", appId().c_str());
    //App load is finished, set use launching time optimization false.
    //If Launch optimization had to be done late, use delayMsForLaunchOptmization
    int delayMs = m_appDesc->delayMsForLaunchOptimization();
    if (delayMs > 0)
        setUseLaunchOptimization(false, delayMs);
    else
        setUseLaunchOptimization(false);
}

void WebPageBlink::didDropAllPeerConnections()
{
}

void WebPageBlink::didSwapCompositorFrame()
{
    if (m_observer)
        m_observer->didSwapPageCompositorFrame();
}


void WebPageBlink::loadFinished(const std::string& url)
{
    LOG_INFO(MSGID_WEBPAGE_LOAD_FINISHED, 2,
        PMLOGKS("APP_ID", appId().c_str()),
        PMLOGKFV("PID", "%d", getWebProcessPID()),
        "url from web engine : %s", url.c_str());

    if (cleaningResources()) {
        LOG_INFO(MSGID_WEBPAGE_LOAD_FINISHED,
            2,
            PMLOGKS("APP_ID", appId().c_str()),
            PMLOGKFV("PID", "%d", getWebProcessPID()),
            "cleaningResources():true; (should be about:blank) emit 'didDispatchUnload'");
        FOR_EACH_OBSERVER(WebPageObserver, m_observers, didDispatchUnload());
        return;
    }
    handleLoadFinished();
}

void WebPageBlink::loadStarted()
{
    LOG_INFO(MSGID_PAGE_LOADING, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),  PMLOGKS("LOADING", "STARTED"), "");
    m_hasCloseCallback = false;
    handleLoadStarted();
}

void WebPageBlink::loadStopped(const std::string& url)
{
    LOG_INFO(MSGID_PAGE_LOADING, 3, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()),  PMLOGKS("LOADING", "STOPPED"), "");
}

void WebPageBlink::loadFailed(const std::string& url, int errCode, const std::string& errDesc)
{
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, webPageLoadFailed(errCode));

    // We follow through only if we have SSL error
    if (errDesc != "SSL_ERROR")
        return;

    LOG_WARNING(MSGID_PAGE_LOAD_FAILED, 4,
                PMLOGKS("APP_ID", appId().c_str()),
                PMLOGKFV("ERROR_CODE", "%d", errCode),
                PMLOGKS("ERROR_STR", errDesc.c_str()),
                PMLOGKS("URL", url.c_str()),
                " ");
    m_loadFailedHostname = Url(url).host();
    handleLoadFailed(errCode);
}

void WebPageBlink::didErrorPageLoadedFromNetErrorHelper() {
   m_didErrorPageLoadedFromNetErrorHelper = true;
}

void WebPageBlink::loadVisuallyCommitted()
{
    m_hasBeenShown = true;
    FOR_EACH_OBSERVER(WebPageObserver,
                      m_observers, firstFrameVisuallyCommitted());
}

void WebPageBlink::renderProcessCreated(int pid)
{
    postWebProcessCreated(pid);
}

void WebPageBlink::titleChanged(const std::string& title)
{
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, titleChanged());
}

void WebPageBlink::navigationHistoryChanged()
{
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, navigationHistoryChanged());
}

void WebPageBlink::forwardEvent(void* event)
{
    d->pageView->ForwardWebOSEvent((WebOSEvent*)event);
}

void WebPageBlink::recreateWebView()
{
    LOG_INFO(MSGID_WEBPROC_CRASH, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "recreateWebView; initialize WebPage");
    delete d->pageView;
    if(!m_customPluginPath.empty()) {
        // check setCustomPluginIfNeeded logic
        // not to set duplicated plugin path, it compares m_customPluginPath and new one
        m_customPluginPath = "";  // just make it init state
    }

    init();
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, webViewRecreated());

    if (!m_isSuspended) {
        // Remove white screen while reloading contents due to the renderer crash
        // 1. Reset state to mark next paint for notification when FMP done.
        //    It will be used to make webview visible later.
        d->pageView->ResetStateToMarkNextPaint();
        // 2. Set VisibilityState as Launching
        //    It will be used later, WebViewImpl set RenderWidgetCompositor visible,
        //    and make it keep to render the contents.
        setVisibilityState(WebPageBase::WebPageVisibilityState::WebPageVisibilityStateLaunching);
    }

    if (m_isSuspended)
        m_isSuspended = false;
}

void WebPageBlink::setVisible(bool visible)
{
    d->pageView->SetVisible(visible);
}

void WebPageBlink::setViewportSize()
{
    if (m_appDesc->widthOverride() && m_appDesc->heightOverride()) {
        d->pageView->SetViewportSize(m_appDesc->widthOverride(), m_appDesc->heightOverride());
    }
}

void WebPageBlink::notifyMemoryPressure(webos::WebViewBase::MemoryPressureLevel level)
{
    d->pageView->NotifyMemoryPressure(level);
}

void WebPageBlink::renderProcessCrashed()
{
    LOG_INFO(MSGID_WEBPROC_CRASH, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "m_isSuspended : %s", m_isSuspended?"true":"false");
    if (isClosing()) {
        LOG_INFO(MSGID_WEBPROC_CRASH, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "In Closing; return");
        if (m_closeCallbackTimer.isRunning())
            m_closeCallbackTimer.stop();

        FOR_EACH_OBSERVER(WebPageObserver, m_observers, closingAppProcessDidCrashed());
        return;
    }

    d->m_palmSystem->resetInitialized();
    recreateWebView();
    if (!processCrashed())
        handleForceDeleteWebPage();
}

// functions from webappmanager2
BlinkWebView * WebPageBlink::createPageView()
{
    return new BlinkWebView();
}

BlinkWebView* WebPageBlink::pageView() const
{
    return d->pageView;
}

bool WebPageBlink::inspectable()
{
    return getWebAppManagerConfig()->isInspectorEnabled();
}


// webOSLaunch / webOSRelaunch event:
// webOSLaunch event should be fired after DOMContentLoaded, and contains the launch parameters as it's detail.
// webOSRelaunch event should be fired when an app that is already running is triggered from applicationManager/launch, and
// will also contain the launch parameters as it's detail.
// IF we fire webOSLaunch immediately at handleLoadFinished(), the document may receive it before it has parsed all of the scripts.

// We cannot setup a generic script at page creation, because we don't know the launch parameters at
// that time. So, at load start, we'll take care of adding a user script.  Once that script has been
// added, it does not need to be added again -- triggering a page reload will cause it to fire the
// event again.

// There are a few caveats here, though:
// 1- We don't want to make a seperate HTML file just for this, so we use the C API for adding a UserScript
// 2- The QT API for adding a user script only accepts a URL to a file, not absolute code.
// 3- We can't call WKPageGroupAddUserScript with the same argument more than once unless we want duplicate code to run

// So, we clear out any userscripts that may have been set, add any userscript files (ie Tellurium) via the QT API,
// then add any other userscripts that we might want via the C API, and then proceed.

// IF any further userscripts are desired in the future, they should be added here.
void WebPageBlink::addUserScript(const std::string& script)
{
    d->pageView->addUserScript(script);
}

void WebPageBlink::addUserScriptUrl(const Url& url)
{
    if (!url.isLocalFile()) {
        LOG_DEBUG("WebPageBlink: Couldn't open '%s' as user script because only file:/// URLs are supported.",
                  url.toString().c_str());
        return;
    }

    std::string path(url.toLocalFile());
    std::string script;
    try {
        WebAppManagerUtils::readFileContent(path, script);
        if (script.empty()) {
            LOG_DEBUG("WebPageBlink: Ignoring '%s' as user script because file is empty.", url.toString().c_str());
            return;
        }
    } catch (const std::exception &e) {
        LOG_DEBUG("WebPageBlink: Couldn't set '%s' as user script due to error '%s'.",
                  url.toString().c_str(), e.what());
        return;
    }

    d->pageView->addUserScript(script);
}

void WebPageBlink::setupStaticUserScripts()
{
    d->pageView->clearUserScripts();

    // Load Tellurium test framework if available, as a UserScript
    std::string telluriumNubPath_ = telluriumNubPath();
    if (!telluriumNubPath_.empty()) {
        LOG_DEBUG("Loading tellurium nub at %s", telluriumNubPath_.c_str());
        addUserScriptUrl(Url::fromLocalFile(telluriumNubPath_));
    }
}

void WebPageBlink::closeVkb()
{
}

bool WebPageBlink::isInputMethodActive() const
{
    return d->pageView->IsInputMethodActive();
}

void WebPageBlink::setPageProperties()
{
    if (m_appDesc->isTransparent()) {
        d->pageView->SetTransparentBackground(true);
    }

#if defined(OS_WEBOS) || defined(AGL_DEVEL)
    // Set inspectable. For AGL this feature is only available if the
    // 'agl-devel' distro feature is on.
    if (m_appDesc->isInspectable() || inspectable()) {
        LOG_DEBUG("[%s] inspectable : true or 'debug_system_apps' mode; setInspectablePage(true)", appId().c_str());
        d->pageView->EnableInspectablePage();
    }
#endif

    setTrustLevel(defaultTrustLevel());
    d->pageView->UpdatePreferences();
}

void WebPageBlink::createPalmSystem(WebAppBase* app)
{
    d->m_palmSystem = new PalmSystemBlink(app);
    d->m_palmSystem->setLaunchParams(m_launchParams);
}

std::string WebPageBlink::defaultTrustLevel() const
{
    return m_appDesc->trustLevel();
}

void WebPageBlink::loadExtension()
{
    LOG_DEBUG("WebPageBlink::loadExtension(); Extension : webossystem");
    d->pageView->LoadExtension("webossystem");
    d->pageView->LoadExtension("webosservicebridge");
}

void WebPageBlink::clearExtensions()
{
    if (d && d->pageView)
        d->pageView->ClearExtensions();
}

void WebPageBlink::setCustomPluginIfNeeded()
{
    if (!m_appDesc || !m_appDesc->useCustomPlugin())
        return;

    auto customPluginPath = fs::path(m_appDesc->folderPath()) / "plugins";

    if (!fs::exists(customPluginPath) || !fs::is_directory(customPluginPath))
        return;
    if (!customPluginPath.compare(m_customPluginPath))
        return;

    m_customPluginPath = customPluginPath.string();
    LOG_INFO(MSGID_WAM_DEBUG, 3, PMLOGKS("APP_ID", appId().c_str()),
             PMLOGKFV("PID", "%d", getWebProcessPID()),
             PMLOGKS("CUSTOM_PLUGIN_PATH", m_customPluginPath.c_str()),
             "%s", __func__);

    d->pageView->AddCustomPluginDir(m_customPluginPath);
    d->pageView->AddAvailablePluginDir(m_customPluginPath);
}

void WebPageBlink::setDisallowScrolling(bool disallow)
{
    d->pageView->SetDisallowScrollbarsInMainFrame(disallow);
    d->pageView->SetDisallowScrollingInMainFrame(disallow);
}

int WebPageBlink::renderProcessPid() const
{
    return d->pageView->RenderProcessPid();
}

void WebPageBlink::didRunCloseCallback()
{
    m_closeCallbackTimer.stop();
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()),
             PMLOGKFV("PID", "%d", getWebProcessPID()),
             "WebPageBlink::didRunCloseCallback(); onclose callback done");
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, closeCallbackExecuted());
}

void WebPageBlink::setHasOnCloseCallback(bool hasCloseCallback)
{
    m_hasCloseCallback = hasCloseCallback;
}

void WebPageBlink::executeCloseCallback(bool forced)
{
    std::stringstream script;
    script
        << "window.webOSSystem._onCloseWithNotify_('"
        << (forced ? "forced" : "normal")
        << "');";
    evaluateJavaScript(script.str());

    m_closeCallbackTimer.start(kExecuteCloseCallbackTimeOutMs, this, &WebPageBlink::timeoutCloseCallback);
}

void WebPageBlink::timeoutCloseCallback()
{
    m_closeCallbackTimer.stop();
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "WebPageBlink::timeoutCloseCallback(); onclose callback Timeout");
    FOR_EACH_OBSERVER(WebPageObserver, m_observers, timeoutExecuteCloseCallback());
}

void WebPageBlink::setFileAccessBlocked(bool blocked)
{
    //TO_DO: Need to verify when shnapshot is ready.
    webos::WebViewBase::SetFileAccessBlocked(blocked);
}

void WebPageBlink::setAdditionalContentsScale(float scaleX, float scaleY)
{
    d->pageView->SetAdditionalContentsScale(scaleX, scaleY);
}

void WebPageBlink::updateHardwareResolution()
{
    std::string hardwareWidth, hardwareHeight;
    getDeviceInfo("HardwareScreenWidth", hardwareWidth);
    getDeviceInfo("HardwareScreenHeight", hardwareHeight);
    int w = stringTo<int>(hardwareWidth);
    int h = stringTo<int>(hardwareHeight);
    d->pageView->SetHardwareResolution(w, h);
}

void WebPageBlink::updateBoardType()
{
    std::string boardType;
    getDeviceInfo("boardType", boardType);
    d->pageView->SetBoardType(boardType);
}

void WebPageBlink::updateMediaCodecCapability()
{
    fs::path file("/etc/umediaserver/device_codec_capability_config.json");
    if (!fs::exists(file) || !fs::is_regular_file(file))
        return;

    std::string capability;
    try {
        WebAppManagerUtils::readFileContent(file.string(), capability);
    } catch (const std::exception &e) {
        LOG_DEBUG("WebPageBlink: Couldn't load '%s' due to error '%s'.",
                  file.string().c_str(), e.what());
        return;
    }

    d->pageView->SetMediaCodecCapability(capability);
}

double WebPageBlink::devicePixelRatio()
{
    double appWidth = static_cast<double>(m_appDesc->widthOverride());
    double appHeight =  static_cast<double>(m_appDesc->heightOverride());
    if(appWidth == 0) appWidth = static_cast<double>(currentUiWidth());
    if(appHeight == 0) appHeight = static_cast<double>(currentUiHeight());

    std::string hardwareWidth, hardwareHeight;
    getDeviceInfo("HardwareScreenWidth", hardwareWidth);
    getDeviceInfo("HardwareScreenHeight", hardwareHeight);

    double deviceWidth = stringTo<double>(hardwareWidth);
    double deviceHeight = stringTo<double>(hardwareHeight);

    double ratioX = deviceWidth/appWidth;
    double ratioY = deviceHeight/appHeight;
    double devicePixelRatio = 1.0;
    if(ratioX != ratioY) {
        // device resolution : 5120x2160 (UHD 21:9 - D9)
        // - app resolution : 1280x720 ==> 4:3 (have to take 3)
        // - app resolution : 1920x1080 ==> 2.6:2 (have to take 2)
        devicePixelRatio = (ratioX < ratioY) ? ratioX : ratioY;
    } else {
        // device resolution : 1920x1080
        // - app resolution : 1280x720 ==> 1.5:1.5
        // - app resolution : 1920x1080 ==> 1:1
        // device resolution : 3840x2160
        // - app resolution : 1280x720 ==> 3:3
        // - app resolution : 1920x1080 ==> 2:2
        devicePixelRatio = ratioX;
    }
    LOG_DEBUG("[%s] WebPageBlink::devicePixelRatio(); devicePixelRatio : %f; deviceWidth : %f, deviceHeight : %f, appWidth : %f, appHeight : %f",
        appId().c_str(), devicePixelRatio, deviceWidth, deviceHeight, appWidth, appHeight);
    return devicePixelRatio;
}

void WebPageBlink::updateDatabaseIdentifier()
{
    d->pageView->SetDatabaseIdentifier(m_appId);
}

void WebPageBlink::deleteWebStorages(const std::string& identifier)
{
    d->pageView->DeleteWebStorages(identifier);
}

void WebPageBlink::setInspectorEnable()
{
#if defined(OS_WEBOS) || defined(AGL_DEVEL)
    LOG_DEBUG("[%s] Inspector enable", appId().c_str());
    d->pageView->EnableInspectablePage();
#endif
}

void WebPageBlink::setKeepAliveWebApp(bool keepAlive) {
    LOG_INFO(MSGID_WAM_DEBUG, 2, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), "setKeepAliveWebApp(%s)", keepAlive?"true":"false");
    d->pageView->SetKeepAliveWebApp(keepAlive);
    d->pageView->UpdatePreferences();
}

void WebPageBlink::setLoadErrorPolicy(const std::string& policy)
{
    m_loadErrorPolicy = policy;
    if(policy == "event") {
        // policy : event
        m_hasCustomPolicyForResponse = true;
    } else if (policy == "default") {
        // policy : default, WAM and blink handle all load errors
        m_hasCustomPolicyForResponse = false;
    }
}

bool WebPageBlink::decidePolicyForResponse(bool isMainFrame, int statusCode, const std::string& url, const std::string& statusText)
{
    LOG_INFO(MSGID_WAM_DEBUG, 7, PMLOGKS("APP_ID", appId().c_str()), PMLOGKFV("PID", "%d", getWebProcessPID()), PMLOGKFV("STATUS_CODE", "%d", statusCode),
        PMLOGKS("URL", url.c_str()), PMLOGKS("TEXT", statusText.c_str()), PMLOGKS("MAIN_FRAME", isMainFrame ? "true" : "false"), PMLOGKS("RESPONSE_POLICY", isMainFrame ? "event" : "default"), "");

    // how to WAM3 handle this response
    applyPolicyForUrlResponse(isMainFrame, url, statusCode);

    // how to blink handle this response
    // ACR requirement : even if received error response from subframe(iframe)ACR app should handle that as a error
    return m_hasCustomPolicyForResponse;
}

bool WebPageBlink::acceptsVideoCapture()
{
  return m_appDesc->allowVideoCapture();
}

bool WebPageBlink::acceptsAudioCapture()
{
  return m_appDesc->allowAudioCapture();
}

void WebPageBlink::keyboardVisibilityChanged(bool visible)
{
    std::stringstream javascript;
    std::string v = visible ? "true" : "false";
    javascript
        << "console.log('[WAM] fires keyboardStateChange event : " << v << "');"
        << "var keyboardStateEvent =new CustomEvent('keyboardStateChange', { detail: { 'visibility' : " << v << " } });"
        << "keyboardStateEvent.visibility = " << v << ";"
        << "if(document) document.dispatchEvent(keyboardStateEvent);";
    evaluateJavaScript(javascript.str());
}

void WebPageBlink::updateIsLoadErrorPageFinish()
{
    // If currently loading finished URL is not error page,
    // m_isLoadErrorPageFinish will be updated
    bool wasErrorPage = m_isLoadErrorPageFinish;
    WebPageBase::updateIsLoadErrorPageFinish();

    if (trustLevel() != "trusted" && wasErrorPage != m_isLoadErrorPageFinish) {
        if (m_isLoadErrorPageFinish) {
            LOG_DEBUG("[%s] WebPageBlink::updateIsLoadErrorPageFinish(); m_isLoadErrorPageFinish : %s, set trustLevel : trusted to WAM and webOSSystem_injection", appId().c_str(), m_isLoadErrorPageFinish ? "true" : "false");
            setTrustLevel("trusted");
            updateExtensionData("trustLevel", "trusted");
        }
    } else {
        setTrustLevel(defaultTrustLevel());
        updateExtensionData("trustLevel", trustLevel());
    }
}

void WebPageBlink::setAudioGuidanceOn(bool on)
{
    d->pageView->SetAudioGuidanceOn(on);
    d->pageView->UpdatePreferences();
}

void WebPageBlink::updateBackHistoryAPIDisabled()
{
    d->pageView->SetBackHistoryAPIDisabled(m_appDesc->backHistoryAPIDisabled());
}

void WebPageBlink::setVisibilityState(WebPageVisibilityState visibilityState)
{
    d->pageView->SetVisibilityState(static_cast<webos::WebViewBase::WebPageVisibilityState>(visibilityState));
}

bool WebPageBlink::allowMouseOnOffEvent() const {
    return false;
}

void WebPageBlink::setObserver(WebPageBlinkObserver* observer) {
    m_observer = observer;
}
