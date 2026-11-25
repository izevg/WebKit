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

#pragma once

#include <cstddef>
#include <deque>
#include <string_view>
#include <utility>
#include <wtf/StdLibExtras.h>

namespace WebKit {

inline constexpr size_t maxLocalDiagnosticMessageLength = 16 * 1024;
inline constexpr size_t maxLocalDiagnosticInnerMessageLength = 4 * 1024;
inline constexpr size_t maxLocalDiagnosticTargetIDLength = 256;
inline constexpr size_t maxLocalDiagnosticPendingCallbacks = 512;
inline constexpr size_t maxLocalDiagnosticDeliveryBatchSize = 32;
inline constexpr std::string_view localDiagnosticQueueOverflowMethod = "LocalDiagnostic.queueOverflow";

constexpr bool isAllowedLocalDiagnosticDirectMethod(std::string_view method, bool hasProxyingNetworkAgent, bool hasProxyingPageAgent)
{
    if (method == "Network.enable" || method == "Network.disable")
        return hasProxyingNetworkAgent;
    if (method == "Page.enable" || method == "Page.disable")
        return hasProxyingPageAgent;
    return false;
}

constexpr bool isAllowedLocalDiagnosticTargetMethod(std::string_view method)
{
    return method == "Console.enable"
        || method == "Console.disable"
        || method == "Runtime.enable"
        || method == "Runtime.disable"
        || method == "Network.enable"
        || method == "Network.disable"
        || method == "Page.enable"
        || method == "Page.disable";
}

constexpr bool isAllowedLocalDiagnosticEnvelopeKey(std::string_view key)
{
    return key == "id" || key == "method" || key == "params";
}

constexpr bool isAllowedLocalDiagnosticTargetSendParamsKey(std::string_view key)
{
    return key == "targetId" || key == "message";
}

template<typename Message>
class LocalDiagnosticCallbackQueue {
public:
    explicit LocalDiagnosticCallbackQueue(size_t maxPendingCallbacks = maxLocalDiagnosticPendingCallbacks, size_t maxDeliveryBatchSize = maxLocalDiagnosticDeliveryBatchSize)
        : m_maxPendingCallbacks(maxPendingCallbacks)
        , m_maxDeliveryBatchSize(maxDeliveryBatchSize ? maxDeliveryBatchSize : 1)
    {
    }

    bool isConnected() const { return m_isConnected; }
    size_t pendingCount() const { return m_pendingMessages.size(); }
    size_t droppedCount() const { return m_totalDroppedCount; }
    size_t pendingDroppedCount() const { return m_pendingDroppedCount; }
    size_t maxPendingCallbacks() const { return m_maxPendingCallbacks; }
    size_t maxDeliveryBatchSize() const { return m_maxDeliveryBatchSize; }

    // Returns true when the caller should schedule an asynchronous drain on the main run loop.
    bool enqueue(Message&& message)
    {
        if (!m_isConnected)
            return false;

        if (m_pendingMessages.size() >= m_maxPendingCallbacks) {
            ++m_totalDroppedCount;
            ++m_pendingDroppedCount;
        } else
            m_pendingMessages.push_back(WTF::move(message));

        if (m_deliveryScheduled || m_isDraining)
            return false;

        m_deliveryScheduled = true;
        return true;
    }

    // Drains at most m_maxDeliveryBatchSize messages per turn so the native main loop can
    // service input, rendering, and disconnect calls between batches. Reports any overflow
    // via overflowHandler(droppedSinceLastNotice, totalDropped). Returns true when more
    // work remains and the caller must reschedule another asynchronous drain.
    template<typename Handler, typename OverflowHandler>
    bool drain(Handler&& handler, OverflowHandler&& overflowHandler)
    {
        m_deliveryScheduled = false;
        if (!m_isConnected) {
            m_pendingMessages.clear();
            m_pendingDroppedCount = 0;
            return false;
        }

        m_isDraining = true;
        size_t delivered = 0;
        while (m_isConnected && delivered < m_maxDeliveryBatchSize && !m_pendingMessages.empty()) {
            auto message = WTF::move(m_pendingMessages.front());
            m_pendingMessages.pop_front();
            handler(message);
            ++delivered;
        }

        if (m_isConnected && m_pendingDroppedCount) {
            size_t droppedSinceLastNotice = std::exchange(m_pendingDroppedCount, 0);
            overflowHandler(droppedSinceLastNotice, m_totalDroppedCount);
        }
        m_isDraining = false;

        if (!m_isConnected) {
            m_pendingMessages.clear();
            m_pendingDroppedCount = 0;
            return false;
        }

        if (!m_pendingMessages.empty() || m_pendingDroppedCount) {
            m_deliveryScheduled = true;
            return true;
        }

        return false;
    }

    void disconnect()
    {
        m_isConnected = false;
        m_deliveryScheduled = false;
        m_pendingMessages.clear();
        m_pendingDroppedCount = 0;
    }

private:
    size_t m_maxPendingCallbacks;
    size_t m_maxDeliveryBatchSize;
    std::deque<Message> m_pendingMessages;
    size_t m_totalDroppedCount { 0 };
    size_t m_pendingDroppedCount { 0 };
    bool m_isConnected { true };
    bool m_deliveryScheduled { false };
    bool m_isDraining { false };
};

} // namespace WebKit
