//
//  GTMNSBezierPath+CGPath.m
//
//  Category for extracting a CGPathRef from a NSBezierPath
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
#import "GTMNSBezierPath+CGPath.h"
#import "GTMDefines.h"

@implementation NSBezierPath (GTMBezierPathCGPathAdditions)

//  Extract a CGPathRef from a NSBezierPath.
//
//  Args:
//
//  Returns:
//    Converted CGPathRef.
//    nil if failure.
- (CGPathRef)gtm_CGPath {
  CGMutablePathRef thePath = CGPathCreateMutable();
  if (!thePath) return nil;

  NSInteger elementCount = [self elementCount];

  // The maximum number of points is 3 for a NSCurveToBezierPathElement.
  // (controlPoint1, controlPoint2, and endPoint)
  NSPoint controlPoints[3];

  for (NSInteger i = 0; i < elementCount; i++) {
    switch ([self elementAtIndex:i associatedPoints:controlPoints]) {
      case NSMoveToBezierPathElement:
        CGPathMoveToPoint(thePath, &CGAffineTransformIdentity,
                              controlPoints[0].x, controlPoints[0].y);
        break;
      case NSLineToBezierPathElement:
        CGPathAddLineToPoint(thePath, &CGAffineTransformIdentity,
                              controlPoints[0].x, controlPoints[0].y);
        break;
      case NSCurveToBezierPathElement:
        CGPathAddCurveToPoint(thePath, &CGAffineTransformIdentity,
                              controlPoints[0].x, controlPoints[0].y,
                              controlPoints[1].x, controlPoints[1].y,
                              controlPoints[2].x, controlPoints[2].y);
        break;
      case NSClosePathBezierPathElement:
        CGPathCloseSubpath(thePath);
        break;
      default:  // COV_NF_START
        _GTMDevLog(@"Unknown element at [NSBezierPath (GTMBezierPathCGPathAdditions) cgPath]");
        break;  // COV_NF_END
    };
  }
  return (CGPathRef)GTMCFAutorelease(thePath);
}

@end
