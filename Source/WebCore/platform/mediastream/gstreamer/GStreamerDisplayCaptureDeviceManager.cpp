/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro <alex@igalia.com>
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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "GStreamerCaptureDeviceManager.h"

#include "GStreamerVideoCaptureSource.h"
#include "PipeWireCaptureDevice.h"
#include <wtf/Scope.h>
#include <wtf/SetForScope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UUID.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

WTF_MAKE_STRUCT_TZONE_ALLOCATED_IMPL(GStreamerDisplayCaptureDeviceManager::ActiveSession);

GStreamerDisplayCaptureDeviceManager& GStreamerDisplayCaptureDeviceManager::singleton()
{
    static NeverDestroyed<GStreamerDisplayCaptureDeviceManager> manager;
    return manager;
}

GStreamerDisplayCaptureDeviceManager::GStreamerDisplayCaptureDeviceManager() = default;

GStreamerDisplayCaptureDeviceManager::~GStreamerDisplayCaptureDeviceManager()
{
    while (!m_sessions.isEmpty()) {
        auto sourceId = m_sessions.begin()->key;
        stopSource(sourceId);
    }
}

void GStreamerDisplayCaptureDeviceManager::computeCaptureDevices(CompletionHandler<void()>&& callback)
{
    m_devices.clear();

    CaptureDevice screenCaptureDevice(createVersion4UUIDString(), CaptureDevice::DeviceType::Screen, "Capture Screen"_s);
    screenCaptureDevice.setEnabled(true);
    m_devices.append(WTF::move(screenCaptureDevice));
    callback();
}

CaptureSourceOrError GStreamerDisplayCaptureDeviceManager::createDisplayCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints)
{
    // ScreenCast response handling iterates the main context while the chooser is open.
    if (m_openingSession || m_sessions.contains(device.persistentId()))
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });
    SetForScope openingSession(m_openingSession, true);

    if (!m_portal)
        m_portal = DesktopPortalScreenCast::create();
    if (!m_portal)
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    Ref portal { *m_portal };
    auto session = portal->createScreencastSession();
    if (!session)
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });
    auto closeOnFailure = makeScopeExit([&] {
        session->close();
    });

    // FIXME: Maybe check this depending on device.type().
    auto outputType = GStreamerDisplayCaptureDeviceManager::PipeWireOutputType::Monitor | GStreamerDisplayCaptureDeviceManager::PipeWireOutputType::Window;

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(static_cast<uint32_t>(outputType)));
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(false));

    if (auto version = portal->getProperty("version")) {
        if (g_variant_get_uint32(version.get()) >= 2) {
            // Enable embedded cursor. FIXME: Should be checked in the constraints.
            g_variant_builder_add(&options, "{sv}", "cursor_mode", g_variant_new_uint32(2));
        }
    }

    if (!session->selectSources(options))
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    auto result = session->start();
    if (!result)
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    // The portal interface allows multiple streams but we care only about the first one.
    GUniqueOutPtr<GVariantIter> iter;
    if (!g_variant_lookup(result.get(), "streams", "a(ua{sv})", &iter.outPtr()))
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });
    GRefPtr stream = adoptGRef(g_variant_iter_next_value(iter.get()));
    if (!stream)
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });
    uint32_t nodeId;
    g_variant_get_child(stream.get(), 0, "u", &nodeId);

    auto nodeData = session->openPipewireRemote();
    if (!nodeData)
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    UnixFileDescriptor remote { nodeData->fd, UnixFileDescriptor::Adopt };
    nodeData->objectId = nodeId;
    PipeWireCaptureDevice pipewireCaptureDevice { *nodeData, device.persistentId(), device.type(), device.label(), device.groupId() };
    auto source = GStreamerVideoCaptureSource::createPipewireSource(pipewireCaptureDevice, WTF::move(hashSalts), constraints);
    if (!source)
        return source;

    m_sessions.add(device.persistentId(), makeUnique<ActiveSession>(ActiveSession { WTF::move(*session), WTF::move(remote) }));
    closeOnFailure.release();
    return source;
}

void GStreamerDisplayCaptureDeviceManager::stopSource(const String& persistentID)
{
    auto session = m_sessions.take(persistentID);
    if (session)
        session->portal.close();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
