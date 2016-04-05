#ifndef QXTWINDOWSYSTEM_H
/****************************************************************************
** Copyright (c) 2006 - 2011, the LibQxt project.
** See the Qxt AUTHORS file for a list of authors and copyright holders.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the LibQxt project nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
** <http://libqxt.org>  <foundation@libqxt.org>
*****************************************************************************/

#define QXTWINDOWSYSTEM_H

#include <QWidget>
#include "qxtglobal.h"

typedef QList<WId> WindowList;

class QXT_GUI_EXPORT QxtWindowSystem
{
public:

    static void init();

    static WindowList windows();
    static WId activeWindow();
    static int activeWindow(WId window);
    static int iconifyWindow(WId window);
    static int unmaxizeWindow(WId window);
    static int moveResizeWindow(WId window, QRect const& geom);
    static bool isVisible(WId window);
    static WId findWindow(const QString& title);
    static WId windowAt(const QPoint& pos);
    static QString windowTitle(WId window);
    static QStringList windowTitles();
    static QRect windowGeometry(WId window);
    static uint32_t applicationPID(WId window);
    static QString executablePath(uint32_t pid);

    static void setWindowProperty( WId window,
                                   QString const& key,
                                   QString const& val );
    static QString getWindowProperty(WId window, QString const& key);

    static uint idleTime();
};

#endif // QXTWINDOWSYSTEM_H
