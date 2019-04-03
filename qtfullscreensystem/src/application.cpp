/*!
 * @file application.cpp
 * @brief
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 14.10.2011
 */

#include "application.hpp"
#include "qt_helper.hxx"
#include "PreviewWindow.hpp"
#include "GLWindow.hpp"
#include "Window.hpp"

#include <QCommandLineParser>
#include <QDesktopWidget>
#include <QElapsedTimer>
#include <QScreen>
#include <QSurfaceFormat>

#include <iostream>

namespace qtfullscreensystem
{

  class QtPreview:
    public LinksRouting::SlotType::Preview
  {
    public:
      virtual
      LinksRouting::PreviewWindow*
      getWindow( LinksRouting::SlotType::TextPopup::Popup *popup,
                 uint8_t dev_id = 0 )
      {
        Application* app = dynamic_cast<Application*>(QApplication::instance());
        if( !app )
        {
          qWarning() << "no app";
          return nullptr;
        }

        LR::SlotSubscriber slot_subscriber = app->getSlotSubscriber();

        QtPreviewWindow* w = new PopupWindow(popup);
        w->subscribeSlots(slot_subscriber);

        return w;
      }

      virtual
      LinksRouting::PreviewWindow*
      getWindow( LinksRouting::SlotType::XRayPopup::HoverRect *see_through,
                 uint8_t dev_id = 0 )
      {
        Application* app = dynamic_cast<Application*>(QApplication::instance());
        if( !app )
        {
          qWarning() << "no app";
          return nullptr;
        }

        LR::SlotSubscriber slot_subscriber = app->getSlotSubscriber();

        QtPreviewWindow* w = new SeeThroughWindow(see_through);
        w->subscribeSlots(slot_subscriber);

        return w;
      }

      virtual
      LinksRouting::PreviewWindow*
      getWindow( LinksRouting::ClientWeakRef client,
                 uint8_t dev_id = 0 )
      {
        Application* app = dynamic_cast<Application*>(QApplication::instance());
        if( !app )
        {
          qWarning() << "no app";
          return nullptr;
        }

        LR::SlotSubscriber slot_subscriber = app->getSlotSubscriber();

        QtPreviewWindow* w = new SemanticPreviewWindow(client);
        w->subscribeSlots(slot_subscriber);

        return w;
      }
  };

  //----------------------------------------------------------------------------
  Application::Application(int& argc, char *argv[]):
    Configurable("Application"),
    QApplication(argc, argv),
    _server(&_mutex_slot_links, &_cond_render),
    _mutex_slot_links(QMutex::Recursive)
  {
//    _cur_fbo(0),
//    _do_drag(false),
//    _flags(0),

    setOrganizationDomain("icg.tugraz.at");
    setOrganizationName("icg.tugraz.at");
    setApplicationName("VisLinks");

    auto fmt = QSurfaceFormat::defaultFormat();
    fmt.setMajorVersion(2);
    QSurfaceFormat::setDefaultFormat(fmt);

    const QDir config_dir =
      QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    const QString user_config = config_dir.filePath("config.xml");

    {
      // Ensure config path and user config file exist
      QDir().mkpath(config_dir.path());
      QFile file( user_config );
      if( !file.open(QIODevice::ReadWrite | QIODevice::Text) )
        LOG_ERROR("Failed to open user config!");
      else if( !file.size() )
      {
        LOG_INFO("Creating empty user config in '" << user_config << "'");
        file.write(
          "<!-- VisLinks user config (overrides config file) -->\n"
          "<config/>"
        );
      }
    }

    //--------------------------------
    // Setup component system
    //--------------------------------

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(
     "config",
     QCoreApplication::translate("main", "Path to config file.")
    );
    parser.addOptions({
      { "disable-rendering",
        QCoreApplication::translate("main", "Disable rendering/visual links.") }
    });
    parser.addOptions({
      { "renderer-per-screen",
        QCoreApplication::translate("main", "Enable independent rendering for each screen.") }
    });
    parser.process(*this);

    QStringList pos_args = parser.positionalArguments();
    if( pos_args.size() != 1 )
    {
      qDebug() << "Missing config file!";
      parser.showHelp();
    }

    _disable_rendering = parser.isSet("disable-rendering");
    _use_renderer_per_screen = parser.isSet("renderer-per-screen");

//    publishSlots(_core.getSlotCollector());

    if( !_config.initFrom(pos_args[0].toStdString()) )
      qFatal("Failed to read config");
    _user_config.initFrom( to_string(user_config) );

    _core.startup();
    _core.attachComponent(&_config);
    _core.attachComponent(&_user_config);
    _core.attachComponent(&_server);

    if( !_disable_rendering )
    {
      _core.attachComponent(&_routing_cpu);
      _core.attachComponent(&_routing_cpu_dijkstra);
      _core.attachComponent(&_routing_dummy);
#ifdef USE_GPU_ROUTING
      _core.attachComponent(&_cost_analysis);
      _core.attachComponent(&_routing_gpu);
#endif
      if( !_use_renderer_per_screen )
        _core.attachComponent(&_renderer);
    }

    _core.attachComponent(this);
//    registerArg("DebugDesktopImage", _debug_desktop_image);
//    registerArg("DumpScreenshot", _dump_screenshot = 0);

    if( !_disable_rendering )
    {
      if( !_use_renderer_per_screen )
      {
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::RenderableType::OpenGL);
        fmt.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
        fmt.setAlphaBufferSize(8);
        fmt.setMajorVersion(4);

        _gl_ctx.setFormat(fmt);
        if( !_gl_ctx.create() )
          qFatal("Failed to create OpenGL context.");
        if( !_gl_ctx.format().hasAlpha() )
          qFatal("Failed to enable alpha blending.");

        _offscreen_surface.setFormat(fmt);
        _offscreen_surface.setScreen( QGuiApplication::primaryScreen() );
        _offscreen_surface.create();

        qDebug() << "offscreen" << _offscreen_surface.size();

        for(QScreen* s: QGuiApplication::screens())
        {
          auto w = std::make_shared<RenderWindow>(s->availableGeometry());
          w->setImage(&_fbo_image);
          w->show();
          _windows.push_back(w);

          auto mw = std::make_shared<RenderWindow>(s->availableGeometry());
          mw->show();
          _mask_windows.push_back(mw);
        }
      }
      else
      {
        for(QScreen* s: QGuiApplication::screens())
        {
          auto w = std::make_shared<GLWindow>(s->availableGeometry());
          w->show();
          _render_windows.push_back(w);
        }
      }
    }

    _core.init();

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(200);
  }

  //----------------------------------------------------------------------------
  Application::~Application()
  {

  }

  //----------------------------------------------------------------------------
  void Application::publishSlots(LR::SlotCollector& slot_collector)
  {
    _slot_desktop =
      slot_collector.create<LR::SlotType::Image>("/desktop");
    _slot_desktop->_data->type = LR::SlotType::Image::OpenGLTexture;

    _slot_desktop_rect =
      slot_collector.create<Rect>("/desktop/rect");
    *_slot_desktop_rect->_data =
      QGuiApplication::primaryScreen()->availableVirtualGeometry();
    _slot_desktop_rect->setValid(true);

    _slot_mouse =
      slot_collector.create<LR::SlotType::MouseEvent>("/mouse");
    _slot_popups =
      slot_collector.create<LR::SlotType::TextPopup>("/popups");
    _slot_previews =
      slot_collector.create<LR::SlotType::Preview, QtPreview>("/previews");
  }

  //----------------------------------------------------------------------------
  void Application::subscribeSlots(LR::SlotSubscriber& slot_subscriber)
  {
    if( _disable_rendering )
      return;

#ifdef USE_GPU_ROUTING
    _subscribe_costmap =
      slot_subscriber.getSlot<LR::SlotType::Image>("/costmap");
#endif
    _subscribe_routed_links =
      slot_subscriber.getSlot<LR::LinkDescription::LinkList>("/links");
    _subscribe_outlines =
      slot_subscriber.getSlot<LR::SlotType::CoveredOutline>("/covered-outlines");

    for(auto& w: _render_windows)
      w->subscribeSlots(slot_subscriber);

    if( _use_renderer_per_screen )
    {
      _subscribe_popups =
        slot_subscriber.getSlot<LR::SlotType::TextPopup>("/popups");
      return;
    }

    _subscribe_links =
      slot_subscriber.getSlot<LR::SlotType::Image>("/rendered-links");
    _subscribe_xray_fbo =
      slot_subscriber.getSlot<LR::SlotType::Image>("/rendered-xray");

    for(auto& w: _windows)
      w->subscribeSlots(slot_subscriber);
    for(auto& w: _mask_windows)
      w->subscribeSlots(slot_subscriber);
  }

  //----------------------------------------------------------------------------
  LR::SlotCollector Application::getSlotCollector()
  {
    return _core.getSlotCollector();
  }

  //----------------------------------------------------------------------------
  LR::SlotSubscriber Application::getSlotSubscriber()
  {
    return _core.getSlotSubscriber();
  }

  //----------------------------------------------------------------------------
  void Application::update()
  {
    if( _disable_rendering )
      updateNoRendering();
    else if( _use_renderer_per_screen )
      updateRendererPerScreen();
    else
      updateGlobalRenderer();
  }

  //----------------------------------------------------------------------------
  Application::PopupIndicatorWindow::PopupIndicatorWindow(
    Popups::const_iterator popup
  ):
      _popup(popup)
  {
    setGeometry(popup->region.region.toQRect());
    setFlags( Qt::WindowStaysOnTopHint
            | Qt::FramelessWindowHint );
    show();

    qDebug() << popup->region.region.toQRect() << popup->hover_region.region.toQRect();
  }

  //----------------------------------------------------------------------------
  void Application::updateNoRendering()
  {
    _core.process( Component::Config
                 | Component::DataServer );
  }

  //----------------------------------------------------------------------------
  void Application::updateRendererPerScreen()
  {
    _core.process( Component::Config
                 | Component::DataServer
                 | Component::Routing );

    for(GLWindowRef& win: _render_windows)
      win->process();

    return;
    typedef LR::SlotType::TextPopup::Popup Popup;
    const Popups& popups = _subscribe_popups->_data->popups;

    // Delete popups not needed anymore
    for(auto popup_window_it = _popup_windows.begin();
             popup_window_it != _popup_windows.end(); )
    {
      if( std::find_if( popups.begin(),
                        popups.end(),
                        [&popup_window_it](Popup const& popup)
                        {
                          return popup.region.region == popup_window_it->first;
                        }) == popups.end() )
      {
        qDebug() << "Delete popup" << popup_window_it->first.toQRect();
        popup_window_it = _popup_windows.erase(popup_window_it);
      }
      else
        ++popup_window_it;
    }

    // Create missing/new popups
    for( Popups::const_iterator popup_it = popups.begin();
                                popup_it != popups.end();
                             ++popup_it )
    {
      if( _popup_windows.find(popup_it->region.region) != _popup_windows.end() )
      {
        qDebug() << "Still same popup" << popup_it->region.region.toQRect();
        continue;
      }

      qDebug() << "Create popup";
      _popup_windows.insert({
        popup_it->region.region,
        std::make_shared<PopupIndicatorWindow>(popup_it)
      });

//      float2 pos = popup->region.region.pos
//                 - float2(_geometry.x(), _geometry.y() + 1);
//
//      if(    pos.x < 0 || pos.x > _geometry.width()
//          || pos.y < 0 || pos.y > _geometry.height()
//          || !popup->region.isVisible()
//          || (popup->node && popup->node->get<bool>("hidden")) )
//        continue;
//
//      QRect text_rect(pos.toQPoint(), popup->region.region.size.toQSize());
//
//      painter.drawText(
//        text_rect,
//        Qt::AlignCenter,
//        QString::fromStdString(popup->text)
//      );
    }
  }

  //----------------------------------------------------------------------------
  void Application::updateGlobalRenderer()
  {
    int pass = 1;
    uint32_t _flags = LINKS_DIRTY | RENDER_DIRTY;

    if( !_gl_ctx.makeCurrent(&_offscreen_surface) )
      qFatal("Could not activate OpenGL context.");

    if( !_fbo )
    {
      QSize size = QGuiApplication::primaryScreen()->availableVirtualSize();
      qDebug() << "initFBO" << size;
      _fbo.reset(new QOpenGLFramebufferObject(size));

      _shader_blend = Shader::loadFromFiles("simple.vert", "blend.frag");
      if( !_shader_blend )
        qFatal("Failed to load blend shader.");

      glClearColor(0, 0, 0, 0);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      glViewport(0,0, _fbo->width(), _fbo->height());

      _core.initGL();
    }

    Rect const& desktop = *_slot_desktop_rect->_data;
    glMatrixMode(GL_PROJECTION);
    glOrtho(desktop.l(), desktop.r(), desktop.t(), desktop.b(), -1.0, 1.0);

    //uint32_t old_flags = _flags;
    {
      QMutexLocker lock_links(&_mutex_slot_links);
      uint32_t types = pass == 0
                     ? (Component::Renderer | 64)
                     :   Component::Config
                       | Component::DataServer
                       | ((_flags & LINKS_DIRTY) ? Component::Routing : 0)
                       | ((_flags & RENDER_DIRTY) ? Component::Renderer : 0);

//      std::cout << "types: " << (types & Component::Routing ? "routing " : "")
//                             << (types & Component::Renderer ? "render " : "")
//                             << std::endl;

      _flags = _core.process(types);
    }

    static int counter = 0;
    ++counter;

    //--------------------------------------------------------------------------
//    auto writeTexture = []
//    (
//      const LinksRouting::slot_t<LinksRouting::SlotType::Image>::type& slot,
//      const QString& name
//    )
//    {
//      QImage image( QSize(slot->_data->width,
//                          slot->_data->height),
//                    QImage::Format_RGB888 );
//      glEnable(GL_TEXTURE_2D);
//      glBindTexture(GL_TEXTURE_2D, slot->_data->id);
//      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, image.bits());
//      glDisable(GL_TEXTURE_2D);
//      image.save(name);
//    };

    glFinish();
    //writeTexture(_subscribe_links, QString("links%1.png").arg(counter));

    if( !_fbo->bind() )
      qFatal("Failed to bind FBO.");

    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

#if 1
    GLuint tex_ids[] = {
      _subscribe_xray_fbo->_data->id,
      _subscribe_links->_data->id,
#ifndef USE_DESKTOP_BLEND
      _slot_desktop->_data->id
#endif
    };
    size_t num_textures = sizeof(tex_ids)/sizeof(tex_ids[0]);

    for(size_t i = 0; i < num_textures; ++i)
    {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tex_ids[i]);
    }

    _shader_blend->bind();
    _shader_blend->setUniformValue("xray", 0);
    _shader_blend->setUniformValue("links", 1);
#ifndef USE_DESKTOP_BLEND
    _shader_blend->setUniformValue("desktop", 2);
#endif
#if 0
    _shader_blend->setUniformValue("mouse_pos", pos);
#endif

    glBegin( GL_QUADS );

      glMultiTexCoord2f(GL_TEXTURE0, 0,1);
      glVertex2f(-1, -1);

      glMultiTexCoord2f(GL_TEXTURE0,1,1);
      glVertex2f(1,-1);

      glMultiTexCoord2f(GL_TEXTURE0,1,0);
      glVertex2f(1,1);

      glMultiTexCoord2f(GL_TEXTURE0,0,0);
      glVertex2f(-1, 1);

    glEnd();

    _shader_blend->release();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

#endif
    if( !_fbo->release() )
      qFatal("Failed to release FBO.");

    glFinish();

    _fbo_image = _fbo->toImage();
    //_fbo_image.save(QString("fbo%1.png").arg(counter));

    int timeout = 5;
    for(size_t i = 0; i < _windows.size(); ++i)
    {
      //w->update();
      QTimer::singleShot(timeout, _windows[i].get(), SLOT(update()));
      QTimer::singleShot(timeout, _mask_windows[i].get(), SLOT(update()));

      // Waiting a bit between updates seems to reduce artifacts...
      timeout += 30;
    }

    _gl_ctx.doneCurrent();
  }

} // namespace qtfullscreensystem
