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

#include <QFile>
#include <QLibrary>
#include <QX11Info>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <iostream>

QString utf8StripEscape(QString const& str)
{
  QString clean_str;

  for(int i = 0; i < str.length(); ++i)
  {
    if( str.at(i) == 0x1b )
      i += 2;
    else
      clean_str.push_back(str.at(i));
  }

  return clean_str;
}

//------------------------------------------------------------------------------
static Atom getXAtom(const char* name)
{
  return XInternAtom(QX11Info::display(), name, False);
}

//------------------------------------------------------------------------------
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

  if( XGetWindowProperty( display, window, prop,
                          0, 1024 * sizeof(Window) / 4,
                          False, AnyPropertyType,
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

//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
void QxtWindowSystem::init()
{
  static bool initDone = false;
  if( initDone )
    return;

  XSetErrorHandler(&qxtX11ErrorHandler);
  initDone = true;
}

//------------------------------------------------------------------------------
WindowList QxtWindowSystem::windows()
{
  return qxt_getWindows(getXAtom("_NET_CLIENT_LIST_STACKING"));
}

//------------------------------------------------------------------------------
WId QxtWindowSystem::activeWindow()
{
    return qxt_getWindows(getXAtom("_NET_ACTIVE_WINDOW")).value(0);
}

//------------------------------------------------------------------------------
int QxtWindowSystem::activeWindow(WId window)
{
  init();
  XEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = ClientMessage;
  ev.xclient.display = QX11Info::display();
  ev.xclient.window = window;
  ev.xclient.message_type = getXAtom("_NET_ACTIVE_WINDOW");
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

//------------------------------------------------------------------------------
int QxtWindowSystem::iconifyWindow(WId window)
{
  init();

  XWindowAttributes attr;
  XGetWindowAttributes(QX11Info::display(), window, &attr);

  return XIconifyWindow(
    QX11Info::display(),
    window,
    XScreenNumberOfScreen(attr.screen)
  );
}

//------------------------------------------------------------------------------
int QxtWindowSystem::unmaxizeWindow(WId window)
{
  init();

  XEvent xev;
  memset(&xev, 0, sizeof(xev));
  xev.type = ClientMessage;
  xev.xclient.window = window;
  xev.xclient.message_type = getXAtom("_NET_WM_STATE");
  xev.xclient.format = 32;
  xev.xclient.data.l[0] = 0; //_NET_WM_STATE_REMOVE
  xev.xclient.data.l[1] = getXAtom("_NET_WM_STATE_MAXIMIZED_HORZ");
  xev.xclient.data.l[2] = getXAtom("_NET_WM_STATE_MAXIMIZED_VERT");

  return XSendEvent(
    QX11Info::display(),
    DefaultRootWindow(QX11Info::display()),
    False,
    SubstructureNotifyMask,
    &xev
  );
}

//------------------------------------------------------------------------------
int QxtWindowSystem::moveResizeWindow(WId window, QRect const& geom)
{
  init();
  return XMoveResizeWindow(
    QX11Info::display(),
    window,
    geom.left(),
    geom.top(),
    geom.width(),
    geom.height()
  );
}

//------------------------------------------------------------------------------
bool QxtWindowSystem::isVisible(WId wid)
{
  init();
  Display* display = QX11Info::display();
  bool hidden = false;

  static Atom wm_state  = getXAtom("_NET_WM_STATE"),
              wm_hidden = getXAtom("_NET_WM_STATE_HIDDEN");

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

//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
QString QxtWindowSystem::windowTitle(WId window)
{
  init();

  QString title = utf8StripEscape(getWindowProperty(window, "_NET_WM_NAME"));

  // Some windows only have _NET_WM_NAME set...
  {
//      static Atom wm_name =
//        XInternAtom(QX11Info::display(), "_NET_WM_NAME", False);
//
//      Atom type = 0;
//      int format = 0;
//      unsigned char* data = nullptr;
//      unsigned long count, after;
//
//      if( XGetWindowProperty( QX11Info::display(), window, wm_name,
//                              0, (~0L), False, AnyPropertyType,
//                              &type, &format, &count, &after,
//                              &data ) == Success )
//      {
//        title = QString::fromLocal8Bit((char*)data, count);
//      }
//
//      if( data )
//        XFree(data);

    if( !title.isEmpty() )
      return title;
  }

  XTextProperty prop;
  if( XGetWMName(QX11Info::display(), window, &prop) )
  {
    title = utf8StripEscape(
      QString::fromUtf8( reinterpret_cast<char*>(prop.value),
                         prop.nitems * prop.format / 8 )
    );
  }

  if( title.isEmpty() )
  {
    char* str = 0;
    if( XFetchName(QX11Info::display(), window, &str) )
      title = utf8StripEscape(QString::fromUtf8(str));
    if( str )
      XFree(str);
  }

  return title;
}

//------------------------------------------------------------------------------
QRect QxtWindowSystem::windowGeometry(WId window)
{
  init();
  int x, y;
  uint width, height, border, depth;
  Window root, child;
  Display* display = QX11Info::display();

  if( !XGetGeometry( display, window,
                     &root,
                     &x, &y,
                     &width, &height,
                     &border,
                     &depth ) )
    return QRect();

  QRect rect(x, y, width, height);
  if( !XTranslateCoordinates(display, window, root, x, y, &x, &y, &child) )
    return rect;

  rect.moveTo(x, y);

  static Atom net_frame = getXAtom("_NET_FRAME_EXTENTS");

  Atom type = 0;
  int format = 0;
  uchar* data = 0;
  ulong count, after;
  if( XGetWindowProperty( display, window,
                          net_frame,
                          0, 4,
                          False, AnyPropertyType,
                          &type, &format, &count,
                          &after, &data ) == Success )
  {
    // _NET_FRAME_EXTENTS, left, right, top, bottom, CARDINAL[4]/32
    if( count == 4 )
    {
      long* extents = reinterpret_cast<long*>(data);
      rect.adjust(-extents[0], -extents[2], extents[1], extents[3]);
    }
    if( data )
      XFree(data);
  }

  return rect;
}

//------------------------------------------------------------------------------
uint32_t QxtWindowSystem::applicationPID(WId window)
{
  init();

  static Atom wm_pid = getXAtom("_NET_WM_PID");

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

//------------------------------------------------------------------------------
QString QxtWindowSystem::executablePath(uint32_t pid)
{
  return QFile::symLinkTarget( QString("/proc/%1/exe").arg(pid) );
}

//------------------------------------------------------------------------------
void QxtWindowSystem::setWindowProperty( WId window,
                                         QString const& key,
                                         QString const& val )
{
  init();

  QByteArray const local_val = val.toLocal8Bit();

  XChangeProperty(
    QX11Info::display(),
    window,
    getXAtom(key.toLocal8Bit().data()),
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

  Atom type = 0;
  int format = 0;
  ulong after, prop_count;
  uchar* prop_data = 0;

  if( XGetWindowProperty( QX11Info::display(),
                          window,
                          getXAtom(key.toLocal8Bit().data()),
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
                                        prop_count * format / 8 );

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
