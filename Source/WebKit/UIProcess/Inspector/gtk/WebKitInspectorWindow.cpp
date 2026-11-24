/*
 * Copyright (C) 2017 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS AS IS''
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

#include "config.h"
#include "WebKitInspectorWindow.h"

#include "WebInspectorUIProxy.h"
#include <glib/gi18n-lib.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>

using namespace WebKit;

struct _WebKitInspectorWindow {
    GtkWindow parent;

    GtkWidget* headerBar;

#if USE(GTK4)
    GtkWidget* subtitleLabel;
#endif
};

struct _WebKitInspectorWindowClass {
    GtkWindowClass parent;
};

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GTK port
G_DEFINE_TYPE(WebKitInspectorWindow, webkit_inspector_window, GTK_TYPE_WINDOW)
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

static void webkit_inspector_window_class_init(WebKitInspectorWindowClass*)
{
}

static void webkit_inspector_window_init(WebKitInspectorWindow* window)
{
    window->headerBar = gtk_header_bar_new();

#if USE(GTK4)
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget* titleLabel = gtk_label_new(_("Web Inspector"));
    gtk_widget_set_halign(titleLabel, GTK_ALIGN_CENTER);
    gtk_label_set_single_line_mode(GTK_LABEL(titleLabel), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(titleLabel), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(titleLabel, "title");
    gtk_widget_set_parent(titleLabel, box);

    window->subtitleLabel = gtk_label_new(nullptr);
    gtk_widget_set_halign(window->subtitleLabel, GTK_ALIGN_CENTER);
    gtk_label_set_single_line_mode(GTK_LABEL(window->subtitleLabel), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(window->subtitleLabel), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(window->subtitleLabel, "subtitle");
    gtk_widget_set_parent(window->subtitleLabel, box);
    gtk_widget_hide(window->subtitleLabel);

    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(window->headerBar), box);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(window->headerBar), TRUE);
#else
    gtk_header_bar_set_title(GTK_HEADER_BAR(window->headerBar), _("Web Inspector"));
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(window->headerBar), TRUE);
    gtk_widget_show(window->headerBar);
#endif // USE(GTK4)

    gtk_window_set_titlebar(GTK_WINDOW(window), window->headerBar);
}

GtkWidget* webkitInspectorWindowNew()
{
    return GTK_WIDGET(g_object_new(WEBKIT_TYPE_INSPECTOR_WINDOW,
#if !USE(GTK4)
        "type", GTK_WINDOW_TOPLEVEL,
#endif
        "default-width", WebInspectorUIProxy::initialWindowWidth, "default-height", WebInspectorUIProxy::initialWindowHeight, nullptr));
}

void webkitInspectorWindowSetSubtitle(WebKitInspectorWindow* window, UTF8CStringView subtitle)
{
    g_return_if_fail(WEBKIT_IS_INSPECTOR_WINDOW(window));

#if USE(GTK4)
    if (!subtitle.isNull()) {
        gtk_label_set_text(GTK_LABEL(window->subtitleLabel), subtitle.utf8());
        gtk_widget_show(window->subtitleLabel);
    } else
        gtk_widget_hide(window->subtitleLabel);
#else
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(window->headerBar), subtitle.utf8());
#endif // USE(GTK4)
}

#if USE(GTK4) && ENABLE(QUICKIE_RELOCATABLE_GTK)
static void ensureQuickieInspectorStyles(GdkDisplay* display)
{
    constexpr auto* styleInstalled = "quickie-inspector-style-installed";
    if (g_object_get_data(G_OBJECT(display), styleInstalled))
        return;

    static constexpr auto style = R"CSS(
        .quickie-inspector-docked {
            border-radius: 16px;
            box-shadow: 0 20px 48px -10px rgba(0, 0, 0, 0.38), 0 2px 8px rgba(0, 0, 0, 0.16);
        }
        window.quickie-inspector-window.csd:not(.maximized):not(.fullscreen):not(.tiled) {
            border-radius: 16px;
        }
        window.quickie-inspector-window.csd:not(.maximized):not(.fullscreen):not(.tiled) headerbar.titlebar {
            border-radius: 16px 16px 0 0;
        }
        window.quickie-inspector-window.csd:not(.maximized):not(.fullscreen):not(.tiled) > .quickie-inspector-detached {
            border-radius: 0 0 16px 16px;
        }
    )CSS";
    auto provider = adoptGRef(gtk_css_provider_new());
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider.get(), style);
#else
    gtk_css_provider_load_from_data(provider.get(), style, -1);
#endif
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider.get()), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data(G_OBJECT(display), styleInstalled, GINT_TO_POINTER(1));
}

void webkitInspectorViewSetQuickieAppearance(GtkWidget* view, QuickieInspectorAppearance appearance)
{
    constexpr auto* originalOverflow = "quickie-inspector-original-overflow";
    gtk_widget_remove_css_class(view, "quickie-inspector-docked");
    gtk_widget_remove_css_class(view, "quickie-inspector-detached");

    if (appearance == QuickieInspectorAppearance::Unstyled) {
        if (auto overflow = g_object_steal_data(G_OBJECT(view), originalOverflow))
            gtk_widget_set_overflow(view, static_cast<GtkOverflow>(GPOINTER_TO_INT(overflow) - 1));
        return;
    }

    ensureQuickieInspectorStyles(gtk_widget_get_display(view));
    if (!g_object_get_data(G_OBJECT(view), originalOverflow))
        g_object_set_data(G_OBJECT(view), originalOverflow, GINT_TO_POINTER(gtk_widget_get_overflow(view) + 1));
    gtk_widget_add_css_class(view, appearance == QuickieInspectorAppearance::Docked ? "quickie-inspector-docked" : "quickie-inspector-detached");
    // GTK uses the same rounded CSS padding box for clipping and event picking.
    gtk_widget_set_overflow(view, GTK_OVERFLOW_HIDDEN);
}
#endif
