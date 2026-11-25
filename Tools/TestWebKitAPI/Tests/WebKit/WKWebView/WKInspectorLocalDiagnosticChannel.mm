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

#import "Helpers/Test.h"
#import "Helpers/Utilities.h"
#import "Helpers/cocoa/TestWKWebView.h"
#import <WebKit/WKPreferencesPrivate.h>
#import <WebKit/WKWebViewPrivate.h>
#import <WebKit/_WKInspector.h>
#import <wtf/RetainPtr.h>

#if PLATFORM(MAC)

static NSDictionary *parseJSONDictionary(NSString *message)
{
    NSData *data = [message dataUsingEncoding:NSUTF8StringEncoding];
    if (!data)
        return nil;

    id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return [object isKindOfClass:[NSDictionary class]] ? object : nil;
}

static NSString *serializeJSONDictionary(NSDictionary *dictionary)
{
    NSData *data = [NSJSONSerialization dataWithJSONObject:dictionary options:0 error:nil];
    return data ? adoptNS([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]).autorelease() : nil;
}

static NSString *targetSendMessageJSON(NSInteger outerID, NSString *targetID, NSInteger innerID, NSString *innerMethod)
{
    NSString *innerJSON = serializeJSONDictionary(@{
        @"id": @(innerID),
        @"method": innerMethod,
    });
    return serializeJSONDictionary(@{
        @"id": @(outerID),
        @"method": @"Target.sendMessageToTarget",
        @"params": @{
            @"targetId": targetID,
            @"message": innerJSON,
        },
    });
}

TEST(WKInspectorLocalDiagnosticChannel, ConnectsSubscribesAndDisconnects)
{
    // Arrange
    RetainPtr webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    RetainPtr webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300) configuration:webViewConfiguration.get()]);
    [webView synchronouslyLoadHTMLString:@"<html><body>Diagnostic test</body></html>"];

    __block RetainPtr<NSString> targetID;
    __block bool receivedTargetCreated = false;
    __block bool receivedConsoleEnableReply = false;
    __block bool receivedMessageAfterDisconnect = false;
    __block bool disconnected = false;

    _WKInspector *inspector = [webView _inspector];

    // Act
    BOOL connected = [inspector connectLocalDiagnosticClient:^(NSString *message) {
        if (disconnected) {
            receivedMessageAfterDisconnect = true;
            return;
        }
        NSDictionary *json = parseJSONDictionary(message);
        if ([json[@"method"] isEqualToString:@"Target.targetCreated"]) {
            NSDictionary *targetInfo = json[@"params"][@"targetInfo"];
            if ([targetInfo[@"targetId"] isKindOfClass:[NSString class]]) {
                targetID = targetInfo[@"targetId"];
                receivedTargetCreated = true;
            }
        } else if ([json[@"id"] integerValue] == 1)
            receivedConsoleEnableReply = true;
    }];

    // Assert: single diagnostic frontend enforced
    EXPECT_TRUE(connected);
    EXPECT_FALSE([inspector connectLocalDiagnosticClient:^(NSString *ignored) {
        UNUSED_PARAM(ignored);
    }]);

    TestWebKitAPI::Util::run(&receivedTargetCreated);
    EXPECT_NOT_NULL(targetID.get());

    // Act: allowlisted subscriptions through Target.sendMessageToTarget
    EXPECT_TRUE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(1, targetID.get(), 10, @"Console.enable")]);
    EXPECT_TRUE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(2, targetID.get(), 11, @"Runtime.enable")]);
    EXPECT_TRUE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(3, targetID.get(), 12, @"Network.enable")]);
    EXPECT_TRUE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(4, targetID.get(), 13, @"Page.enable")]);
    EXPECT_TRUE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(5, targetID.get(), 14, @"Console.disable")]);

    TestWebKitAPI::Util::run(&receivedConsoleEnableReply);

    // Act: disconnect cancels queued callbacks and blocks further dispatch
    disconnected = true;
    [inspector disconnectLocalDiagnosticClient];
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(6, targetID.get(), 15, @"Console.enable")]);
    TestWebKitAPI::Util::spinRunLoop(10);
    EXPECT_FALSE(receivedMessageAfterDisconnect);
}

TEST(WKInspectorLocalDiagnosticChannel, RejectsDisallowedAndMalformedMessages)
{
    // Arrange
    RetainPtr webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    RetainPtr webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300) configuration:webViewConfiguration.get()]);
    [webView synchronouslyLoadHTMLString:@"<html><body>Allowlist test</body></html>"];

    __block RetainPtr<NSString> targetID;
    __block bool receivedTargetCreated = false;
    _WKInspector *inspector = [webView _inspector];

    EXPECT_TRUE([inspector connectLocalDiagnosticClient:^(NSString *message) {
        NSDictionary *json = parseJSONDictionary(message);
        if ([json[@"method"] isEqualToString:@"Target.targetCreated"]) {
            targetID = json[@"params"][@"targetInfo"][@"targetId"];
            receivedTargetCreated = true;
        }
    }]);
    TestWebKitAPI::Util::run(&receivedTargetCreated);

    // Act / Assert
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:@""]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:@"{not json"]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:@"{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"1+1\"}}"]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:@"{\"id\":2,\"method\":\"Page.navigate\",\"params\":{\"url\":\"https://example.com\"}}"]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(3, targetID.get(), 20, @"Runtime.evaluate")]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(4, targetID.get(), 21, @"Page.reload")]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(5, targetID.get(), 22, @"DOM.setOuterHTML")]);
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(6, @"unknown-target-id", 23, @"Console.enable")]);

    [inspector disconnectLocalDiagnosticClient];
}

TEST(WKInspectorLocalDiagnosticChannel, VisualInspectorTakesPriorityOverDiagnosticClient)
{
    // Arrange
    RetainPtr webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().preferences._developerExtrasEnabled = YES;
    RetainPtr webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300) configuration:webViewConfiguration.get()]);
    [webView synchronouslyLoadHTMLString:@"<html><body>Priority test</body></html>"];

    __block RetainPtr<NSString> targetID;
    __block bool receivedTargetCreated = false;
    _WKInspector *inspector = [webView _inspector];

    EXPECT_TRUE([inspector connectLocalDiagnosticClient:^(NSString *message) {
        NSDictionary *json = parseJSONDictionary(message);
        if ([json[@"method"] isEqualToString:@"Target.targetCreated"]) {
            targetID = json[@"params"][@"targetInfo"][@"targetId"];
            receivedTargetCreated = true;
        }
    }]);
    TestWebKitAPI::Util::run(&receivedTargetCreated);

    // Act: opening visual Inspector preempts the local diagnostic client
    [inspector show];
    TestWebKitAPI::Util::spinRunLoop(10);

    // Assert
    EXPECT_FALSE([inspector dispatchDiagnosticMessage:targetSendMessageJSON(1, targetID.get(), 30, @"Console.enable")]);
    EXPECT_FALSE([inspector connectLocalDiagnosticClient:^(NSString *ignored) {
        UNUSED_PARAM(ignored);
    }]);

    // Act: closing visual Inspector allows diagnostic client to connect again
    [inspector close];
    TestWebKitAPI::Util::spinRunLoop(10);
    EXPECT_TRUE([inspector connectLocalDiagnosticClient:^(NSString *ignored) {
        UNUSED_PARAM(ignored);
    }]);
    [inspector disconnectLocalDiagnosticClient];
}

#endif
