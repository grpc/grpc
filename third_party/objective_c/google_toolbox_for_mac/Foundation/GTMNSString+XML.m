//
//  GTMNSString+XML.m
//
//  Copyright 2007-2008 Google Inc.
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
#import "GTMNSString+XML.h"

// Export a nonsense symbol to suppress a libtool warning when this is linked alone in a static lib.
__attribute__((visibility("default")))
    char NSString_GTMNSStringXMLAdditionsExportToSuppressLibToolWarning = 0;

enum {
  kGTMXMLCharModeEncodeQUOT  = 0,
  kGTMXMLCharModeEncodeAMP   = 1,
  kGTMXMLCharModeEncodeAPOS  = 2,
  kGTMXMLCharModeEncodeLT    = 3,
  kGTMXMLCharModeEncodeGT    = 4,
  kGTMXMLCharModeValid       = 99,
  kGTMXMLCharModeInvalid     = 100,
};
typedef NSUInteger GTMXMLCharMode;

static NSString *gXMLEntityList[] = {
  // this must match the above order
  @"&quot;",
  @"&amp;",
  @"&apos;",
  @"&lt;",
  @"&gt;",
};

GTM_INLINE GTMXMLCharMode XMLModeForUnichar(UniChar c) {

  // Per XML spec Section 2.2 Characters
  //   ( http://www.w3.org/TR/REC-xml/#charsets )
  //
  //   Char    ::=       #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] |
  //                      [#x10000-#x10FFFF]

  if (c <= 0xd7ff)  {
    if (c >= 0x20) {
      switch (c) {
        case 34:
          return kGTMXMLCharModeEncodeQUOT;
        case 38:
          return kGTMXMLCharModeEncodeAMP;
        case 39:
          return kGTMXMLCharModeEncodeAPOS;
        case 60:
          return kGTMXMLCharModeEncodeLT;
        case 62:
          return kGTMXMLCharModeEncodeGT;
        default:
          return kGTMXMLCharModeValid;
      }
    } else {
      if (c == '\n')
        return kGTMXMLCharModeValid;
      if (c == '\r')
        return kGTMXMLCharModeValid;
      if (c == '\t')
        return kGTMXMLCharModeValid;
      return kGTMXMLCharModeInvalid;
    }
  }

  if (c < 0xE000)
    return kGTMXMLCharModeInvalid;

  if (c <= 0xFFFD)
    return kGTMXMLCharModeValid;

  // UniChar can't have the following values
  // if (c < 0x10000)
  //   return kGTMXMLCharModeInvalid;
  // if (c <= 0x10FFFF)
  //   return kGTMXMLCharModeValid;

  return kGTMXMLCharModeInvalid;
} // XMLModeForUnichar


static NSString *AutoreleasedCloneForXML(NSString *src, BOOL escaping) {
  //
  // NOTE:
  // We don't use CFXMLCreateStringByEscapingEntities because it's busted in
  // 10.3 (http://lists.apple.com/archives/Cocoa-dev/2004/Nov/msg00059.html) and
  // it doesn't do anything about the chars that are actually invalid per the
  // xml spec.
  //

  // we can't use the CF call here because it leaves the invalid chars
  // in the string.
  NSUInteger length = [src length];
  if (!length) {
    return src;
  }

  NSMutableString *finalString = [NSMutableString string];

  // this block is common between GTMNSString+HTML and GTMNSString+XML but
  // it's so short that it isn't really worth trying to share.
  const UniChar *buffer = CFStringGetCharactersPtr((CFStringRef)src);
  if (!buffer) {
    // We want this buffer to be autoreleased.
    NSMutableData *data = [NSMutableData dataWithLength:length * sizeof(UniChar)];
    if (!data) {
      // COV_NF_START  - Memory fail case
      _GTMDevLog(@"couldn't alloc buffer");
      return nil;
      // COV_NF_END
    }
    [src getCharacters:[data mutableBytes]];
    buffer = [data bytes];
  }

  const UniChar *goodRun = buffer;
  NSUInteger goodRunLength = 0;

  for (NSUInteger i = 0; i < length; ++i) {

    GTMXMLCharMode cMode = XMLModeForUnichar(buffer[i]);

    // valid chars go as is, and if we aren't doing entities, then
    // everything goes as is.
    if ((cMode == kGTMXMLCharModeValid) ||
        (!escaping && (cMode != kGTMXMLCharModeInvalid))) {
      // goes as is
      goodRunLength += 1;
    } else {
      // it's something we have to encode or something invalid

      // start by adding what we already collected (if anything)
      if (goodRunLength) {
        CFStringAppendCharacters((CFMutableStringRef)finalString,
                                 goodRun,
                                 goodRunLength);
        goodRunLength = 0;
      }

      // if it wasn't invalid, add the encoded version
      if (cMode != kGTMXMLCharModeInvalid) {
        // add this encoded
        [finalString appendString:gXMLEntityList[cMode]];
      }

      // update goodRun to point to the next UniChar
      goodRun = buffer + i + 1;
    }
  }

  // anything left to add?
  if (goodRunLength) {
    CFStringAppendCharacters((CFMutableStringRef)finalString,
                             goodRun,
                             goodRunLength);
  }
  return finalString;
} // AutoreleasedCloneForXML

@implementation NSString (GTMNSStringXMLAdditions)

- (NSString *)gtm_stringBySanitizingAndEscapingForXML {
  return AutoreleasedCloneForXML(self, YES);
} // gtm_stringBySanitizingAndEscapingForXML

- (NSString *)gtm_stringBySanitizingToXMLSpec {
  return AutoreleasedCloneForXML(self, NO);
} // gtm_stringBySanitizingToXMLSpec

@end
