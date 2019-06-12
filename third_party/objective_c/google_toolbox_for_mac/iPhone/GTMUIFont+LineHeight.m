//
//  GTMUIFont+LineHeight.m
//
//  Copyright 2008 Google Inc.
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

#import "GTMUIFont+LineHeight.h"

#import <Availability.h>

// Export a nonsense symbol to suppress a libtool warning when this is linked
// alone in a static lib.
__attribute__((visibility("default")))
    char GTMUIFont_LineHeightExportToSuppressLibToolWarning = 0;

@implementation UIFont (GTMLineHeight)
- (CGFloat)gtm_lineHeight {
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_7_0
  // |sizeWithFont:| is deprecated in iOS 7, replaced by |sizeWithAttributes:|
  return [@"Fake line with gjy" sizeWithFont:self].height;
#else
  return [@"Fake line with gjy" sizeWithAttributes:@{NSFontAttributeName:self}].height;
#endif
}
@end
