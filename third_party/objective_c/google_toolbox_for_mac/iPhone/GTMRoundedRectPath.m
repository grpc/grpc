//
//  GTMRoundedRectPath.m
//
//  Copyright 2010 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//
#include "GTMRoundedRectPath.h"

#include <Foundation/Foundation.h>

void GTMCGContextAddRoundRect(CGContextRef context,
                              CGRect rect,
                              CGFloat radius) {
  if (!CGRectIsEmpty(rect)) {
    if (radius > 0.0) {
      // Clamp radius to be no larger than half the rect's width or height.
      radius = MIN(radius, 0.5f * MIN(rect.size.width, rect.size.height));

      CGPoint topLeft = CGPointMake(CGRectGetMinX(rect), CGRectGetMaxY(rect));
      CGPoint topRight = CGPointMake(CGRectGetMaxX(rect), CGRectGetMaxY(rect));
      CGPoint bottomRight = CGPointMake(CGRectGetMaxX(rect),
                                        CGRectGetMinY(rect));

      CGContextMoveToPoint(context, CGRectGetMidX(rect), CGRectGetMaxY(rect));
      CGContextAddArcToPoint(context, topLeft.x, topLeft.y, rect.origin.x,
                             rect.origin.y, radius);
      CGContextAddArcToPoint(context, rect.origin.x, rect.origin.y,
                             bottomRight.x, bottomRight.y, radius);
      CGContextAddArcToPoint(context, bottomRight.x, bottomRight.y,
                             topRight.x, topRight.y, radius);
      CGContextAddArcToPoint(context, topRight.x, topRight.y,
                             topLeft.x, topLeft.y, radius);
      CGContextAddLineToPoint(context, CGRectGetMidX(rect), CGRectGetMaxY(rect));
    } else {
      CGContextAddRect(context, rect);
    }
  }
}

void GTMCGPathAddRoundRect(CGMutablePathRef path,
                           const CGAffineTransform *m,
                           CGRect rect,
                           CGFloat radius) {
  if (!CGRectIsEmpty(rect)) {
    if (radius > 0.0) {
      // Clamp radius to be no larger than half the rect's width or height.
      radius = MIN(radius, 0.5f * MIN(rect.size.width, rect.size.height));

      CGPoint topLeft = CGPointMake(CGRectGetMinX(rect), CGRectGetMaxY(rect));
      CGPoint topRight = CGPointMake(CGRectGetMaxX(rect), CGRectGetMaxY(rect));
      CGPoint bottomRight = CGPointMake(CGRectGetMaxX(rect),
                                        CGRectGetMinY(rect));

      CGPathMoveToPoint(path, m, CGRectGetMidX(rect), CGRectGetMaxY(rect));
      CGPathAddArcToPoint(path, m, topLeft.x, topLeft.y,
                          rect.origin.x, rect.origin.y, radius);
      CGPathAddArcToPoint(path, m, rect.origin.x, rect.origin.y,
                          bottomRight.x, bottomRight.y, radius);
      CGPathAddArcToPoint(path, m, bottomRight.x, bottomRight.y,
                          topRight.x, topRight.y, radius);
      CGPathAddArcToPoint(path, m, topRight.x, topRight.y,
                          topLeft.x, topLeft.y, radius);
      CGPathAddLineToPoint(path, m, CGRectGetMidX(rect), CGRectGetMaxY(rect));
    } else {
      CGPathAddRect(path, m, rect);
    }
  }
}

CGPathRef GTMCreateRoundedRectPath(CGRect rect, CGFloat radius) {
  CGPathRef immutablePath = NULL;
  CGMutablePathRef path = CGPathCreateMutable();
  if (path) {
    GTMCGPathAddRoundRect(path, NULL, rect, radius);
    CGPathCloseSubpath(path);
    immutablePath = CGPathCreateCopy(path);
    CGPathRelease(path);
  }
  return immutablePath;
}
