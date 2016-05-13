/*
 * Rect.cxx
 *
 *  Created on: 21.11.2012
 *      Author: tom
 */

#include "float2.hpp"
#include <algorithm>

//------------------------------------------------------------------------------
Rect::Rect():
  pos(0,0),
  size(-1,-1)
{

}

//------------------------------------------------------------------------------
Rect::Rect(const float2& pos, const float2& size):
  pos(pos),
  size(size)
{

}

//------------------------------------------------------------------------------
bool Rect::isValid() const
{
  return size.x >= 0 && size.y >= 0;
}

//------------------------------------------------------------------------------
void Rect::expand(const float2& p)
{
  if( size.x < 0 || size.y < 0 )
  {
    pos = p;
    size = float2(0,0);
  }
  else
  {
    float left   = std::min(p.x, l()),
          right  = std::max(p.x, r()),
          top    = std::min(p.y, t()),
          bottom = std::max(p.y, b());

    pos.x = left;
    pos.y = top;
    size.x = right - left;
    size.y = bottom - top;
  }
}

//------------------------------------------------------------------------------
void Rect::translate(const float2& d)
{
  pos += d;
}

//------------------------------------------------------------------------------
bool Rect::contains(float x, float y, float margin) const
{
  return x >= pos.x - margin && x <= pos.x + size.x + margin
      && y >= pos.y - margin && y <= pos.y + size.y + margin;
}

//------------------------------------------------------------------------------
bool Rect::contains(const float2& pos, float margin) const
{
  return contains(pos.x, pos.y, margin);
}

//------------------------------------------------------------------------------
bool Rect::intersects(const Rect& other) const
{
  return !(  other.r() < l() || r() < other.l()
          || other.b() < t() || b() < other.t() );
}

//------------------------------------------------------------------------------
std::string Rect::toString(bool round) const
{
  std::stringstream strm;
  auto do_round =  [round](float num)->float{ if(round) return static_cast<float>(std::floor(num + 0.5)); else return num; };

  strm << "{\"x\":" << do_round(pos.x) << ","
           "\"y\":" << do_round(pos.y) << ","
           "\"width\":" << do_round(size.x) << ","
           "\"height\":" << do_round(size.y) << "}";
  return strm.str();
}

//------------------------------------------------------------------------------
Rect& Rect::operator *=(float a)
{
  pos *= a;
  size *= a;
  return *this;
}

//------------------------------------------------------------------------------
Rect Rect::operator+(const float2& offset) const
{
  return Rect( pos + offset, size );
}

//------------------------------------------------------------------------------
Rect Rect::operator-(const float2& offset) const
{
  return Rect( pos - offset, size );
}

//------------------------------------------------------------------------------
bool Rect::operator==(const Rect& rhs) const
{
  return pos == rhs.pos && size == rhs.size;
}

//------------------------------------------------------------------------------
bool Rect::operator!=(const Rect& rhs) const
{
  return !(*this == rhs);
}

//------------------------------------------------------------------------------
bool Rect::operator<(const Rect& rhs) const
{
  if( pos.x < rhs.pos.x )
    return true;
  else if( pos.x == rhs.pos.x )
    return size.x < rhs.size.x;
  else
    return false;
}

//------------------------------------------------------------------------------
std::istream& operator>>(std::istream& strm, Rect& rect)
{
  float l, r, t, b;
  strm >> l >> t >> r >> b;
  rect = Rect( float2(l, t), float2(r - l, b - t) );
  return strm;
}

//------------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& strm, const Rect& r)
{
  return (strm << r.l() << ' ' << r.t() << ' ' << r.r() << ' ' << r.b());
}
