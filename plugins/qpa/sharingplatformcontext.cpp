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
#include "sharingplatformcontext.h"
#include "integration.h"
#include "window.h"
#include "../../abstract_backend.h"
#include "../../wayland_server.h"
#include "../../shell_client.h"

#include <QOpenGLFramebufferObject>

namespace KWin
{

namespace QPA
{

SharingPlatformContext::SharingPlatformContext(QOpenGLContext *context, Integration *integration)
    : AbstractPlatformContext(context, integration, waylandServer()->backend()->sceneEglDisplay())
{
    create();
}

bool SharingPlatformContext::makeCurrent(QPlatformSurface *surface)
{
    Window *window = static_cast<Window*>(surface);
    if (eglMakeCurrent(eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, context())) {
        window->bindContentFBO();
        return true;
    }
    return false;
}

bool SharingPlatformContext::isSharing() const
{
    return false;
}

void SharingPlatformContext::swapBuffers(QPlatformSurface *surface)
{
    Window *window = static_cast<Window*>(surface);
    auto c = window->shellClient();
    if (!c) {
        return;
    }
    makeCurrent(surface);
    glFlush();
    c->setInternalFramebufferObject(window->swapFBO());
    window->bindContentFBO();
}

GLuint SharingPlatformContext::defaultFramebufferObject(QPlatformSurface *surface) const
{
    if (Window *window = dynamic_cast<Window*>(surface)) {
        const auto &fbo = window->contentFBO();
        if (!fbo.isNull()) {
            return fbo->handle();
        }
    }
    return 0;
}

void SharingPlatformContext::create()
{
    if (config() == 0) {
        return;
    }
    if (!bindApi()) {
        return;
    }
    createContext(waylandServer()->backend()->sceneEglContext());
}

}
}
