/*
 * color_helpers.cxx
 *
 *  Created on: Nov 17, 2016
 *      Author: tom
 */

#include "color_helpers.h"

#include <GL/gl.h>

//------------------------------------------------------------------------------
QColor operator*(float fac, const QColor& color)
{
  return QColor( static_cast<int>(fac * color.red() + .5f),
                 static_cast<int>(fac * color.green() + .5f),
                 static_cast<int>(fac * color.blue() + .5f),
                 static_cast<int>(fac * color.alpha() + .5f) );
}

//------------------------------------------------------------------------------
QColor& operator*=(QColor& color, float fac)
{
  color = fac * color;
  return color;
}

//------------------------------------------------------------------------------
void qtGlColor(const QColor& c)
{
  glColor4f(c.redF(), c.greenF(), c.blueF(), c.alphaF());
}
