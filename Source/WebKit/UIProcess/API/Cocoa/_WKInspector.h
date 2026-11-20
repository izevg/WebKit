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

#import <Foundation/Foundation.h>
#import <WebKit/WKFoundation.h>
#import <WebKit/_WKInspectorExtensionHost.h>
#import <WebKit/_WKInspectorIBActions.h>

NS_ASSUME_NONNULL_BEGIN

@class WKWebView;
@class _WKFrameHandle;
@class _WKInspectorExtension;
@protocol _WKInspectorDelegate;

typedef NS_ENUM(NSInteger, _WKInspectorAttachmentSide) {
    _WKInspectorAttachmentSideBottom,
    _WKInspectorAttachmentSideRight,
    _WKInspectorAttachmentSideLeft,
} WK_API_AVAILABLE(macos(WK_MAC_TBA), ios(WK_IOS_TBA), visionos(WK_XROS_TBA));

WK_CLASS_AVAILABLE(macos(10.14.4), ios(12.2))
@interface _WKInspector : NSObject <_WKInspectorExtensionHost, _WKInspectorIBActions>

- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, weak) id <_WKInspectorDelegate> delegate WK_API_AVAILABLE(macos(12.0), ios(15.0));

@property (nonatomic, readonly) WKWebView *webView;
@property (nonatomic, readonly) BOOL isConnected;
@property (nonatomic, readonly) BOOL isVisible;
@property (nonatomic, readonly) BOOL isFront;
@property (nonatomic, readonly) BOOL isProfilingPage;
@property (nonatomic, readonly) BOOL isElementSelectionActive;

- (void)connect;
- (void)hide;
- (void)showMainResourceForFrame:(_WKFrameHandle *)frame;
- (void)attach;
- (void)detach;
- (void)togglePageProfiling;
- (void)toggleElementSelection;
- (void)printErrorToConsole:(NSString *)error;

/**
 * @abstract Shows the associated Web Inspector instance and selects a tab.
 * @param tabIdentifier One of "elements", "network", "sources", "timeline",
 * "storage", "graphics", "layers", "audit", or "console". Unknown identifiers
 * and tabs that are not available for the inspected page are ignored.
 */
- (void)showTab:(NSString *)tabIdentifier WK_API_AVAILABLE(macos(WK_MAC_TBA), ios(WK_IOS_TBA), visionos(WK_XROS_TBA));

/**
 * @abstract Attaches the associated Web Inspector instance to the given side
 * of the inspected view. Moves it if it is already attached to another side.
 */
- (void)attachToSide:(_WKInspectorAttachmentSide)side WK_API_AVAILABLE(macos(WK_MAC_TBA), ios(WK_IOS_TBA), visionos(WK_XROS_TBA));

@end

NS_ASSUME_NONNULL_END
