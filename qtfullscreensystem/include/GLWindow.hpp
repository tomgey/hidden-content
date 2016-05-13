/*
 * GLWindow.hpp
 *
 *  Created on: May 3, 2016
 *      Author: tom
 */

#ifndef QTFULLSCREENSYSTEM_INCLUDE_GLWINDOW_HPP_
#define QTFULLSCREENSYSTEM_INCLUDE_GLWINDOW_HPP_

#include <slots.hpp>
#include <slotdata/mouse_event.hpp>
#include <slotdata/text_popup.hpp>

#include <QOpenGLWindow>
#include <memory>

namespace qtfullscreensystem
{
  namespace LR = LinksRouting;

  class GLWindow:
    public QOpenGLWindow
  {
    public:
      GLWindow(const QRect& geometry);
      virtual ~GLWindow();

      virtual uint32_t process();

      void subscribeSlots(LR::SlotSubscriber& slot_subscriber);

    protected:
      QRect     _geometry;

      LR::slot_t<LR::LinkDescription::LinkList>::type _subscribe_links;
      LR::slot_t<LR::SlotType::CoveredOutline >::type _subscribe_outlines;
      LR::slot_t<LR::SlotType::XRayPopup      >::type _subscribe_xray;

      virtual void paintGL();
//      virtual void moveEvent(QMoveEvent *event);
//      virtual void paintEvent(QPaintEvent* e);
  };
  typedef std::shared_ptr<GLWindow> GLWindowRef;

} // namespace qtfullscreensystem

#endif /* QTFULLSCREENSYSTEM_INCLUDE_GLWINDOW_HPP_ */
