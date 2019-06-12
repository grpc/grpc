//
//  GTMNSBezierPath+RoundRect.h
//
//  Category for adding utility functions for creating
//  round rectangles.
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

#import <Cocoa/Cocoa.h>
#import "GTMDefines.h"

///  Category for adding utility functions for creating round rectangles.
@interface NSBezierPath (GMBezierPathRoundRectAdditions)

///  Inscribe a round rectangle inside of rectangle |rect| with a corner radius of |radius|
//
//  Args:
//    rect: outer rectangle to inscribe into
//    radius: radius of the corners. |radius| is clamped internally
//            to be no larger than the smaller of half |rect|'s width or height
//
//  Returns:
//    Auto released NSBezierPath
+ (NSBezierPath *)gtm_bezierPathWithRoundRect:(NSRect)rect
                                 cornerRadius:(CGFloat)radius;

///  Inscribe a round rectangle inside of rectangle |rect| with corner radii specified
//
//  Args:
//    rect: outer rectangle to inscribe into
//    radius*: radii of the corners
//
//  Returns:
//    Auto released NSBezierPath
+ (NSBezierPath *)gtm_bezierPathWithRoundRect:(NSRect)rect
                          topLeftCornerRadius:(CGFloat)radiusTL
                         topRightCornerRadius:(CGFloat)radiusTR
                       bottomLeftCornerRadius:(CGFloat)radiusBL
                      bottomRightCornerRadius:(CGFloat)radiusBR;

///  Adds a path which is a round rectangle inscribed inside of rectangle |rect| with a corner radius of |radius|
//
//  Args:
//    rect: outer rectangle to inscribe into
//    radius: radius of the corners. |radius| is clamped internally
//            to be no larger than the smaller of half |rect|'s width or height
- (void)gtm_appendBezierPathWithRoundRect:(NSRect)rect
                             cornerRadius:(CGFloat)radius;

///  Adds a path which is a round rectangle inscribed inside of rectangle |rect| with a corner radii specified
//
//  Args:
//    rect: outer rectangle to inscribe into
//    radius*: radii of the corners
- (void)gtm_appendBezierPathWithRoundRect:(NSRect)rect
                      topLeftCornerRadius:(CGFloat)radiusTL
                     topRightCornerRadius:(CGFloat)radiusTR
                   bottomLeftCornerRadius:(CGFloat)radiusBL
                  bottomRightCornerRadius:(CGFloat)radiusBR;
@end
