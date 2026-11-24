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
#include <gio/gio.h>
#include <wtf/FileSystem.h>
#include <wtf/JSONValues.h>
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

static void loadPage(WebKitWebView* view, GMainLoop* loop)
{
    auto handler = g_signal_connect(view, "load-changed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent event, GMainLoop* loop) {
        if (event == WEBKIT_LOAD_FINISHED)
            g_main_loop_quit(loop);
    }), loop);
    webkit_web_view_load_html(view, "<!doctype html><p>Local automation test</p>", nullptr);
    runLoop(loop);
    g_signal_handler_disconnect(view, handler);
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
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.createBrowsingContext"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.sendInspectorMessage"})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"page","url":"file:///tmp/local"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"page","url":"https://user:password@example.invalid/"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"url":"https://example.invalid/"}})"));
    g_assert_false(webkit_automation_session_send_message(session, R"({"id":1,"method":"Automation.navigateBrowsingContext","params":{"handle":"","url":"https://example.invalid/"}})"));

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
    g_test_add_func("/webkit/WebKitAutomationSession/local-rejects-inspector-environment", testLocalFactoryRejectsInspectorEnvironment);
}

void afterAll()
{
}
