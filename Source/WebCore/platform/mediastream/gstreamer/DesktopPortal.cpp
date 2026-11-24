/*
 * Copyright (C) 2025 Igalia S.L.
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
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "DesktopPortal.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "GStreamerCommon.h"
#include <gio/gunixfdlist.h>
#include <gst/video/video-format.h>
#include <optional>
#include <unistd.h>
#include <wtf/Scope.h>
#include <wtf/UUID.h>
#include <wtf/WeakRandomNumber.h>
#include <wtf/glib/GLibExtras.h>
#include <wtf/glib/GMallocString.h>
#include <wtf/glib/GUniquePtr.h>

#if GST_CHECK_VERSION(1, 24, 0)
#include <gst/video/video-info-dma.h>
#endif

namespace WebCore {

static const Seconds s_dbusCallTimeout = 10_ms;

static GRefPtr<GDBusProxy> createDBusProxy(ASCIILiteral interfaceName)
{
    GUniqueOutPtr<GError> error;
    GRefPtr proxy = adoptGRef(g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
        static_cast<GDBusProxyFlags>(G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES), nullptr,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", interfaceName.characters(), nullptr, &error.outPtr()));
    if (error) {
        SAFE_GST_PRINTERRLN("Unable to connect to the Deskop portal: %s", UTF8CStringView::unsafeFromUTF8(error->message));
        return nullptr;
    }
    return proxy;
}

RefPtr<DesktopPortalCamera> DesktopPortalCamera::create()
{
    auto interfaceName = "org.freedesktop.portal.Camera"_s;
    auto proxy = createDBusProxy(interfaceName);
    if (!proxy)
        return nullptr;
    return adoptRef(*new DesktopPortalCamera(interfaceName, WTF::move(proxy)));
}

DesktopPortalCamera::DesktopPortalCamera(ASCIILiteral interfaceName, GRefPtr<GDBusProxy>&& proxy)
    : DesktopPortal(interfaceName, WTF::move(proxy))
{
}

RefPtr<DesktopPortalScreenCast> DesktopPortalScreenCast::create()
{
    auto interfaceName = "org.freedesktop.portal.ScreenCast"_s;
    auto proxy = createDBusProxy(interfaceName);
    if (!proxy)
        return nullptr;
    return adoptRef(*new DesktopPortalScreenCast(interfaceName, WTF::move(proxy)));
}

DesktopPortalScreenCast::DesktopPortalScreenCast(ASCIILiteral interfaceName, GRefPtr<GDBusProxy>&& proxy)
    : DesktopPortal(interfaceName, WTF::move(proxy))
{
}

DesktopPortal::DesktopPortal(ASCIILiteral interfaceName, GRefPtr<GDBusProxy>&& proxy)
    : m_interfaceName(interfaceName)
    , m_proxy(WTF::move(proxy))
{
}

DesktopPortal::~DesktopPortal()
{
    if (m_responseSignalId)
        g_dbus_connection_signal_unsubscribe(g_dbus_proxy_get_connection(m_proxy.get()), m_responseSignalId);

    if (m_currentResponseCallback)
        m_currentResponseCallback(nullptr);
}

GRefPtr<GVariant> DesktopPortal::getProperty(ASCIILiteral name)
{
    GRefPtr propertiesResult = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "org.freedesktop.DBus.Properties.Get",
        g_variant_new("(ss)", m_interfaceName.characters(), name.characters()), G_DBUS_CALL_FLAGS_NONE,
        s_dbusCallTimeout.millisecondsAs<int>(), nullptr, nullptr));
    if (propertiesResult) {
        GRefPtr<GVariant> property;
        g_variant_get(propertiesResult.get(), "(v)", &property.outPtr());
        return property;
    }
    return nullptr;
}

void DesktopPortal::notifyResponse(GVariant* parameters)
{
    auto signalId = std::exchange(m_responseSignalId, 0);

    m_currentResponseCallback(parameters);

    if (signalId)
        g_dbus_connection_signal_unsubscribe(g_dbus_proxy_get_connection(m_proxy.get()), signalId);
}

bool DesktopPortalCamera::isCameraPresent()
{
    auto isCameraPresent = getProperty("IsCameraPresent"_s);
    if (!isCameraPresent)
        return false;

    return g_variant_get_boolean(isCameraPresent.get());
}

void DesktopPortalCamera::accessCamera(Function<void(std::optional<int>)>&& callback)
{
    auto token = makeString("WebKit"_s, weakRandomNumber<uint32_t>());
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", gVariantNewString(token.utf8()));

    auto connection = g_dbus_proxy_get_connection(m_proxy.get());
    auto connectionString = StringView::fromLatin1(g_dbus_connection_get_unique_name(connection));
    auto sender = makeStringByReplacingAll(connectionString.substring(1), '.', '_');
    auto objectPath = makeString("/org/freedesktop/portal/desktop/request/"_s, sender, '/', token);

    m_currentResponseCallback = [callback = WTF::move(callback), this](auto* variant) mutable {
        if (!variant) {
            callback(std::nullopt);
            return;
        }

        uint32_t response;
        g_variant_get(variant, "(u@a{sv})", &response, nullptr);
        // 0 response value means the user allowed device access.
        if (!response) {
            callback(openCameraPipewireRemote());
            return;
        }
        callback(std::nullopt);
    };

    m_responseSignalId = g_dbus_connection_signal_subscribe(connection, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
        "Response", objectPath.utf8().legacyCStringPointer(), nullptr, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, reinterpret_cast<GDBusSignalCallback>(+[](GDBusConnection*, const char* /* senderName */, const char* /* objectPath */, const char* /* interfaceName */, const char* /* signalName */, GVariant* parameters, gpointer userData) {
            auto& self = *reinterpret_cast<DesktopPortal*>(userData);
            self.notifyResponse(parameters);
        }), this, nullptr);

    g_dbus_proxy_call(m_proxy.get(), "AccessCamera", g_variant_new("(a{sv})", &options), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, reinterpret_cast<GAsyncReadyCallback>(+[](GDBusProxy* proxy, GAsyncResult* result, gpointer) {
        GRefPtr finalResult = adoptGRef(g_dbus_proxy_call_finish(proxy, result, nullptr));
    }), nullptr);
}

std::optional<int> DesktopPortalCamera::openCameraPipewireRemote()
{
    GRefPtr<GUnixFDList> fdList;
    int fd = -1;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUniqueOutPtr<GError> error;
    GRefPtr result = adoptGRef(g_dbus_proxy_call_with_unix_fd_list_sync(m_proxy.get(), "OpenPipeWireRemote",
        g_variant_new("(a{sv})", &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &fdList.outPtr(), nullptr, &error.outPtr()));
    if (error) {
        SAFE_GST_PRINTERRLN("Unable to open pipewire remote. Error: %s", UTF8CStringView::unsafeFromUTF8(error->message));
        return { };
    }

    int index;
    g_variant_get(result.get(), "(h)", &index);
    fd = g_unix_fd_list_get(fdList.get(), index, &error.outPtr());
    if (fd == -1) {
        SAFE_GST_PRINTERRLN("Unable to open pipewire remote. Error: %s", UTF8CStringView::unsafeFromUTF8(error->message));
        return { };
    }
    return fd;
}

static String screenCastPath(GDBusConnection* connection, ASCIILiteral kind, const String& token)
{
    auto* uniqueName = g_dbus_connection_get_unique_name(connection);
    if (!uniqueName)
        return { };
    auto connectionName = StringView::fromLatin1(uniqueName);
    auto sender = makeStringByReplacingAll(connectionName.substring(1), '.', '_');
    return makeString("/org/freedesktop/portal/desktop/"_s, kind, '/', sender, '/', token);
}

ASCIICString DesktopPortalScreenCast::currentOwner() const
{
    GUniquePtr<char> owner(g_dbus_proxy_get_name_owner(m_proxy.get()));
    return ASCIICString(owner.get());
}

GRefPtr<GVariant> DesktopPortalScreenCast::request(const ASCIICString& owner, ASCIILiteral method, const String& token, GVariant* parameters, bool* explicitlyRejected)
{
    GRefPtr arguments = adoptGRef(g_variant_ref_sink(parameters));
    if (owner.isNull() || currentOwner() != owner)
        return nullptr;

    struct Response {
        const char* owner;
        GRefPtr<GMainLoop> loop;
        GRefPtr<GVariant> signal;
        bool ownerLost { false };
    };
    Response response { owner.data(), adoptGRef(g_main_loop_new(g_main_context_get_thread_default(), FALSE)), nullptr };

    auto* connection = g_dbus_proxy_get_connection(m_proxy.get());
    auto path = screenCastPath(connection, "request"_s, token).utf8();
    if (path.isNull())
        return nullptr;
    // The portal can send Response before returning the method reply.
    auto responseId = g_dbus_connection_signal_subscribe(connection, owner.data(), "org.freedesktop.portal.Request",
        "Response", path.legacyCStringPointer(), nullptr, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
        [](GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* signal, gpointer data) {
            auto& response = *static_cast<Response*>(data);
            response.signal = signal;
            g_main_loop_quit(response.loop.get());
        }, &response, nullptr);
    auto ownerId = g_signal_connect(m_proxy.get(), "notify::g-name-owner", G_CALLBACK(+[](GDBusProxy* proxy, GParamSpec*, gpointer data) {
        auto& response = *static_cast<Response*>(data);
        auto currentOwner = GUniquePtr<char>(g_dbus_proxy_get_name_owner(proxy));
        if (g_strcmp0(currentOwner.get(), response.owner)) {
            response.ownerLost = true;
            g_main_loop_quit(response.loop.get());
        }
    }), &response);
    auto unsubscribe = makeScopeExit([&] {
        g_signal_handler_disconnect(m_proxy.get(), ownerId);
        g_dbus_connection_signal_unsubscribe(connection, responseId);
    });

    GRefPtr reply = adoptGRef(g_dbus_connection_call_sync(connection, owner.data(), "/org/freedesktop/portal/desktop", m_interfaceName.characters(),
        method.characters(), arguments.get(), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, nullptr));
    auto cancelRequest = [&] {
        g_dbus_connection_call(connection, owner.data(), path.legacyCStringPointer(), "org.freedesktop.portal.Request", "Close",
            nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
    };
    if (!reply) {
        cancelRequest();
        return nullptr;
    }

    const char* returnedPath;
    g_variant_get(reply.get(), "(&o)", &returnedPath);
    if (g_strcmp0(path.legacyCStringPointer(), returnedPath)) {
        cancelRequest();
        return nullptr;
    }

    GRefPtr timeout = adoptGRef(g_timeout_source_new_seconds(method == "Start"_s ? 120 : 10));
    g_source_set_callback(timeout.get(), [](gpointer loop) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(loop));
        return G_SOURCE_REMOVE;
    }, response.loop.get(), nullptr);
    g_source_attach(timeout.get(), g_main_loop_get_context(response.loop.get()));
    if (!response.signal && !response.ownerLost)
        g_main_loop_run(response.loop.get());
    g_source_destroy(timeout.get());

    if (response.ownerLost)
        return nullptr;
    if (!response.signal) {
        cancelRequest();
        return nullptr;
    }
    if (!g_variant_is_of_type(response.signal.get(), G_VARIANT_TYPE("(ua{sv})")))
        return nullptr;

    unsigned result;
    GRefPtr<GVariant> details;
    g_variant_get(response.signal.get(), "(u@a{sv})", &result, &details.outPtr());
    if (explicitlyRejected)
        *explicitlyRejected = result == 1 || result == 2;
    return result ? nullptr : details;
}

std::optional<DesktopPortalScreenCast::ScreencastSession> DesktopPortalScreenCast::createScreencastSession()
{
    auto owner = currentOwner();
    if (owner.isNull())
        return std::nullopt;

    auto token = makeString("WebKit"_s, weakRandomNumber<uint32_t>());
    auto sessionToken = makeString("WebKit"_s, weakRandomNumber<uint32_t>());
    auto expectedPath = screenCastPath(g_dbus_proxy_get_connection(m_proxy.get()), "session"_s, sessionToken);
    if (expectedPath.isNull())
        return std::nullopt;

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));
    g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.ascii().data()));

    bool explicitlyRejected = false;
    auto result = request(owner, "CreateSession"_s, token, g_variant_new("(a{sv})", &options), &explicitlyRejected);
    const char* returnedPath;
    if (result && g_variant_lookup(result.get(), "session_handle", "&s", &returnedPath)) {
        auto sessionPath = String::fromUTF8(unsafeSpan(returnedPath));
        if (sessionPath == expectedPath)
            return { ScreencastSession { WTF::move(sessionPath), WTF::move(owner), *this } };
    }

    // The portal may have created the session even if its reply was lost or invalid.
    if (!explicitlyRejected)
        ScreencastSession { WTF::move(expectedPath), WTF::move(owner), *this }.close();
    return std::nullopt;
}

void DesktopPortalScreenCast::ScreencastSession::close()
{
    auto path = std::exchange(m_path, { });
    if (path.isNull() || m_portal->currentOwner() != m_owner)
        return;

    GUniqueOutPtr<GError> error;
    auto* connection = g_dbus_proxy_get_connection(m_portal->m_proxy.get());
    GRefPtr result = adoptGRef(g_dbus_connection_call_sync(connection, m_owner.data(), path.ascii().data(), "org.freedesktop.portal.Session", "Close",
        nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error.outPtr()));
    if (error)
        SAFE_GST_PRINTERRLN("Portal session could not be closed: %s", UTF8CStringView::unsafeFromUTF8(error->message));
}

GRefPtr<GVariant> DesktopPortalScreenCast::ScreencastSession::selectSources(GVariantBuilder& options)
{
    auto token = makeString("WebKit"_s, weakRandomNumber<uint32_t>());
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));

    return m_portal->request(m_owner, "SelectSources"_s, token, gVariantNew("(oa{sv})", m_path.ascii(), &options));
}

GRefPtr<GVariant> DesktopPortalScreenCast::ScreencastSession::start()
{
    auto token = makeString("WebKit"_s, weakRandomNumber<uint32_t>());
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));

    return m_portal->request(m_owner, "Start"_s, token, gVariantNew("(osa{sv})", m_path.ascii(), "", &options));
}

std::optional<PipeWireNodeData> DesktopPortalScreenCast::ScreencastSession::openPipewireRemote()
{
    if (m_portal->currentOwner() != m_owner)
        return std::nullopt;

    GRefPtr<GUnixFDList> fdList;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUniqueOutPtr<GError> error;
    auto* connection = g_dbus_proxy_get_connection(m_portal->m_proxy.get());
    GRefPtr result = adoptGRef(g_dbus_connection_call_with_unix_fd_list_sync(connection, m_owner.data(), "/org/freedesktop/portal/desktop",
        m_portal->m_interfaceName.characters(), "OpenPipeWireRemote", gVariantNew("(oa{sv})", m_path.ascii(), &options),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &fdList.outPtr(), nullptr, &error.outPtr()));
    if (error) {
        SAFE_GST_PRINTERRLN("Unable to open pipewire remote. Error: %s", UTF8CStringView::unsafeFromUTF8(error->message));
        return { };
    }
    if (!result || !fdList)
        return std::nullopt;

    int index;
    g_variant_get(result.get(), "(h)", &index);
    if (index < 0 || index >= g_unix_fd_list_get_length(fdList.get()))
        return std::nullopt;
    int fd = g_unix_fd_list_get(fdList.get(), index, &error.outPtr());
    if (fd == -1) {
        SAFE_GST_PRINTERRLN("Unable to open pipewire remote. Error: %s", UTF8CStringView::unsafeFromUTF8(error->message));
        return { };
    }

    auto nodeData = PipeWireNodeData(0);
    nodeData.fd = fd;
    nodeData.path = m_path;
    nodeData.caps = adoptGRef(gst_caps_new_empty_simple("video/x-raw"));
    gst_caps_set_features_simple(nodeData.caps.get(), gst_caps_features_new("memory:DMABuf", nullptr));

#if GST_CHECK_VERSION(1, 24, 0)
    auto fourcc = gst_video_dma_drm_fourcc_from_format(GST_VIDEO_FORMAT_BGRA);
    auto drmFormat = GMallocString::unsafeAdoptFromUTF8(gst_video_dma_drm_fourcc_to_string(fourcc, 0));
    gst_caps_set_simple(nodeData.caps.get(), "format", G_TYPE_STRING, "DMA_DRM", "drm-format", G_TYPE_STRING, drmFormat.utf8(), nullptr);
#endif
    return nodeData;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
