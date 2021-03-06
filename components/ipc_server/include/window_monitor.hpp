/*
 * window_monitor.hpp
 *
 *  Created on: 17.07.2012
 *      Author: tom
 */

#ifndef WINDOW_MONITOR_HPP_
#define WINDOW_MONITOR_HPP_

#include <QxtGui/qxtwindowsystem.h>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>
#include <map>

#include "float2.hpp"

namespace LinksRouting
{
  struct WindowInfo
  {
    WId         id;
    uint32_t    pid;
    uint16_t    ptr_id;
    bool        minimized,
                covered;
    QRect       region,
                region_launcher;
    QString     title;

    explicit WindowInfo( WId id,
                         uint32_t pid = 0,
                         bool minimized = false,
                         const QRect& region = QRect(),
                         const QString& title = "" ):
      id(id),
      pid(pid),
      ptr_id(2), // Virtual core pointer has always id=2
      minimized(minimized),
      covered(false),
      region(region),
      title(title)
    {}

    bool operator==(const WindowInfo& rhs) const
    {
      // We don't care about title and ptr changes -> Only compare other values
      return id == rhs.id
          && region == rhs.region
          && minimized == rhs.minimized
          && covered == rhs.covered
          && region_launcher == rhs.region_launcher;
    }

    bool operator!=(const WindowInfo& rhs) const
    {
      return !(*this == rhs);
    }

    bool isValid() const
    {
      return id != 0;
    }

    /// Get current geometry (uncached)
    QRect queryGeometry() const
    {
      return QxtWindowSystem::windowGeometry(id);
    }
  };
  typedef std::vector<WindowInfo> WindowInfos;
  typedef std::vector<WindowInfos::const_iterator> WindowInfoIterators;

  class WindowRegions
  {
    public:
      WindowRegions(const WindowInfos& windows);

      WindowInfos::const_iterator find(WId wid) const;
      WindowInfos::const_iterator find(uint32_t pid, const QString& title) const;
      WindowInfos::const_iterator find(const QString& title) const;

      WindowInfoIterators find_all(uint32_t pid, const QString& title) const;
      WindowInfoIterators find_all(const QString& title) const;

      WId findId(const QString& title) const;
      WId findId(uint32_t pid, const QString& title = "") const;

      WindowInfos::const_iterator begin() const;
      WindowInfos::const_iterator end() const;

      WindowInfos::const_reverse_iterator rbegin() const;
      WindowInfos::const_reverse_iterator rend() const;

      WindowInfos::const_reverse_iterator windowAt(const QPoint& point) const;
      WId windowIdAt(const QPoint& point) const;

      bool hit( const WindowInfos::const_iterator& first_above,
                const QPoint& point,
                Rect* reg = 0,
                WId* wid = 0 ) const;

    private:
      const WindowInfos _windows;
  };

  class WindowMonitor:
    public QThread
  {
    Q_OBJECT

    public:

      typedef std::function<void(const WindowRegions&)> RegionsCallback;

      /**
       * @param own_id  id of own window where links are renderd (will be
       *                ignored)
       */
      WindowMonitor(RegionsCallback cb_regions_changed);


      /**
       * Get all visible windows in stacking order and filtered by minimum size
       */
      WindowRegions getWindows() const;

      /**
       * Set dimensions and position of visible/drawable desktop area
       * @param desktop
       */
      void setDesktopRect(const QRect& desktop);

    protected:

      QRect          _desktop_rect;
      WindowInfos    _regions,
                     _last_regions;
      int            _timeout;
      QTimer         _timer;
      RegionsCallback _cb_regions_changed;

      mutable int    _launcher_size;
      QStringList    _launchers;

      /**
       * Get all visible windows in stacking order and filtered by minimum size
       */
      WindowInfos getWindowInfos() const;

	protected slots:
      void check();

  };

  std::ostream& operator<<(std::ostream& strm, const QRect& r);

} // namespace LinksRouting

#endif /* WINDOW_MONITOR_HPP_ */
