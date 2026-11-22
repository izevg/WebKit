/*
 * Copyright (C) 2026 izevg. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"

#if ENABLE(REMOTE_INSPECTOR)

#import "Helpers/PlatformUtilities.h"
#import "Helpers/Test.h"
#import "Helpers/cocoa/TestWKWebView.h"
#import <WebKit/WKProcessPoolPrivate.h>
#import <WebKit/WKWebViewConfigurationPrivate.h>
#import <WebKit/_WKAutomationSession.h>
#import <WebKit/_WKAutomationSessionConfiguration.h>
#import <wtf/RetainPtr.h>

// Exercises the in-process automation client: no RemoteInspector transport,
// the test itself plays the driver through -connectLocalClient: and
// -dispatchMessage:.

namespace TestWebKitAPI {

static NSString *localAutomationSerialize(NSDictionary *object)
{
    NSData *data = [NSJSONSerialization dataWithJSONObject:object options:0 error:nil];
    return adoptNS([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]).autorelease();
}

static NSDictionary *localAutomationParse(NSString *json)
{
    if (!json)
        return nil;
    id object = [NSJSONSerialization JSONObjectWithData:[json dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    return [object isKindOfClass:NSDictionary.class] ? object : nil;
}

// Every message the session sent, oldest first.
class LocalAutomationClient {
public:
    LocalAutomationClient()
        : m_messages(adoptNS([[NSMutableArray alloc] init]))
    {
    }

    void (^handler())(NSString *) {
        NSMutableArray *messages = m_messages.get();
        return [[^(NSString *message) {
            [messages addObject:message];
        } copy] autorelease];
    }

    // The reply to the Automation command with the given id.
    NSDictionary *reply(NSInteger commandID) const
    {
        for (NSString *raw in m_messages.get()) {
            NSDictionary *message = localAutomationParse(raw);
            if ([message[@"id"] isEqual:@(commandID)])
                return message;
        }
        return nil;
    }

    // Inspector messages for a page target, unwrapped from
    // Automation.receiveInspectorMessage and Target.dispatchMessageFromTarget.
    NSArray<NSDictionary *> *targetMessages(NSString *targetID) const
    {
        NSMutableArray *result = [NSMutableArray array];
        for (NSDictionary *inspectorMessage in inspectorMessages()) {
            if (![inspectorMessage[@"method"] isEqual:@"Target.dispatchMessageFromTarget"])
                continue;
            NSDictionary *params = inspectorMessage[@"params"];
            if (![params[@"targetId"] isEqual:targetID])
                continue;
            if (NSDictionary *message = localAutomationParse(params[@"message"]))
                [result addObject:message];
        }
        return result;
    }

    NSString *pageTargetID() const
    {
        for (NSDictionary *inspectorMessage in inspectorMessages()) {
            if (![inspectorMessage[@"method"] isEqual:@"Target.targetCreated"])
                continue;
            NSDictionary *targetInfo = inspectorMessage[@"params"][@"targetInfo"];
            if ([targetInfo[@"type"] isEqual:@"page"])
                return targetInfo[@"targetId"];
        }
        return nil;
    }

private:
    NSArray<NSDictionary *> *inspectorMessages() const
    {
        NSMutableArray *result = [NSMutableArray array];
        for (NSString *raw in m_messages.get()) {
            NSDictionary *message = localAutomationParse(raw);
            if (![message[@"method"] isEqual:@"Automation.receiveInspectorMessage"])
                continue;
            if (NSDictionary *inspectorMessage = localAutomationParse(message[@"params"][@"message"]))
                [result addObject:inspectorMessage];
        }
        return result;
    }

    RetainPtr<NSMutableArray> m_messages;
};

static RetainPtr<TestWKWebView> createLocalAutomationWebView()
{
    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [configuration _setControlledByAutomation:YES];
    return adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 800, 600) configuration:configuration.get()]);
}

static RetainPtr<_WKAutomationSession> createLocalAutomationSession(BOOL inspectorPassthroughEnabled)
{
    RetainPtr configuration = adoptNS([[_WKAutomationSessionConfiguration alloc] init]);
    [configuration setInspectorPassthroughEnabled:inspectorPassthroughEnabled];
    RetainPtr session = adoptNS([[_WKAutomationSession alloc] initWithConfiguration:configuration.get()]);
    [session setSessionIdentifier:@"local-automation-test"];
    return session;
}

static RetainPtr<NSString> localAutomationFirstBrowsingContextHandle(_WKAutomationSession *session, LocalAutomationClient& client, NSInteger commandID)
{
    EXPECT_TRUE([session dispatchMessage:localAutomationSerialize(@{ @"id": @(commandID), @"method": @"Automation.getBrowsingContexts", @"params": @{ } })]);
    EXPECT_TRUE(Util::waitFor([&] {
        return !!client.reply(commandID);
    }));

    NSDictionary *reply = client.reply(commandID);
    EXPECT_NULL(reply[@"error"]);
    NSArray *contexts = reply[@"result"][@"contexts"];
    EXPECT_EQ(contexts.count, 1u);
    return contexts.firstObject[@"handle"];
}

TEST(WKAutomationSessionLocal, GetBrowsingContexts)
{
    RetainPtr webView = createLocalAutomationWebView();
    [webView synchronouslyLoadHTMLString:@"<body>agent</body>" baseURL:[NSURL URLWithString:@"http://webkit.org/"]];

    RetainPtr session = createLocalAutomationSession(NO);
    LocalAutomationClient client;

    // Commands are refused until the session has a client and a process pool.
    EXPECT_FALSE([session dispatchMessage:@"{\"id\":1,\"method\":\"Automation.getBrowsingContexts\"}"]);

    EXPECT_TRUE([session connectLocalClient:client.handler()]);
    EXPECT_FALSE([session connectLocalClient:client.handler()]);
    EXPECT_FALSE([session dispatchMessage:@"{\"id\":1,\"method\":\"Automation.getBrowsingContexts\"}"]);

    RetainPtr processPool = [webView configuration].processPool;
    [processPool _setAutomationSession:session.get()];
    EXPECT_TRUE([session isPaired]);

    RetainPtr handle = localAutomationFirstBrowsingContextHandle(session.get(), client, 2);
    EXPECT_TRUE([handle hasPrefix:@"page-"]);

    // Without the configuration flag (and no delegate), the inspector passthrough stays closed.
    NSString *inner = localAutomationSerialize(@{ @"id": @1, @"method": @"Target.setPauseOnStart", @"params": @{ @"pauseOnStart": @NO } });
    EXPECT_TRUE([session dispatchMessage:localAutomationSerialize(@{ @"id": @3, @"method": @"Automation.sendInspectorMessage", @"params": @{ @"browsingContextHandle": handle.get(), @"message": inner } })]);
    EXPECT_TRUE(Util::waitFor([&] {
        return !!client.reply(3);
    }));
    EXPECT_NOT_NULL(client.reply(3)[@"error"]);

    [session disconnectLocalClient];
    EXPECT_FALSE([session isPaired]);
    EXPECT_FALSE([session dispatchMessage:@"{\"id\":4,\"method\":\"Automation.getBrowsingContexts\"}"]);

    [processPool _setAutomationSession:nil];
}

TEST(WKAutomationSessionLocal, ConnectAfterSettingOnProcessPoolFails)
{
    RetainPtr webView = createLocalAutomationWebView();
    RetainPtr session = createLocalAutomationSession(NO);
    LocalAutomationClient client;

    // Once on a pool the session is a remote-inspector target; a local client must come first.
    RetainPtr processPool = [webView configuration].processPool;
    [processPool _setAutomationSession:session.get()];
    EXPECT_FALSE([session connectLocalClient:client.handler()]);

    [processPool _setAutomationSession:nil];
}

TEST(WKAutomationSessionLocal, HoldAndFulfillRequestThroughInspectorPassthrough)
{
    RetainPtr webView = createLocalAutomationWebView();
    [webView synchronouslyLoadHTMLString:@"<body>agent</body>" baseURL:[NSURL URLWithString:@"http://webkit.org/"]];

    RetainPtr session = createLocalAutomationSession(YES);
    LocalAutomationClient client;
    EXPECT_TRUE([session connectLocalClient:client.handler()]);

    RetainPtr processPool = [webView configuration].processPool;
    [processPool _setAutomationSession:session.get()];

    RetainPtr handle = localAutomationFirstBrowsingContextHandle(session.get(), client, 1);
    ASSERT_NOT_NULL(handle.get());

    NSInteger nextCommandID = 2;
    auto sendInspectorMessage = [&](NSDictionary *message) {
        NSInteger commandID = nextCommandID++;
        EXPECT_TRUE([session dispatchMessage:localAutomationSerialize(@{
            @"id": @(commandID),
            @"method": @"Automation.sendInspectorMessage",
            @"params": @{ @"browsingContextHandle": handle.get(), @"message": localAutomationSerialize(message) },
        })]);
        EXPECT_TRUE(Util::waitFor([&] {
            return !!client.reply(commandID);
        }));
        EXPECT_NULL(client.reply(commandID)[@"error"]);
    };

    // The first passthrough message connects the page's inspector controller,
    // which announces its page target.
    sendInspectorMessage(@{ @"id": @1, @"method": @"Target.setPauseOnStart", @"params": @{ @"pauseOnStart": @NO } });
    EXPECT_TRUE(Util::waitFor([&] {
        return !!client.pageTargetID();
    }));
    RetainPtr<NSString> targetID = client.pageTargetID();
    ASSERT_NOT_NULL(targetID.get());

    auto targetReply = [&](NSInteger innerID) -> NSDictionary * {
        for (NSDictionary *message in client.targetMessages(targetID.get())) {
            if ([message[@"id"] isEqual:@(innerID)])
                return message;
        }
        return nil;
    };
    auto sendToTarget = [&](NSInteger innerID, NSString *method, NSDictionary *params) {
        sendInspectorMessage(@{
            @"id": @(100 + innerID),
            @"method": @"Target.sendMessageToTarget",
            @"params": @{ @"targetId": targetID.get(), @"message": localAutomationSerialize(@{ @"id": @(innerID), @"method": method, @"params": params }) },
        });
        EXPECT_TRUE(Util::waitFor([&] {
            return !!targetReply(innerID);
        }));
        EXPECT_NULL(targetReply(innerID)[@"error"]);
    };

    sendToTarget(1, @"Network.enable", @{ });
    sendToTarget(2, @"Network.setInterceptionEnabled", @{ @"enabled": @YES });
    sendToTarget(3, @"Network.addInterception", @{ @"url": @"http://webkit.org/quickie-approve", @"stage": @"request" });

    // The page posts; the request is held until the client decides.
    [webView evaluateJavaScript:@"window.result = 'pending'; fetch('/quickie-approve', { method: 'POST', body: 'pay=1' }).then(r => r.text()).then(t => window.result = t, e => window.result = 'error: ' + e); 0" completionHandler:nil];

    RetainPtr<NSDictionary> intercepted;
    EXPECT_TRUE(Util::waitFor([&] {
        for (NSDictionary *message in client.targetMessages(targetID.get())) {
            if ([message[@"method"] isEqual:@"Network.requestIntercepted"]) {
                intercepted = message[@"params"];
                return true;
            }
        }
        return false;
    }));
    ASSERT_NOT_NULL(intercepted.get());
    EXPECT_WK_STREQ(@"POST", intercepted.get()[@"request"][@"method"]);
    EXPECT_WK_STREQ(@"http://webkit.org/quickie-approve", intercepted.get()[@"request"][@"url"]);
    EXPECT_WK_STREQ(@"pay=1", intercepted.get()[@"request"][@"postData"]);
    EXPECT_WK_STREQ(@"pending", [webView objectByEvaluatingJavaScript:@"window.result"]);

    // Approve by fulfilling the held request; nothing reaches the network.
    sendToTarget(4, @"Network.interceptRequestWithResponse", @{
        @"requestId": intercepted.get()[@"requestId"],
        @"content": @"approved",
        @"base64Encoded": @NO,
        @"mimeType": @"text/plain",
        @"status": @200,
        @"statusText": @"OK",
        @"headers": @{ },
    });
    EXPECT_TRUE(Util::waitFor([&] {
        return [[webView objectByEvaluatingJavaScript:@"window.result"] isEqual:@"approved"];
    }));

    [session disconnectLocalClient];
    [processPool _setAutomationSession:nil];
}

} // namespace TestWebKitAPI

#endif // ENABLE(REMOTE_INSPECTOR)
