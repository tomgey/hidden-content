/*
 * window_monitor.cpp
 *
 *  Created on: 17.07.2012
 *      Author: tom
 */

#include "window_monitor.hpp"
#include "log.hpp"
#include "qt_helper.hxx"
#include "JSON.hpp"

#include <QApplication>
#include <QDesktopWidget>
#include <QDebug>
#include <QProcess>
#include <QWindow>

//#define WINDOW_MONITOR_LOG_REGION_CHANGES

namespace LinksRouting
{
  //----------------------------------------------------------------------------
  std::ostream& operator<<(std::ostream& strm, const QRect& r)
  {
    return strm << "(" << r.x() << "|" << r.y() << ") "
                << r.width() << "x" << r.height();
  }

  //----------------------------------------------------------------------------
  WindowRegions::WindowRegions(const WindowInfos& windows):
    _windows(windows)
  {

  }

  //----------------------------------------------------------------------------
  WindowInfos::const_iterator WindowRegions::find(WId wid) const
  {
    return std::find_if
    (
      _windows.begin(),
      _windows.end(),
      [wid](const WindowInfo& winfo)
      {
        return winfo.id == wid;
      }
    );
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_iterator WindowRegions::find( uint32_t pid,
                                                   const QString& title) const
  {
    LOG_DEBUG( "Find window by pid (" << pid << ") and title '"
               << title.toStdString() << "'" );

    return std::find_if
    (
      _windows.begin(),
      _windows.end(),
      [pid, &title](const WindowInfo& winfo)
      {
        return winfo.pid == pid
            && ( title.isEmpty() || winfo.title.startsWith(title) );
      }
    );
  }

  //----------------------------------------------------------------------------
  WindowInfoIterators WindowRegions::find_all( uint32_t pid,
                                               const QString& title) const
  {
    LOG_DEBUG( "Find all windows by pid (" << pid << ") and title '"
                                           << title.toStdString() << "'" );

    WindowInfoIterators its;
    for(auto w = _windows.begin(); w != _windows.end(); ++w)
      if( w->pid == pid && (title.isEmpty() || w->title.startsWith(title)) )
        its.push_back(w);

    return its;
  }

  //----------------------------------------------------------------------------
  WindowInfoIterators WindowRegions::find_all(const QString& title) const
  {
    LOG_DEBUG("Find all windows by title '" << title.toStdString() << "'");

    WindowInfoIterators its;
    for(auto w = _windows.begin(); w != _windows.end(); ++w)
      if( w->title.startsWith(title) )
        its.push_back(w);

    return its;
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_iterator WindowRegions::find(const QString& title) const
  {
    LOG_DEBUG("Find window by title '" << title.toStdString() << "'");
    return std::find_if
    (
      _windows.begin(),
      _windows.end(),
      [&title](const WindowInfo& winfo)
      {
        return winfo.title.startsWith(title);
      }
    );
  }

  //----------------------------------------------------------------------------
  WId WindowRegions::findId(const QString& title) const
  {
    auto win = find(title);
    if( win != _windows.end() )
      return win->id;
    return 0;
  }

  //----------------------------------------------------------------------------
  WId WindowRegions::findId(uint32_t pid, const QString& title) const
  {
    auto win = find(pid, title);
    if( win != _windows.end() )
      return win->id;
    return 0;
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_iterator WindowRegions::begin() const
  {
    return _windows.begin();
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_iterator WindowRegions::end() const
  {
    return _windows.end();
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_reverse_iterator WindowRegions::rbegin() const
  {
    return _windows.rbegin();
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_reverse_iterator WindowRegions::rend() const
  {
    return _windows.rend();
  }

  //----------------------------------------------------------------------------
  WindowInfos::const_reverse_iterator
  WindowRegions::windowAt(const QPoint& pos) const
  {
    for( auto reg = _windows.rbegin(); reg != _windows.rend(); ++reg )
    {
      if( reg->region.contains(pos) )
        return reg;
    }
    return _windows.rend();
  }

  //----------------------------------------------------------------------------
  WId WindowRegions::windowIdAt(const QPoint& point) const
  {
    auto window = windowAt(point);
    if( window != _windows.rend() )
      return window->id;

    return 0;
  }

  //----------------------------------------------------------------------------
  bool WindowRegions::hit( const WindowInfos::const_iterator& first_above,
                           const QPoint& point,
                           Rect* reg,
                           WId* wid ) const
  {
    for(auto it = first_above; it != _windows.end(); ++it)
      if( !it->minimized && it->region.contains(point) )
      {
        if( reg )
          *reg = it->region;
        if( wid )
          *wid = it->id;
        return true;
      }
    return false;
  }

  //----------------------------------------------------------------------------
  WindowMonitor::WindowMonitor(RegionsCallback cb_regions_changed):
	_timeout(-1),
	_cb_regions_changed(cb_regions_changed),
	_launcher_size(0)
  {
	connect(&_timer, SIGNAL(timeout()), this, SLOT(check()));
	_timer.start(150);
  }

  //----------------------------------------------------------------------------
  WindowRegions WindowMonitor::getWindows() const
  {
    return WindowRegions( getWindowInfos() );
  }

  //----------------------------------------------------------------------------
  void WindowMonitor::setDesktopRect(const QRect& desktop)
  {
    _desktop_rect = desktop;
  }

  //----------------------------------------------------------------------------
  WindowInfos WindowMonitor::getWindowInfos() const
  {
    WId maximized_wid = 0;
    std::vector<WId> own_wids;
    for(QWindow const* w: QGuiApplication::topLevelWindows())
      own_wids.push_back(w->winId());

    // Now get the actual list of windows
    WindowInfos regions;
    foreach(WId id, QxtWindowSystem::windows())
    {
      if( std::find(own_wids.begin(), own_wids.end(), id) != own_wids.end() )
        continue;

      QRect region = QxtWindowSystem::windowGeometry(id);
      QString title = QxtWindowSystem::windowTitle(id);

      if( title == "unity-launcher" )
      {
        int launcher_size = region.width() - 17;
        if( _launcher_size != launcher_size )
        {
          _launcher_size = launcher_size;
          // TODO notify?
        }
        continue;
      }

      QString win_type =
        QxtWindowSystem::getWindowProperty(id, "LINKS_SYSTEM_TYPE");

      // Ignore small regions (tooltips, etc.)
      if(  win_type.isEmpty()
         && ( region.width() <= 64
           || region.height() <= 64
           //|| region.width() * region.height() <= 8192 // concept nodes can be
                                                         // smaller...
            )
        )
        continue;

      regions.push_back(WindowInfo(
        id,
        QxtWindowSystem::applicationPID(id),
        QxtWindowSystem::isVisible(id) && region.right() > 0,
        region,
        title
      ));

      if( !regions.back().minimized )
      {
        QRect visible_region = _desktop_rect.intersected(region);
        if(    visible_region.left() <= 0.08 * _desktop_rect.width()
            && visible_region.right() >= 0.94 * _desktop_rect.width()
            && visible_region.top() <= 0.06 * _desktop_rect.height()
            && visible_region.bottom() >= 0.96 * _desktop_rect.height() )
          maximized_wid = id;
      }
    }

    if( maximized_wid )
    {
      // Mark all windows below maximized window as minimized
      for(WindowInfo& winfo: regions)
        if( maximized_wid != winfo.id )
          //winfo.covered = true;
          winfo.minimized = true;
        else
          break;
    }

#ifdef __linux__
    const int LAUNCHER_PADDING = 11;
    const int LAUNCHER_FIRST_OFFSET = 32
                                    // Dash icon
                                    + LAUNCHER_PADDING + _launcher_size;
    int pos_y = LAUNCHER_FIRST_OFFSET;

    for(QString const& launcher: _launchers)
    {
      QString name(launcher);
      name.remove("localhost");

      for(WindowInfo& winfo: regions)
      {
        bool match = false;
        QString title = winfo.title;
        title.remove(QRegExp("\\W")); // Title of Unity launchers has all non
                                      // alphanumeric characters removed

        if( title == name || title == name + "MozillaFirefox" || title == name + "Aurora" )
        {
          match = true;
        }
        else if( winfo.title.contains("Google Maps") )
        {
          match = launcher == "Map";
        }
        else
        {
          QStringList title_words =
            winfo.title.split(' ', QString::SkipEmptyParts);
          match = !title_words.empty()
               && launcher.contains(title_words.last(), Qt::CaseInsensitive);
        }
        if( match )
        {
          winfo.region_launcher =
            QRect(9, pos_y, _launcher_size, _launcher_size);
        }
      }
      pos_y += LAUNCHER_PADDING + _launcher_size;
    }
#endif

    return regions;
  }

  //----------------------------------------------------------------------------
  void WindowMonitor::check()
  {
#ifdef __linux__
    // Get list of pinned apps in the Ubuntu Unity launcher
    QString program = "/usr/bin/dconf";
    QStringList arguments;
    arguments << "read" << "/com/canonical/unity/launcher/favorites";
    QProcess dconf;
    dconf.start(program, arguments);
#endif

    WindowInfos regions = getWindowInfos();
    if( regions != _last_regions )
      _timeout = 2;
    _last_regions = regions;

    if( regions == _regions )
      _timeout = -1;

#ifdef __linux__
    dconf.waitForFinished();

    QByteArray dconf_response = dconf.readAllStandardOutput();
    dconf_response.replace('\'', '"');

    QStringList launchers =
      from_json<QStringList>(parseJsonArray(dconf_response))
      .replaceInStrings(".desktop", "")
      .replaceInStrings("application://", "");

    if( launchers != _launchers )
    {
      _launchers = launchers;
      _cb_regions_changed( getWindows() );
    }
#endif

    if( _timeout >= 0 )
    {
      if( _timeout == 0 )
      {
        _regions = regions;

#ifdef WINDOW_MONITOR_LOG_REGION_CHANGES
        LOG_INFO("Trigger reroute...");

        for( auto reg = regions.begin(); reg != regions.end(); ++reg )
        {
          std::cout << "(" << reg->id << ' ' << reg->minimized << ") "
                    << reg->title.left(15)
                    << ", launcher = " << reg->region_launcher
                    << ", reg = " << reg->region
                    << std::endl;
        }
#endif
        _cb_regions_changed( WindowRegions(regions) );
      }
      _timeout -= 1;
    }
  }

} // namespace LinksRouting
