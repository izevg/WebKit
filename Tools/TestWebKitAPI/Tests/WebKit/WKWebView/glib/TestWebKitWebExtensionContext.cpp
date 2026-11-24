/*
 * Copyright (C) 2026 Igalia, S.L. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(WK_WEB_EXTENSIONS)

#include "TestMain.h"
#if PLATFORM(GTK)
#include "WebViewTest.h"
#endif
#include <WebKitWebExtensionInternal.h>
#include <wtf/HashMap.h>
#include <wtf/glib/GLibExtras.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

static GRefPtr<GBytes> createGBytes(const gchar* string)
{
    return gBytesNewStatic(unsafeSpan(string));
}

static GRefPtr<GBytes> createGBytes(const UTF8CString& string)
{
    return gBytesNew(string.span());
}

static void testContentScriptsParsing(Test* test, gconstpointer)
{
    GUniqueOutPtr<GError> error;
    auto parseExtensionManifest = [&](const gchar* contentScripts) {
        auto manifestString = makeString("{ \"manifest_version\": 2, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\", \"content_scripts\": "_s, String::fromUTF8(contentScripts), " }"_s);
        GRefPtr extension = adoptGRef(webkitWebExtensionCreate({ { "manifest.json"_s, createGBytes(manifestString.utf8()) } }, &error.outPtr()));
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(extension.get()));
        return extension;
    };

    GRefPtr<WebKitWebExtension> extension = parseExtensionManifest("[{ \"js\": [ \"test.js\", 1, \"\" ], \"css\": [ false, \"test.css\", \"\" ], \"matches\": [ \"*://*/\" ] }]");
    g_assert_no_error(error.get());
    GRefPtr<WebKitWebExtensionContext> context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"js\": [ \"test.js\", 1, \"\" ], \"css\": [ false, \"test.css\", \"\" ], \"matches\": [ \"*://*/\" ], \"exclude_matches\": [ \"*://*.example.com/\" ] }]");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"js\": [ \"test.js\", 1, \"\" ], \"css\": [ false, \"test.css\", \"\" ], \"matches\": [ \"*://*.example.com/\" ] }]");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"css\": [ false, \"test.css\", \"\" ], \"css_origin\": \"user\", \"matches\": [ \"*://*.example.com/\" ] }]");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"css\": [ false, \"test.css\", \"\" ], \"css_origin\": \"author\", \"matches\": [ \"*://*.example.com/\" ] }]");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    // Invalid cases

    extension = parseExtensionManifest("[]");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_false(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("{ \"invalid\": true }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_false(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"js\": [ \"test.js\" ], \"matches\": [] }]");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_false(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"js\": [ \"test.js\" ], \"matches\": [ \"*://*.example.com/\" ], \"world\": \"INVALID\" }]");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));

    extension = parseExtensionManifest("[{ \"css\": [ false, \"test.css\", \"\" ], \"css_origin\": \"bad\", \"matches\": [ \"*://*.example.com/\" ] }]");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_true(webkit_web_extension_context_get_has_injected_content(context.get()));
    g_assert_false(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://webkit.org/"));
    g_assert_true(webkit_web_extension_context_has_injected_content_for_uri(context.get(), "https://example.com/"));
}

static void testOptionsPageURIParsing(Test* test, gconstpointer)
{
    GUniqueOutPtr<GError> error;
    auto parseExtensionManifest = [&](const gchar* manifestString) {
        GRefPtr extension = adoptGRef(webkitWebExtensionCreate({ { "manifest.json"_s, createGBytes(manifestString) } }, &error.outPtr()));
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(extension.get()));
        return extension;
    };

    GRefPtr<WebKitWebExtension> extension = parseExtensionManifest("{ \"options_page\": \"options.html\", \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_no_error(error.get());
    GRefPtr<WebKitWebExtensionContext> context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    GUniquePtr<char> optionsURI(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "options.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_options_page_uri(context.get()), ==, optionsURI.get());

    extension = parseExtensionManifest("{ \"options_page\": 123, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_options_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"options_ui\": { \"page\": \"options.html\" }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    optionsURI.reset(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "options.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_options_page_uri(context.get()), ==, optionsURI.get());

    extension = parseExtensionManifest("{ \"options_ui\": { \"page\": 123 }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_options_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"options_page\": \"\", \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_options_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"options_ui\": { }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_options_page_uri(context.get()));
}

static void testURIOverridesParsing(Test* test, gconstpointer)
{
    GUniqueOutPtr<GError> error;
    auto parseExtensionManifest = [&](const gchar* manifestString) {
        GRefPtr extension = adoptGRef(webkitWebExtensionCreate({ { "manifest.json"_s, createGBytes(manifestString) } }, &error.outPtr()));
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(extension.get()));
        return extension;
    };

    GRefPtr<WebKitWebExtension> extension = parseExtensionManifest("{ \"browser_url_overrides\": { \"newtab\": \"newtab.html\" }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_no_error(error.get());
    GRefPtr<WebKitWebExtensionContext> context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    GUniquePtr<char> newTabPageURI(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "newtab.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()), ==, newTabPageURI.get());

    extension = parseExtensionManifest("{ \"browser_url_overrides\": { \"newtab\": 123 }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"browser_url_overrides\": { }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"browser_url_overrides\": { \"newtab\": \"\" }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"chrome_url_overrides\": { \"newtab\": \"newtab.html\" }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_no_error(error.get());
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    newTabPageURI.reset(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "newtab.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()), ==, newTabPageURI.get());

    extension = parseExtensionManifest("{ \"chrome_url_overrides\": { \"newtab\": 123 }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"chrome_url_overrides\": { }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));

    extension = parseExtensionManifest("{ \"chrome_url_overrides\": { \"newtab\": \"\" }, \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\" }");
    g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_ERROR, WEBKIT_WEB_EXTENSION_ERROR_INVALID_MANIFEST_ENTRY);
    context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    g_assert_null(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()));
}

static void testLoadBackgroundContentWithoutController(Test* test, gconstpointer)
{
    GUniqueOutPtr<GError> error;
    auto parseExtensionManifest = [&](const gchar* manifestString, const gchar* backgroundScript) {
        GRefPtr extension = adoptGRef(webkitWebExtensionCreate({ { "manifest.json"_s, createGBytes(manifestString) }, { "background.js"_s, createGBytes(backgroundScript) } }, &error.outPtr()));
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(extension.get()));
        return extension;
    };

    GRefPtr<WebKitWebExtension> extension = parseExtensionManifest("{ \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\", \"background\": { \"scripts\": [ \"background.js\" ], \"type\": \"module\", \"persistent\": false } }", "const img = new Image(); img.src = 'non-existent-image.png';");
    g_assert_no_error(error.get());
    GRefPtr<WebKitWebExtensionContext> context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    g_assert_no_error(error.get());
    GRefPtr<GMainLoop> mainLoop(g_main_loop_new(nullptr, TRUE));

    webkit_web_extension_context_load_background_content(context.get(), nullptr, [](GObject* object, GAsyncResult* result, gpointer userData) mutable {
        auto* mainLoop = static_cast<GMainLoop*>(userData);
        GUniqueOutPtr<GError> error;
        g_assert_false(webkit_web_extension_context_load_background_content_finish(WEBKIT_WEB_EXTENSION_CONTEXT(object), result, &error.outPtr()));
        g_assert_error(error.get(), WEBKIT_WEB_EXTENSION_CONTEXT_ERROR, WEBKIT_WEB_EXTENSION_CONTEXT_ERROR_NOT_LOADED);
        g_main_loop_quit(mainLoop);
    }, mainLoop.get());

    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_main_loop_run(mainLoop.get());
}

static void testBaseURI(Test* test, gconstpointer)
{
    GUniqueOutPtr<GError> error;
    auto parseExtensionManifest = [&](const gchar* manifestString) {
        GRefPtr extension = adoptGRef(webkitWebExtensionCreate({ { "manifest.json"_s, createGBytes(manifestString) } }, &error.outPtr()));
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(extension.get()));
        return extension;
    };

    GRefPtr<WebKitWebExtension> extension = parseExtensionManifest("{ \"options_page\": \"options.html\", \"manifest_version\": 3, \"name\": \"Test\", \"description\": \"Test\", \"version\": \"1.0\", \"browser_url_overrides\": { \"newtab\": \"newtab.html\" } }");
    g_assert_no_error(error.get());
    GRefPtr<WebKitWebExtensionContext> context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(context.get()));
    g_assert_no_error(error.get());

    GUniquePtr<char> optionsURI(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "options.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_options_page_uri(context.get()), ==, optionsURI.get());

    GUniquePtr<char> newTabPageURI(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "newtab.html", nullptr));
    g_assert_cmpstr(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()), ==, newTabPageURI.get());

    webkit_web_extension_match_pattern_register_custom_url_scheme("test-extension");
    webkit_web_extension_context_set_base_uri(context.get(), "test-extension://aaabbbcccddd");
    optionsURI.reset(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "options.html", nullptr));
    g_assert_cmpstr(optionsURI.get(), ==, "test-extension://aaabbbcccddd/options.html");
    g_assert_cmpstr(webkit_web_extension_context_get_options_page_uri(context.get()), ==, optionsURI.get());

    newTabPageURI.reset(g_strconcat(webkit_web_extension_context_get_base_uri(context.get()), "newtab.html", nullptr));
    g_assert_cmpstr(newTabPageURI.get(), ==, "test-extension://aaabbbcccddd/newtab.html");
    g_assert_cmpstr(webkit_web_extension_context_get_override_new_tab_page_uri(context.get()), ==, newTabPageURI.get());
}

#if PLATFORM(GTK)
static void testGrantedContentWorld(WebViewTest* test, gconstpointer)
{
    constexpr auto manifest = R"({
        "manifest_version": 3,
        "name": "Content world test",
        "version": "1.0",
        "host_permissions": ["https://required.example/*"],
        "optional_host_permissions": ["https://optional.example/*"],
        "externally_connectable": {"matches": ["https://external.example/*"]},
        "content_scripts": [{
            "matches": ["*://*/fixture/*"],
            "js": ["content.js"],
            "run_at": "document_end"
        }, {
            "matches": ["*://*/fixture/*"],
            "js": ["main.js"],
            "world": "MAIN",
            "run_at": "document_end"
        }, {
            "matches": ["*://*/fixture/*"],
            "include_globs": ["*fixture/glob/page"],
            "js": ["glob.js"],
            "run_at": "document_end"
        }]
    })";
    constexpr auto script = R"({
        document.documentElement.dataset.extensionScriptRuns = Number(document.documentElement.dataset.extensionScriptRuns || "0") + 1;
        const listener = () => {};
        chrome.runtime.onMessage.addListener(listener);
        if (chrome.runtime.onMessage.hasListener(listener))
            document.documentElement.dataset.listenerRegistered = "yes";
    })";
    GUniqueOutPtr<GError> error;
    GRefPtr extension = adoptGRef(webkitWebExtensionCreate({
        { "manifest.json"_s, createGBytes(manifest) },
        { "content.js"_s, createGBytes(script) },
        { "main.js"_s, createGBytes("document.documentElement.dataset.mainWorldScriptRan = 'yes';") },
        { "glob.js"_s, createGBytes("document.documentElement.dataset.globScriptRan = 'yes';") },
    }, &error.outPtr()));
    g_assert_no_error(error.get());
    auto** requestedHosts = webkit_web_extension_get_requested_host_match_patterns(extension.get());
    g_assert_nonnull(requestedHosts);
    bool foundContentHost = false;
    bool foundRequiredHost = false;
    unsigned hostCount = 0;
    for (auto** current = requestedHosts; *current; ++current) {
        auto* pattern = webkit_web_extension_match_pattern_get_string(*current);
        foundContentHost |= !g_strcmp0(pattern, "*://*/fixture/*");
        foundRequiredHost |= !g_strcmp0(pattern, "https://required.example/*");
        ++hostCount;
        webkit_web_extension_match_pattern_unref(*current);
    }
    g_free(requestedHosts);
    g_assert_cmpuint(hostCount, ==, 2);
    g_assert_true(foundContentHost);
    g_assert_true(foundRequiredHost);
    GRefPtr context = adoptGRef(webkit_web_extension_context_new_for_extension(extension.get(), &error.outPtr()));
    g_assert_no_error(error.get());
    extension.clear();

    GRefPtr session = adoptGRef(webkit_network_session_new_ephemeral());
    GRefPtr controller = adoptGRef(webkit_web_extension_controller_new(session.get()));
    GRefPtr webView = adoptGRef(WEBKIT_WEB_VIEW(g_object_ref_sink(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "network-session", session.get(),
        "web-extension-controller", controller.get(),
        nullptr))));
    GRefPtr secondSession = adoptGRef(webkit_network_session_new_ephemeral());
    auto makeForeignView = [&] {
        return adoptGRef(WEBKIT_WEB_VIEW(g_object_ref_sink(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "network-session", secondSession.get(),
            "user-content-manager", webkit_web_view_get_user_content_manager(webView.get()),
            nullptr))));
    };
    GRefPtr earlyForeignView = makeForeignView();

    auto checkScriptRuns = [&](WebKitWebView* view, unsigned expected) {
        auto* runs = test->runJavaScriptAndWaitUntilFinished("Number(document.documentElement.dataset.extensionScriptRuns || '0')", &error.outPtr(), view);
        g_assert_no_error(error.get());
        g_assert_cmpfloat(WebViewTest::javascriptResultToNumber(runs), ==, expected);
    };

    auto checkPage = [&](WebKitWebView* view, const char* uri, bool shouldInject) {
        test->loadHtml("<!doctype html><title>Extension fixture</title>", uri, view);
        test->waitUntilLoadFinished(view);
        checkScriptRuns(view, shouldInject ? 1 : 0);
        auto* injected = test->runJavaScriptAndWaitUntilFinished("document.documentElement.dataset.listenerRegistered === 'yes'", &error.outPtr(), view);
        g_assert_no_error(error.get());
        g_assert_cmpint(WebViewTest::javascriptResultToBoolean(injected), ==, shouldInject);
        auto* exposed = test->runJavaScriptAndWaitUntilFinished("typeof globalThis.chrome?.runtime !== 'undefined'", &error.outPtr(), view);
        g_assert_no_error(error.get());
        g_assert_false(WebViewTest::javascriptResultToBoolean(exposed));
        auto* mainWorld = test->runJavaScriptAndWaitUntilFinished("document.documentElement.dataset.mainWorldScriptRan === 'yes'", &error.outPtr(), view);
        g_assert_no_error(error.get());
        g_assert_false(WebViewTest::javascriptResultToBoolean(mainWorld));
        auto* glob = test->runJavaScriptAndWaitUntilFinished("document.documentElement.dataset.globScriptRan === 'yes'", &error.outPtr(), view);
        g_assert_no_error(error.get());
        g_assert_false(WebViewTest::javascriptResultToBoolean(glob));
    };

    g_assert_true(webkit_web_extension_controller_load(controller.get(), context.get(), &error.outPtr()));
    g_assert_no_error(error.get());

    GRefPtr secondController = adoptGRef(webkit_web_extension_controller_new(secondSession.get()));
    GUniqueOutPtr<GError> alreadyLoaded;
    g_assert_false(webkit_web_extension_controller_load(secondController.get(), context.get(), &alreadyLoaded.outPtr()));
    g_assert_error(alreadyLoaded.get(), WEBKIT_WEB_EXTENSION_CONTEXT_ERROR, WEBKIT_WEB_EXTENSION_CONTEXT_ERROR_ALREADY_LOADED);

    constexpr auto allowedPage = "http://127.0.0.1/fixture/granted/page";
    constexpr auto securePage = "https://127.0.0.1/fixture/granted/page";
    checkPage(webView.get(), allowedPage, false);

    const char* grant[] = { "http://127.0.0.1/fixture/granted/*", nullptr };
    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), grant, &error.outPtr()));
    g_assert_no_error(error.get());
    checkPage(webView.get(), securePage, false);
    checkPage(webView.get(), "http://localhost/fixture/granted/page", false);
    checkPage(webView.get(), "http://127.0.0.1/fixture/not-granted/page", false);
    checkPage(webView.get(), allowedPage, true);

    const char* additionalGrant[] = { grant[0], "https://unrelated.example/*", nullptr };
    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), additionalGrant, &error.outPtr()));
    g_assert_no_error(error.get());
    checkScriptRuns(webView.get(), 1);

    const char* narrowedGrant[] = { "http://127.0.0.1/fixture/granted/page*", additionalGrant[1], nullptr };
    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), narrowedGrant, &error.outPtr()));
    g_assert_no_error(error.get());
    checkScriptRuns(webView.get(), 1);
    checkPage(webView.get(), "http://127.0.0.1/fixture/granted/page-two", true);
    checkPage(webView.get(), "http://127.0.0.1/fixture/granted/other", false);

    checkPage(test->webView(), allowedPage, false);
    checkPage(earlyForeignView.get(), allowedPage, false);
    GRefPtr lateForeignView = makeForeignView();
    checkPage(lateForeignView.get(), allowedPage, false);

    auto* openerManager = webkit_web_view_get_user_content_manager(webView.get());
    auto* foreignManager = webkit_web_view_get_user_content_manager(earlyForeignView.get());
    GRefPtr relatedView = adoptGRef(WEBKIT_WEB_VIEW(g_object_ref_sink(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "related-view", webView.get(),
        "web-extension-controller", secondController.get(),
        "user-content-manager", foreignManager,
        nullptr))));
    g_assert_true(webkit_web_view_get_network_session(relatedView.get()) == session.get());
    g_assert_true(webkit_web_view_get_user_content_manager(relatedView.get()) != foreignManager);
    checkPage(relatedView.get(), allowedPage, true);

    auto openPopup = [&](WebKitWebView* opener, WebKitWebExtensionController* controller, WebKitUserContentManager* manager) {
        struct PopupData {
            WebKitWebExtensionController* controller;
            WebKitUserContentManager* manager;
            WebKitWebView* view { nullptr };
        } data { controller, manager };
        auto handler = g_signal_connect(opener, "create", G_CALLBACK(+[](WebKitWebView* opener, WebKitNavigationAction*, PopupData* data) -> WebKitWebView* {
            data->view = WEBKIT_WEB_VIEW(g_object_ref_sink(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                "related-view", opener,
                "web-extension-controller", data->controller,
                "user-content-manager", data->manager,
                nullptr)));
            return data->view;
        }), &data);
        webkit_settings_set_javascript_can_open_windows_automatically(webkit_web_view_get_settings(opener), TRUE);
        auto* opened = test->runJavaScriptAndWaitUntilFinished("window.open() !== null", &error.outPtr(), opener);
        g_signal_handler_disconnect(opener, handler);
        g_assert_no_error(error.get());
        g_assert_true(WebViewTest::javascriptResultToBoolean(opened));
        g_assert_nonnull(data.view);
        return adoptGRef(data.view);
    };

    auto relatedPopup = openPopup(webView.get(), secondController.get(), foreignManager);
    g_assert_true(webkit_web_view_get_network_session(relatedPopup.get()) == session.get());
    g_assert_true(webkit_web_view_get_user_content_manager(relatedPopup.get()) != foreignManager);
    checkPage(relatedPopup.get(), allowedPage, true);

    auto foreignPopup = openPopup(earlyForeignView.get(), controller.get(), openerManager);
    g_assert_true(webkit_web_view_get_network_session(foreignPopup.get()) == secondSession.get());
    g_assert_true(webkit_web_view_get_user_content_manager(foreignPopup.get()) != openerManager);
    checkPage(foreignPopup.get(), allowedPage, false);
    checkPage(earlyForeignView.get(), allowedPage, false);

    const char* secureHosts[] = { "https://*/*", "https://127.0.0.1/fixture/granted/*", nullptr };
    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), secureHosts, &error.outPtr()));
    g_assert_no_error(error.get());
    checkPage(webView.get(), securePage, true);
    checkPage(webView.get(), "https://127.0.0.1/outside-fixture/page", false);
    checkPage(webView.get(), allowedPage, false);

    const char* revoked[] = { nullptr };
    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), revoked, &error.outPtr()));
    g_assert_no_error(error.get());
    checkPage(webView.get(), securePage, false);

    g_assert_true(webkit_web_extension_context_set_granted_hosts(context.get(), grant, &error.outPtr()));
    g_assert_no_error(error.get());
    checkPage(webView.get(), allowedPage, true);
    g_assert_true(webkit_web_extension_controller_unload(controller.get(), context.get(), &error.outPtr()));
    g_assert_no_error(error.get());
    checkPage(webView.get(), allowedPage, false);
}
#endif

void beforeAll()
{
    Test::add("WebKitWebExtensionContext", "content-scripts-parsing", testContentScriptsParsing);
    Test::add("WebKitWebExtensionContext", "options-page-uri-parsing", testOptionsPageURIParsing);
    Test::add("WebKitWebExtensionContext", "uri-overrides-parsing", testURIOverridesParsing);
    Test::add("WebKitWebExtensionContext", "load-background-content-without-controller", testLoadBackgroundContentWithoutController);
    Test::add("WebKitWebExtensionContext", "base-uri", testBaseURI);
#if PLATFORM(GTK)
    WebViewTest::add("WebKitWebExtensionContext", "granted-content-world", testGrantedContentWorld);
#endif

    // Some code in WebExtensionContext increases the amount of time allotted to a particular process
    // when running in the Test Runner. Set a consistent Program Name (we can't set an Application ID since the Test Runner doesn't use GApplication)
    // so WebExtensionContext can detect the test runner
#if PLATFORM(WPE)
    g_set_prgname("org.webkit.app-TestWebKitWPE");
#else
    g_set_prgname("org.webkit.app-TestWebKitGTK");
#endif
}

void afterAll()
{
}

#endif // ENABLE(WK_WEB_EXTENSIONS)
