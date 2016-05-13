/*!
 * @file application.hpp
 * @brief
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 14.10.2011
 */

#ifndef _APPLICATION_HPP_
#define _APPLICATION_HPP_

#include "Shader.hpp"
#include "Window.hpp"
#include "GLWindow.hpp"

#include "staticcore.h"
#include "xmlconfig.h"
#include "ipc_server.hpp"
#include "cpurouting.h"
#include "cpurouting-dijkstra.h"
#include "dummyrouting.h"
#if USE_GPU_ROUTING
# include "glcostanalysis.h"
# include "gpurouting.h"
#endif
#include "glrenderer.h"

#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QTimer>

namespace qtfullscreensystem
{
  namespace LR = LinksRouting;

  class Application:
    public QApplication,
    public LR::Component,
    public LR::ComponentArguments
  {
    Q_OBJECT

    public:

      Application(int& argc, char *argv[]);
      virtual ~Application();

      void publishSlots(LR::SlotCollector& slots);
      void subscribeSlots(LR::SlotSubscriber& slot_subscriber);

      LR::SlotCollector getSlotCollector();
      LR::SlotSubscriber getSlotSubscriber();

    signals:
      void frame();

    public slots:

      void update();

    protected:

      typedef LR::SlotType::TextPopup::Popups Popups;
      class PopupIndicatorWindow:
        public QWindow
      {
        public:
          PopupIndicatorWindow(const Popups::const_iterator popup);

        private:
          const Popups::const_iterator _popup;
      };

      typedef std::shared_ptr<PopupIndicatorWindow> PopupIndicatorWindowRef;
      typedef std::map<Rect, PopupIndicatorWindowRef> PopupIndicatorMap;

      // ----------
      // Slots
      // ----------

      LR::slot_t<LR::SlotType::Image>::type             _slot_desktop;
      LR::slot_t<Rect>::type                            _slot_desktop_rect;
      LR::slot_t<LR::SlotType::MouseEvent>::type        _slot_mouse;
      LR::slot_t<LR::SlotType::TextPopup>::type         _slot_popups;
      LR::slot_t<LR::SlotType::Preview>::type           _slot_previews;

      // TODO make readonly
      LR::slot_t<LR::SlotType::Image          >::type   _subscribe_links,
                                                        _subscribe_xray_fbo,
                                                        _subscribe_costmap;
      LR::slot_t<LR::LinkDescription::LinkList>::type   _subscribe_routed_links;
      LR::slot_t<LR::SlotType::CoveredOutline >::type   _subscribe_outlines;
      LR::slot_t<LR::SlotType::TextPopup      >::type   _subscribe_popups;

      // ----------
      // Components
      // ----------

      LR::StaticCore            _core;
      LR::XmlConfig             _config,
                                _user_config;
      LR::IPCServer             _server;
      LR::CPURouting            _routing_cpu;
      LR::Dijkstra::CPURouting  _routing_cpu_dijkstra;
      LR::DummyRouting          _routing_dummy;
#if USE_GPU_ROUTING
      LR::GlCostAnalysis        _cost_analysis;
      LR::GPURouting            _routing_gpu;
#endif
      LR::GlRenderer            _renderer;

      // ----------
      // Rendering
      // ----------

      bool _disable_rendering,
           _use_renderer_per_screen;

      QOffscreenSurface                         _offscreen_surface;
      QOpenGLContext                            _gl_ctx;
      std::unique_ptr<QOpenGLFramebufferObject> _fbo;
      QImage                                    _fbo_image;
      ShaderPtr                                 _shader_blend;

      std::vector<WindowRef>    _windows;
      std::vector<WindowRef>    _mask_windows;
      std::vector<GLWindowRef>  _render_windows;

      // Locks/Mutex
      QMutex            _mutex_slot_links;
      QWaitCondition    _cond_render;

      PopupIndicatorMap _popup_windows;

      void updateNoRendering();
      void updateRendererPerScreen();
      void updateGlobalRenderer();
  };

} // namespace qtfullscreensystem

#endif /* _APPLICATION_HPP_ */
