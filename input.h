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
#ifndef KWIN_INPUT_H
#define KWIN_INPUT_H
#include <kwinglobals.h>
#include <QAction>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QEvent>
#include <QWeakPointer>
#include <config-kwin.h>

class KGlobalAccelInterface;
class QKeySequence;

struct xkb_context;
struct xkb_keymap;
struct xkb_state;
typedef uint32_t xkb_mod_index_t;
typedef uint32_t xkb_keysym_t;

namespace KWin
{
class GlobalShortcutsManager;
class Toplevel;
class Xkb;
class InputEventFilter;
class PointerInputRedirection;

namespace Decoration
{
class DecoratedClientImpl;
}

namespace LibInput
{
    class Connection;
}

/**
 * @brief This class is responsible for redirecting incoming input to the surface which currently
 * has input or send enter/leave events.
 *
 * In addition input is intercepted before passed to the surfaces to have KWin internal areas
 * getting input first (e.g. screen edges) and filter the input event out if we currently have
 * a full input grab.
 *
 */
class KWIN_EXPORT InputRedirection : public QObject
{
    Q_OBJECT
public:
    enum PointerButtonState {
        PointerButtonReleased,
        PointerButtonPressed
    };
    enum PointerAxis {
        PointerAxisVertical,
        PointerAxisHorizontal
    };
    enum KeyboardKeyState {
        KeyboardKeyReleased,
        KeyboardKeyPressed
    };
    virtual ~InputRedirection();
    void init();

    /**
     * @return const QPointF& The current global pointer position
     */
    QPointF globalPointer() const;
    Qt::MouseButtons qtButtonStates() const;
    Qt::KeyboardModifiers keyboardModifiers() const;

    void registerShortcut(const QKeySequence &shortcut, QAction *action);
    /**
     * @overload
     *
     * Like registerShortcut, but also connects QAction::triggered to the @p slot on @p receiver.
     * It's recommended to use this method as it ensures that the X11 timestamp is updated prior
     * to the @p slot being invoked. If not using this overload it's required to ensure that
     * registerShortcut is called before connecting to QAction's triggered signal.
     **/
    template <typename T>
    void registerShortcut(const QKeySequence &shortcut, QAction *action, T *receiver, void (T::*slot)());
    void registerPointerShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton pointerButtons, QAction *action);
    void registerAxisShortcut(Qt::KeyboardModifiers modifiers, PointerAxisDirection axis, QAction *action);
    void registerGlobalAccel(KGlobalAccelInterface *interface);

    /**
     * @internal
     */
    void processPointerMotion(const QPointF &pos, uint32_t time);
    /**
     * @internal
     */
    void processPointerButton(uint32_t button, PointerButtonState state, uint32_t time);
    /**
     * @internal
     */
    void processPointerAxis(PointerAxis axis, qreal delta, uint32_t time);
    /**
     * @internal
     */
    void processKeyboardKey(uint32_t key, KeyboardKeyState state, uint32_t time);
    /**
     * @internal
     */
    void processKeyboardModifiers(uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked, uint32_t group);
    /**
     * @internal
     **/
    void processKeymapChange(int fd, uint32_t size);
    void processTouchDown(qint32 id, const QPointF &pos, quint32 time);
    void processTouchUp(qint32 id, quint32 time);
    void processTouchMotion(qint32 id, const QPointF &pos, quint32 time);
    void cancelTouch();
    void touchFrame();

    bool supportsPointerWarping() const;
    void warpPointer(const QPointF &pos);

    void uninstallInputEventFilter(InputEventFilter *filter);
    Toplevel *findToplevel(const QPoint &pos);
    GlobalShortcutsManager *shortcuts() const {
        return m_shortcuts;
    }
    void insertTouchId(quint32 internalId, qint32 kwaylandId);
    qint32 touchId(quint32 internalId);
    void removeTouchId(quint32 internalId);

    void updateKeyboardWindow();
    void updateTouchWindow(const QPointF &pos);

    QVector<InputEventFilter*> filters() const {
        return m_filters;
    }
    PointerInputRedirection *pointer() const {
        return m_pointer;
    }

Q_SIGNALS:
    /**
     * @brief Emitted when the global pointer position changed
     *
     * @param pos The new global pointer position.
     */
    void globalPointerChanged(const QPointF &pos);
    /**
     * @brief Emitted when the state of a pointer button changed.
     *
     * @param button The button which changed
     * @param state The new button state
     */
    void pointerButtonStateChanged(uint32_t button, InputRedirection::PointerButtonState state);
    /**
     * @brief Emitted when a pointer axis changed
     *
     * @param axis The axis on which the even occurred
     * @param delta The delta of the event.
     */
    void pointerAxisChanged(InputRedirection::PointerAxis axis, qreal delta);
    /**
     * @brief Emitted when the modifiers changes.
     *
     * Only emitted for the mask which is provided by Qt::KeyboardModifiers, if other modifiers
     * change signal is not emitted
     *
     * @param newMods The new modifiers state
     * @param oldMods The previous modifiers state
     */
    void keyboardModifiersChanged(Qt::KeyboardModifiers newMods, Qt::KeyboardModifiers oldMods);
    /**
     * @brief Emitted when the state of a key changed.
     *
     * @param keyCode The keycode of the key which changed
     * @param oldMods The new key state
     */
    void keyStateChanged(quint32 keyCode, InputRedirection::KeyboardKeyState state);

private:
    void setupLibInput();
    void setupLibInputWithScreens();
    void registerShortcutForGlobalAccelTimestamp(QAction *action);
    void updateFocusedTouchPosition();
    void setupWorkspace();
    void reconfigure();
    void setupInputFilters();
    void installInputEventFilter(InputEventFilter *filter);
    PointerInputRedirection *m_pointer;
    QScopedPointer<Xkb> m_xkb;
    /**
     * @brief The Toplevel which currently receives touch events
     */
    QWeakPointer<Toplevel> m_touchWindow;
    /**
     * external/kwayland
     **/
    QHash<qint32, qint32> m_touchIdMapper;

    GlobalShortcutsManager *m_shortcuts;

    LibInput::Connection *m_libInput = nullptr;

    QVector<InputEventFilter*> m_filters;

    KWIN_SINGLETON(InputRedirection)
    friend InputRedirection *input();
    friend class DecorationEventFilter;
    friend class InternalWindowEventFilter;
    friend class ForwardInputFilter;
};

/**
 * Base class for filtering input events inside InputRedirection.
 *
 * The idea behind the InputEventFilter is to have task oriented
 * filters. E.g. there is one filter taking care of a locked screen,
 * one to take care of interacting with window decorations, etc.
 *
 * A concrete subclass can reimplement the virtual methods and decide
 * whether an event should be filtered out or not by returning either
 * @c true or @c false. E.g. the lock screen filter can easily ensure
 * that all events are filtered out.
 *
 * As soon as a filter returns @c true the processing is stopped. If
 * a filter returns @c false the next one is invoked. This means a filter
 * installed early gets to see more events than a filter installed later on.
 *
 * Deleting an instance of InputEventFilter automatically uninstalls it from
 * InputRedirection.
 **/
class InputEventFilter
{
public:
    InputEventFilter();
    virtual ~InputEventFilter();

    /**
     * Event filter for pointer events which can be described by a QMouseEvent.
     *
     * Please note that the button translation in QMouseEvent cannot cover all
     * possible buttons. Because of that also the @p nativeButton code is passed
     * through the filter. For internal areas it's fine to use @p event, but for
     * passing to client windows the @p nativeButton should be used.
     *
     * @param event The event information about the move or button press/release
     * @param nativeButton The native key code of the button, for move events 0
     * @return @c true to stop further event processing, @c false to pass to next filter
     **/
    virtual bool pointerEvent(QMouseEvent *event, quint32 nativeButton);
    /**
     * Event filter for pointer axis events.
     *
     * @param event The event information about the axis event
     * @return @c true to stop further event processing, @c false to pass to next filter
     **/
    virtual bool wheelEvent(QWheelEvent *event);
    /**
     * Event filter for keyboard events.
     *
     * @param event The event information about the key event
     * @return @c tru to stop further event processing, @c false to pass to next filter.
     **/
    virtual bool keyEvent(QKeyEvent *event);
    virtual bool touchDown(quint32 id, const QPointF &pos, quint32 time);
    virtual bool touchMotion(quint32 id, const QPointF &pos, quint32 time);
    virtual bool touchUp(quint32 id, quint32 time);
};

class Xkb
{
public:
    Xkb(InputRedirection *input);
    ~Xkb();
    void installKeymap(int fd, uint32_t size);
    void updateModifiers(uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked, uint32_t group);
    void updateKey(uint32_t key, InputRedirection::KeyboardKeyState state);
    xkb_keysym_t toKeysym(uint32_t key);
    QString toString(xkb_keysym_t keysym);
    Qt::Key toQtKey(xkb_keysym_t keysym);
    Qt::KeyboardModifiers modifiers() const;

    quint32 getMods(quint32 components);
    quint32 getGroup();
private:
    void updateKeymap(xkb_keymap *keymap);
    void createKeymapFile();
    void updateModifiers();
    InputRedirection *m_input;
    xkb_context *m_context;
    xkb_keymap *m_keymap;
    xkb_state *m_state;
    xkb_mod_index_t m_shiftModifier;
    xkb_mod_index_t m_controlModifier;
    xkb_mod_index_t m_altModifier;
    xkb_mod_index_t m_metaModifier;
    Qt::KeyboardModifiers m_modifiers;
    struct {
        uint pressCount = 0;
        Qt::KeyboardModifier modifier = Qt::NoModifier;
    } m_modOnlyShortcut;
};

inline
InputRedirection *input()
{
    return InputRedirection::s_self;
}

template <typename T>
inline
void InputRedirection::registerShortcut(const QKeySequence &shortcut, QAction *action, T *receiver, void (T::*slot)()) {
    registerShortcut(shortcut, action);
    connect(action, &QAction::triggered, receiver, slot);
}

inline
Qt::KeyboardModifiers Xkb::modifiers() const
{
    return m_modifiers;
}

} // namespace KWin

Q_DECLARE_METATYPE(KWin::InputRedirection::KeyboardKeyState)
Q_DECLARE_METATYPE(KWin::InputRedirection::PointerButtonState)
Q_DECLARE_METATYPE(KWin::InputRedirection::PointerAxis)

#endif // KWIN_INPUT_H
