//
//  GTMNSColor+Luminance.m
//
//  Copyright 2009 Google Inc.
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

#import "GTMDefines.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

#import "GTMNSColor+Luminance.h"

static const CGFloat kGTMLuminanceDarkCutoff = 0.6;

@implementation NSColorSpace (GTMNSColorSpaceLuminanceHelpers)

// TODO(alcor): we may want to keep one of these around for performance reasons
+ (NSColorSpace *)gtm_labColorSpace {
  // Observer= 2°, Illuminant= D65
  // TODO(alcor): these should come from ColorSync
  CGFloat whitePoint[3] = {0.95047, 1.0, 1.08883};
  CGFloat blackPoint[3] = {0, 0, 0};
  CGFloat range[4] = {-127, 127, -127, 127};
  CGColorSpaceRef cs = CGColorSpaceCreateLab(whitePoint, blackPoint, range);
  NSColorSpace *space = nil;
  if (cs) {
    space = [[[NSColorSpace alloc] initWithCGColorSpace:cs] autorelease];
    CGColorSpaceRelease(cs);
  }
  return space;
}
@end

@implementation NSColor (GTMLuminance)

- (NSColor *)labColor {
  return [self colorUsingColorSpace:[NSColorSpace gtm_labColorSpace]];
}

- (CGFloat)gtm_luminance {
  CGFloat lab[4];
  lab[0] = 0.0;
  [[self labColor] getComponents:lab];
  return lab[0] / 100.0;
}

- (NSColor *)gtm_colorByAdjustingLuminance:(CGFloat)luminance
                                saturation:(CGFloat)saturation {
  CGFloat lab[4];
  [[self labColor] getComponents:lab];
  lab[0] *= 1.0 + luminance;
  // If luminance is greater than 100, we desaturate it so that we don't get
  // wild colors coming out of the forumula
  if (lab[0] > 100) {
    CGFloat clipping = lab[0] - 100;
    CGFloat desaturation = (50.0 - clipping) / 50.0;
    saturation = MIN(saturation, desaturation);
  }
  lab[1] *= saturation;
  lab[2] *= saturation;
  return [NSColor colorWithColorSpace:[NSColorSpace gtm_labColorSpace]
                           components:lab
                                count:sizeof(lab) / sizeof(lab[0])];
}

- (NSColor *)gtm_colorByAdjustingLuminance:(CGFloat)luminance {
  return [self gtm_colorByAdjustingLuminance:luminance saturation:1.0];
}

// TODO(alcor): these constants are largely made up, come up with a consistent
// set of values or at least guidelines
- (NSColor *)gtm_colorAdjustedFor:(GTMColorationUse)use {
  NSColor *color = nil;
  switch (use) {
    case GTMColorationBaseHighlight:
      color = [self gtm_colorByAdjustingLuminance:0.15];
      break;
    case GTMColorationBaseMidtone:
      color = self;
      break;
    case GTMColorationBaseShadow:
      color = [self gtm_colorByAdjustingLuminance:-0.15];
      break;
    case GTMColorationBasePenumbra:
      color = [self gtm_colorByAdjustingLuminance:-0.10];
      break;
    case GTMColorationLightHighlight:
      color = [self gtm_colorByAdjustingLuminance:0.25];
      color = [color blendedColorWithFraction:0.9 ofColor:[NSColor whiteColor]];
      break;
    case GTMColorationLightMidtone:
      color = [self blendedColorWithFraction:0.8 ofColor:[NSColor whiteColor]];
      break;
    case GTMColorationLightShadow:
      color = [self blendedColorWithFraction:0.7 ofColor:[NSColor whiteColor]];
      color = [color gtm_colorByAdjustingLuminance:-0.02];
      break;
    case GTMColorationLightPenumbra:
      color = [self blendedColorWithFraction:0.8 ofColor:[NSColor whiteColor]];
      color = [color gtm_colorByAdjustingLuminance:-0.01];
      break;
    case GTMColorationDarkHighlight:
      color = [self gtm_colorByAdjustingLuminance:-0.20];
      break;
    case GTMColorationDarkMidtone:
      color = [self gtm_colorByAdjustingLuminance:-0.25];
      break;
    case GTMColorationDarkShadow:
      color = [self gtm_colorByAdjustingLuminance:-0.30 saturation:1.4];
      break;
    case GTMColorationDarkPenumbra:
      color = [self gtm_colorByAdjustingLuminance:-0.25];
      break;
    default:
      _GTMDevLog(@"Invalid Coloration Use %lu", (unsigned long)use);
      color = self;
      break;
  }
  return color;
}
const CGFloat kDefaultFade = 0.3;

- (NSColor *)gtm_colorAdjustedFor:(GTMColorationUse)use faded:(BOOL)fade {
  NSColor *color = [self gtm_colorAdjustedFor:use];
  if (fade) {
    CGFloat luminance = [color gtm_luminance];
    color = [color gtm_colorByAdjustingLuminance:
               kDefaultFade * (1.0 - luminance)
                                      saturation:kDefaultFade];
  }
  return color;
}

- (BOOL)gtm_isDarkColor {
  return [self gtm_luminance] < kGTMLuminanceDarkCutoff;
}

- (NSColor *)gtm_legibleTextColor {
  return [self gtm_isDarkColor] ? [NSColor whiteColor] : [NSColor blackColor];
}

@end
#endif // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
