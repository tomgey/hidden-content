/*!
 * @file float2.hpp
 * @brief Provides a simple 2d vector
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 12.10.2011
 */

#ifndef _FLOAT2_HPP_
#define _FLOAT2_HPP_

#include <cmath>
#include <iosfwd>
#include <sstream>

/**
 * A simple 2d vector
 */
struct float2
{
  float x;
  float y;

  /**
   *
   */
  float2(): x(), y() {}

  /**
   *
   */
  float2(float _x, float _y) : x(_x), y(_y) { }
  
  /**
   *
   */
  float* ptr()
  {
    return &x;
  }

  /**
   *
   */
  float length() const
  {
    return std::sqrt(x*x + y*y);
  }

  /**
   *
   */
  float2& normalize()
  {
    float l = length();
    x /= l;
    y /= l;

    return *this;
  }

  /**
   * Get a normal to this vector
   */
  float2 normal() const
  {
    return float2(y, -x);
  }

  /**
   *
   */
  float dot(const float2& rhs) const
  {
    return x * rhs.x + y * rhs.y;
  }

  float2& operator+=(const float2& rhs)
  {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }

  float2& operator-=(const float2& rhs)
  {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }

  float2& operator*=(float rhs)
  {
    x *= rhs;
    y *= rhs;
    return *this;
  }

  float2& operator/=(float rhs)
  {
    x /= rhs;
    y /= rhs;
    return *this;
  }

  bool operator==(const float2& rhs)
  {
    return x == rhs.x && y == rhs.y;
  }

  bool operator!=(const float2& rhs)
  {
    return !(*this == rhs);
  }
};

/**
 *
 */
inline float2 operator +(const float2& a, const float2& b)
{
  return float2(a.x + b.x, a.y + b.y);
}

/**
 *
 */
inline float2 operator +(const float2& a, float b)
{
  return float2(a.x + b, a.y + b);
}

/**
 *
 */
inline float2 operator -(const float2& a, const float2& b)
{
  return float2(a.x - b.x, a.y - b.y);
}

/**
 *
 */
inline float2 operator -(const float2& a)
{
  return float2(-a.x, -a.y);
}

/**
 *
 */
inline const float2 operator *(float a, const float2& v)
{
  return float2(a * v.x, a * v.y);
}

/**
 *
 */
inline const float2 operator *(const float2& v, float a)
{
  return a * v;
}

/**
 *
 */
inline const float operator *(const float2& a, const float2& b)
{
  return a.dot(b);
}

/**
 *
 */
inline const float2 operator /(const float2& v, float a)
{
  return float2(v.x / a, v.y / a);
}

/**
 *
 */
inline const float2 operator /(const float2& lhs, const float2& rhs)
{
  return float2(lhs.x / rhs.x, lhs.y / rhs.y);
}

/**
 *
 */
inline std::ostream& operator<<(std::ostream& strm, const float2& p)
{
  return strm << "(" << p.x << "|" << p.y << ")";
}

struct Rect
{
  float2 pos, size;

  Rect();
  Rect(const float2& pos, const float2& size);

#ifdef QRECT_H
  Rect(const QRect& r):
    pos(r.left(), r.top()),
    size(r.width(), r.height())
  {}
#endif

  float l() const { return pos.x; }
  float r() const { return pos.x + size.x; }

  float t() const { return pos.y; }
  float b() const { return pos.y + size.y; }

  void expand(const float2& p);
  bool contains(float x, float y, float margin = 0.f) const;

  std::string toString(bool round = false) const;

  Rect& operator *=(float a);
};

inline const Rect operator *(float a, const Rect& r)
{
  return Rect(r) *= a;
}

inline std::ostream& operator<<(std::ostream& strm, const Rect& r)
{
  return strm << r.pos << " -> " << r.size;
}

#endif /* _FLOAT2_HPP_ */
