/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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

#include "cursor.h"
// kwin
#include <kwinglobals.h>
#include "input.h"
#include "main.h"
#include "utils.h"
#include "x11eventfilter.h"
#include "xcbutils.h"
// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
// Qt
#include <QAbstractEventDispatcher>
#include <QDBusConnection>
#include <QScreen>
#include <QTimer>
// xcb
#include <xcb/xfixes.h>
#include <xcb/xcb_cursor.h>
// X11
#include <X11/Xlib.h>
#if HAVE_X11_XINPUT
#include <X11/extensions/XInput2.h>
#else
#define XI_RawMotion 0
#endif
#include <fixx11h.h>

namespace KWin
{
Cursor *Cursor::s_self = nullptr;

Cursor *Cursor::create(QObject *parent)
{
    Q_ASSERT(!s_self);
#ifndef KCMRULES
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        s_self = new X11Cursor(parent);
    } else {
        s_self = new InputRedirectionCursor(parent);
    }
#else
    s_self = new X11Cursor(parent);
#endif
    return s_self;
}

Cursor::Cursor(QObject *parent)
    : QObject(parent)
    , m_mousePollingCounter(0)
    , m_cursorTrackingCounter(0)
    , m_themeName("default")
    , m_themeSize(24)
{
    loadThemeSettings();
    QDBusConnection::sessionBus().connect(QString(), QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"),
                                          QStringLiteral("notifyChange"), this, SLOT(slotKGlobalSettingsNotifyChange(int,int)));
}

Cursor::~Cursor()
{
    s_self = NULL;
}

void Cursor::loadThemeSettings()
{
    QString themeName = QString::fromUtf8(qgetenv("XCURSOR_THEME"));
    bool ok = false;
    // XCURSOR_SIZE might not be set (e.g. by startkde)
    uint themeSize = 0;
    if (qEnvironmentVariableIsSet("XCURSOR_SIZE")) {
        themeSize = qgetenv("XCURSOR_SIZE").toUInt(&ok);
    }
    if (!ok) {
        if (QScreen *s = QGuiApplication::primaryScreen()) {
            themeSize = s->logicalDotsPerInchY() * 16 / 72;
            ok = true;
        }
    }
    if (!themeName.isEmpty() && ok) {
        updateTheme(themeName, themeSize);
        return;
    }
    // didn't get from environment variables, read from config file
    loadThemeFromKConfig();
}

void Cursor::loadThemeFromKConfig()
{
    KConfigGroup mousecfg(KSharedConfig::openConfig("kcminputrc", KConfig::NoGlobals), "Mouse");
    const QString themeName = mousecfg.readEntry("cursorTheme", "default");
    bool ok = false;
    uint themeSize = mousecfg.readEntry("cursorSize", QString("24")).toUInt(&ok);
    if (!ok) {
        themeSize = 24;
    }
    updateTheme(themeName, themeSize);
}

void Cursor::updateTheme(const QString &name, int size)
{
    if (m_themeName != name || m_themeSize != size) {
        m_themeName = name;
        m_themeSize = size;
        emit themeChanged();
    }
}

void Cursor::slotKGlobalSettingsNotifyChange(int type, int arg)
{
    Q_UNUSED(arg)
    if (type == 5 /*CursorChanged*/) {
        loadThemeFromKConfig();
        // sync to environment
        qputenv("XCURSOR_THEME", m_themeName.toUtf8());
        qputenv("XCURSOR_SIZE", QByteArray::number(m_themeSize));
    }
}

QPoint Cursor::pos()
{
    s_self->doGetPos();
    return s_self->m_pos;
}

void Cursor::setPos(const QPoint &pos)
{
    // first query the current pos to not warp to the already existing pos
    if (pos == Cursor::pos()) {
        return;
    }
    s_self->m_pos = pos;
    s_self->doSetPos();
}

void Cursor::setPos(int x, int y)
{
    Cursor::setPos(QPoint(x, y));
}

xcb_cursor_t Cursor::getX11Cursor(Qt::CursorShape shape)
{
    Q_UNUSED(shape)
    return XCB_CURSOR_NONE;
}

xcb_cursor_t Cursor::getX11Cursor(const QByteArray &name)
{
    Q_UNUSED(name)
    return XCB_CURSOR_NONE;
}

xcb_cursor_t Cursor::x11Cursor(Qt::CursorShape shape)
{
    return s_self->getX11Cursor(shape);
}

xcb_cursor_t Cursor::x11Cursor(const QByteArray &name)
{
    return s_self->getX11Cursor(name);
}

void Cursor::doSetPos()
{
    emit posChanged(m_pos);
}

void Cursor::doGetPos()
{
}

void Cursor::updatePos(const QPoint &pos)
{
    if (m_pos == pos) {
        return;
    }
    m_pos = pos;
    emit posChanged(m_pos);
}

void Cursor::startMousePolling()
{
    ++m_mousePollingCounter;
    if (m_mousePollingCounter == 1) {
        doStartMousePolling();
    }
}

void Cursor::stopMousePolling()
{
    Q_ASSERT(m_mousePollingCounter > 0);
    --m_mousePollingCounter;
    if (m_mousePollingCounter == 0) {
        doStopMousePolling();
    }
}

void Cursor::doStartMousePolling()
{
}

void Cursor::doStopMousePolling()
{
}

void Cursor::startCursorTracking()
{
    ++m_cursorTrackingCounter;
    if (m_cursorTrackingCounter == 1) {
        doStartCursorTracking();
    }
}

void Cursor::stopCursorTracking()
{
    Q_ASSERT(m_cursorTrackingCounter > 0);
    --m_cursorTrackingCounter;
    if (m_cursorTrackingCounter == 0) {
        doStopCursorTracking();
    }
}

void Cursor::doStartCursorTracking()
{
}

void Cursor::doStopCursorTracking()
{
}

void Cursor::notifyCursorChanged(uint32_t serial)
{
    if (m_cursorTrackingCounter <= 0) {
        // cursor change tracking is currently disabled, so don't emit signal
        return;
    }
    emit cursorChanged(serial);
}

#ifndef KCMRULES
class XInputEventFilter : public X11EventFilter
{
public:
    XInputEventFilter(X11Cursor *parent, int xi_opcode)
        : X11EventFilter(XCB_GE_GENERIC, xi_opcode, XI_RawMotion)
        , m_x11Cursor(parent)
        {}
    virtual ~XInputEventFilter() = default;

    bool event(xcb_generic_event_t *event) override {
        Q_UNUSED(event)
        m_x11Cursor->schedulePoll();
        return false;
    }

private:
    X11Cursor *m_x11Cursor;
};
#endif

X11Cursor::X11Cursor(QObject *parent)
    : Cursor(parent)
    , m_timeStamp(XCB_TIME_CURRENT_TIME)
    , m_buttonMask(0)
    , m_resetTimeStampTimer(new QTimer(this))
    , m_mousePollingTimer(new QTimer(this))
    , m_hasXInput(false)
    , m_xiOpcode(0)
    , m_needsPoll(false)
{
    initXInput();
    m_resetTimeStampTimer->setSingleShot(true);
    connect(m_resetTimeStampTimer, SIGNAL(timeout()), SLOT(resetTimeStamp()));
    // TODO: How often do we really need to poll?
    m_mousePollingTimer->setInterval(50);
    connect(m_mousePollingTimer, SIGNAL(timeout()), SLOT(mousePolled()));

    connect(this, &Cursor::themeChanged, this, [this] { m_cursors.clear(); });
}

X11Cursor::~X11Cursor()
{
}

void X11Cursor::initXInput()
{
#ifndef KCMRULES
#if HAVE_X11_XINPUT
    if (qEnvironmentVariableIsSet("KWIN_NO_XI2")) {
        return;
    }
    Display *dpy = display();
    int xi_opcode, event, error;
    // init XInput extension
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
        return;
    }

    // verify that the XInput extension is at at least version 2.0
    int major = 2, minor = 0;
    int result = XIQueryVersion(dpy, &major, &minor);
    if (result == BadImplementation) {
        // Xinput 2.2 returns BadImplementation if checked against 2.0
        major = 2;
        minor = 2;
        if (XIQueryVersion(dpy, &major, &minor) != Success) {
            return;
        }
    } else if (result != Success) {
        return;
    }
    m_hasXInput = true;
    m_xiOpcode = xi_opcode;
#endif
#endif
}

void X11Cursor::doSetPos()
{
    const QPoint &pos = currentPos();
    xcb_warp_pointer(connection(), XCB_WINDOW_NONE, rootWindow(), 0, 0, 0, 0, pos.x(), pos.y());
    // call default implementation to emit signal
    Cursor::doSetPos();
}

void X11Cursor::doGetPos()
{
    if (m_timeStamp != XCB_TIME_CURRENT_TIME &&
            m_timeStamp == xTime()) {
        // time stamps did not change, no need to query again
        return;
    }
    m_timeStamp = xTime();
    Xcb::Pointer pointer(rootWindow());
    if (pointer.isNull()) {
        return;
    }
    m_buttonMask = pointer->mask;
    updatePos(pointer->root_x, pointer->root_y);
    m_resetTimeStampTimer->start(0);
}

void X11Cursor::resetTimeStamp()
{
    m_timeStamp = XCB_TIME_CURRENT_TIME;
}

void X11Cursor::aboutToBlock()
{
    if (m_needsPoll) {
        mousePolled();
        m_needsPoll = false;
    }
}

void X11Cursor::doStartMousePolling()
{
    if (m_hasXInput) {
#ifndef KCMRULES
#if HAVE_X11_XINPUT
        m_xiEventFilter.reset(new XInputEventFilter(this, m_xiOpcode));

        // this assumes KWin is the only one setting events on the root window
        // given Qt's source code this seems to be true. If it breaks, we need to change
        XIEventMask evmasks[1];
        unsigned char mask1[XIMaskLen(XI_LASTEVENT)];

        memset(mask1, 0, sizeof(mask1));

        XISetMask(mask1, XI_RawMotion);

        evmasks[0].deviceid = XIAllMasterDevices;
        evmasks[0].mask_len = sizeof(mask1);
        evmasks[0].mask = mask1;
        XISelectEvents(display(), rootWindow(), evmasks, 1);
        connect(qApp->eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, this, &X11Cursor::aboutToBlock);
#endif
#endif
    } else {
        m_mousePollingTimer->start();
    }
}

void X11Cursor::doStopMousePolling()
{
    if (m_hasXInput) {
#ifndef KCMRULES
#if HAVE_X11_XINPUT
        m_xiEventFilter.reset();

        XIEventMask evmasks[1];
        unsigned char mask1[(XI_LASTEVENT + 7)/8];

        memset(mask1, 0, sizeof(mask1));

        evmasks[0].deviceid = XIAllMasterDevices;
        evmasks[0].mask_len = sizeof(mask1);
        evmasks[0].mask = mask1;
        XISelectEvents(display(), rootWindow(), evmasks, 1);
        disconnect(qApp->eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, this, &X11Cursor::aboutToBlock);
#endif
#endif
    } else {
        m_mousePollingTimer->stop();
    }
}

void X11Cursor::doStartCursorTracking()
{
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
}

void X11Cursor::doStopCursorTracking()
{
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), 0);
}

void X11Cursor::mousePolled()
{
    static QPoint lastPos = currentPos();
    static uint16_t lastMask = m_buttonMask;
    doGetPos(); // Update if needed
    if (lastPos != currentPos() || lastMask != m_buttonMask) {
        emit mouseChanged(currentPos(), lastPos,
            x11ToQtMouseButtons(m_buttonMask), x11ToQtMouseButtons(lastMask),
            x11ToQtKeyboardModifiers(m_buttonMask), x11ToQtKeyboardModifiers(lastMask));
        lastPos = currentPos();
        lastMask = m_buttonMask;
    }
}

xcb_cursor_t X11Cursor::getX11Cursor(Qt::CursorShape shape)
{
    return getX11Cursor(cursorName(shape));
}

xcb_cursor_t X11Cursor::getX11Cursor(const QByteArray &name)
{
    auto it = m_cursors.constFind(name);
    if (it != m_cursors.constEnd()) {
        return it.value();
    }
    return createCursor(name);
}

xcb_cursor_t X11Cursor::createCursor(const QByteArray &name)
{
    if (name.isEmpty()) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_context_t *ctx;
    if (xcb_cursor_context_new(connection(), defaultScreen(), &ctx) < 0) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_t cursor = xcb_cursor_load_cursor(ctx, name.constData());
    if (cursor == XCB_CURSOR_NONE) {
        static const QHash<QByteArray, QVector<QByteArray>> alternatives = {
            {QByteArrayLiteral("left_ptr"),       {QByteArrayLiteral("arrow"),
                                                   QByteArrayLiteral("dnd-none"),
                                                   QByteArrayLiteral("op_left_arrow")}},
            {QByteArrayLiteral("cross"),          {QByteArrayLiteral("crosshair"),
                                                   QByteArrayLiteral("diamond-cross"),
                                                   QByteArrayLiteral("cross-reverse")}},
            {QByteArrayLiteral("up_arrow"),       {QByteArrayLiteral("center_ptr"),
                                                   QByteArrayLiteral("sb_up_arrow"),
                                                   QByteArrayLiteral("centre_ptr")}},
            {QByteArrayLiteral("wait"),           {QByteArrayLiteral("watch"),
                                                   QByteArrayLiteral("progress")}},
            {QByteArrayLiteral("ibeam"),          {QByteArrayLiteral("xterm"),
                                                   QByteArrayLiteral("text")}},
            {QByteArrayLiteral("size_all"),       {QByteArrayLiteral("fleur")}},
            {QByteArrayLiteral("pointing_hand"),  {QByteArrayLiteral("hand2"),
                                                   QByteArrayLiteral("hand"),
                                                   QByteArrayLiteral("hand1"),
                                                   QByteArrayLiteral("pointer"),
                                                   QByteArrayLiteral("e29285e634086352946a0e7090d73106"),
                                                   QByteArrayLiteral("9d800788f1b08800ae810202380a0822")}},
            {QByteArrayLiteral("size_ver"),       {QByteArrayLiteral("00008160000006810000408080010102"),
                                                   QByteArrayLiteral("sb_v_double_arrow"),
                                                   QByteArrayLiteral("v_double_arrow"),
                                                   QByteArrayLiteral("n-resize"),
                                                   QByteArrayLiteral("s-resize"),
                                                   QByteArrayLiteral("col-resize"),
                                                   QByteArrayLiteral("top_side"),
                                                   QByteArrayLiteral("bottom_side"),
                                                   QByteArrayLiteral("base_arrow_up"),
                                                   QByteArrayLiteral("base_arrow_down"),
                                                   QByteArrayLiteral("based_arrow_down"),
                                                   QByteArrayLiteral("based_arrow_up")}},
            {QByteArrayLiteral("size_hor"),       {QByteArrayLiteral("028006030e0e7ebffc7f7070c0600140"),
                                                   QByteArrayLiteral("sb_h_double_arrow"),
                                                   QByteArrayLiteral("h_double_arrow"),
                                                   QByteArrayLiteral("e-resize"),
                                                   QByteArrayLiteral("w-resize"),
                                                   QByteArrayLiteral("row-resize"),
                                                   QByteArrayLiteral("right_side"),
                                                   QByteArrayLiteral("left_side")}},
            {QByteArrayLiteral("size_bdiag"),     {QByteArrayLiteral("fcf1c3c7cd4491d801f1e1c78f100000"),
                                                   QByteArrayLiteral("fd_double_arrow"),
                                                   QByteArrayLiteral("bottom_left_corner"),
                                                   QByteArrayLiteral("top_right_corner")}},
            {QByteArrayLiteral("size_fdiag"),     {QByteArrayLiteral("c7088f0f3e6c8088236ef8e1e3e70000"),
                                                   QByteArrayLiteral("bd_double_arrow"),
                                                   QByteArrayLiteral("bottom_right_corner"),
                                                   QByteArrayLiteral("top_left_corner")}},
            {QByteArrayLiteral("whats_this"),     {QByteArrayLiteral("d9ce0ab605698f320427677b458ad60b"),
                                                   QByteArrayLiteral("left_ptr_help"),
                                                   QByteArrayLiteral("help"),
                                                   QByteArrayLiteral("question_arrow"),
                                                   QByteArrayLiteral("dnd-ask"),
                                                   QByteArrayLiteral("5c6cd98b3f3ebcb1f9c7f1c204630408")}},
            {QByteArrayLiteral("split_h"),        {QByteArrayLiteral("14fef782d02440884392942c11205230"),
                                                   QByteArrayLiteral("size_hor")}},
            {QByteArrayLiteral("split_v"),        {QByteArrayLiteral("2870a09082c103050810ffdffffe0204"),
                                                   QByteArrayLiteral("size_ver")}},
            {QByteArrayLiteral("forbidden"),      {QByteArrayLiteral("03b6e0fcb3499374a867c041f52298f0"),
                                                   QByteArrayLiteral("circle"),
                                                   QByteArrayLiteral("dnd-no-drop"),
                                                   QByteArrayLiteral("not-allowed")}},
            {QByteArrayLiteral("left_ptr_watch"), {QByteArrayLiteral("3ecb610c1bf2410f44200f48c40d3599"),
                                                   QByteArrayLiteral("00000000000000020006000e7e9ffc3f"),
                                                   QByteArrayLiteral("08e8e1c95fe2fc01f976f1e063a24ccd")}},
            {QByteArrayLiteral("openhand"),       {QByteArrayLiteral("9141b49c8149039304290b508d208c40"),
                                                   QByteArrayLiteral("all_scroll"),
                                                   QByteArrayLiteral("all-scroll")}},
            {QByteArrayLiteral("closedhand"),     {QByteArrayLiteral("05e88622050804100c20044008402080"),
                                                   QByteArrayLiteral("4498f0e0c1937ffe01fd06f973665830"),
                                                   QByteArrayLiteral("9081237383d90e509aa00f00170e968f"),
                                                   QByteArrayLiteral("fcf21c00b30f7e3f83fe0dfd12e71cff")}},
            {QByteArrayLiteral("dnd-link"),       {QByteArrayLiteral("link"),
                                                   QByteArrayLiteral("alias"),
                                                   QByteArrayLiteral("3085a0e285430894940527032f8b26df"),
                                                   QByteArrayLiteral("640fb0e74195791501fd1ed57b41487f"),
                                                   QByteArrayLiteral("a2a266d0498c3104214a47bd64ab0fc8")}},
            {QByteArrayLiteral("dnd-copy"),       {QByteArrayLiteral("copy"),
                                                   QByteArrayLiteral("1081e37283d90000800003c07f3ef6bf"),
                                                   QByteArrayLiteral("6407b0e94181790501fd1e167b474872"),
                                                   QByteArrayLiteral("b66166c04f8c3109214a4fbd64a50fc8")}},
            {QByteArrayLiteral("dnd-move"),       {QByteArrayLiteral("move")}}
        };
        auto it = alternatives.find(name);
        if (it != alternatives.end()) {
            const auto &names = it.value();
            for (auto cit = names.begin(); cit != names.end(); ++cit) {
                cursor = xcb_cursor_load_cursor(ctx, (*cit).constData());
                if (cursor != XCB_CURSOR_NONE) {
                    break;
                }
            }
        }
    }
    if (cursor != XCB_CURSOR_NONE) {
        m_cursors.insert(name, cursor);
    }
    xcb_cursor_context_free(ctx);
    return cursor;
}

QByteArray Cursor::cursorName(Qt::CursorShape shape) const
{
    switch (shape) {
    case Qt::ArrowCursor:
        return QByteArray("left_ptr");
    case Qt::UpArrowCursor:
        return QByteArray("up_arrow");
    case Qt::CrossCursor:
        return QByteArray("cross");
    case Qt::WaitCursor:
        return QByteArray("wait");
    case Qt::IBeamCursor:
        return QByteArray("ibeam");
    case Qt::SizeVerCursor:
        return QByteArray("size_ver");
    case Qt::SizeHorCursor:
        return QByteArray("size_hor");
    case Qt::SizeBDiagCursor:
        return QByteArray("size_bdiag");
    case Qt::SizeFDiagCursor:
        return QByteArray("size_fdiag");
    case Qt::SizeAllCursor:
        return QByteArray("size_all");
    case Qt::SplitVCursor:
        return QByteArray("split_v");
    case Qt::SplitHCursor:
        return QByteArray("split_h");
    case Qt::PointingHandCursor:
        return QByteArray("pointing_hand");
    case Qt::ForbiddenCursor:
        return QByteArray("forbidden");
    case Qt::OpenHandCursor:
        return QByteArray("openhand");
    case Qt::ClosedHandCursor:
        return QByteArray("closedhand");
    case Qt::WhatsThisCursor:
        return QByteArray("whats_this");
    case Qt::BusyCursor:
        return QByteArray("left_ptr_watch");
    case Qt::DragMoveCursor:
        return QByteArray("dnd-move");
    case Qt::DragCopyCursor:
        return QByteArray("dnd-copy");
    case Qt::DragLinkCursor:
        return QByteArray("dnd-link");
    default:
        return QByteArray();
    }
}

InputRedirectionCursor::InputRedirectionCursor(QObject *parent)
    : Cursor(parent)
    , m_currentButtons(Qt::NoButton)
{
    connect(input(), SIGNAL(globalPointerChanged(QPointF)), SLOT(slotPosChanged(QPointF)));
    connect(input(), SIGNAL(pointerButtonStateChanged(uint32_t,InputRedirection::PointerButtonState)),
            SLOT(slotPointerButtonChanged()));
#ifndef KCMRULES
    connect(input(), &InputRedirection::keyboardModifiersChanged,
            this, &InputRedirectionCursor::slotModifiersChanged);
    connect(kwinApp(), &Application::x11ConnectionChanged, this,
        [this] {
            if (isCursorTracking()) {
                doStartCursorTracking();
            }
        }, Qt::QueuedConnection
    );
#endif
}

InputRedirectionCursor::~InputRedirectionCursor()
{
}

void InputRedirectionCursor::doSetPos()
{
    if (input()->supportsPointerWarping()) {
        input()->warpPointer(currentPos());
    }
    slotPosChanged(input()->globalPointer());
    emit posChanged(currentPos());
}

void InputRedirectionCursor::slotPosChanged(const QPointF &pos)
{
    const QPoint oldPos = currentPos();
    updatePos(pos.toPoint());
    emit mouseChanged(pos.toPoint(), oldPos, m_currentButtons, m_currentButtons,
                      input()->keyboardModifiers(), input()->keyboardModifiers());
}

void InputRedirectionCursor::slotModifiersChanged(Qt::KeyboardModifiers mods, Qt::KeyboardModifiers oldMods)
{
    emit mouseChanged(currentPos(), currentPos(), m_currentButtons, m_currentButtons, mods, oldMods);
}

void InputRedirectionCursor::slotPointerButtonChanged()
{
    const Qt::MouseButtons oldButtons = m_currentButtons;
    m_currentButtons = input()->qtButtonStates();
    const QPoint pos = currentPos();
    emit mouseChanged(pos, pos, m_currentButtons, oldButtons, input()->keyboardModifiers(), input()->keyboardModifiers());
}

void InputRedirectionCursor::doStartCursorTracking()
{
    if (!kwinApp()->x11Connection()) {
        return;
    }
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
    // TODO: also track the Wayland cursor
}

void InputRedirectionCursor::doStopCursorTracking()
{
    if (!kwinApp()->x11Connection()) {
        return;
    }
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), 0);
    // TODO: also track the Wayland cursor
}

} // namespace
