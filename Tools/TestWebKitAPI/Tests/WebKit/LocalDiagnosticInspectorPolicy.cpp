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

#include "config.h"
#include "LocalDiagnosticInspectorPolicy.h"

#include "Helpers/Test.h"
#include <string>
#include <utility>
#include <vector>

namespace TestWebKitAPI {

using WebKit::LocalDiagnosticCallbackQueue;
using WebKit::isAllowedLocalDiagnosticDirectMethod;
using WebKit::isAllowedLocalDiagnosticEnvelopeKey;
using WebKit::isAllowedLocalDiagnosticTargetMethod;
using WebKit::isAllowedLocalDiagnosticTargetSendParamsKey;

TEST(LocalDiagnosticInspectorPolicy, DirectAllowlistRequiresProxyingAgents)
{
    // Arrange / Act / Assert
    EXPECT_TRUE(isAllowedLocalDiagnosticDirectMethod("Network.enable", true, false));
    EXPECT_TRUE(isAllowedLocalDiagnosticDirectMethod("Network.disable", true, false));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Network.enable", false, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Network.disable", false, true));

    EXPECT_TRUE(isAllowedLocalDiagnosticDirectMethod("Page.enable", false, true));
    EXPECT_TRUE(isAllowedLocalDiagnosticDirectMethod("Page.disable", false, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Page.enable", true, false));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Page.disable", true, false));

    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Console.enable", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Runtime.enable", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Runtime.evaluate", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Page.navigate", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Page.reload", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("DOM.setOuterHTML", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Network.interceptWithRequest", true, true));
    EXPECT_FALSE(isAllowedLocalDiagnosticDirectMethod("Target.sendMessageToTarget", true, true));
}

TEST(LocalDiagnosticInspectorPolicy, TargetAllowlistOnlyPermitsReadOnlySubscriptions)
{
    // Arrange / Act / Assert
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Console.enable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Console.disable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Runtime.enable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Runtime.disable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Network.enable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Network.disable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Page.enable"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetMethod("Page.disable"));

    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Runtime.evaluate"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Runtime.callFunctionOn"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Page.navigate"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Page.reload"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("DOM.setOuterHTML"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Network.interceptContinue"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Console.clearMessages"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod("Target.pause"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetMethod(""));
}

TEST(LocalDiagnosticInspectorPolicy, EnvelopeAndTargetSendParamsKeysAreStrict)
{
    // Arrange / Act / Assert
    EXPECT_TRUE(isAllowedLocalDiagnosticEnvelopeKey("id"));
    EXPECT_TRUE(isAllowedLocalDiagnosticEnvelopeKey("method"));
    EXPECT_TRUE(isAllowedLocalDiagnosticEnvelopeKey("params"));
    EXPECT_FALSE(isAllowedLocalDiagnosticEnvelopeKey("sessionId"));
    EXPECT_FALSE(isAllowedLocalDiagnosticEnvelopeKey("extra"));
    EXPECT_FALSE(isAllowedLocalDiagnosticEnvelopeKey(""));

    EXPECT_TRUE(isAllowedLocalDiagnosticTargetSendParamsKey("targetId"));
    EXPECT_TRUE(isAllowedLocalDiagnosticTargetSendParamsKey("message"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetSendParamsKey("id"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetSendParamsKey("extra"));
    EXPECT_FALSE(isAllowedLocalDiagnosticTargetSendParamsKey(""));
}

TEST(LocalDiagnosticInspectorPolicy, QueueDrainsInBoundedBatchesAndYieldsMainLoop)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 16, 3 };
    for (int i = 0; i < 10; ++i) {
        bool shouldSchedule = queue.enqueue("m" + std::to_string(i));
        EXPECT_EQ(shouldSchedule, !i);
    }

    std::vector<std::string> delivered;
    std::vector<size_t> batchSizes;
    size_t overflowCalls = 0;

    // Act
    bool more = true;
    while (more) {
        size_t before = delivered.size();
        more = queue.drain(
            [&](const std::string& message) {
                delivered.push_back(message);
            },
            [&](size_t, size_t) {
                ++overflowCalls;
            });
        batchSizes.push_back(delivered.size() - before);
    }

    // Assert
    EXPECT_EQ(overflowCalls, 0u);
    EXPECT_EQ(queue.droppedCount(), 0u);
    EXPECT_EQ(queue.pendingCount(), 0u);
    EXPECT_EQ(delivered.size(), 10u);
    EXPECT_EQ(batchSizes.size(), 4u);
    EXPECT_EQ(batchSizes[0], 3u);
    EXPECT_EQ(batchSizes[1], 3u);
    EXPECT_EQ(batchSizes[2], 3u);
    EXPECT_EQ(batchSizes[3], 1u);
    EXPECT_EQ(delivered.front(), std::string("m0"));
    EXPECT_EQ(delivered.back(), std::string("m9"));
}

TEST(LocalDiagnosticInspectorPolicy, QueueOverflowIsObservableAndTracksCounts)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 4, 2 };
    for (int i = 0; i < 7; ++i)
        queue.enqueue("m" + std::to_string(i));

    EXPECT_EQ(queue.pendingCount(), 4u);
    EXPECT_EQ(queue.droppedCount(), 3u);
    EXPECT_EQ(queue.pendingDroppedCount(), 3u);

    std::vector<std::string> delivered;
    std::vector<std::pair<size_t, size_t>> overflows;

    // Act: first batch reports the 3 dropped messages and leaves 2 queued.
    bool moreAfterFirst = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
        },
        [&](size_t droppedSinceLastNotice, size_t totalDropped) {
            overflows.emplace_back(droppedSinceLastNotice, totalDropped);
        });

    // Assert
    EXPECT_TRUE(moreAfterFirst);
    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_EQ(delivered[0], std::string("m0"));
    EXPECT_EQ(delivered[1], std::string("m1"));
    ASSERT_EQ(overflows.size(), 1u);
    EXPECT_EQ(overflows[0].first, 3u);
    EXPECT_EQ(overflows[0].second, 3u);
    EXPECT_EQ(queue.pendingDroppedCount(), 0u);

    // Act: second batch drains remaining messages without duplicate overflow notice.
    bool moreAfterSecond = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
        },
        [&](size_t droppedSinceLastNotice, size_t totalDropped) {
            overflows.emplace_back(droppedSinceLastNotice, totalDropped);
        });

    // Assert
    EXPECT_FALSE(moreAfterSecond);
    EXPECT_EQ(delivered.size(), 4u);
    EXPECT_EQ(delivered[2], std::string("m2"));
    EXPECT_EQ(delivered[3], std::string("m3"));
    EXPECT_EQ(overflows.size(), 1u);

    // Act: subsequent overflow reports incremental delta and cumulative total.
    for (int i = 0; i < 6; ++i)
        queue.enqueue("n" + std::to_string(i));
    bool moreAfterThird = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
        },
        [&](size_t droppedSinceLastNotice, size_t totalDropped) {
            overflows.emplace_back(droppedSinceLastNotice, totalDropped);
        });

    // Assert
    EXPECT_TRUE(moreAfterThird);
    EXPECT_EQ(overflows.size(), 2u);
    EXPECT_EQ(overflows[1].first, 2u);
    EXPECT_EQ(overflows[1].second, 5u);
}

TEST(LocalDiagnosticInspectorPolicy, QueueOverflowWithZeroCapacityStillSchedulesNotice)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 0, 4 };
    bool scheduled = queue.enqueue("dropped");
    std::vector<std::string> delivered;
    std::vector<std::pair<size_t, size_t>> overflows;

    // Act
    bool more = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
        },
        [&](size_t droppedSinceLastNotice, size_t totalDropped) {
            overflows.emplace_back(droppedSinceLastNotice, totalDropped);
        });

    // Assert
    EXPECT_TRUE(scheduled);
    EXPECT_FALSE(more);
    EXPECT_EQ(delivered.size(), 0u);
    EXPECT_EQ(overflows.size(), 1u);
    EXPECT_EQ(overflows[0].first, 1u);
    EXPECT_EQ(overflows[0].second, 1u);
}

TEST(LocalDiagnosticInspectorPolicy, DisconnectCancelsQueuedMessagesAndOverflowImmediately)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 4, 2 };
    for (int i = 0; i < 6; ++i)
        queue.enqueue("m" + std::to_string(i));

    // Act
    queue.disconnect();
    bool scheduledAfterDisconnect = queue.enqueue("late");
    size_t deliveredCount = 0;
    size_t overflowCalls = 0;
    bool more = queue.drain(
        [&](const std::string&) {
            ++deliveredCount;
        },
        [&](size_t, size_t) {
            ++overflowCalls;
        });

    // Assert
    EXPECT_FALSE(queue.isConnected());
    EXPECT_FALSE(scheduledAfterDisconnect);
    EXPECT_FALSE(more);
    EXPECT_EQ(queue.pendingCount(), 0u);
    EXPECT_EQ(queue.pendingDroppedCount(), 0u);
    EXPECT_EQ(queue.droppedCount(), 2u);
    EXPECT_EQ(deliveredCount, 0u);
    EXPECT_EQ(overflowCalls, 0u);
}

TEST(LocalDiagnosticInspectorPolicy, ReentrantDisconnectMidBatchStopsRemainingCallbacks)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 4, 4 };
    for (int i = 0; i < 6; ++i)
        queue.enqueue("m" + std::to_string(i));

    std::vector<std::string> delivered;
    size_t overflowCalls = 0;

    // Act
    bool more = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
            if (delivered.size() == 2)
                queue.disconnect();
        },
        [&](size_t, size_t) {
            ++overflowCalls;
        });

    // Assert
    EXPECT_FALSE(more);
    EXPECT_FALSE(queue.isConnected());
    EXPECT_EQ(delivered.size(), 2u);
    EXPECT_EQ(delivered[0], std::string("m0"));
    EXPECT_EQ(delivered[1], std::string("m1"));
    EXPECT_EQ(overflowCalls, 0u);
    EXPECT_EQ(queue.pendingCount(), 0u);
}

TEST(LocalDiagnosticInspectorPolicy, ReentrantEnqueueDuringDrainDoesNotDoubleSchedule)
{
    // Arrange
    LocalDiagnosticCallbackQueue<std::string> queue { 8, 2 };
    EXPECT_TRUE(queue.enqueue("m0"));
    EXPECT_FALSE(queue.enqueue("m1"));

    std::vector<std::string> delivered;
    bool scheduledDuringDrain = true;

    // Act
    bool moreAfterFirst = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
            if (message == "m0")
                scheduledDuringDrain = queue.enqueue("m2");
        },
        [&](size_t, size_t) { });

    bool moreAfterSecond = queue.drain(
        [&](const std::string& message) {
            delivered.push_back(message);
        },
        [&](size_t, size_t) { });

    // Assert
    EXPECT_FALSE(scheduledDuringDrain);
    EXPECT_TRUE(moreAfterFirst);
    EXPECT_FALSE(moreAfterSecond);
    EXPECT_EQ(delivered.size(), 3u);
    EXPECT_EQ(delivered[0], std::string("m0"));
    EXPECT_EQ(delivered[1], std::string("m1"));
    EXPECT_EQ(delivered[2], std::string("m2"));
}

} // namespace TestWebKitAPI
