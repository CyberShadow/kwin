/********************************************************************
 This file is part of the KDE project.

 Copyright (C) 2012 Martin Gr��lin <mgraesslin@kde.org>
 Copyright (C) 2016 Marco Martin <mart@kde.org>

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
/*global effect, effects, animate, animationTime, Effect*/
var morphingEffect = {
    duration: animationTime(150),
    loadConfig: function () {
        "use strict";
        morphingEffect.duration = animationTime(150);
    },
    cleanup: function(window) {
        "use strict";
        //FIXME: this crashes badly?
       /* if (window.moveAnimation) {
            cancel(window.moveAnimation);
            delete window.moveAnimation;
        }
        if (window.fadeAnimation) {
            cancel(window.fadeAnimation);
            delete window.fadeAnimation;
        }*/
        window.olderGeometry = undefined;
    },
    geometryChange: function (window, oldGeometry) {
        "use strict";

        //only tooltips and notifications
        if (!window.tooltip && !window.notification) {
            return;
        }

        var newGeometry = window.geometry;

        //only do the transition for near enough tooltips,
        //don't cross the whole screen: ugly
        if (Math.abs(newGeometry.x - oldGeometry.x) > newGeometry.width * 4 ||
            Math.abs(newGeometry.y - oldGeometry.y) > newGeometry.height * 4) {
            return;
        //Also don't animate very small steps
        } else if (Math.abs(newGeometry.x - oldGeometry.x) < 10 &&
            Math.abs(newGeometry.y - oldGeometry.y) < 10 ) {
            return;
        }

        //don't resize it "too much", set as four times
        if ((newGeometry.width / oldGeometry.width) > 4 ||
            (newGeometry.width / oldGeometry.width) < 0.25 ||
            (newGeometry.height / oldGeometry.height) > 4 ||
            (newGeometry.height / oldGeometry.height) < 0.25) {
            return;
        }

        //WindowForceBackgroundContrastRole
        window.setData(7, true);
        //WindowForceBlurRole
        window.setData(5, true);

        if (window.olderGeometry !== undefined) {
            oldGeometry = window.olderGeometry;
        }
        if (window.oldGeometry !== undefined) {
            window.olderGeometry = window.oldGeometry;
        } else {
            window.olderGeometry = oldGeometry;
        }
        window.oldGeometry = newGeometry;

        var animProgress = 0;
        if (window.moveAnimation) {
            animProgress = progress(window.moveAnimation);

            //modify olderGeometry by the progress
            oldGeometry.x = oldGeometry.x + (newGeometry.x - oldGeometry.x) * animProgress;
            oldGeometry.y = oldGeometry.y + (newGeometry.y - oldGeometry.y) * animProgress;
            oldGeometry.width = oldGeometry.width + (newGeometry.width - oldGeometry.width) * animProgress;
            oldGeometry.height = oldGeometry.height + (newGeometry.height - oldGeometry.height) * animProgress;

            cancel(window.moveAnimation);
            delete window.moveAnimation;
        }
        if (window.fadeAnimation) {
            cancel(window.fadeAnimation);
            delete window.fadeAnimation;
        }

        var startX;
        if (window.olderGeometry.x + window.olderGeometry.width == newGeometry.x + newGeometry.width) {
            startX = (newGeometry.width - oldGeometry.width)/2;
        } else {
            startX = oldGeometry.x - newGeometry.x + (oldGeometry.width - newGeometry.width)/2
        }

        var startY;
        if (window.olderGeometry.y + window.olderGeometry.height == newGeometry.y + newGeometry.height) {
            startY = (newGeometry.height - oldGeometry.height)/2;
        } else {
            startY = oldGeometry.y - newGeometry.y + (oldGeometry.height - newGeometry.height)/2;
        }


        window.moveAnimation = animate({
            window: window,
            duration: morphingEffect.duration,
            animations: [{
                type: Effect.Size,
                to: {
                    value1: newGeometry.width,
                    value2: newGeometry.height
                },
                from: {
                    value1: oldGeometry.width,
                    value2: oldGeometry.height
                }
            }, {
                type: Effect.Translation,
                to: {
                    value1: 0,
                    value2: 0
                },
                from: {
                    value1: startX,
                    value2: startY
                }
            }]
        });

        //if an actual resize didn't happen, the old pixmap is useless, let's
        //not do a fade
        if (window.geometry.width != oldGeometry.width ||
            window.geometry.height != oldGeometry.height) {
            window.fadeAnimation = animate({
                window: window,
                duration: morphingEffect.duration,
                animations: [{
                    type: Effect.CrossFadePrevious,
                    to: 1.0,
                    from: 0.0
                }]
            });
        }

        window.oldGeometry = window.geometry;
        window.olderGeometry = oldGeometry;
    },
    init: function () {
        "use strict";
        effect.configChanged.connect(morphingEffect.loadConfig);
        effects.windowGeometryShapeChanged.connect(morphingEffect.geometryChange);
        effect.animationEnded.connect(morphingEffect.cleanup);
    }
};
morphingEffect.init();
