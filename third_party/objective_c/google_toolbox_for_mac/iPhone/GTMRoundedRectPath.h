//
//  GTMRoundedRectPath.h
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

#import <CoreGraphics/CoreGraphics.h>

#import "GTMDefines.h"

GTM_EXTERN_C_BEGIN

//  Inscribe a round rectangle inside of rectangle |rect| with a corner radius
//  of |radius|
//
//  Args:
//    rect: outer rectangle to inscribe into
//    radius: radius of the corners. |radius| is clamped internally
//            to be no larger than the smaller of half |rect|'s width or height
void GTMCGContextAddRoundRect(CGContextRef context,
                              CGRect rect,
                              CGFloat radius);

//  Adds a path which is a round rectangle inscribed inside of rectangle |rect|
//  with a corner radius of |radius|
//
//  Args:
//    path: path to add the rounded rectangle to
//       m: matrix modifying the round rect
//    rect: outer rectangle to inscribe into
//    radius: radius of the corners. |radius| is clamped internally
//            to be no larger than the smaller of half |rect|'s width or height
void GTMCGPathAddRoundRect(CGMutablePathRef path,
                           const CGAffineTransform *m,
                           CGRect rect,
                           CGFloat radius);

// Allocates a new rounded corner rectangle path.
// DEPRECATED. Please use one of the above.
CGPathRef GTMCreateRoundedRectPath(CGRect rect, CGFloat radius);

GTM_EXTERN_C_END
