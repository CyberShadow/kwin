/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "egl_hwcomposer_backend.h"
#include "hwcomposer_backend.h"
#include "logging.h"
#include "screens_hwcomposer.h"
#include "composite.h"
#include "input.h"
#include "virtual_terminal.h"
#include "wayland_server.h"
// KWayland
#include <KWayland/Server/display.h>
#include <KWayland/Server/output_interface.h>
#include <KWayland/Server/seat_interface.h>
// hybris/android
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/lights.h>
// linux
#include <linux/input.h>

// based on test_hwcomposer.c from libhybris project (Apache 2 licensed)

namespace KWin
{

HwcomposerBackend::HwcomposerBackend(QObject *parent)
    : AbstractBackend(parent)
{
    handleOutputs();
}

HwcomposerBackend::~HwcomposerBackend()
{
    if (!m_outputBlank) {
        toggleBlankOutput();
    }
    if (m_device) {
        hwc_close_1(m_device);
    }
}

static KWayland::Server::OutputInterface *createOutput(hwc_composer_device_1_t *device)
{
    uint32_t configs[5];
    size_t numConfigs = 5;
    if (device->getDisplayConfigs(device, 0, configs, &numConfigs) != 0) {
        qCWarning(KWIN_HWCOMPOSER) << "Failed to get hwcomposer display configurations";
        return nullptr;
    }

    int32_t attr_values[5];
    uint32_t attributes[] = {
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_VSYNC_PERIOD ,
        HWC_DISPLAY_NO_ATTRIBUTE
    };
    device->getDisplayAttributes(device, 0, configs[0], attributes, attr_values);
    QSize pixel(attr_values[0], attr_values[1]);
    if (pixel.isEmpty()) {
        return nullptr;
    }

    using namespace KWayland::Server;
    OutputInterface *o = waylandServer()->display()->createOutput(waylandServer()->display());
    o->addMode(pixel, OutputInterface::ModeFlag::Current | OutputInterface::ModeFlag::Preferred, (attr_values[4] == 0) ? 60000 : 10E11/attr_values[4]);

    if (attr_values[2] != 0 && attr_values[3] != 0) {
         static const qreal factor = 25.4;
         o->setPhysicalSize(QSizeF(qreal(pixel.width() * 1000) / qreal(attr_values[2]) * factor,
                                   qreal(pixel.height() * 1000) / qreal(attr_values[3]) * factor).toSize());
    } else {
         // couldn't read physical size, assume 96 dpi
         o->setPhysicalSize(pixel / 3.8);
    }
    o->create();
    return o;
}

void HwcomposerBackend::init()
{
    hw_module_t *hwcModule = nullptr;
    if (hw_get_module(HWC_HARDWARE_MODULE_ID, (const hw_module_t **)&hwcModule) != 0) {
        qCWarning(KWIN_HWCOMPOSER) << "Failed to get hwcomposer module";
        emit initFailed();
        return;
    }

    hwc_composer_device_1_t *hwcDevice = nullptr;
    if (hwc_open_1(hwcModule, &hwcDevice) != 0) {
        qCWarning(KWIN_HWCOMPOSER) << "Failed to open hwcomposer device";
        emit initFailed();
        return;
    }

    // unblank, setPowerMode?
    m_device = hwcDevice;

    // register callbacks
    hwc_procs_t *procs = new hwc_procs_t;
    procs->invalidate = [] (const struct hwc_procs* procs) {
        Q_UNUSED(procs)
    };
    procs->vsync = [] (const struct hwc_procs* procs, int disp, int64_t timestamp) {
        Q_UNUSED(procs)
        if (disp != 0) {
            return;
        }
        dynamic_cast<HwcomposerBackend*>(waylandServer()->backend())->wakeVSync();
    };
    procs->hotplug = [] (const struct hwc_procs* procs, int disp, int connected) {
        Q_UNUSED(procs)
        Q_UNUSED(disp)
        Q_UNUSED(connected)
    };
    m_device->registerProcs(m_device, procs);

    initLights();
    toggleBlankOutput();
    connect(input(), &InputRedirection::keyStateChanged, this,
        [this] (quint32 key, InputRedirection::KeyboardKeyState state) {
            if (state != InputRedirection::KeyboardKeyState::KeyboardKeyReleased) {
                return;
            }
            if (key == KEY_POWER) {
                toggleBlankOutput();
            }
        }
    );

    // get display configuration
    auto output = createOutput(hwcDevice);
    if (!output) {
        emit initFailed();
        return;
    }
    m_displaySize = output->pixelSize();
    m_refreshRate = output->refreshRate();
    if (m_refreshRate != 0) {
        m_vsyncInterval = 1000000/m_refreshRate;
    }
    if (m_lights) {
        using namespace KWayland::Server;
        output->setDpmsSupported(true);
        auto updateDpms = [this, output] {
            output->setDpmsMode(m_outputBlank ? OutputInterface::DpmsMode::Off : OutputInterface::DpmsMode::On);
        };
        updateDpms();
        connect(this, &HwcomposerBackend::outputBlankChanged, this, updateDpms);
        connect(output, &OutputInterface::dpmsModeRequested, this,
            [this] (KWayland::Server::OutputInterface::DpmsMode mode) {
                if (mode == OutputInterface::DpmsMode::On) {
                    if (m_outputBlank) {
                        toggleBlankOutput();
                    }
                } else {
                    if (!m_outputBlank) {
                        toggleBlankOutput();
                    }
                }
            }
        );
    }
    qCDebug(KWIN_HWCOMPOSER) << "Display size:" << m_displaySize;
    qCDebug(KWIN_HWCOMPOSER) << "Refresh rate:" << m_refreshRate;

    VirtualTerminal::create(this);
    VirtualTerminal::self()->init();
    emit screensQueried();
    setReady(true);
}

void HwcomposerBackend::initLights()
{
    hw_module_t *lightsModule = nullptr;
    if (hw_get_module(LIGHTS_HARDWARE_MODULE_ID, (const hw_module_t **)&lightsModule) != 0) {
        qCWarning(KWIN_HWCOMPOSER) << "Failed to get lights module";
        return;
    }
    light_device_t *lightsDevice = nullptr;
    if (lightsModule->methods->open(lightsModule, LIGHT_ID_BACKLIGHT, (hw_device_t **)&lightsDevice) != 0) {
        qCWarning(KWIN_HWCOMPOSER) << "Failed to create lights device";
        return;
    }
    m_lights = lightsDevice;
}

void HwcomposerBackend::toggleBlankOutput()
{
    if (!m_device) {
        return;
    }
    m_outputBlank = !m_outputBlank;
    toggleScreenBrightness();
    m_device->blank(m_device, 0, m_outputBlank ? 1 : 0);
    // only disable Vsycn, enable happens after next frame rendered
    if (m_outputBlank) {
         enableVSync(false);
    }
    // enable/disable compositor repainting when blanked
    setOutputsEnabled(!m_outputBlank);
    if (Compositor *compositor = Compositor::self()) {
        if (!m_outputBlank) {
            compositor->addRepaintFull();
        }
    }
    emit outputBlankChanged();
}

void HwcomposerBackend::toggleScreenBrightness()
{
    if (!m_lights) {
        return;
    }
    const int brightness = m_outputBlank ? 0 : 0xFF;
    struct light_state_t state;
    state.flashMode = LIGHT_FLASH_NONE;
    state.brightnessMode = BRIGHTNESS_MODE_USER;

    state.color = (int)((0xffU << 24) | (brightness << 16) |
                        (brightness << 8) | brightness);
    m_lights->set_light(m_lights, &state);
}

void HwcomposerBackend::enableVSync(bool enable)
{
    if (m_hasVsync == enable) {
        return;
    }
    const int result = m_device->eventControl(m_device, 0, HWC_EVENT_VSYNC, enable ? 1: 0);
    m_hasVsync = enable && (result == 0);
}

HwcomposerWindow *HwcomposerBackend::createSurface()
{
    return new HwcomposerWindow(this);
}

Screens *HwcomposerBackend::createScreens(QObject *parent)
{
    return new HwcomposerScreens(this, parent);
}

OpenGLBackend *HwcomposerBackend::createOpenGLBackend()
{
    return new EglHwcomposerBackend(this);
}

void HwcomposerBackend::waitVSync()
{
    if (!m_hasVsync) {
         return;
    }
    m_vsyncMutex.lock();
    m_vsyncWaitCondition.wait(&m_vsyncMutex, m_vsyncInterval);
    m_vsyncMutex.unlock();
}

void HwcomposerBackend::wakeVSync()
{
    m_vsyncMutex.lock();
    m_vsyncWaitCondition.wakeAll();
    m_vsyncMutex.unlock();
}

static void initLayer(hwc_layer_1_t *layer, const hwc_rect_t &rect)
{
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = rect;
    layer->displayFrame = rect;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;
    layer->planeAlpha = 0xFF;
}

HwcomposerWindow::HwcomposerWindow(HwcomposerBackend *backend)
    : HWComposerNativeWindow(backend->size().width(), backend->size().height(), HAL_PIXEL_FORMAT_RGB_888)
    , m_backend(backend)
{
    setBufferCount(3);

    size_t size = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
    hwc_display_contents_1_t *list = (hwc_display_contents_1_t*)malloc(size);
    m_list = (hwc_display_contents_1_t**)malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
    for (int i = 0; i < HWC_NUM_DISPLAY_TYPES; ++i) {
        m_list[i] = nullptr;
    }
    // Assign buffer only to the first item, otherwise you get tearing
    // if passed the same to multiple places
    // see https://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin/commit/f1d802151e8a4f5d10d60eb8de8e07552b93a34a
    m_list[0] = list;
    const hwc_rect_t rect = {
        0,
        0,
        m_backend->size().width(),
        m_backend->size().height()
    };
    initLayer(&list->hwLayers[0], rect);
    initLayer(&list->hwLayers[1], rect);

    list->retireFenceFd = -1;
    list->flags = HWC_GEOMETRY_CHANGED;
    list->numHwLayers = 2;
}

HwcomposerWindow::~HwcomposerWindow()
{
    // TODO: cleanup
}

void HwcomposerWindow::present(HWComposerNativeWindowBuffer *buffer)
{
    m_backend->waitVSync();
    hwc_composer_device_1_t *device = m_backend->device();

    auto fblayer = &m_list[0]->hwLayers[1];
    fblayer->handle = buffer->handle;
    fblayer->acquireFenceFd = getFenceBufferFd(buffer);
    fblayer->releaseFenceFd = -1;

    int err = device->prepare(device, 1, m_list);
    assert(err == 0);

    err = device->set(device, 1, m_list);
    assert(err == 0);
    m_backend->enableVSync(true);
    setFenceBufferFd(buffer, fblayer->releaseFenceFd);

    if (m_list[0]->retireFenceFd != -1) {
        close(m_list[0]->retireFenceFd);
        m_list[0]->retireFenceFd = -1;
    }
    m_list[0]->flags = 0;
}

}
