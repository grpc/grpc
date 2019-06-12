//
//  GTMRegex.h
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
#import <regex.h>
#import "GTMDefines.h"

/// Options for controlling the behavior of the matches
enum {

  kGTMRegexOptionIgnoreCase            = 0x01,
    // Ignore case in matching, ie: 'a' matches 'a' or 'A'

  kGTMRegexOptionSupressNewlineSupport = 0x02,
    // By default (without this option), regular expressions are implicitly
    // processed on a line by line basis, where "lines" are delimited by newline
    // characters. In this mode '.' (dot) does NOT match newline characters, and
    // '^' and '$' match at the beginning and end of the string as well as
    // around newline characters. This behavior matches the default behavior for
    // regular expressions in other languages including Perl and Python. For
    // example,
    //     foo.*bar
    // would match
    //     fooAAAbar
    // but would NOT match
    //     fooAAA\nbar
    // With the kGTMRegexOptionSupressNewlineSupport option, newlines are treated
    // just like any other character which means that '.' will match them. In
    // this mode, ^ and $ only match the beginning and end of the input string
    // and do NOT match around the newline characters. For example,
    //     foo.*bar
    // would match
    //     fooAAAbar
    // and would also match
    //     fooAAA\nbar

};
typedef NSUInteger GTMRegexOptions;

/// Global contants needed for errors from consuming patterns

// Ignore the "Macro name is a reserved identifier" warning in this section
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-id-macro"

#undef _EXTERN
#undef _INITIALIZE_AS
#if GTMREGEX_DEFINE_GLOBALS
#define _EXTERN
#define _INITIALIZE_AS(x) =x
#else
#define _EXTERN GTM_EXTERN
#define _INITIALIZE_AS(x)
#endif

#pragma clang diagnostic pop

_EXTERN NSString* kGTMRegexErrorDomain _INITIALIZE_AS(@"com.google.mactoolbox.RegexDomain");

enum {
  kGTMRegexPatternParseFailedError = -100
};

// Keys for the userInfo from a kGTMRegexErrorDomain/kGTMRegexPatternParseFailedError error
_EXTERN NSString* kGTMRegexPatternErrorPattern _INITIALIZE_AS(@"pattern");
_EXTERN NSString* kGTMRegexPatternErrorErrorString _INITIALIZE_AS(@"patternError");

/// Class for doing Extended Regex operations w/ libregex (see re_format(7)).
//
// NOTE: the docs for recomp/regexec make *no* claims about i18n.  All work
// within this class is done w/ UTF-8 so Unicode should move through it safely,
// however, the character classes described in re_format(7) might not really
// be unicode "savvy", so use them and this class w/ that in mind.
//
// Example usage:
//
//   NSArray *inputArrayOfStrings = ...
//   NSArray *matches = [NSMutableArray array];
//
//   GTMRegex *regex = [GTMRegex regexWithPattern:@"foo.*bar"];
//   for (NSString *curStr in inputArrayOfStrings) {
//     if ([regex matchesString:curStr])
//       [matches addObject:curStr];
//   }
//   ....
//
// -------------
//
//  If you need to include something dynamic in a pattern:
//
//   NSString *pattern =
//     [NSString stringWithFormat:@"^foo:%@bar",
//       [GTMRegex escapedPatternForString:inputStr]];
//   GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
//   ....
//
// -------------
//
//   GTMRegex *regex = [GTMRegex regexWithPattern:@"(foo+)(bar)"];
//   NSString *highlighted =
//     [regex stringByReplacingMatchesInString:inputString
//                             withReplacement:@"<i>\\1</i><b>\\2</b>"];
//   ....
//

// Use NSRegularExpression instead
NS_DEPRECATED(10_0, 10_7, 1_0, 4_0)
@interface GTMRegex : NSObject {
 @private
  NSString *pattern_;
  GTMRegexOptions options_;
  regex_t regexData_;
}

/// Create a new, autoreleased object w/ the given regex pattern with the default options
+ (id)regexWithPattern:(NSString *)pattern;

/// Create a new, autoreleased object w/ the given regex pattern and specify the matching options
+ (id)regexWithPattern:(NSString *)pattern options:(GTMRegexOptions)options;

/// Create a new, autoreleased object w/ the given regex pattern, specify the matching options and receive any error consuming the pattern.
+ (id)regexWithPattern:(NSString *)pattern
               options:(GTMRegexOptions)options
             withError:(NSError **)outErrorOrNULL;

/// Returns a new, autoreleased copy of |str| w/ any pattern chars in it escaped so they have no meaning when used w/in a pattern.
+ (NSString *)escapedPatternForString:(NSString *)str;

/// Initialize a new object w/ the given regex pattern with the default options
- (id)initWithPattern:(NSString *)pattern;

/// Initialize a new object w/ the given regex pattern and specify the matching options
- (id)initWithPattern:(NSString *)pattern options:(GTMRegexOptions)options;

/// Initialize a new object w/ the given regex pattern, specify the matching options, and receive any error consuming the pattern.
- (id)initWithPattern:(NSString *)pattern
              options:(GTMRegexOptions)options
            withError:(NSError **)outErrorOrNULL;

/// Returns the number of sub patterns in the pattern
//
// Sub Patterns are basically the number of parenthesis blocks w/in the pattern.
//   ie: The pattern "foo((bar)|(baz))" has 3 sub patterns.
//
- (NSUInteger)subPatternCount;

/// Returns YES if the whole string |str| matches the pattern.
- (BOOL)matchesString:(NSString *)str;

/// Returns a new, autoreleased array of string that contain the subpattern matches for the string.
//
// If the whole string does not match the pattern, nil is returned.
//
// The api follows the conventions of most regex engines, and index 0 (zero) is
// the full match, then the subpatterns are index 1, 2, ... going left to right.
// If the pattern has optional subpatterns, then anything that didn't match
// will have NSNull at that index.
//   ie: The pattern "(fo(o+))((bar)|(baz))" has five subpatterns, and when
//       applied to the string "foooooobaz" you'd get an array of:
//              0: "foooooobaz"
//              1: "foooooo"
//              2: "ooooo"
//              3: "baz"
//              4: NSNull
//              5: "baz"
//
- (NSArray *)subPatternsOfString:(NSString *)str;

/// Returns the first match for this pattern in |str|.
- (NSString *)firstSubStringMatchedInString:(NSString *)str;

/// Returns YES if this pattern some substring of |str|.
- (BOOL)matchesSubStringInString:(NSString *)str;

/// Returns a new, autoreleased enumerator that will walk segments (GTMRegexStringSegment) of |str| based on the pattern.
//
// This will split the string into "segments" using the given pattern.  You get
// both the matches and parts that are inbetween matches.  ie-the entire string
// will eventually be returned.
//
// See GTMRegexStringSegment for more infomation and examples.
//
- (NSEnumerator *)segmentEnumeratorForString:(NSString *)str;

/// Returns a new, autoreleased enumerator that will walk only the matching segments (GTMRegexStringSegment) of |str| based on the pattern.
//
// This extracts the "segments" of the string that used the pattern.  So it can
// be used to collect all of the matching substrings from within a string.
//
// See GTMRegexStringSegment for more infomation and examples.
//
- (NSEnumerator *)matchSegmentEnumeratorForString:(NSString *)str;

/// Returns a new, autoreleased string with all matches of the pattern in |str| replaced with |replacementPattern|.
//
// Replacement uses the SED substitution like syntax w/in |replacementPattern|
// to allow the use of matches in the replacment.  The replacement pattern can
// make use of any number of match references by using a backslash followed by
// the match subexpression number (ie-"\2", "\0", ...), see subPatternsOfString:
// for details on the subexpression indexing.
//
// REMINDER: you need to double-slash since the slash has meaning to the
// compiler/preprocessor.  ie: "\\0"
//
- (NSString *)stringByReplacingMatchesInString:(NSString *)str
                               withReplacement:(NSString *)replacementPattern;

@end

/// Class returned by the nextObject for the enumerators from GTMRegex
//
// The two enumerators on from GTMRegex return objects of this type.  This object
// represents a "piece" of the string the enumerator is walking.  It's the apis
// on this object allow you to figure out why each segment was returned and to
// act on it.
//
// The easiest way to under stand this how the enumerators and this class works
// is through and examples ::
//    Pattern: "foo+"
//     String: "fo bar foobar foofooo baz"
// If you walk this w/ -segmentEnumeratorForString you'll get:
//   # nextObjects Calls   -isMatch       -string
//          1                 NO         "fo bar "
//          2                 YES        "foo"
//          3                 NO         "bar "
//          4                 YES        "foo"
//          5                 YES        "fooo"
//          6                 NO         " baz"
// And if you walk this w/ -matchSegmentEnumeratorForString you'll get:
//   # nextObjects Calls   -isMatch       -string
//          1                 YES        "foo"
//          2                 YES        "foo"
//          3                 YES        "fooo"
// (see the comments on subPatternString for how it works)
//
// Example usage:
//
//   NSMutableString processedStr = [NSMutableString string];
//   NSEnumerator *enumerator =
//     [inputStr segmentEnumeratorForPattern:@"foo+((ba+r)|(ba+z))"];
//   GTMRegexStringSegment *segment = nil;
//   while ((segment = [enumerator nextObject]) != nil) {
//     if ([segment isMatch]) {
//       if ([segment subPatterString:2] != nil) {
//         // matched: "(ba+r)"
//         [processStr appendFormat:@"<b>%@</b>", [segment string]];
//       } else {
//         // matched: "(ba+z)"
//         [processStr appendFormat:@"<i>%@</i>", [segment string]];
//       }
//     } else {
//       [processStr appendString:[segment string]];
//     }
//   }
//   // proccessedStr now has all the versions of foobar wrapped in bold tags,
//   // and all the versons of foobaz in italics tags.
//   //   ie: " fooobar foobaaz " ==> " <b>fooobar</b> <i>foobaaz</i> "
//
@interface GTMRegexStringSegment : NSObject {
 @private
  NSData *utf8StrBuf_;
  regmatch_t *regMatches_;  // STRONG: ie-we call free
  NSUInteger numRegMatches_;
  BOOL isMatch_;
}

/// Returns YES if this segment from from a match of the regex, false if it was a segment between matches.
//
// Use -isMatch to see if the segment from from a match of the pattern or if the
// segment is some text between matches.  (NOTE: isMatch is always YES for
// matchSegmentEnumeratorForString)
//
- (BOOL)isMatch;

/// Returns a new, autoreleased string w/ the full text segment from the original string.
- (NSString *)string;

/// Returns a new, autoreleased string w/ the |index| sub pattern from this segment of the original string.
//
// This api follows the conventions of most regex engines, and index 0 (zero) is
// the full match, then the subpatterns are index 1, 2, ... going left to right.
// If the pattern has optional subpatterns, then anything that didn't match
// will return nil.
//   ie: When using the pattern "(fo(o+))((bar)|(baz))" the following indexes
//       fetch these values for a segment where -string is @"foooooobaz":
//              0: "foooooobaz"
//              1: "foooooo"
//              2: "ooooo"
//              3: "baz"
//              4: nil
//              5: "baz"
//
- (NSString *)subPatternString:(NSUInteger)index;

@end

/// Some helpers to streamline usage of GTMRegex
//
// Example usage:
//
//   if ([inputStr matchesPattern:@"foo.*bar"]) {
//     // act on match
//     ....
//   }
//
// -------------
//
//   NSString *subStr = [inputStr firstSubStringMatchedByPattern:@"^foo:.*$"];
//   if (subStr != nil) {
//     // act on subStr
//     ....
//   }
//
// -------------
//
//   NSArray *headingList =
//     [inputStr allSubstringsMatchedByPattern:@"^Heading:.*$"];
//   // act on the list of headings
//   ....
//
// -------------
//
//   NSString *highlightedString =
//     [inputString stringByReplacingMatchesOfPattern:@"(foo+)(bar)"
//                                    withReplacement:@"<i>\\1</i><b>\\2</b>"];
//   ....
//
@interface NSString (GTMRegexAdditions)

/// Returns YES if the full string matches regex |pattern| using the default match options
- (BOOL)gtm_matchesPattern:(NSString *)pattern;

/// Returns a new, autoreleased array of strings that contain the subpattern matches of |pattern| using the default match options
//
// See [GTMRegex subPatternsOfString:] for information about the returned array.
//
- (NSArray *)gtm_subPatternsOfPattern:(NSString *)pattern;

/// Returns a new, autoreleased string w/ the first substring that matched the regex |pattern| using the default match options
- (NSString *)gtm_firstSubStringMatchedByPattern:(NSString *)pattern;

/// Returns YES if a substring string matches regex |pattern| using the default match options
- (BOOL)gtm_subStringMatchesPattern:(NSString *)pattern;

/// Returns a new, autoreleased array of substrings in the string that match the regex |pattern| using the default match options
//
// Note: if the string has no matches, you get an empty array.
- (NSArray *)gtm_allSubstringsMatchedByPattern:(NSString *)pattern;

/// Returns a new, autoreleased segment enumerator that will break the string using pattern w/ the default match options
//
// The enumerator returns GTMRegexStringSegment options, see that class for more
// details and examples.
//
- (NSEnumerator *)gtm_segmentEnumeratorForPattern:(NSString *)pattern;

/// Returns a new, autoreleased segment enumerator that will only return matching segments from the string using pattern w/ the default match options
//
// The enumerator returns GTMRegexStringSegment options, see that class for more
// details and examples.
//
- (NSEnumerator *)gtm_matchSegmentEnumeratorForPattern:(NSString *)pattern;

/// Returns a new, autoreleased string with all matches for pattern |pattern| are replaced w/ |replacementPattern|.  Uses the default match options.
//
// |replacemetPattern| has support for using any subExpression that matched,
// see [GTMRegex stringByReplacingMatchesInString:withReplacement:] above
// for details.
//
- (NSString *)gtm_stringByReplacingMatchesOfPattern:(NSString *)pattern
                                    withReplacement:(NSString *)replacementPattern;

@end
