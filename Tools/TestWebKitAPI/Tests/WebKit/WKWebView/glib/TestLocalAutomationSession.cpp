/*
 * Copyright (C) 2026 Quickie contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "TestMain.h"
#include <cstdlib>
#include <gio/gio.h>
#include <wtf/FileSystem.h>
#include <wtf/Function.h>
#include <wtf/JSONValues.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/MakeString.h>

static constexpr const char* getContexts = R"({"id":1,"method":"Automation.getBrowsingContexts"})";
static constexpr const char* inspectorEnvironmentChild = "WEBKIT_TEST_LOCAL_AUTOMATION_INSPECTOR_ENVIRONMENT";

static void runLoop(GMainLoop* loop)
{
    auto timeout = g_timeout_add_seconds(10, [](gpointer) -> gboolean {
        g_error("Timed out waiting for local automation");
        return G_SOURCE_REMOVE;
    }, nullptr);
    g_main_loop_run(loop);
    g_source_remove(timeout);
}

static void loadHTML(WebKitWebView* view, GMainLoop* loop, const char* html)
{
    auto handler = g_signal_connect(view, "load-changed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent event, GMainLoop* loop) {
        if (event == WEBKIT_LOAD_FINISHED)
            g_main_loop_quit(loop);
    }), loop);
    webkit_web_view_load_html(view, html, nullptr);
    runLoop(loop);
    g_signal_handler_disconnect(view, handler);
}

static void loadPage(WebKitWebView* view, GMainLoop* loop)
{
    loadHTML(view, loop, "<!doctype html><p>Local automation test</p>");
}

static void assertRemoteAutomationStaysDisabled(WebKitWebContext* context)
{
    g_test_expect_message(nullptr, G_LOG_LEVEL_WARNING, "*dedicated to local automation");
    webkit_web_context_set_automation_allowed(context, TRUE);
    g_test_assert_expected_messages();
    g_assert_false(webkit_web_context_is_automation_allowed(context));
}

static RefPtr<JSON::Object> parseMessage(const char* message)
{
    auto value = JSON::Value::parseJSON(String::fromUTF8(message));
    g_assert_true(!!value);
    auto response = value->asObject();
    g_assert_true(!!response);
    return response;
}

static UTF8CString navigationCommand(unsigned id, const String& handle, unsigned timeout)
{
    return makeString("{\"id\":"_s, id, ",\"method\":\"Automation.navigateBrowsingContext\",\"params\":{\"handle\":\""_s, handle, "\",\"url\":\"https://example.invalid/approved\",\"pageLoadTimeout\":"_s, timeout, "}}"_s).utf8();
}

static UTF8CString buildProtocolCommand(unsigned id, ASCIILiteral method, RefPtr<JSON::Object>&& params = nullptr)
{
    auto request = JSON::Object::create();
    request->setInteger("id"_s, static_cast<int>(id));
    request->setString("method"_s, method);
    if (params)
        request->setObject("params"_s, params.releaseNonNull());
    return request->toJSONString().utf8();
}

static UTF8CString buildInspectorCommand(unsigned id, const String& handle, const String& innerMessage)
{
    auto params = JSON::Object::create();
    params->setString("browsingContextHandle"_s, handle);
    params->setString("message"_s, innerMessage);
    return buildProtocolCommand(id, "Automation.sendInspectorMessage"_s, WTF::move(params));
}

static UTF8CString buildNestedTargetCommand(unsigned outerID, const String& handle, unsigned targetSendID, const String& targetID, const String& nestedMessage)
{
    auto targetParams = JSON::Object::create();
    targetParams->setString("targetId"_s, targetID);
    targetParams->setString("message"_s, nestedMessage);
    auto targetCommand = JSON::Object::create();
    targetCommand->setInteger("id"_s, static_cast<int>(targetSendID));
    targetCommand->setString("method"_s, "Target.sendMessageToTarget"_s);
    targetCommand->setObject("params"_s, WTF::move(targetParams));
    return buildInspectorCommand(outerID, handle, targetCommand->toJSONString());
}

struct LocalMessageRecorder {
    explicit LocalMessageRecorder(WebKitAutomationSession* session)
        : session(session)
        , loop(adoptGRef(g_main_loop_new(nullptr, FALSE)))
    {
        handlerID = g_signal_connect(session, "message-received", G_CALLBACK(+[](WebKitAutomationSession*, const char* message, LocalMessageRecorder* recorder) {
            recorder->messages.append(parseMessage(message).releaseNonNull());
            g_main_loop_quit(recorder->loop.get());
        }), this);
    }

    ~LocalMessageRecorder()
    {
        if (handlerID)
            g_signal_handler_disconnect(session, handlerID);
    }

    void waitUntil(NOESCAPE const Function<bool()>& condition)
    {
        static constexpr gint64 timeoutMicroseconds = 10 * G_USEC_PER_SEC;
        gint64 deadline = g_get_monotonic_time() + timeoutMicroseconds;
        auto tickID = g_timeout_add(20, [](gpointer data) -> gboolean {
            g_main_loop_quit(static_cast<GMainLoop*>(data));
            return G_SOURCE_CONTINUE;
        }, loop.get());
        while (!condition()) {
            g_assert_cmpint(g_get_monotonic_time(), <, deadline);
            g_main_loop_run(loop.get());
        }
        g_source_remove(tickID);
    }

    RefPtr<JSON::Object> replyForID(unsigned id) const
    {
        for (const auto& message : messages) {
            if (auto messageID = message->getInteger("id"_s); messageID && *messageID == static_cast<int>(id))
                return message.ptr();
        }
        return nullptr;
    }

    Ref<JSON::Object> sendAndWaitForReply(unsigned id, ASCIILiteral method, RefPtr<JSON::Object>&& params = nullptr)
    {
        auto encoded = buildProtocolCommand(id, method, WTF::move(params));
        g_assert_true(webkit_automation_session_send_message(session, encoded.legacyCStringPointer()));
        RefPtr<JSON::Object> reply;
        waitUntil([&] {
            reply = replyForID(id);
            return !!reply;
        });
        return reply.releaseNonNull();
    }

    Vector<Ref<JSON::Object>> inspectorMessagesForHandle(const String& handle) const
    {
        Vector<Ref<JSON::Object>> result;
        for (const auto& message : messages) {
            if (message->getString("method"_s) != "Automation.receiveInspectorMessage"_s)
                continue;
            auto params = message->getObject("params"_s);
            if (!params || params->getString("browsingContextHandle"_s) != handle)
                continue;
            auto parsed = JSON::Value::parseJSON(params->getString("message"_s));
            if (auto object = parsed ? parsed->asObject() : nullptr)
                result.append(object.releaseNonNull());
        }
        return result;
    }

    String pageTargetID(const String& handle) const
    {
        for (const auto& inspectorMessage : inspectorMessagesForHandle(handle)) {
            if (inspectorMessage->getString("method"_s) != "Target.targetCreated"_s)
                continue;
            auto params = inspectorMessage->getObject("params"_s);
            auto targetInfo = params ? params->getObject("targetInfo"_s) : nullptr;
            if (targetInfo && targetInfo->getString("type"_s) == "page"_s) {
                auto targetID = targetInfo->getString("targetId"_s);
                if (!targetID.isEmpty())
                    return targetID;
            }
        }
        return { };
    }

    Vector<Ref<JSON::Object>> targetMessages(const String& handle, const String& targetID) const
    {
        Vector<Ref<JSON::Object>> result;
        for (const auto& inspectorMessage : inspectorMessagesForHandle(handle)) {
            if (inspectorMessage->getString("method"_s) != "Target.dispatchMessageFromTarget"_s)
                continue;
            auto params = inspectorMessage->getObject("params"_s);
            if (!params || params->getString("targetId"_s) != targetID)
                continue;
            auto parsed = JSON::Value::parseJSON(params->getString("message"_s));
            if (auto object = parsed ? parsed->asObject() : nullptr)
                result.append(object.releaseNonNull());
        }
        return result;
    }

    WebKitAutomationSession* session;
    GRefPtr<GMainLoop> loop;
    unsigned long handlerID { 0 };
    Vector<Ref<JSON::Object>> messages;
};

static void testLocalBrowsingContexts(Test* test, gconstpointer)
{
    GRefPtr<WebKitWebContext> context = adoptGRef(webkit_web_context_new_with_local_automation("local-scope"));
    g_assert_nonnull(context.get());
    assertRemoteAutomationStaysDisabled(context.get());
    g_assert_null(webkit_web_context_get_local_automation_session(test->m_webContext.get()));
    auto* session = webkit_web_context_get_local_automation_session(context.get());
    g_assert_nonnull(session);
    g_assert_cmpstr(webkit_automation_session_get_id(session), ==, "local-scope");
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(session));

    auto controlledView = test->createWebView("web-context", context.get(), "is-controlled-by-automation", TRUE, nullptr);
    auto uncontrolledView = test->createWebView("web-context", context.get(), nullptr);
    auto* networkSession = webkit_web_view_get_network_session(controlledView.get());
    g_assert_true(webkit_network_session_is_ephemeral(networkSession));
    g_assert_true(networkSession == webkit_web_view_get_network_session(uncontrolledView.get()));
    g_assert_true(networkSession != test->m_networkSession.get());

    GRefPtr<GMainLoop> loop = adoptGRef(g_main_loop_new(nullptr, FALSE));
    loadPage(uncontrolledView.get(), loop.get());
    loadPage(controlledView.get(), loop.get());

    struct {
        GMainLoop* loop;
        String handle;
    } reply { loop.get(), { } };
    auto handler = g_signal_connect(session, "message-received", G_CALLBACK(+[](WebKitAutomationSession*, const char* message, decltype(reply)* reply) {
        auto response = parseMessage(message);
        auto id = response->getInteger("id"_s);
        if (!id)
            return;
        g_assert_cmpint(*id, ==, 1);
        auto result = response->getObject("result"_s);
        g_assert_true(!!result);
        auto contexts = result->getArray("contexts"_s);
        g_assert_true(!!contexts);
        g_assert_cmpuint(contexts->length(), ==, 1);
        auto context = contexts->get(0)->asObject();
        g_assert_true(!!context);
        reply->handle = context->getString("handle"_s);
        g_assert_false(reply->handle.isEmpty());
        g_main_loop_quit(reply->loop);
    }), &reply);
    g_assert_true(webkit_automation_session_send_message(session, getContexts));
    g_assert_true(reply.handle.isEmpty());
    runLoop(loop.get());
    g_signal_handler_disconnect(session, handler);

    struct {
        GMainLoop* loop;
        GRefPtr<WebKitPolicyDecision> decision;
        bool quitOnDecision { false };
    } policy { loop.get(), nullptr };
    auto policyHandler = g_signal_connect(controlledView.get(), "decide-policy", G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, decltype(policy)* policy) -> gboolean {
        g_assert_cmpuint(type, ==, WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION);
        auto* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        g_assert_cmpstr(webkit_uri_request_get_uri(webkit_navigation_action_get_request(action)), ==, "https://example.invalid/approved");
        policy->decision = decision;
        if (policy->quitOnDecision)
            g_main_loop_quit(policy->loop);
        return TRUE;
    }), &policy);

    auto timeoutHandler = g_signal_connect(session, "message-received", G_CALLBACK(+[](WebKitAutomationSession*, const char* message, GMainLoop* loop) {
        auto response = parseMessage(message);
        auto id = response->getInteger("id"_s);
        if (!id)
            return;
        g_assert_cmpint(*id, ==, 2);
        auto error = response->getObject("error"_s);
        g_assert_true(!!error);
        g_assert_true(error->getString("message"_s) == "Timeout"_s);
        g_main_loop_quit(loop);
    }), loop.get());
    g_assert_true(webkit_automation_session_send_message(session, navigationCommand(2, reply.handle, 1000).legacyCStringPointer()));
    runLoop(loop.get());
    g_signal_handler_disconnect(session, timeoutHandler);
    g_assert_nonnull(policy.decision.get());
    webkit_policy_decision_ignore(policy.decision.get());
    policy.decision = nullptr;

    policy.quitOnDecision = true;
    g_assert_true(webkit_automation_session_send_message(session, navigationCommand(3, reply.handle, 500).legacyCStringPointer()));
    runLoop(loop.get());
    g_signal_handler_disconnect(controlledView.get(), policyHandler);

    // Keep the detached session alive beyond its navigation deadline. Its timer
    // can no longer consult the process pool, and the held request never goes out.
    GRefPtr<WebKitAutomationSession> detachedSession = session;
    webkit_web_context_close_local_automation_session(context.get());
    g_assert_false(webkit_automation_session_send_message(detachedSession.get(), getContexts));
    g_timeout_add(600, [](gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(data));
        return G_SOURCE_REMOVE;
    }, loop.get());
    runLoop(loop.get());
    webkit_policy_decision_ignore(policy.decision.get());
}

static void testLocalCloseDropsQueuedReplies(Test* test, gconstpointer)
{
    g_assert_null(webkit_web_context_new_with_local_automation(""));
    g_assert_null(webkit_web_context_new_with_local_automation("\xFF"));

    GRefPtr<WebKitWebContext> context = adoptGRef(webkit_web_context_new_with_local_automation("local-close"));
    auto* session = webkit_web_context_get_local_automation_session(context.get());
    g_assert_nonnull(session);
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(session));
    g_assert_false(webkit_automation_session_send_message(session, "{"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"method":"Automation.getBrowsingContexts"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.evaluateJavaScriptFunction","params":{"browsingContextHandle":"page","function":"function(){return 1;}","arguments":[]}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.processBidiMessage","params":{"message":"{}"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.emitActiveBidiScriptRealmCreatedEvents"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.getAllCookies","params":{"browsingContextHandle":"page"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.addSingleCookie","params":{"browsingContextHandle":"page","cookie":{}}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.deleteAllCookies","params":{"browsingContextHandle":"page"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.setStorageAccessPolicy","params":{"browsingContextHandle":"page","blocked":false}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.deleteSession"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.getSessionPermissions"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.setSessionPermissions","params":{"permissions":[]}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.performApplicationCommand","params":{"browsingContextHandle":"page","commandName":"x","arguments":"{}"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.setFilesToSelectForFileUpload","params":{"browsingContextHandle":"page","filenames":["/etc/passwd"]}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.sendInspectorMessage"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.sendInspectorMessage","params":{"browsingContextHandle":"page","message":"{"}})"));
    g_assert_false(webkit_automation_session_send_message(session, buildInspectorCommand(1, "page"_s, R"({"id":1,"method":"Runtime.evaluate","params":{"expression":"1+1"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildInspectorCommand(1, "page"_s, R"({"id":1,"method":"Network.setInterceptionEnabled","params":{"enabled":true}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildInspectorCommand(1, "page"_s, R"({"id":1,"method":"Page.getCookies"})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildInspectorCommand(1, "page"_s, R"({"id":1,"method":"DOM.setOuterHTML","params":{"nodeId":1,"outerHTML":"<p/>"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, "target-1"_s, R"({"id":3,"method":"Runtime.evaluate","params":{"expression":"1+1"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, "target-1"_s, R"({"id":3,"method":"Network.addInterception","params":{"url":"http://example.invalid/"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, "target-1"_s, R"({"id":3,"method":"Page.setBootstrapScript","params":{"source":"1"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, "target-1"_s, R"({"id":3,"method":"DOM.resolveNode","params":{"nodeId":1}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, "target-1"_s, R"({"id":3,"method":"Target.sendMessageToTarget","params":{"targetId":"target-2","message":"{\"id\":4,\"method\":\"Console.enable\"}"}})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, buildNestedTargetCommand(1, "page"_s, 2, ""_s, R"({"id":3,"method":"Console.enable"})"_s).legacyCStringPointer()));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"page","url":"file:///tmp/local"}})"));
    g_assert_false(webkit_automation_session_send_message(session, "{\"id\":1,\"method\":\"Automation.navigateBrowsingContext\",\"params\":{\"handle\":\"page\",\"url\":\"javascript:alert(1)\"}}"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"page","url":"https://user:password@example.invalid/"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"url":"https://example.invalid/"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"","url":"https://example.invalid/"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.takeScreenshot","params":{"handle":""}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.computeElementLayout","params":{"browsingContextHandle":"page","frameHandle":"","coordinateSystem":"Viewport"}})"));

    GRefPtr<GMainLoop> loop = adoptGRef(g_main_loop_new(nullptr, FALSE));
    struct {
        WebKitWebContext* context;
        GMainLoop* loop;
        unsigned count { 0 };
        unsigned closeCount { 0 };
    } state { context.get(), loop.get() };
    g_signal_connect(session, "will-close", G_CALLBACK(+[](WebKitAutomationSession* session, decltype(state)* state) {
        g_assert_cmpuint(++state->closeCount, ==, 1);
        g_assert_null(webkit_web_context_get_local_automation_session(state->context));
        g_assert_false(webkit_automation_session_send_message(session, getContexts));
    }), &state);
    g_signal_connect(session, "message-received", G_CALLBACK(+[](WebKitAutomationSession* session, const char*, decltype(state)* state) {
        g_assert_cmpuint(++state->count, ==, 1);
        webkit_web_context_close_local_automation_session(state->context);
        g_assert_null(webkit_web_context_get_local_automation_session(state->context));
        g_assert_false(webkit_automation_session_send_message(session, getContexts));
        g_idle_add([](gpointer data) -> gboolean {
            g_main_loop_quit(static_cast<GMainLoop*>(data));
            return G_SOURCE_REMOVE;
        }, state->loop);
    }), &state);
    g_assert_true(webkit_automation_session_send_message(session, getContexts));
    g_assert_true(webkit_automation_session_send_message(session, R"({"id":2,"method":"Automation.getBrowsingContexts"})"));
    g_assert_cmpuint(state.count, ==, 0);
    runLoop(loop.get());
    g_assert_cmpuint(state.count, ==, 1);
    g_assert_cmpuint(state.closeCount, ==, 1);
    webkit_web_context_close_local_automation_session(context.get());
    assertRemoteAutomationStaysDisabled(context.get());
    auto view = test->createWebView("web-context", context.get(), "is-controlled-by-automation", TRUE, nullptr);
    g_assert_true(webkit_network_session_is_ephemeral(webkit_web_view_get_network_session(view.get())));
}

static void testLocalTrustedClickScreenshotAndInspectorEvents(Test* test, gconstpointer)
{
    GRefPtr<WebKitWebContext> context = adoptGRef(webkit_web_context_new_with_local_automation("local-actions"));
    g_assert_nonnull(context.get());
    auto* session = webkit_web_context_get_local_automation_session(context.get());
    g_assert_nonnull(session);
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(session));

    auto controlledView = test->createWebView("web-context", context.get(), "is-controlled-by-automation", TRUE, nullptr);
    g_assert_false(gtk_widget_get_realized(GTK_WIDGET(controlledView.get())));
    g_assert_false(gtk_widget_get_mapped(GTK_WIDGET(controlledView.get())));
    gtk_widget_allocate(GTK_WIDGET(controlledView.get()), 640, 480, -1, nullptr);

    GRefPtr<GMainLoop> loop = adoptGRef(g_main_loop_new(nullptr, FALSE));
    loadHTML(controlledView.get(), loop.get(),
        "<!doctype html>"
        "<style>html,body{margin:0;padding:0;width:640px;height:480px;background:rgb(16,128,224);overflow:hidden;}</style>"
        "<button id='btn' style='position:fixed;left:0;top:0;width:640px;height:480px;border:0;margin:0;padding:0;background:rgb(16,128,224);color:rgb(16,128,224);'>Click</button>"
        "<script>"
        "document.title = 'initial';"
        "document.getElementById('btn').addEventListener('click', (e) => {"
        "  document.title = e.isTrusted ? 'trusted-click' : 'untrusted-click';"
        "  console.warn('local-automation-clicked:' + e.isTrusted);"
        "});"
        "</script>");

    LocalMessageRecorder recorder(session);
    auto contextsReply = recorder.sendAndWaitForReply(1, "Automation.getBrowsingContexts"_s);
    auto contextsResult = contextsReply->getObject("result"_s);
    g_assert_true(!!contextsResult);
    auto contexts = contextsResult->getArray("contexts"_s);
    g_assert_true(!!contexts);
    g_assert_cmpuint(contexts->length(), ==, 1);
    auto contextInfo = contexts->get(0)->asObject();
    g_assert_true(!!contextInfo);
    String handle = contextInfo->getString("handle"_s);
    g_assert_false(handle.isEmpty());

    // Local screenshot in a hidden, unmapped, unrealized view allocated at 640x480.
    auto screenshotParams = JSON::Object::create();
    screenshotParams->setString("handle"_s, handle);
    screenshotParams->setBoolean("clipToViewport"_s, true);
    auto screenshotReply = recorder.sendAndWaitForReply(2, "Automation.takeScreenshot"_s, WTF::move(screenshotParams));
    g_assert_false(!!screenshotReply->getObject("error"_s));
    auto screenshotResult = screenshotReply->getObject("result"_s);
    g_assert_true(!!screenshotResult);
    String encodedPNG = screenshotResult->getString("data"_s);
    g_assert_false(encodedPNG.isEmpty());

    gsize decodedLength = 0;
    GUniquePtr<guchar> decodedData(g_base64_decode(encodedPNG.utf8().legacyCStringPointer(), &decodedLength));
    g_assert_nonnull(decodedData.get());
    g_assert_cmpuint(decodedLength, >, 0);
    GRefPtr<GBytes> pngBytes = adoptGRef(g_bytes_new_take(decodedData.release(), decodedLength));
    GUniqueOutPtr<GError> textureError;
    GRefPtr<GdkTexture> texture = adoptGRef(gdk_texture_new_from_bytes(pngBytes.get(), &textureError.outPtr()));
    g_assert_no_error(textureError.get());
    g_assert_nonnull(texture.get());
    g_assert_cmpint(gdk_texture_get_width(texture.get()), ==, 640);
    g_assert_cmpint(gdk_texture_get_height(texture.get()), ==, 480);

    constexpr int stride = 640 * 4;
    Vector<uint8_t> pixels(stride * 480);
    gdk_texture_download(texture.get(), pixels.mutableSpan().data(), stride);
    uint32_t centerPixel = 0;
    auto centerBytes = pixels.span().subspan(240 * stride + 320 * 4, sizeof(centerPixel));
    memcpySpan(asMutableByteSpan(centerPixel), centerBytes);
    int alpha = static_cast<int>((centerPixel >> 24) & 0xFF);
    int red = static_cast<int>((centerPixel >> 16) & 0xFF);
    int green = static_cast<int>((centerPixel >> 8) & 0xFF);
    int blue = static_cast<int>(centerPixel & 0xFF);
    g_assert_cmpint(alpha, ==, 255);
    g_assert_cmpint(std::abs(red - 16), <=, 2);
    g_assert_cmpint(std::abs(green - 128), <=, 2);
    g_assert_cmpint(std::abs(blue - 224), <=, 2);

    // Enable Inspector passthrough observation via Target.setPauseOnStart + nested Console.enable & DOM.getDocument.
    auto pauseParams = JSON::Object::create();
    pauseParams->setBoolean("pauseOnStart"_s, false);
    auto pauseCommand = JSON::Object::create();
    pauseCommand->setInteger("id"_s, 1);
    pauseCommand->setString("method"_s, "Target.setPauseOnStart"_s);
    pauseCommand->setObject("params"_s, WTF::move(pauseParams));
    auto inspectorInitParams = JSON::Object::create();
    inspectorInitParams->setString("browsingContextHandle"_s, handle);
    inspectorInitParams->setString("message"_s, pauseCommand->toJSONString());
    auto inspectorInitReply = recorder.sendAndWaitForReply(3, "Automation.sendInspectorMessage"_s, WTF::move(inspectorInitParams));
    g_assert_false(!!inspectorInitReply->getObject("error"_s));

    String targetID;
    recorder.waitUntil([&] {
        targetID = recorder.pageTargetID(handle);
        return !targetID.isEmpty();
    });

    auto sendToTargetAndWait = [&](unsigned outerID, unsigned targetSendID, unsigned innerID, ASCIILiteral method) -> Ref<JSON::Object> {
        auto inner = JSON::Object::create();
        inner->setInteger("id"_s, static_cast<int>(innerID));
        inner->setString("method"_s, method);
        auto wrapped = buildNestedTargetCommand(outerID, handle, targetSendID, targetID, inner->toJSONString());
        g_assert_true(webkit_automation_session_send_message(session, wrapped.legacyCStringPointer()));
        RefPtr<JSON::Object> outerReply;
        recorder.waitUntil([&] {
            outerReply = recorder.replyForID(outerID);
            return !!outerReply;
        });
        g_assert_false(!!outerReply->getObject("error"_s));

        RefPtr<JSON::Object> targetReply;
        recorder.waitUntil([&] {
            for (const auto& message : recorder.targetMessages(handle, targetID)) {
                if (auto id = message->getInteger("id"_s); id && *id == static_cast<int>(innerID)) {
                    targetReply = message.ptr();
                    return true;
                }
            }
            return false;
        });
        g_assert_false(!!targetReply->getObject("error"_s));
        return targetReply.releaseNonNull();
    };

    sendToTargetAndWait(4, 101, 1, "Console.enable"_s);
    auto documentReply = sendToTargetAndWait(5, 102, 2, "DOM.getDocument"_s);
    auto documentResult = documentReply->getObject("result"_s);
    g_assert_true(!!documentResult && !!documentResult->getObject("root"_s));

    // Trusted mouse click event and resulting Console.messageAdded event.
    auto clickPosition = JSON::Object::create();
    clickPosition->setDouble("x"_s, 100.0);
    clickPosition->setDouble("y"_s, 100.0);
    auto clickParams = JSON::Object::create();
    clickParams->setString("handle"_s, handle);
    clickParams->setObject("position"_s, WTF::move(clickPosition));
    clickParams->setString("button"_s, "Left"_s);
    clickParams->setString("interaction"_s, "SingleClick"_s);
    clickParams->setArray("modifiers"_s, JSON::Array::create());
    auto clickReply = recorder.sendAndWaitForReply(6, "Automation.performMouseInteraction"_s, WTF::move(clickParams));
    g_assert_false(!!clickReply->getObject("error"_s));

    recorder.waitUntil([&] {
        return !g_strcmp0(webkit_web_view_get_title(controlledView.get()), "trusted-click");
    });

    recorder.waitUntil([&] {
        for (const auto& message : recorder.targetMessages(handle, targetID)) {
            if (message->getString("method"_s) != "Console.messageAdded"_s)
                continue;
            auto params = message->getObject("params"_s);
            auto consoleMessage = params ? params->getObject("message"_s) : nullptr;
            if (consoleMessage && consoleMessage->getString("text"_s).contains("local-automation-clicked:true"_s))
                return true;
        }
        return false;
    });

    auto cancelParams = JSON::Object::create();
    cancelParams->setString("handle"_s, handle);
    auto cancelReply = recorder.sendAndWaitForReply(7, "Automation.cancelInteractionSequence"_s, WTF::move(cancelParams));
    g_assert_false(!!cancelReply->getObject("error"_s));

    // Wrong-context rejection: another local automation context's handle is rejected with WindowNotFound.
    GRefPtr<WebKitWebContext> otherContext = adoptGRef(webkit_web_context_new_with_local_automation("local-other"));
    g_assert_nonnull(otherContext.get());
    auto* otherSession = webkit_web_context_get_local_automation_session(otherContext.get());
    g_assert_nonnull(otherSession);
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(otherSession));

    auto otherView = test->createWebView("web-context", otherContext.get(), "is-controlled-by-automation", TRUE, nullptr);
    loadPage(otherView.get(), loop.get());
    LocalMessageRecorder otherRecorder(otherSession);
    auto otherContextsReply = otherRecorder.sendAndWaitForReply(1, "Automation.getBrowsingContexts"_s);
    auto otherContexts = otherContextsReply->getObject("result"_s)->getArray("contexts"_s);
    g_assert_cmpuint(otherContexts->length(), ==, 1);
    String otherHandle = otherContexts->get(0)->asObject()->getString("handle"_s);
    g_assert_false(otherHandle.isEmpty());
    g_assert_true(otherHandle != handle);

    auto wrongClickPosition = JSON::Object::create();
    wrongClickPosition->setDouble("x"_s, 10.0);
    wrongClickPosition->setDouble("y"_s, 10.0);
    auto wrongClickParams = JSON::Object::create();
    wrongClickParams->setString("handle"_s, otherHandle);
    wrongClickParams->setObject("position"_s, WTF::move(wrongClickPosition));
    wrongClickParams->setString("button"_s, "Left"_s);
    wrongClickParams->setString("interaction"_s, "SingleClick"_s);
    wrongClickParams->setArray("modifiers"_s, JSON::Array::create());
    auto wrongClickReply = recorder.sendAndWaitForReply(8, "Automation.performMouseInteraction"_s, WTF::move(wrongClickParams));
    auto wrongClickError = wrongClickReply->getObject("error"_s);
    g_assert_true(!!wrongClickError && wrongClickError->getString("message"_s).startsWith("WindowNotFound"_s));

    auto wrongScreenshotParams = JSON::Object::create();
    wrongScreenshotParams->setString("handle"_s, otherHandle);
    auto wrongScreenshotReply = recorder.sendAndWaitForReply(9, "Automation.takeScreenshot"_s, WTF::move(wrongScreenshotParams));
    auto wrongScreenshotError = wrongScreenshotReply->getObject("error"_s);
    g_assert_true(!!wrongScreenshotError && wrongScreenshotError->getString("message"_s).startsWith("WindowNotFound"_s));

    auto wrongInspectorParams = JSON::Object::create();
    wrongInspectorParams->setString("browsingContextHandle"_s, otherHandle);
    wrongInspectorParams->setString("message"_s, R"({"id":1,"method":"Target.setPauseOnStart","params":{"pauseOnStart":false}})"_s);
    auto wrongInspectorReply = recorder.sendAndWaitForReply(10, "Automation.sendInspectorMessage"_s, WTF::move(wrongInspectorParams));
    auto wrongInspectorError = wrongInspectorReply->getObject("error"_s);
    g_assert_true(!!wrongInspectorError && wrongInspectorError->getString("message"_s).startsWith("WindowNotFound"_s));

    // createBrowsingContext rejects both a foreign controlled view and a normal-profile view.
    auto createForeignHandler = g_signal_connect(session, "create-web-view", G_CALLBACK(+[](WebKitAutomationSession*, gpointer userData) -> WebKitWebView* {
        return WEBKIT_WEB_VIEW(userData);
    }), otherView.get());
    auto createForeignReply = recorder.sendAndWaitForReply(11, "Automation.createBrowsingContext"_s);
    g_signal_handler_disconnect(session, createForeignHandler);
    auto createForeignError = createForeignReply->getObject("error"_s);
    g_assert_true(!!createForeignError && createForeignError->getString("message"_s).startsWith("InternalError"_s));

    auto normalProfileView = test->createWebView("web-context", test->m_webContext.get(), nullptr);
    auto createNormalHandler = g_signal_connect(session, "create-web-view", G_CALLBACK(+[](WebKitAutomationSession*, gpointer userData) -> WebKitWebView* {
        return WEBKIT_WEB_VIEW(userData);
    }), normalProfileView.get());
    auto createNormalReply = recorder.sendAndWaitForReply(12, "Automation.createBrowsingContext"_s);
    g_signal_handler_disconnect(session, createNormalHandler);
    auto createNormalError = createNormalReply->getObject("error"_s);
    g_assert_true(!!createNormalError && createNormalError->getString("message"_s).startsWith("InternalError"_s));

    webkit_web_context_close_local_automation_session(otherContext.get());
    webkit_web_context_close_local_automation_session(context.get());
}

static void testLocalFactoryRejectsInspectorEnvironment()
{
    auto executable = FileSystem::currentExecutablePath();
    for (auto* variable : { "WEBKIT_INSPECTOR_SERVER", "WEBKIT_INSPECTOR_HTTP_SERVER" }) {
        GRefPtr<GSubprocessLauncher> launcher = adoptGRef(g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE));
        g_subprocess_launcher_setenv(launcher.get(), inspectorEnvironmentChild, variable, TRUE);
        // Initializing WebKit with this value would emit a fatal test warning.
        g_subprocess_launcher_setenv(launcher.get(), variable, "invalid-address", TRUE);
        GUniqueOutPtr<GError> error;
        GRefPtr<GSubprocess> child = adoptGRef(g_subprocess_launcher_spawn(launcher.get(), &error.outPtr(), executable.legacyCStringPointer(), nullptr));
        g_assert_no_error(error.get());
        g_assert_nonnull(child.get());
        g_assert_true(g_subprocess_wait_check(child.get(), nullptr, &error.outPtr()));
        g_assert_no_error(error.get());
    }
}

void beforeAll()
{
    if (const auto* variable = g_getenv(inspectorEnvironmentChild)) {
        g_assert_nonnull(g_getenv(variable));
        g_test_add_func("/webkit/WebKitAutomationSession/local-inspector-environment-child", [] {
            g_assert_null(webkit_web_context_new_with_local_automation("local-environment"));
        });
        return;
    }

    g_unsetenv("WEBKIT_INSPECTOR_SERVER");
    g_unsetenv("WEBKIT_INSPECTOR_HTTP_SERVER");
    Test::add("WebKitAutomationSession", "local-browsing-contexts", testLocalBrowsingContexts);
    Test::add("WebKitAutomationSession", "local-close-drops-queued-replies", testLocalCloseDropsQueuedReplies);
    Test::add("WebKitAutomationSession", "local-trusted-click-screenshot-and-inspector-events", testLocalTrustedClickScreenshotAndInspectorEvents);
    g_test_add_func("/webkit/WebKitAutomationSession/local-rejects-inspector-environment", testLocalFactoryRejectsInspectorEnvironment);
}

void afterAll()
{
}
