#include "qxtwindowsystem.h"
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

#include <QLibrary>
#include <QX11Info>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <iostream>

static WindowList qxt_getWindows(Atom prop)
{
  QxtWindowSystem::init();
    Display* display = QX11Info::display();
    Window window = QX11Info::appRootWindow();

    WindowList res;
    Atom type = 0;
    int format = 0;
    uchar* data = 0;
    ulong count, after;

    if( XGetWindowProperty(display, window, prop, 0, 1024 * sizeof(Window) / 4, False, AnyPropertyType,
                           &type, &format, &count, &after, &data) == Success )
    {
        Window* list = reinterpret_cast<Window*>(data);
        for( uint i = 0; i < count; ++i )
          res += list[i];
        if( data )
            XFree(data);
    }
    return res;
}

static int qxtX11ErrorHandler(Display* dpy, XErrorEvent* error)
{
  const size_t msg_size = 256;
  char msg[msg_size];
  if( !XGetErrorText(dpy, error->error_code, msg, msg_size) )
    return 1;

  msg[msg_size - 1] = 0;
  std::cout << "XError: " << msg << std::endl;
  return 0;
}

void QxtWindowSystem::init()
{
  static bool initDone = false;
  if( !initDone )
  {
    XSetErrorHandler(&qxtX11ErrorHandler);
    initDone = true;
  }
}

WindowList QxtWindowSystem::windows()
{
    static Atom net_clients = 0;
    if (!net_clients)
        net_clients = XInternAtom(QX11Info::display(), "_NET_CLIENT_LIST_STACKING", True);

    return qxt_getWindows(net_clients);
}

static Atom atomNetActive()
{
  static Atom net_active = 0;
  if (!net_active)
      net_active = XInternAtom(QX11Info::display(), "_NET_ACTIVE_WINDOW", True);
  return net_active;
}

WId QxtWindowSystem::activeWindow()
{
    return qxt_getWindows(atomNetActive()).value(0);
}

int QxtWindowSystem::activeWindow(WId window)
{
    init();
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.display = QX11Info::display();
    ev.xclient.window = window;
    ev.xclient.message_type = atomNetActive();
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2L; /* 2 == Message from a window pager */
    ev.xclient.data.l[1] = CurrentTime;

    XWindowAttributes attr;
    XGetWindowAttributes(QX11Info::display(), window, &attr);
    return XSendEvent(
      QX11Info::display(),
      attr.screen->root,
      False,
      SubstructureNotifyMask | SubstructureRedirectMask,
      &ev
    ) == 0;
}

bool QxtWindowSystem::isVisible(WId wid)
{
  init();
  Display* display = QX11Info::display();
  bool hidden = false;

  static Atom wm_state  = XInternAtom(display, "_NET_WM_STATE", True),
              wm_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", True);

  Atom type = 0;
  int format = 0;
  uchar* data = 0;
  ulong count, after;

  // Check visibility
  if( XGetWindowProperty( display, wid, wm_state,
                          0, (~0L), False, AnyPropertyType,
                          &type, &format, &count, &after, &data ) == Success )
  {
    Atom* props = reinterpret_cast<Atom*>(data);
    for(size_t j = 0; j < count; ++j)
      if( props[j] == wm_hidden )
      {
        hidden = true;
        break;
      }
  }

  if( data )
    XFree(data);

  return hidden;
}

WId QxtWindowSystem::findWindow(const QString& title)
{
    Window result = 0;
    WindowList list = windows();
    foreach (const Window &wid, list)
    {
        if (windowTitle(wid) == title)
        {
            result = wid;
            break;
        }
    }
    return result;
}

WId QxtWindowSystem::windowAt(const QPoint& pos)
{
    Window result = 0;
    WindowList list = windows();
    for (int i = list.size() - 1; i >= 0; --i)
    {
        WId wid = list.at(i);
        if (windowGeometry(wid).contains(pos))
        {
            result = wid;
            break;
        }
    }
    return result;
}

QString QxtWindowSystem::windowTitle(WId window)
{
    init();
    XTextProperty prop;
    if( XGetWMName(QX11Info::display(), window, &prop) )
      return QString::fromLocal8Bit( reinterpret_cast<char*>(prop.value),
                                     prop.nitems );

    QString name;
    char* str = 0;
    if (XFetchName(QX11Info::display(), window, &str))
        name = QString::fromUtf8(str);
    if (str)
        XFree(str);

    return name;
}

QRect QxtWindowSystem::windowGeometry(WId window)
{
    init();
    int x, y;
    uint width, height, border, depth;
    Window root, child;
    Display* display = QX11Info::display();
    if( !XGetGeometry(display, window, &root, &x, &y, &width, &height, &border, &depth) )
    {
      std::cout << "fail 1" << std::endl;
      return QRect();
    }

    QRect rect(x, y, width, height);
    if( !XTranslateCoordinates(display, window, root, x, y, &x, &y, &child) )
    {
      std::cout << "fail 2" << std::endl;
      return rect;
    }
    rect.moveTo(x, y);

    static Atom net_frame = 0;
    if (!net_frame)
        net_frame = XInternAtom(QX11Info::display(), "_NET_FRAME_EXTENTS", True);

    Atom type = 0;
    int format = 0;
    uchar* data = 0;
    ulong count, after;
    if (XGetWindowProperty(display, window, net_frame, 0, 4, False, AnyPropertyType,
                           &type, &format, &count, &after, &data) == Success)
    {
        // _NET_FRAME_EXTENTS, left, right, top, bottom, CARDINAL[4]/32
        if (count == 4)
        {
            long* extents = reinterpret_cast<long*>(data);
            rect.adjust(-extents[0], -extents[2], extents[1], extents[3]);
        }
        if (data)
            XFree(data);
    }
    return rect;
}

uint32_t QxtWindowSystem::applicationPID(WId window)
{
  init();

  static Atom wm_pid = XInternAtom(QX11Info::display(), "_NET_WM_PID", False);

  Atom type = 0;
  int format = 0;
  ulong after, prop_count;
  uchar* prop_data = 0;

  if( XGetWindowProperty( QX11Info::display(),
                          window,
                          wm_pid,
                          0, (~0L),
                          False,
                          AnyPropertyType,
                          &type,
                          &format,
                          &prop_count,
                          &after,
                          &prop_data ) != Success )
  {
    return 0;
  }

  if( !prop_data )
    return 0;

  uint32_t win_pid = reinterpret_cast<uint32_t*>(prop_data)[0];
  XFree(prop_data);

  return win_pid;
}

void QxtWindowSystem::setWindowProperty( WId window,
                                         QString const& key,
                                         QString const& val )
{
  init();

  QByteArray const local_val = val.toLocal8Bit();

  XChangeProperty(
    QX11Info::display(),
    window,
    XInternAtom(QX11Info::display(), key.toLocal8Bit().data(), False),
    XA_STRING,
    8,
    PropModeReplace,
    (unsigned char*)local_val.data(),
    local_val.size()
  );
}

QString QxtWindowSystem::getWindowProperty(WId window, QString const& key)
{
  init();

  Atom prop_atom =
    XInternAtom(QX11Info::display(), key.toLocal8Bit().data(), False);

  Atom type = 0;
  int format = 0;
  ulong after, prop_count;
  uchar* prop_data = 0;

  if( XGetWindowProperty( QX11Info::display(),
                          window,
                          prop_atom,
                          0, (~0L),
                          False,
                          XA_STRING,
                          &type,
                          &format,
                          &prop_count,
                          &after,
                          &prop_data ) != Success )
  {
    return "";
  }

  if( !prop_data )
    return "";

  QString val = QString::fromLocal8Bit( reinterpret_cast<char*>(prop_data),
                                         prop_count );

  XFree(prop_data);

  return val;
}

typedef struct {
    Window  window;     /* screen saver window - may not exist */
    int     state;      /* ScreenSaverOff, ScreenSaverOn, ScreenSaverDisabled*/
    int     kind;       /* ScreenSaverBlanked, ...Internal, ...External */
    unsigned long    til_or_since;   /* time til or since screen saver */
    unsigned long    idle;      /* total time since last user input */
    unsigned long   eventMask; /* currently selected events for this client */
} XScreenSaverInfo;

typedef XScreenSaverInfo* (*XScreenSaverAllocInfo)();
typedef Status (*XScreenSaverQueryInfo)(Display* display, Drawable* drawable, XScreenSaverInfo* info);

static XScreenSaverAllocInfo _xScreenSaverAllocInfo = 0;
static XScreenSaverQueryInfo _xScreenSaverQueryInfo = 0;

uint QxtWindowSystem::idleTime()
{
    static bool xssResolved = false;
    if (!xssResolved) {
        QLibrary xssLib(QLatin1String("Xss"), 1);
        if (xssLib.load()) {
            _xScreenSaverAllocInfo = (XScreenSaverAllocInfo) xssLib.resolve("XScreenSaverAllocInfo");
            _xScreenSaverQueryInfo = (XScreenSaverQueryInfo) xssLib.resolve("XScreenSaverQueryInfo");
            xssResolved = true;
        }
    }

    uint idle = 0;
#if 0
    if (xssResolved)
    {
        XScreenSaverInfo* info = _xScreenSaverAllocInfo();
        const int screen = QX11Info::appScreen();
        Qt::HANDLE rootWindow = QX11Info::appRootWindow(screen);
        _xScreenSaverQueryInfo(QX11Info::display(), (Drawable*) rootWindow, info);
        idle = info->idle;
        if (info)
            XFree(info);
    }
#endif
    return idle;
}
