//
//  GTMNSString+XML.h
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

#import <Foundation/Foundation.h>

/// Utilities for NSStrings containing XML
@interface NSString (GTMNSStringXMLAdditions)

/// Get a string where characters that need escaping for XML are escaped and invalid characters removed
//
/// This call escapes '&', '<, '>', '\'', '"' per the xml spec and removes all
/// invalid characters as defined by Section 2.2 of the xml spec.
///
/// For obvious reasons this call is only safe once.
//
//  Returns:
//    Autoreleased NSString
//
- (NSString *)gtm_stringBySanitizingAndEscapingForXML;

/// Get a string where characters that invalid characters per the XML spec have been removed
//
/// This call removes all invalid characters as defined by Section 2.2 of the
/// xml spec.  If you are writing XML yourself, you probably was to use the
/// above api (gtm_stringBySanitizingAndEscapingForXML) so any entities also
/// get escaped.
//
//  Returns:
//    Autoreleased NSString
//
- (NSString *)gtm_stringBySanitizingToXMLSpec;

// There is no stringByUnescapingFromXML because the XML parser will do this.
// The above api is here just incase you need to create XML yourself.

@end
