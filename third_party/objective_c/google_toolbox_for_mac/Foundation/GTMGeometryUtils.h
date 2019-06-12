//
//  GTMGeometryUtils.h
//
//  Utilities for geometrical utilities such as conversions
//  between different types.
//
//  Copyright 2006-2008 Google Inc.
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

#import <Foundation/Foundation.h>
#import "GTMDefines.h"
#if GTM_IPHONE_SDK
#import <CoreGraphics/CoreGraphics.h>
#endif //  GTM_IPHONE_SDK

#ifdef __cplusplus
extern "C" {
#endif

enum {
  GTMScaleProportionally = 0,   // Fit proportionally
  GTMScaleToFit,                // Forced fit (distort if necessary)
  GTMScaleNone,                 // Don't scale (clip)
  GTMScaleToFillProportionally = 101  // Scale proportionally to fill area
};
typedef NSUInteger GTMScaling;

enum {
  GTMRectAlignCenter = 0,
  GTMRectAlignTop,
  GTMRectAlignTopLeft,
  GTMRectAlignTopRight,
  GTMRectAlignLeft,
  GTMRectAlignBottom,
  GTMRectAlignBottomLeft,
  GTMRectAlignBottomRight,
  GTMRectAlignRight
};
typedef NSUInteger GTMRectAlignment;

#pragma mark -
#pragma mark CG - Point On Rect
/// Return middle of min X side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of min X side of rect
GTM_INLINE CGPoint GTMCGMidMinX(CGRect rect) {
  return CGPointMake(CGRectGetMinX(rect), CGRectGetMidY(rect));
}

/// Return middle of max X side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of max X side of rect
GTM_INLINE CGPoint GTMCGMidMaxX(CGRect rect) {
  return CGPointMake(CGRectGetMaxX(rect), CGRectGetMidY(rect));
}

/// Return middle of max Y side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of max Y side of rect
GTM_INLINE CGPoint GTMCGMidMaxY(CGRect rect) {
  return CGPointMake(CGRectGetMidX(rect), CGRectGetMaxY(rect));
}

/// Return middle of min Y side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of min Y side of rect
GTM_INLINE CGPoint GTMCGMidMinY(CGRect rect) {
  return CGPointMake(CGRectGetMidX(rect), CGRectGetMinY(rect));
}

/// Return center of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the center of rect
GTM_INLINE CGPoint GTMCGCenter(CGRect rect) {
  return CGPointMake(CGRectGetMidX(rect), CGRectGetMidY(rect));
}

#pragma mark -
#pragma mark CG - Rect-Size Conversion

/// Return size of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    size of rectangle
GTM_INLINE CGSize GTMCGRectSize(CGRect rect) {
  return CGSizeMake(CGRectGetWidth(rect), CGRectGetHeight(rect));
}

/// Return rectangle of size
//
//  Args:
//    size - size
//
//  Returns:
//    rectangle of size (origin 0,0)
GTM_INLINE CGRect GTMCGRectOfSize(CGSize size) {
  return CGRectMake(0.0f, 0.0f, size.width, size.height);
}

#pragma mark -
#pragma mark CG - Rect Scaling and Alignment

///  Scales an CGRect
//
//  Args:
//    inRect: Rect to scale
//    xScale: fraction to scale (1.0 is 100%)
//    yScale: fraction to scale (1.0 is 100%)
//
//  Returns:
//    Converted Rect
GTM_INLINE CGRect GTMCGRectScale(CGRect inRect, CGFloat xScale, CGFloat yScale) {
  return CGRectMake(inRect.origin.x, inRect.origin.y,
                    inRect.size.width * xScale, inRect.size.height * yScale);
}


/// Align rectangles
//
//  Args:
//    alignee - rect to be aligned
//    aligner - rect to be aligned from
//    alignment - way to align the rectangles
CGRect GTMCGAlignRectangles(CGRect alignee, CGRect aligner,
                            GTMRectAlignment alignment);
/// Scale rectangle
//
//  Args:
//    scalee - rect to be scaled
//    size - size to scale to
//    scaling - way to scale the rectangle
CGRect GTMCGScaleRectangleToSize(CGRect scalee, CGSize size,
                                 GTMScaling scaling);

#pragma mark -
#pragma mark CG - Miscellaneous

/// Calculate the distance between two points.
//
//  Args:
//    pt1 first point
//    pt2 second point
//
//  Returns:
//    Distance
GTM_INLINE CGFloat GTMCGDistanceBetweenPoints(CGPoint pt1, CGPoint pt2) {
  CGFloat dX = pt1.x - pt2.x;
  CGFloat dY = pt1.y - pt2.y;
#if CGFLOAT_IS_DOUBLE
  return sqrt(dX * dX + dY * dY);
#else
  return sqrtf(dX * dX + dY * dY);
#endif
}

#if !GTM_IPHONE_SDK
// iPhone does not have NSTypes defined, only CGTypes. So no NSRect, NSPoint etc.

#pragma mark -
#pragma mark NS <-> CG Rect Conversion

///  Convert from a NSRect to a CGRect.
//
  ///  NSRect are relative to 0,0 in lower left;
///  CGRect are relative to 0,0 in lower left
//
//  Args:
//    inRect: NSRect to convert
//
//  Returns:
//    Converted CGRect
GTM_INLINE CGRect GTMNSRectToCGRect(NSRect inRect) {
  CGRect cg = {
    .origin = {.x = inRect.origin.x, .y = inRect.origin.y},
    .size = {.width = inRect.size.width, .height = inRect.size.height}
  };
  return cg;
}


#pragma mark -
#pragma mark NS <-> CG Size Conversion

///  Convert from a NSSize to a CGSize.
//
//  Args:
//    inSize: NSSize to convert
//
//  Returns:
//    Converted CGSize
GTM_INLINE CGSize GTMNSSizeToCGSize(NSSize inSize) {
  CGSize cg = {.width = inSize.width, .height = inSize.height};
  return cg;
}

#pragma mark -
#pragma mark NS - Point On Rect

/// Return middle of min X side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of min X side of rect
GTM_INLINE NSPoint GTMNSMidMinX(NSRect rect) {
  return NSMakePoint(NSMinX(rect), NSMidY(rect));
}

/// Return middle of max X side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of max X side of rect
GTM_INLINE NSPoint GTMNSMidMaxX(NSRect rect) {
  return NSMakePoint(NSMaxX(rect), NSMidY(rect));
}

/// Return middle of max Y side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of max Y side of rect
GTM_INLINE NSPoint GTMNSMidMaxY(NSRect rect) {
  return NSMakePoint(NSMidX(rect), NSMaxY(rect));
}

/// Return middle of min Y side of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the middle of min Y side of rect
GTM_INLINE NSPoint GTMNSMidMinY(NSRect rect) {
  return NSMakePoint(NSMidX(rect), NSMinY(rect));
}

/// Return center of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    point located in the center of rect
GTM_INLINE NSPoint GTMNSCenter(NSRect rect) {
  return NSMakePoint(NSMidX(rect), NSMidY(rect));
}

#pragma mark -
#pragma mark NS - Rect-Size Conversion

/// Return size of rectangle
//
//  Args:
//    rect - rectangle
//
//  Returns:
//    size of rectangle
GTM_INLINE NSSize GTMNSRectSize(NSRect rect) {
  return NSMakeSize(NSWidth(rect), NSHeight(rect));
}

/// Return rectangle of size
//
//  Args:
//    size - size
//
//  Returns:
//    rectangle of size (origin 0,0)
GTM_INLINE NSRect GTMNSRectOfSize(NSSize size) {
  return NSMakeRect(0.0f, 0.0f, size.width, size.height);
}

#pragma mark -
#pragma mark NS - Rect Scaling and Alignment

///  Scales an NSRect
//
//  Args:
//    inRect: Rect to scale
//    xScale: fraction to scale (1.0 is 100%)
//    yScale: fraction to scale (1.0 is 100%)
//
//  Returns:
//    Converted Rect
GTM_INLINE NSRect GTMNSRectScale(NSRect inRect, CGFloat xScale, CGFloat yScale) {
  return NSMakeRect(inRect.origin.x, inRect.origin.y,
                    inRect.size.width * xScale, inRect.size.height * yScale);
}

/// Align rectangles
//
//  Args:
//    alignee - rect to be aligned
//    aligner - rect to be aligned from
GTM_INLINE NSRect GTMNSAlignRectangles(NSRect alignee, NSRect aligner,
                                       GTMRectAlignment alignment) {
  return NSRectFromCGRect(GTMCGAlignRectangles(GTMNSRectToCGRect(alignee),
                                               GTMNSRectToCGRect(aligner),
                                               alignment));
}

/// Align a rectangle to another
//
//  Args:
//    scalee - rect to be scaled
//    scaler - rect to scale to
//    scaling - way to scale the rectangle
//    alignment - way to align the scaled rectangle
GTM_INLINE NSRect GTMNSScaleRectToRect(NSRect scalee,
                                       NSRect scaler,
                                       GTMScaling scaling,
                                       GTMRectAlignment alignment) {

  return NSRectFromCGRect(
           GTMCGAlignRectangles(
             GTMCGScaleRectangleToSize(GTMNSRectToCGRect(scalee),
                                       GTMNSSizeToCGSize(scaler.size),
                                       scaling),
             GTMNSRectToCGRect(scaler),
             alignment));
}

/// Scale rectangle
//
//  Args:
//    scalee - rect to be scaled
//    size - size to scale to
//    scaling - way to scale the rectangle
GTM_INLINE NSRect GTMNSScaleRectangleToSize(NSRect scalee, NSSize size,
                                            GTMScaling scaling) {
  return NSRectFromCGRect(GTMCGScaleRectangleToSize(GTMNSRectToCGRect(scalee),
                                                     GTMNSSizeToCGSize(size),
                                                     scaling));
}

#pragma mark -
#pragma mark NS - Miscellaneous

/// Calculate the distance between two points.
//
//  Args:
//    pt1 first point
//    pt2 second point
//
//  Returns:
//    Distance
GTM_INLINE CGFloat GTMNSDistanceBetweenPoints(NSPoint pt1, NSPoint pt2) {
  CGPoint cgpt1 = {.x = pt1.x, .y = pt1.y};
  CGPoint cgpt2 = {.x = pt2.x, .y = pt2.y};
  return GTMCGDistanceBetweenPoints(cgpt1, cgpt2);
}

#endif //  !GTM_IPHONE_SDK

#ifdef __cplusplus
}
#endif
