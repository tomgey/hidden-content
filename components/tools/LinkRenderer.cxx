/*
 * LinkRenderer.cxx
 *
 *  Created on: May 9, 2016
 *      Author: tom
 */

#include <QRect> // Include before float2.hpp to enable QRect converting

#include "LinkRenderer.hpp"
#include "NodeRenderer.hpp"
#include "color_helpers.h"

#include <QDebug>
#include <GL/gl.h>

namespace LinksRouting
{
  //----------------------------------------------------------------------------
  bool LinkRenderer::renderLinks(const LinkDescription::LinkList& links)
  {
    bool rendered_anything = false;
    int pass = 1;

    NodeRenderer renderer;
    renderer.setUseStencil(true);
    renderer.setLineWidth(3);

    for(auto link = links.begin(); link != links.end(); ++link)
    {
      QColor color_cur = link->_color,
             color_covered_cur = 0.4f * color_cur;
      renderer.setColor(color_cur, color_covered_cur);

      HyperEdgeQueue hedges_open;
      HyperEdgeSet   hedges_done;

      hedges_open.push(link->_link.get());
      do
      {
        const LinkDescription::HyperEdge* hedge = hedges_open.front();
        hedges_open.pop();

        if( hedges_done.find(hedge) != hedges_done.end() )
          continue;

        if( hedge->get<bool>("covered") || hedge->get<bool>("outside") )
          qtGlColor(color_covered_cur);
        else
          qtGlColor(color_cur);

        auto fork = hedge->getHyperEdgeDescription();
        if( !fork )
        {
          rendered_anything |=
           renderer.renderNodes( hedge->getNodes(),
                                 &hedges_open,
                                 &hedges_done,
                                 false,
                                 pass );
        }
        else
        {
          // Don't draw where region highlights already have been drawn
//          glStencilFunc(GL_EQUAL, 0, 1);
//          glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

          LinkDescription::nodes_t nodes;
          for( auto& segment: fork->outgoing )
          {
            if(     pass == 1
                && !segment.trail.empty()
                /*&& segment.trail.front().x >= 0
                && segment.trail.front().y >= 24*/ )
            {
              // Draw path
              float widen_size = 0.f;
              if(   !segment.nodes.empty()
                  && segment.nodes.back()->getChildren().empty()
                  && segment.get<bool>("widen-end", true) )
              {
                if( !segment.nodes.back()
                            ->get<std::string>("virtual-outside").empty() )
                  widen_size = 13;
                else
                  widen_size = 55;
              }
              line_borders_t region = calcLineBorders( segment.trail,
                                                       3,
                                                       false,
                                                       widen_size );

              qtGlColor(   segment.get<bool>("covered")
                         ? color_covered_cur
                         : color_cur );
              glBegin(GL_TRIANGLE_STRIP);
              for( auto first = std::begin(region.first),
                        second = std::begin(region.second);
                   first != std::end(region.first);
                   ++first,
                   ++second )
              {
                glVertex2f(first->x, first->y);
                glVertex2f(second->x, second->y);
              }
              glEnd();
            }

            // Collect nodes for drawing them after the links to prevent links
            // crossing regions.
//            nodes.insert( nodes.end(),
//                          segment.nodes.begin(),
//                          segment.nodes.end() );

            rendered_anything |= renderer.renderNodes( segment.nodes,
                                                       &hedges_open,
                                                       &hedges_done,
                                                       false,
                                                       pass );

//            std::cout << "addSegmentNodes" << std::endl;
//            LinkDescription::printNodeList(segment.nodes);
          }

          rendered_anything |= renderer.renderNodes( nodes,
                                                     &hedges_open,
                                                     &hedges_done,
                                                     false,
                                                     pass );
        }

        hedges_done.insert(hedge);
      } while( !hedges_open.empty() );
    }

    return rendered_anything;
  }

  //----------------------------------------------------------------------------
  bool LinkRenderer::wouldRenderLinks( const Rect& bbox,
                                       const LinkDescription::LinkList& links )
  {
    NodeRenderer node_renderer;

    for(auto link = links.begin(); link != links.end(); ++link)
    {
      HyperEdgeQueue hedges_open;
      HyperEdgeSet   hedges_done;

      hedges_open.push(link->_link.get());
      do
      {
        const LinkDescription::HyperEdge* hedge = hedges_open.front();
        hedges_open.pop();

        if( hedges_done.find(hedge) != hedges_done.end() )
          continue;

        auto fork = hedge->getHyperEdgeDescription();
        if( !fork )
        {

          if( node_renderer.wouldRenderNodes( bbox,
                                              hedge->getNodes(),
                                              &hedges_open,
                                              &hedges_done,
                                              false ) )
            return true;
        }
        else
        {
          LinkDescription::nodes_t nodes;
          for( auto& segment: fork->outgoing )
          {
            if( !segment.trail.empty() )
            {
              // Draw path
              float widen_size = 0.f;
              if(   !segment.nodes.empty()
                  && segment.nodes.back()->getChildren().empty()
                  && segment.get<bool>("widen-end", true) )
              {
                if( !segment.nodes.back()
                            ->get<std::string>("virtual-outside").empty() )
                  widen_size = 13;
                else
                  widen_size = 55;
              }
              line_borders_t region = calcLineBorders( segment.trail,
                                                       3,
                                                       false,
                                                       widen_size );

              for( auto first = std::begin(region.first),
                        second = std::begin(region.second);
                   first != std::end(region.first);
                   ++first,
                   ++second )
              {
                glVertex2f(first->x, first->y);
                glVertex2f(second->x, second->y);
              }
            }

            if( node_renderer.wouldRenderNodes( bbox,
                                                segment.nodes,
                                                &hedges_open,
                                                &hedges_done,
                                                false ) )
              return true;
          }

          if( node_renderer.wouldRenderNodes( bbox,
                                              nodes,
                                              &hedges_open,
                                              &hedges_done,
                                              false ) )
            return true;
        }

        hedges_done.insert(hedge);
      } while( !hedges_open.empty() );
    }

    return false;
  }

} // namespace LinksRouting
