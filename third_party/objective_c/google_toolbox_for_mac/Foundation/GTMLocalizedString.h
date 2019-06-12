//
//  GTMLocalizedString.h
//
//  Copyright (c) 2010 Google Inc. All rights reserved.
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

// The NSLocalizedString macros do not have NS_FORMAT_ARGUMENT modifiers put
// on them which means you get warnings on Snow Leopard with when
// GCC_WARN_TYPECHECK_CALLS_TO_PRINTF = YES and you do things like:
// NSString *foo
//   = [NSString stringWithFormat:NSLocalizedString(@"blah %@", nil), @"bar"];
// The GTMLocalizedString functions fix that for you so you can do:
// NSString *foo
//   = [NSString stringWithFormat:GTMLocalizedString(@"blah %@", nil), @"bar"];
// and you will compile cleanly.
// If you use genstrings you can call it with
// genstrings -s GTMLocalizedString ...
// and it should work as expected.
// You can override how GTM gets its localized strings (if you are using
// something other than NSLocalizedString) by redefining
// GTMLocalizedStringWithDefaultValueInternal.

#ifndef GTMLocalizedStringWithDefaultValueInternal
  #define GTMLocalizedStringWithDefaultValueInternal \
      NSLocalizedStringWithDefaultValue
#endif

GTM_INLINE NS_FORMAT_ARGUMENT(1) NSString *GTMLocalizedString(
    NSString *key,  NSString *comment) {
  return GTMLocalizedStringWithDefaultValueInternal(key,
                                                    nil,
                                                    [NSBundle mainBundle],
                                                    @"",
                                                    comment);
}

GTM_INLINE NS_FORMAT_ARGUMENT(1) NSString *GTMLocalizedStringFromTable(
    NSString *key, NSString *tableName, NSString *comment) {
  return GTMLocalizedStringWithDefaultValueInternal(key,
                                                    tableName,
                                                    [NSBundle mainBundle],
                                                    @"",
                                                    comment);
}

GTM_INLINE NS_FORMAT_ARGUMENT(1) NSString *GTMLocalizedStringFromTableInBundle(
    NSString *key,  NSString *tableName, NSBundle *bundle, NSString *comment) {
  return GTMLocalizedStringWithDefaultValueInternal(key,
                                                    tableName,
                                                    bundle,
                                                    @"",
                                                    comment);
}

GTM_INLINE NS_FORMAT_ARGUMENT(1) NSString *GTMLocalizedStringWithDefaultValue(
    NSString *key, NSString *tableName, NSBundle *bundle, NSString *value,
    NSString *comment) {
  return GTMLocalizedStringWithDefaultValueInternal(key,
                                                    tableName,
                                                    bundle,
                                                    value,
                                                    comment);
}

