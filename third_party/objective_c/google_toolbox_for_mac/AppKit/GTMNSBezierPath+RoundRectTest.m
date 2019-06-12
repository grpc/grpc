//
//  GTMNSBezierPath+RoundRectTest.m
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

#import "GTMNSBezierPath+RoundRect.h"
#import "GTMSenTestCase.h"

@interface GTMNSBezierPath_RoundRectTest : GTMTestCase
@end

@implementation GTMNSBezierPath_RoundRectTest

- (void)testRoundRects {
  NSBitmapImageRep *offscreenRep = [[[NSBitmapImageRep alloc]
                                     initWithBitmapDataPlanes:NULL
                                                   pixelsWide:100
                                                   pixelsHigh:100
                                                bitsPerSample:8
                                              samplesPerPixel:4
                                                     hasAlpha:YES
                                                     isPlanar:NO
                                               colorSpaceName:NSDeviceRGBColorSpace
                                                 bitmapFormat:NSAlphaFirstBitmapFormat
                                                  bytesPerRow:0
                                                 bitsPerPixel:0] autorelease];

  // set offscreen context
  NSGraphicsContext *nsContext =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:offscreenRep];
  [NSGraphicsContext setCurrentContext:nsContext];
  NSRect theRects[] = {
    NSMakeRect(0.0, 10.0, 0.0, 0.0), //Empty Rect test
    NSMakeRect(50.0, 10.0, 30.0, 30.0), //Square Test
    NSMakeRect(100.0, 10.0, 1.0, 2.0), //Small Test
    NSMakeRect(120.0, 10.0, 15.0, 20.0), //Medium Test
    NSMakeRect(140.0, 10.0, 150.0, 30.0),  //Large Test
    NSMakeRect(300.0, 10.0, 150.0, 30.0)  //Large Test 2 (for different radius)
  };
  const NSUInteger theRectCount = sizeof(theRects) / sizeof(theRects[0]);

  // Line Width Tests
  CGFloat theLineWidths[] = { 0.5, 50.0, 2.0 };
  const NSUInteger theLineWidthCount =
      sizeof(theLineWidths) / sizeof(theLineWidths[0]);
  NSUInteger i, j;

  for (i = 0; i < theLineWidthCount; ++i) {
    for (j = 0; j < theRectCount; ++j) {
      CGFloat cornerRadius = ( (j < (theRectCount - 1)) ? 20.0 : 0.0 );
      NSBezierPath *roundRect = [NSBezierPath gtm_bezierPathWithRoundRect:theRects[j]
                                                             cornerRadius:cornerRadius];
      [roundRect setLineWidth: theLineWidths[i]];
      [roundRect stroke];
      CGFloat newWidth = 35.0;
      if (i < theLineWidthCount - 1) {
        newWidth += theLineWidths[i + 1] + theLineWidths[i];
      }
      theRects[j].origin.y += newWidth;
    }
  }

  // Fill test
  NSColor *theColors[] = {
    [NSColor colorWithCalibratedRed:1.0 green:0.0 blue:0.0 alpha:1.0],
    [NSColor colorWithCalibratedRed:0.2 green:0.4 blue:0.6 alpha:0.4]
  };
  const NSUInteger theColorCount = sizeof(theColors)/sizeof(theColors[0]);

  for (i = 0; i < theColorCount; ++i) {
    for (j = 0; j < theRectCount; ++j) {
      CGFloat cornerRadius = ( (j < (theRectCount - 1)) ? 10.0 : 0.0 );
      NSBezierPath *roundRect = [NSBezierPath gtm_bezierPathWithRoundRect:theRects[j]
                                                             cornerRadius:cornerRadius];
      [theColors[i] setFill];
      [roundRect fill];
      theRects[j].origin.y += 35.0;
    }
  }

  // Flatness test
  CGFloat theFlatness[] = {0.0, 0.1, 1.0, 10.0};
  const NSUInteger theFlatnessCount = sizeof(theFlatness)/sizeof(theFlatness[0]);

  for (i = 0; i < theFlatnessCount; i++) {
    for (j = 0; j < theRectCount; ++j) {
      CGFloat cornerRadius = ( (j < (theRectCount - 1)) ? 6.0 : 0.0 );
      NSBezierPath *roundRect = [NSBezierPath gtm_bezierPathWithRoundRect:theRects[j]
                                                             cornerRadius:cornerRadius];
      [roundRect setFlatness:theFlatness[i]];
      [roundRect stroke];
      theRects[j].origin.y += 35.0;
    }
  }

  // Different radii
  NSRect bigRect = NSMakeRect(50, 440, 200, 40);
  NSBezierPath *roundRect = [NSBezierPath gtm_bezierPathWithRoundRect:bigRect
                                                  topLeftCornerRadius:0.0
                                                 topRightCornerRadius:5.0
                                               bottomLeftCornerRadius:10.0
                                              bottomRightCornerRadius:20.0];
  [roundRect setLineWidth:5.0];
  [roundRect stroke];
}


@end
