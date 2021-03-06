/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#define WL_EGL_PLATFORM 1
#include "integration.h"
#include "abstract_backend.h"
#include "backingstore.h"
#include "nativeinterface.h"
#include "platformcontextwayland.h"
#include "screen.h"
#include "sharingplatformcontext.h"
#include "window.h"
#include "../../main.h"
#include "../../wayland_server.h"

#include <KWayland/Client/compositor.h>
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/shell.h>
#include <KWayland/Client/surface.h>
#include <KWayland/Server/clientconnection.h>

#include <QCoreApplication>
#include <QtConcurrentRun>

#include <qpa/qplatformwindow.h>
#include <QtCore/private/qeventdispatcher_unix_p.h>
#include <QtPlatformSupport/private/qgenericunixfontdatabase_p.h>
#include <QtPlatformSupport/private/qgenericunixthemes_p.h>
#include <QtPlatformSupport/private/qunixeventdispatcher_qpa_p.h>

namespace KWin
{

namespace QPA
{

Integration::Integration()
    : QObject()
    , QPlatformIntegration()
    , m_fontDb(new QGenericUnixFontDatabase())
    , m_nativeInterface(new NativeInterface(this))
{
}

Integration::~Integration() = default;

bool Integration::hasCapability(Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps:
        return true;
    case OpenGL:
        return true;
    case ThreadedOpenGL:
        return false;
    case BufferQueueingOpenGL:
        return false;
    case MultipleWindows:
    case NonFullScreenWindows:
        return true;
    case RasterGLSurface:
        return false;
    default:
        return QPlatformIntegration::hasCapability(cap);
    }
}

void Integration::initialize()
{
    // TODO: start initialize Wayland once the internal Wayland connection is created
    connect(kwinApp(), &Application::screensCreated, this, &Integration::initializeWayland, Qt::QueuedConnection);
    QPlatformIntegration::initialize();
}

QAbstractEventDispatcher *Integration::createEventDispatcher() const
{
    return new QUnixEventDispatcherQPA;
}

QPlatformBackingStore *Integration::createPlatformBackingStore(QWindow *window) const
{
    auto registry = waylandServer()->internalClientRegistry();
    const auto shm = registry->interface(KWayland::Client::Registry::Interface::Shm);
    if (shm.name == 0u) {
        return nullptr;
    }
    return new BackingStore(window, registry->createShmPool(shm.name, shm.version, window));
}

QPlatformWindow *Integration::createPlatformWindow(QWindow *window) const
{
    auto c = compositor();
    auto s = shell();
    if (!s || !c) {
        return new QPlatformWindow(window);
    } else {
        // don't set window as parent, cause infinite recursion in PlasmaQuick::Dialog
        auto surface = c->createSurface(c);
        return new Window(window, surface, s->createSurface(surface, surface), this);
    }
}

QPlatformFontDatabase *Integration::fontDatabase() const
{
    return m_fontDb;
}

QPlatformTheme *Integration::createPlatformTheme(const QString &name) const
{
    return QGenericUnixTheme::createUnixTheme(name);
}

QStringList Integration::themeNames() const
{
    if (qEnvironmentVariableIsSet("KDE_FULL_SESSION")) {
        return QStringList({QStringLiteral("kde")});
    }
    return QStringList({QLatin1String(QGenericUnixTheme::name)});
}

QPlatformNativeInterface *Integration::nativeInterface() const
{
    return m_nativeInterface;
}

QPlatformOpenGLContext *Integration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    if (waylandServer()->backend()->supportsQpaContext()) {
        return new SharingPlatformContext(context, const_cast<Integration*>(this));
    }
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        const_cast<Integration*>(this)->initEgl();
    }
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        return nullptr;
    }
    return new PlatformContextWayland(context, const_cast<Integration*>(this));
}

void Integration::initializeWayland()
{
    if (m_registry) {
        return;
    }
    using namespace KWayland::Client;
    auto setupRegistry = [this] {
        m_registry = waylandServer()->internalClientRegistry();
        connect(m_registry, &Registry::outputAnnounced, this, &Integration::createWaylandOutput);
        const auto outputs = m_registry->interfaces(Registry::Interface::Output);
        for (const auto &o : outputs) {
            createWaylandOutput(o.name, o.version);
        }
    };
    if (waylandServer()->internalClientRegistry()) {
        setupRegistry();
    } else {
        connect(waylandServer()->internalClientConection(), &ConnectionThread::connected, this, setupRegistry, Qt::QueuedConnection);
    }
}

void Integration::createWaylandOutput(quint32 name, quint32 version)
{
    using namespace KWayland::Client;
    auto o = m_registry->createOutput(name, version, this);
    connect(o, &Output::changed, this,
        [this, o] {
            disconnect(o, &Output::changed, nullptr, nullptr);
            // TODO: handle screen removal
            screenAdded(new Screen(o));
        }
    );
    waylandServer()->internalClientConection()->flush();
}

KWayland::Client::Compositor *Integration::compositor() const
{
    if (!m_compositor) {
        using namespace KWayland::Client;
        auto registry = waylandServer()->internalClientRegistry();
        const auto c = registry->interface(Registry::Interface::Compositor);
        if (c.name != 0u) {
            const_cast<Integration*>(this)->m_compositor = registry->createCompositor(c.name, c.version, registry);
        }
    }
    return m_compositor;
}

KWayland::Client::Shell *Integration::shell() const
{
    if (!m_shell) {
        using namespace KWayland::Client;
        auto registry = waylandServer()->internalClientRegistry();
        const auto s = registry->interface(Registry::Interface::Shell);
        if (s.name != 0u) {
            const_cast<Integration*>(this)->m_shell = registry->createShell(s.name, s.version, registry);
        }
    }
    return m_shell;
}

EGLDisplay Integration::eglDisplay() const
{
    return m_eglDisplay;
}

void Integration::initEgl()
{
    Q_ASSERT(m_eglDisplay == EGL_NO_DISPLAY);
    // This variant uses Wayland as the EGL platform
    qputenv("EGL_PLATFORM", "wayland");
    m_eglDisplay = eglGetDisplay(waylandServer()->internalClientConection()->display());
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        return;
    }
    // call eglInitialize in a thread to not block
    QFuture<bool> future = QtConcurrent::run([this] () -> bool {
        EGLint major, minor;
        if (eglInitialize(m_eglDisplay, &major, &minor) == EGL_FALSE) {
            return false;
        }
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            return false;
        }
        return true;
    });
    // TODO: make this better
    while (!future.isFinished()) {
        waylandServer()->internalClientConection()->flush();
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
    }
    if (!future.result()) {
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
    }
}

}
}
