/*
 * color_helpers.h
 *
 *  Created on: Nov 17, 2016
 *      Author: tom
 */

#ifndef LR_COLOR_HELPERS_H_
#define LR_COLOR_HELPERS_H_

#include <QColor>

QColor operator*(float fac, const QColor& color);
QColor& operator*=(QColor& color, float fac);

void qtGlColor(const QColor& c);

#endif /* LR_COLOR_HELPERS_H_ */
