/*
 * Copyright (C) 2018-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebAuthenticatorCoordinatorProxy.h"

#if ENABLE(WEB_AUTHN)

#include "APIUIClient.h"
#include "AuthenticatorManager.h"
#include "LocalService.h"
#include "Logging.h"
#include "WebAuthenticationFlags.h"
#include "WebAuthenticatorCoordinatorProxyMessages.h"
#include "WebFrameProxy.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"
#include "WebsiteDataStore.h"
#include <WebCore/AuthenticatorResponseData.h>
#include <WebCore/ExceptionData.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/WebAuthenticationUtils.h>
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {
using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebAuthenticatorCoordinatorProxy);

Ref<WebAuthenticatorCoordinatorProxy> WebAuthenticatorCoordinatorProxy::create(WebPageProxy& webPageProxy)
{
    return adoptRef(*new WebAuthenticatorCoordinatorProxy(webPageProxy));
}

WebAuthenticatorCoordinatorProxy::WebAuthenticatorCoordinatorProxy(WebPageProxy& webPageProxy)
    : m_webPageProxy(webPageProxy)
{
    protect(webPageProxy.legacyMainFrameProcess())->addMessageReceiver(Messages::WebAuthenticatorCoordinatorProxy::messageReceiverName(), webPageProxy.webPageIDInMainFrameProcess(), *this);
}

WebAuthenticatorCoordinatorProxy::~WebAuthenticatorCoordinatorProxy()
{
#if HAVE(UNIFIED_ASC_AUTH_UI)
    cancel([]() { });
#endif // HAVE(UNIFIED_ASC_AUTH_UI)
    if (RefPtr webPageProxy = m_webPageProxy.get())
        protect(webPageProxy->legacyMainFrameProcess())->removeMessageReceiver(Messages::WebAuthenticatorCoordinatorProxy::messageReceiverName(), webPageProxy->webPageIDInMainFrameProcess());
}

std::optional<SharedPreferencesForWebProcess> WebAuthenticatorCoordinatorProxy::sharedPreferencesForWebProcess() const
{
    auto* webPageProxy = m_webPageProxy.get();
    return webPageProxy ? webPageProxy->legacyMainFrameProcess().sharedPreferencesForWebProcess() : std::nullopt;
}

void WebAuthenticatorCoordinatorProxy::makeCredential(IPC::Connection& connection, FrameIdentifier frameId, FrameInfoData&& frameInfo, PublicKeyCredentialCreationOptions&& options, MediationRequirement mediation, RequestCompletionHandler&& handler)
{
    RefPtr webPageProxy = m_webPageProxy.get();
    if (!webPageProxy) {
        handler({ }, (AuthenticatorAttachment)0, ExceptionData { ExceptionCode::NotSupportedError, "This request is not supported at this time."_s });
        RELEASE_LOG_ERROR(WebAuthn, "WebPageProxy had been released");
        return;
    }

    RefPtr frame = WebFrameProxy::webFrame(frameId);
    if (!frame) {
        RELEASE_LOG_ERROR(WebAuthn, "Frame not found for WebAuthn MakeCredential request");
        return handler({ }, static_cast<AuthenticatorAttachment>(0), ExceptionData { ExceptionCode::InvalidStateError });
    }
    if (frame->url().protocolIsInHTTPFamily()) {
        auto expectedOrigin = SecurityOriginData::fromURLWithoutStrictOpaqueness(frame->url());
        MESSAGE_CHECK_COMPLETION_BASE(frameInfo.securityOrigin == expectedOrigin, connection,
            handler({ }, static_cast<AuthenticatorAttachment>(0), ExceptionData { ExceptionCode::InvalidStateError }));
    }

    handleRequest({ { }, WTF::move(options), *webPageProxy, WebAuthenticationPanelResult::Unavailable, nullptr, GlobalFrameIdentifier { webPageProxy->webPageIDInMainFrameProcess(), frameId }, WTF::move(frameInfo), String(), nullptr, mediation, std::nullopt }, WTF::move(handler));
}

void WebAuthenticatorCoordinatorProxy::getAssertion(IPC::Connection& connection, FrameIdentifier frameId, FrameInfoData&& frameInfo, PublicKeyCredentialRequestOptions&& options, MediationRequirement mediation, std::optional<WebCore::SecurityOriginData> parentOrigin, RequestCompletionHandler&& handler)
{
    RefPtr webPageProxy = m_webPageProxy.get();
    if (!webPageProxy) {
        handler({ }, (AuthenticatorAttachment)0, ExceptionData { ExceptionCode::NotSupportedError, "This request is not supported at this time."_s });
        RELEASE_LOG_ERROR(WebAuthn, "WebPageProxy had been released");
        return;
    }

    RefPtr frame = WebFrameProxy::webFrame(frameId);
    if (!frame) {
        RELEASE_LOG_ERROR(WebAuthn, "Frame not found for WebAuthn GetAssertion request");
        return handler({ }, static_cast<AuthenticatorAttachment>(0), ExceptionData { ExceptionCode::InvalidStateError });
    }
    if (frame->url().protocolIsInHTTPFamily()) {
        auto expectedOrigin = SecurityOriginData::fromURLWithoutStrictOpaqueness(frame->url());
        MESSAGE_CHECK_COMPLETION_BASE(frameInfo.securityOrigin == expectedOrigin, connection,
            handler({ }, static_cast<AuthenticatorAttachment>(0), ExceptionData { ExceptionCode::InvalidStateError }));
    }

    if (parentOrigin) {
        bool foundMatchingAncestor = false;
        bool hasHTTPAncestor = false;
        for (RefPtr ancestor = frame->parentFrame(); ancestor; ancestor = ancestor->parentFrame()) {
            if (!ancestor->url().protocolIsInHTTPFamily())
                continue;
            hasHTTPAncestor = true;
            auto ancestorOrigin = SecurityOriginData::fromURLWithoutStrictOpaqueness(ancestor->url());
            if (*parentOrigin == ancestorOrigin) {
                foundMatchingAncestor = true;
                break;
            }
        }
        if (hasHTTPAncestor) {
            MESSAGE_CHECK_COMPLETION_BASE(foundMatchingAncestor, connection,
                handler({ }, static_cast<AuthenticatorAttachment>(0), ExceptionData { ExceptionCode::InvalidStateError }));
        }
    }

    handleRequest({ { }, WTF::move(options), *webPageProxy, WebAuthenticationPanelResult::Unavailable, nullptr, GlobalFrameIdentifier { webPageProxy->webPageIDInMainFrameProcess(), frameId }, WTF::move(frameInfo), String(), nullptr, mediation, parentOrigin }, WTF::move(handler));
}

void WebAuthenticatorCoordinatorProxy::handleRequest(WebAuthenticationRequestData&& data, RequestCompletionHandler&& handler)
{
    if (!data.frameInfo)
        return handler({ }, AuthenticatorAttachment::Platform, ExceptionData { ExceptionCode::InvalidStateError });

#if ENABLE(WEB_AUTHN_OWN_MANAGER)
    // Conditional mediation needs an account picker in the page's autofill UI, which the
    // _WKWebAuthenticationPanel API has no place for. isConditionalMediationAvailable() reports
    // false, and a conditional request fails as the spec asks instead of opening a modal panel.
    if (data.mediation == MediationRequirement::Conditional) {
        Ref authenticatorManager = m_webPageProxy->websiteDataStore().authenticatorManager();
        if (!authenticatorManager->isMock() && !authenticatorManager->isVirtual())
            return handler({ }, AuthenticatorAttachment::Platform, ExceptionData { ExceptionCode::NotSupportedError, "Conditional mediation is not supported."_s });
    }
#endif

    auto origin = API::SecurityOrigin::create(data.frameInfo->securityOrigin);

    bool shouldRequestConditionalRegistration = std::holds_alternative<PublicKeyCredentialCreationOptions>(data.options) && data.mediation == MediationRequirement::Conditional;

    String username;
    if (shouldRequestConditionalRegistration)
        username = std::get<PublicKeyCredentialCreationOptions>(data.options).user.name;

    CompletionHandler<void(bool)> afterConsent = [weakThis = WeakPtr { *this }, data = WTF::move(data), handler = WTF::move(handler)] (bool result) mutable {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return handler({ }, AuthenticatorAttachment::Platform, ExceptionData { ExceptionCode::InvalidStateError });

        Ref authenticatorManager = protectedThis->m_webPageProxy->websiteDataStore().authenticatorManager();
        if (result) {
#if (HAVE(UNIFIED_ASC_AUTH_UI) || HAVE(WEB_AUTHN_AS_MODERN)) && !ENABLE(WEB_AUTHN_OWN_MANAGER)
            if (!authenticatorManager->isMock() && !authenticatorManager->isVirtual()) {
                if (!protectedThis->isASCAvailable()) {
                    handler({ }, AuthenticatorAttachment::Platform, ExceptionData { ExceptionCode::NotSupportedError, "Not implemented."_s });
                    RELEASE_LOG_ERROR(WebAuthn, "Web Authentication is not currently supported in this environment.");
                    return;
                }
                // performRequest calls out to ASCAgent which will then call [_WKWebAuthenticationPanel makeCredential/getAssertionWithChallenge]
                // which calls authenticatorManager->handleRequest(..)
                protectedThis->performRequest(WTF::move(data), WTF::move(handler));
                return;
            }
#endif // (HAVE(UNIFIED_ASC_AUTH_UI) || HAVE(WEB_AUTHN_AS_MODERN)) && !ENABLE(WEB_AUTHN_OWN_MANAGER)

            // From here on WebKit's own AuthenticatorManager serves the request. A cross-origin
            // iframe gets here only for get(): the web process lets it through when the
            // publickey-credentials-get permissions policy allows it (create() stays same-origin),
            // and getAssertion() checked parentOrigin against the frame's real ancestors. The
            // client data then carries crossOrigin and topOrigin, as the spec asks, the same as on
            // the AuthenticationServices path; no blanket rejection of cross-origin callers.

            RefPtr<ArrayBuffer> clientDataJSON;
            // AS API makes no difference between SameSite vs CrossOrigin
            WebAuthn::Scope scope = data.parentOrigin ? WebAuthn::Scope::CrossOrigin : WebAuthn::Scope::SameOrigin;
            auto topOrigin = data.parentOrigin ? data.parentOrigin->toString() : nullString();
            WTF::switchOn(data.options, [&](const PublicKeyCredentialCreationOptions& options) {
                clientDataJSON = buildClientDataJson(ClientDataType::Create, options.challenge, data.frameInfo->securityOrigin.securityOrigin(), scope, topOrigin);
            }, [&](const PublicKeyCredentialRequestOptions& options) {
                clientDataJSON = buildClientDataJson(ClientDataType::Get, options.challenge, data.frameInfo->securityOrigin.securityOrigin(), scope, topOrigin);
            });
            data.hash = buildClientDataJsonHash(*clientDataJSON);

            auto requestID = ++protectedThis->m_lastAuthenticatorManagerRequestID;
            protectedThis->m_pendingAuthenticatorManagerRequestID = requestID;
            authenticatorManager->handleRequest(WTF::move(data), [weakThis, requestID, handler = WTF::move(handler), clientDataJSON = WTF::move(clientDataJSON)] (Variant<Ref<AuthenticatorResponse>, ExceptionData>&& result) mutable {
                ASSERT(RunLoop::isMain());
                // Not protected with a RefPtr: this also runs from cancel() in the destructor.
                if (weakThis && weakThis->m_pendingAuthenticatorManagerRequestID == requestID)
                    weakThis->m_pendingAuthenticatorManagerRequestID = 0;
                WTF::switchOn(result, [&](const Ref<AuthenticatorResponse>& response) {
                    auto responseData = response->data();
                    responseData.clientDataJSON = WTF::move(clientDataJSON);
                    handler(responseData, response->attachment(), { });
                }, [&](const ExceptionData& exception) {
                    handler({ }, (AuthenticatorAttachment)0, exception);
                });
            });
        } else {
            handler({ }, (AuthenticatorAttachment)0, ExceptionData { ExceptionCode::NotAllowedError, "This request has been cancelled by the user."_s });
            RELEASE_LOG_ERROR(WebAuthn, "Request cancelled due to rejected prompt after lack of user gesture.");
        }
    };

    Ref authenticatorManager = m_webPageProxy->websiteDataStore().authenticatorManager();
    if (shouldRequestConditionalRegistration && !authenticatorManager->isMock() && !authenticatorManager->isVirtual()) {
        m_webPageProxy->uiClient().requestWebAuthenticationConditonalMediationRegistration(username, [weakThis = WeakPtr { *this }, username, afterConsent = WTF::move(afterConsent), origin = origin->securityOrigin()] (std::optional<bool> consented) mutable {
            RefPtr protectedThis = weakThis.get();
            if (!protectedThis)
                return afterConsent(false);
#if HAVE(WEB_AUTHN_AS_MODERN)
            afterConsent(consented ? *consented : protectedThis->removeMatchingAutofillEventForUsername(username, origin));
#else
            afterConsent(consented && *consented);
#endif
        });
        return;
    }

    afterConsent(true);
}

void WebAuthenticatorCoordinatorProxy::cancelPendingAuthenticatorManagerRequest()
{
    if (!m_pendingAuthenticatorManagerRequestID)
        return;
    m_pendingAuthenticatorManagerRequestID = 0;
    RefPtr webPageProxy = m_webPageProxy.get();
    if (!webPageProxy)
        return;
    // Ends the request with NotAllowedError and dismisses the embedder's panel.
    Ref authenticatorManager = webPageProxy->websiteDataStore().authenticatorManager();
    authenticatorManager->cancel();
}

#if !HAVE(UNIFIED_ASC_AUTH_UI) && !HAVE(WEB_AUTHN_AS_MODERN)
void WebAuthenticatorCoordinatorProxy::cancel(CompletionHandler<void()>&& completionHandler)
{
    cancelPendingAuthenticatorManagerRequest();
    completionHandler();
}

void WebAuthenticatorCoordinatorProxy::isUserVerifyingPlatformAuthenticatorAvailable(const SecurityOriginData&, QueryCompletionHandler&& handler)
{
    handler(LocalService::isAvailable());
}

void WebAuthenticatorCoordinatorProxy::isConditionalMediationAvailable(const SecurityOriginData&, QueryCompletionHandler&& handler)
{
    handler(false);
}
#endif // !HAVE(UNIFIED_ASC_AUTH_UI) && !HAVE(WEB_AUTHN_AS_MODERN)

#if HAVE(WEB_AUTHN_AS_MODERN)

void WebAuthenticatorCoordinatorProxy::removeExpiredAutofillEvents()
{
    m_recentAutofills.removeAllMatching([] (const AutofillEvent& event) {
        constexpr Seconds autofillEventTimeout { 5 * 60 };
        auto now = MonotonicTime::now();
        return event.time + autofillEventTimeout < now;
    });
}

void WebAuthenticatorCoordinatorProxy::recordAutofill(const String& username, const URL& url)
{
    removeExpiredAutofillEvents();
    m_recentAutofills.append({
        MonotonicTime::now(),
        username,
        url,
    });
}

bool WebAuthenticatorCoordinatorProxy::removeMatchingAutofillEventForUsername(const String& username, const WebCore::SecurityOriginData& securityOrigin)
{
    removeExpiredAutofillEvents();
    bool value = m_recentAutofills.removeLastMatching([&] (const AutofillEvent& event) {
        return event.username == username && RegistrableDomain { event.url } == RegistrableDomain { securityOrigin };
    });
    return value;
}
#endif // HAVE(WEB_AUTHN_AS_MODERN)

} // namespace WebKit

#endif // ENABLE(WEB_AUTHN)
