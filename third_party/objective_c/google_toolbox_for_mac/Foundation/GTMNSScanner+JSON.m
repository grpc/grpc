//
//  GTMNSScanner+JSON.m
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
#import "GTMNSScanner+JSON.h"

// Export a nonsense symbol to suppress a libtool warning when this is linked
// alone in a static lib.
__attribute__((visibility("default")))
    char NSScanner_GTMNSScannerJSONAdditionsExportToSuppressLibToolWarning = 0;

@implementation NSScanner (GTMNSScannerJSONAdditions)

- (BOOL)gtm_scanJSONString:(NSString **)jsonString
                 startChar:(unichar)startChar
                   endChar:(unichar)endChar {
  BOOL isGood = NO;
  NSRange jsonRange = { NSNotFound, 0 };
  NSString *scanString = [self string];
  NSUInteger startLocation = [self scanLocation];
  NSUInteger length = [scanString length];
  NSUInteger blockOpen = 0;
  NSCharacterSet *charsToSkip = [self charactersToBeSkipped];
  BOOL inQuoteMode = NO;
  NSUInteger i;
  for (i = startLocation; i < length; ++i) {
    unichar jsonChar = [scanString characterAtIndex:i];
    if (jsonChar == startChar && !inQuoteMode) {
      if (blockOpen == 0) {
        jsonRange.location = i;
      }
      blockOpen += 1;
    } else if (blockOpen == 0) {
      // If we haven't opened our block skip over any characters in
      // charsToSkip.
      if (![charsToSkip characterIsMember:jsonChar]) {
        break;
      }
    } else if (jsonChar == endChar && !inQuoteMode) {
      blockOpen -= 1;
      if (blockOpen == 0) {
        i += 1; // Move onto next character
        jsonRange.length = i - jsonRange.location;
        break;
      }
    } else {
      if (jsonChar == '"') {
        inQuoteMode = !inQuoteMode;
      } else if (inQuoteMode && jsonChar == '\\') {
        // Skip the escaped character if it isn't the last one
        if (i < length - 1) ++i;
      }
    }
  }
  [self setScanLocation:i];
  if (blockOpen == 0 && jsonRange.location != NSNotFound) {
    isGood = YES;
    if (jsonString) {
      *jsonString = [scanString substringWithRange:jsonRange];
    }
  }
  return isGood;
}

- (BOOL)gtm_scanJSONObjectString:(NSString **)jsonString {
  return [self gtm_scanJSONString:jsonString startChar:'{' endChar:'}'];
}

- (BOOL)gtm_scanJSONArrayString:(NSString**)jsonString {
  return [self gtm_scanJSONString:jsonString startChar:'[' endChar:']'];
}

@end
