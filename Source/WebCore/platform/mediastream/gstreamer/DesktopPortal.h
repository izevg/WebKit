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

#pragma once

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "PipeWireNodeData.h"
#include <gio/gio.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/CString.h>
#include <wtf/text/UTF8CStringView.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class DesktopPortal : public RefCounted<DesktopPortal> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DesktopPortal);
public:
    DesktopPortal(ASCIILiteral, GRefPtr<GDBusProxy>&&);
    virtual ~DesktopPortal();

    GRefPtr<GVariant> getProperty(ASCIILiteral name);

    using ResponseCallback = CompletionHandler<void(GVariant*)>;

    void notifyResponse(GVariant* parameters);

protected:
    ASCIILiteral m_interfaceName;
    GRefPtr<GDBusProxy> m_proxy;
    ResponseCallback m_currentResponseCallback;
    unsigned m_responseSignalId { 0 };
};

class DesktopPortalCamera : public DesktopPortal {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DesktopPortalCamera);
public:
    static RefPtr<DesktopPortalCamera> create();

    bool isCameraPresent();
    void accessCamera(Function<void(std::optional<int>)>&&);

private:
    std::optional<int> openCameraPipewireRemote();

    DesktopPortalCamera(ASCIILiteral, GRefPtr<GDBusProxy>&&);
};

class DesktopPortalScreenCast : public DesktopPortal {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DesktopPortalScreenCast);
public:
    static RefPtr<DesktopPortalScreenCast> create();

    class ScreencastSession {
    public:
        ScreencastSession(String&& path, ASCIICString&& owner, DesktopPortalScreenCast& portal)
            : m_path(WTF::move(path))
            , m_owner(WTF::move(owner))
            , m_portal(portal)
        {
        }
        GRefPtr<GVariant> selectSources(GVariantBuilder&);
        GRefPtr<GVariant> start();
        std::optional<PipeWireNodeData> openPipewireRemote();
        void close();

    private:
        String m_path;
        ASCIICString m_owner;
        Ref<DesktopPortalScreenCast> m_portal;
    };

    std::optional<ScreencastSession> createScreencastSession();
private:
    DesktopPortalScreenCast(ASCIILiteral, GRefPtr<GDBusProxy>&&);
    ASCIICString currentOwner() const;
    GRefPtr<GVariant> request(const ASCIICString& owner, ASCIILiteral method, const String& token, GVariant* parameters, bool* explicitlyRejected = nullptr);
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
