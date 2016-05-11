// Include first to get conversion functions to/from float2
#include <QPoint>
#include <QRect>
#include <QSize>

#include "LinkRenderer.hpp"
#include "GLWindow.hpp"
#include <QDebug>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <GL/gl.h>

#include <cassert>
#include <iostream>

namespace qtfullscreensystem
{
  //----------------------------------------------------------------------------
  GLWindow::GLWindow(const QRect& geometry):
    _geometry(geometry)
  {
    qDebug() << "new window" << geometry;
    setGeometry(geometry);

    // don't allow user to change the window size
//    setFixedSize(geometry.size());

    // transparent, always on top window without any decoration
    setFlags( Qt::WindowStaysOnTopHint
            | Qt::FramelessWindowHint
            | Qt::MSWindowsOwnDC
            | Qt::X11BypassWindowManagerHint
            | Qt::WindowTransparentForInput
            );
//    setAttribute(Qt::WA_NoSystemBackground);
//    setAttribute(Qt::WA_TranslucentBackground);
//    setAttribute(Qt::WA_TransparentForMouseEvents);

    // Transparent background/blending with desktop
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    setFormat(format);
  }

  //----------------------------------------------------------------------------
  GLWindow::~GLWindow()
  {

  }

  //----------------------------------------------------------------------------
  uint32_t GLWindow::process()
  {
    if( LinksRouting::LinkRenderer()
          .wouldRenderLinks( Rect(geometry()),
                             *_subscribe_links->_data ) )
    {
      if( !isVisible() )
        show();

      QTimer::singleShot(0, this, SLOT(update()));
    }
    else if( isVisible() )
      hide();

    return 0;
  }

  //----------------------------------------------------------------------------
  void GLWindow::subscribeSlots(LR::SlotSubscriber& slot_subscriber)
  {
    _subscribe_links =
      slot_subscriber.getSlot<LR::LinkDescription::LinkList>("/links");
    _subscribe_popups =
      slot_subscriber.getSlot<LR::SlotType::TextPopup>("/popups");
    _subscribe_outlines =
      slot_subscriber.getSlot<LR::SlotType::CoveredOutline>("/covered-outlines");
    _subscribe_xray =
      slot_subscriber.getSlot<LR::SlotType::XRayPopup>("/xray");
  }

  //----------------------------------------------------------------------------
  void GLWindow::paintGL()
  {
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    QRect geom = geometry();
    glOrtho(geom.left(), geom.right(), geom.bottom(), geom.top(), -1.0, 1.0);

    LinksRouting::LinkRenderer renderer;
    renderer.renderLinks(*_subscribe_links->_data);
  }

} // namespace qtfullscreensystem
