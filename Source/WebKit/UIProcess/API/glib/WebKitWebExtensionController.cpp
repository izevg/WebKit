/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "WebKitWebExtensionController.h"

#if ENABLE(2022_GLIB_API) && PLATFORM(GTK)

#include "WebKitError.h"
#include "WebKitPrivate.h"
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/WTFGType.h>

#if ENABLE(WK_WEB_EXTENSIONS)
#include "APIError.h"
#include "WebExtensionController.h"
#include "WebKitWebExtensionContextPrivate.h"
#include "WebKitWebExtensionControllerPrivate.h"
#include "WebKitWebsiteDataManagerPrivate.h"
#include "WebsiteDataStore.h"
#include <utility>
#endif

/**
 * WebKitWebExtensionController:
 *
 * Owns extension contexts for a single GTK browsing session.
 *
 * Since: 2.56
 */
struct _WebKitWebExtensionControllerPrivate {
#if ENABLE(WK_WEB_EXTENSIONS)
    GRefPtr<WebKitNetworkSession> session;
    RefPtr<WebKit::WebExtensionController> controller;
    HashMap<WebKitWebExtensionContext*, GRefPtr<WebKitWebExtensionContext>> contexts;
#endif
};

WEBKIT_DEFINE_FINAL_TYPE(WebKitWebExtensionController, webkit_web_extension_controller, G_TYPE_OBJECT, GObject)

static void webkit_web_extension_controller_class_init(WebKitWebExtensionControllerClass* klass)
{
#if ENABLE(WK_WEB_EXTENSIONS)
    G_OBJECT_CLASS(klass)->dispose = [](GObject* object) {
        auto* priv = WEBKIT_WEB_EXTENSION_CONTROLLER(object)->priv;
        if (RefPtr controller = std::exchange(priv->controller, nullptr))
            controller->unloadAll();
        priv->contexts.clear();
        G_OBJECT_CLASS(webkit_web_extension_controller_parent_class)->dispose(object);
    };
#else
    UNUSED_PARAM(klass);
#endif
}

/**
 * webkit_web_extension_controller_new:
 * @session: the only [class@NetworkSession] whose views may use this controller
 *
 * Returns: (transfer full): a WebExtensions controller
 *
 * Since: 2.56
 */
WebKitWebExtensionController* webkit_web_extension_controller_new(WebKitNetworkSession* session)
{
    g_return_val_if_fail(WEBKIT_IS_NETWORK_SESSION(session), nullptr);

    auto* object = WEBKIT_WEB_EXTENSION_CONTROLLER(g_object_new(WEBKIT_TYPE_WEB_EXTENSION_CONTROLLER, nullptr));
#if ENABLE(WK_WEB_EXTENSIONS)
    object->priv->session = session;
    auto configuration = WebKit::WebExtensionControllerConfiguration::createNonPersistent();
    auto* manager = webkit_network_session_get_website_data_manager(session);
    configuration->setDefaultWebsiteDataStore(&webkitWebsiteDataManagerGetDataStore(manager));
    object->priv->controller = WebKit::WebExtensionController::create(WTF::move(configuration));
#endif
    return object;
}

#if ENABLE(WK_WEB_EXTENSIONS)
RefPtr<WebKit::WebExtensionController> webkitWebExtensionControllerForDataStore(WebKitWebExtensionController* object, WebKit::WebsiteDataStore& store)
{
    RefPtr controller = object->priv->controller;
    if (!controller || &controller->configuration().defaultWebsiteDataStore() != &store)
        return nullptr;
    return controller;
}

static gboolean propagateError(const RefPtr<API::Error>& error, GError** outError)
{
    if (error) {
        auto description = error->localizedDescription().utf8();
        g_set_error_literal(outError, webkit_web_extension_context_error_quark(), toWebKitWebExtensionContextError(error->errorCode()), description.legacyCStringPointer());
    } else
        g_set_error_literal(outError, webkit_web_extension_context_error_quark(), WEBKIT_WEB_EXTENSION_CONTEXT_ERROR_UNKNOWN, "Invalid extension context");
    return FALSE;
}
#endif

/**
 * webkit_web_extension_controller_load:
 * @object: a #WebKitWebExtensionController
 * @context: a #WebKitWebExtensionContext
 * @error: return location for error or %NULL
 *
 * Loads @context with its current permission grants.
 *
 * Returns: whether the context loaded
 *
 * Since: 2.56
 */
gboolean webkit_web_extension_controller_load(WebKitWebExtensionController* object, WebKitWebExtensionContext* context, GError** error)
{
    g_return_val_if_fail(WEBKIT_IS_WEB_EXTENSION_CONTROLLER(object), FALSE);
    g_return_val_if_fail(WEBKIT_IS_WEB_EXTENSION_CONTEXT(context), FALSE);

#if ENABLE(WK_WEB_EXTENSIONS)
    RefPtr controller = object->priv->controller;
    RefPtr extensionContext = webkitWebExtensionContextToImpl(context);
    if (!controller || !extensionContext)
        return propagateError(nullptr, error);
    if (extensionContext->isLoaded())
        return propagateError(extensionContext->createError(WebKit::WebExtensionContext::Error::AlreadyLoaded), error);
    auto result = controller->load(*extensionContext);
    if (!result)
        return propagateError(result.error(), error);
    object->priv->contexts.add(context, GRefPtr { context });
    return TRUE;
#else
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "WebExtensions are disabled");
    return FALSE;
#endif
}

/**
 * webkit_web_extension_controller_unload:
 * @object: a #WebKitWebExtensionController
 * @context: a #WebKitWebExtensionContext
 * @error: return location for error or %NULL
 *
 * Unloads @context from its views.
 *
 * Returns: whether the context unloaded
 *
 * Since: 2.56
 */
gboolean webkit_web_extension_controller_unload(WebKitWebExtensionController* object, WebKitWebExtensionContext* context, GError** error)
{
    g_return_val_if_fail(WEBKIT_IS_WEB_EXTENSION_CONTROLLER(object), FALSE);
    g_return_val_if_fail(WEBKIT_IS_WEB_EXTENSION_CONTEXT(context), FALSE);

#if ENABLE(WK_WEB_EXTENSIONS)
    RefPtr controller = object->priv->controller;
    RefPtr extensionContext = webkitWebExtensionContextToImpl(context);
    if (!controller || !extensionContext)
        return propagateError(nullptr, error);
    auto result = controller->unload(*extensionContext);
    if (!result)
        return propagateError(result.error(), error);
    object->priv->contexts.remove(context);
    return TRUE;
#else
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "WebExtensions are disabled");
    return FALSE;
#endif
}

#endif
