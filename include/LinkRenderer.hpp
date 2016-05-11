/*
 * LinkRenderer.hpp
 *
 *  Created on: May 9, 2016
 *      Author: tom
 */

#ifndef LR_LINKRENDERER_HPP_
#define LR_LINKRENDERER_HPP_

#include "linkdescription.h"

namespace LinksRouting
{
  class LinkRenderer
  {
    public:
      bool renderLinks(const LinkDescription::LinkList& links);

      /**
       * Check if anything would be rendered inside the given bounding box
       */
      bool wouldRenderLinks( const Rect& bbox,
                             const LinkDescription::LinkList& links );
  };

} // namespace LinksRouting

#endif /* LR_LINKRENDERER_HPP_ */
