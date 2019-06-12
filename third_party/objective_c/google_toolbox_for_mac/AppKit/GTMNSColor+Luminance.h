//
//  GTMNSColor+Luminance.h
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
#import <Cocoa/Cocoa.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

enum {
  GTMColorationBaseHighlight,
  GTMColorationBaseMidtone,
  GTMColorationBaseShadow,
  GTMColorationBasePenumbra,
  GTMColorationLightHighlight,
  GTMColorationLightMidtone,
  GTMColorationLightShadow,
  GTMColorationLightPenumbra,
  GTMColorationDarkHighlight,
  GTMColorationDarkMidtone,
  GTMColorationDarkShadow,
  GTMColorationDarkPenumbra
};
typedef NSUInteger GTMColorationUse;

@interface NSColorSpace (GTMNSColorSpaceLuminanceHelpers)
+ (NSColorSpace *)gtm_labColorSpace;
@end

@interface NSColor (GTMLuminanceAdditions)
- (CGFloat)gtm_luminance;

// Create a color modified by lightening or darkening it (-1.0 to 1.0)
- (NSColor *)gtm_colorByAdjustingLuminance:(CGFloat)luminance;

// Create a color modified by lightening or darkening it (-1.0 to 1.0)
- (NSColor *)gtm_colorByAdjustingLuminance:(CGFloat)luminance
                                saturation:(CGFloat)saturation;

// Returns a color adjusted for a specific usage
- (NSColor *)gtm_colorAdjustedFor:(GTMColorationUse)use;
- (NSColor *)gtm_colorAdjustedFor:(GTMColorationUse)use faded:(BOOL)fade;

// Returns whether the color is in the dark half of the spectrum
- (BOOL)gtm_isDarkColor;

// Returns a color that is legible on this color. (Nothing to do with textColor)
- (NSColor *)gtm_legibleTextColor;
@end

#endif
