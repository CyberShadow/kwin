/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#include "events.h"

#include <QSize>

namespace KWin
{
namespace LibInput
{

Event *Event::create(libinput_event *event)
{
    if (!event) {
        return nullptr;
    }
    const auto t = libinput_event_get_type(event);
    // TODO: add touch events
    // TODO: add device notify events
    switch (t) {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
        return new KeyEvent(event);
    case LIBINPUT_EVENT_POINTER_AXIS:
    case LIBINPUT_EVENT_POINTER_BUTTON:
    case LIBINPUT_EVENT_POINTER_MOTION:
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
        return new PointerEvent(event, t);
    case LIBINPUT_EVENT_TOUCH_DOWN:
    case LIBINPUT_EVENT_TOUCH_UP:
    case LIBINPUT_EVENT_TOUCH_MOTION:
    case LIBINPUT_EVENT_TOUCH_CANCEL:
    case LIBINPUT_EVENT_TOUCH_FRAME:
        return new TouchEvent(event, t);
    default:
        return new Event(event, t);
    }
}

Event::Event(libinput_event *event, libinput_event_type type)
    : m_event(event)
    , m_type(type)
{
}

Event::~Event()
{
    libinput_event_destroy(m_event);
}

libinput_device *Event::device() const
{
    return libinput_event_get_device(m_event);
}

KeyEvent::KeyEvent(libinput_event *event)
    : Event(event, LIBINPUT_EVENT_KEYBOARD_KEY)
    , m_keyboardEvent(libinput_event_get_keyboard_event(event))
{
}

KeyEvent::~KeyEvent() = default;

uint32_t KeyEvent::key() const
{
    return libinput_event_keyboard_get_key(m_keyboardEvent);
}

InputRedirection::KeyboardKeyState KeyEvent::state() const
{
    switch (libinput_event_keyboard_get_key_state(m_keyboardEvent)) {
    case LIBINPUT_KEY_STATE_PRESSED:
        return InputRedirection::KeyboardKeyPressed;
    case LIBINPUT_KEY_STATE_RELEASED:
        return InputRedirection::KeyboardKeyReleased;
    }
    abort();
}

uint32_t KeyEvent::time() const
{
    return libinput_event_keyboard_get_time(m_keyboardEvent);
}

PointerEvent::PointerEvent(libinput_event *event, libinput_event_type type)
    : Event(event, type)
    , m_pointerEvent(libinput_event_get_pointer_event(event))
{
}

PointerEvent::~PointerEvent() = default;

QPointF PointerEvent::absolutePos() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE);
    return QPointF(libinput_event_pointer_get_absolute_x(m_pointerEvent),
                   libinput_event_pointer_get_absolute_y(m_pointerEvent));
}

QPointF PointerEvent::absolutePos(const QSize &size) const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE);
    return QPointF(libinput_event_pointer_get_absolute_x_transformed(m_pointerEvent, size.width()),
                   libinput_event_pointer_get_absolute_y_transformed(m_pointerEvent, size.height()));
}

QPointF PointerEvent::delta() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_MOTION);
    return QPointF(libinput_event_pointer_get_dx(m_pointerEvent), libinput_event_pointer_get_dy(m_pointerEvent));
}

uint32_t PointerEvent::time() const
{
    return libinput_event_pointer_get_time(m_pointerEvent);
}

uint32_t PointerEvent::button() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_BUTTON);
    return libinput_event_pointer_get_button(m_pointerEvent);
}

InputRedirection::PointerButtonState PointerEvent::buttonState() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_BUTTON);
    switch (libinput_event_pointer_get_button_state(m_pointerEvent)) {
    case LIBINPUT_BUTTON_STATE_PRESSED:
        return InputRedirection::PointerButtonPressed;
    case LIBINPUT_BUTTON_STATE_RELEASED:
        return InputRedirection::PointerButtonReleased;
    }
    abort();
}

QVector<InputRedirection::PointerAxis> PointerEvent::axis() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_AXIS);
    QVector<InputRedirection::PointerAxis> a;
    if (libinput_event_pointer_has_axis(m_pointerEvent, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
        a << InputRedirection::PointerAxisHorizontal;
    }
    if (libinput_event_pointer_has_axis(m_pointerEvent, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
        a << InputRedirection::PointerAxisVertical;
    }
    return a;
}

qreal PointerEvent::axisValue(InputRedirection::PointerAxis axis) const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_POINTER_AXIS);
    const libinput_pointer_axis a = axis == InputRedirection::PointerAxisHorizontal
                                          ? LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL
                                          : LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL;
    return libinput_event_pointer_get_axis_value(m_pointerEvent, a);
}

TouchEvent::TouchEvent(libinput_event *event, libinput_event_type type)
    : Event(event, type)
    , m_touchEvent(libinput_event_get_touch_event(event))
{
}

TouchEvent::~TouchEvent() = default;

quint32 TouchEvent::time() const
{
    return libinput_event_touch_get_time(m_touchEvent);
}

QPointF TouchEvent::absolutePos() const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_TOUCH_DOWN || type() == LIBINPUT_EVENT_TOUCH_MOTION);
    return QPointF(libinput_event_touch_get_x(m_touchEvent),
                   libinput_event_touch_get_y(m_touchEvent));
}

QPointF TouchEvent::absolutePos(const QSize &size) const
{
    Q_ASSERT(type() == LIBINPUT_EVENT_TOUCH_DOWN || type() == LIBINPUT_EVENT_TOUCH_MOTION);
    return QPointF(libinput_event_touch_get_x_transformed(m_touchEvent, size.width()),
                   libinput_event_touch_get_y_transformed(m_touchEvent, size.height()));
}

qint32 TouchEvent::id() const
{
    Q_ASSERT(type() != LIBINPUT_EVENT_TOUCH_CANCEL && type() != LIBINPUT_EVENT_TOUCH_FRAME);
    return libinput_event_touch_get_slot(m_touchEvent);
}

}
}
