//
//  GTMUILocalizer.m
//
//  Copyright 2011 Google Inc.
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
#import "GTMUILocalizer.h"

@interface GTMUILocalizer (GTMUILocalizerPrivate)
- (void)localizeAccessibility:(id)object;

// Never recursively call any of these methods. Always call
// -[self localizeObject:recursively:].
- (void)localizeToolbar:(UIToolbar *)toolbar;
- (void)localizeSegmentedControl:(UISegmentedControl *)segmentedControl;
- (void)localizeView:(UIView *)view recursively:(BOOL)recursive;
- (void)localizeButton:(UIButton *)button;
@end

@implementation GTMUILocalizer
@synthesize owner = owner_;
@synthesize otherObjectToLocalize = otherObjectToLocalize_;
@synthesize yetAnotherObjectToLocalize = yetAnotherObjectToLocalize_;

- (id)initWithBundle:(NSBundle *)bundle {
  if ((self = [super init])) {
    bundle_ = [bundle retain];
  }
  return self;
}

- (void)dealloc {
  [bundle_ release];
  [super dealloc];
}

- (void)awakeFromNib {
  [super awakeFromNib];
  id owner = self.owner;
  if (owner) {
    NSBundle *newBundle = [[self class] bundleForOwner:owner];
    bundle_ = [newBundle retain];
    [self localizeObject:self.owner recursively:YES];
    [self localizeObject:self.otherObjectToLocalize recursively:YES];
    [self localizeObject:self.yetAnotherObjectToLocalize recursively:YES];
  } else {
    _GTMDevLog(@"Expected an owner set for %@", self);
  }
  // Clear the outlets.
  self.owner = nil;
  self.otherObjectToLocalize = nil;
  self.yetAnotherObjectToLocalize = nil;
}

+ (NSBundle *)bundleForOwner:(id)owner {
  NSBundle *newBundle = nil;
  if (owner) {
    if ([owner isKindOfClass:[UIViewController class]]) {
      newBundle = [(UIViewController *)owner nibBundle];
    }
    if (!newBundle) {
      newBundle = [NSBundle mainBundle];
    }
  }
  return newBundle;
}

- (NSString *)localizedStringForString:(NSString *)string {
  NSString *localized = nil;
  if (bundle_ && [string hasPrefix:@"^"]) {
    NSString *notFoundValue = @"__GTM_NOT_FOUND__";
    NSString *key = [string substringFromIndex:1];
    localized = [bundle_ localizedStringForKey:key
                                         value:notFoundValue
                                         table:nil];
    if ([localized isEqualToString:notFoundValue]) {
      localized = nil;
    }
  }
  return localized;
}

- (void)localizeObject:(id)object recursively:(BOOL)recursive {
  if (object) {
    if ([object isKindOfClass:[UIViewController class]]) {
        UIView *view = [object view];
        [self localizeView:view recursively:recursive];
    } else if ([object isKindOfClass:[UIToolbar class]]) {
      [self localizeToolbar:(UIToolbar*)object];
    } else if ([object isKindOfClass:[UISegmentedControl class]]) {
      [self localizeSegmentedControl:(UISegmentedControl*)object];
    } else if ([object isKindOfClass:[UIView class]]) {
      [self localizeView:(UIView *)object recursively:recursive];
    }
  }
}

- (void)localizeToolbar:(UIToolbar *)toolbar {
  // NOTE: Like the header says, -items only gives us what is in the toolbar
  // which is usually the default items, if the toolbar supports customization
  // there is no way to fetch those possible items to tweak their contents.
  for (UIBarItem* item in [toolbar items]) {
    NSString *title = [item title];
    if (title) {
      title = [self localizedStringForString:title];
      if (title) {
        [item setTitle:title];
      }
    }
  }
}

- (void)localizeSegmentedControl:(UISegmentedControl *)segmentedControl {
  // A UISegmentedControl uses a few objects as subviews, but they aren't
  // documented.  It happened to work out that their inherritance was right
  // with the selectors they implemented that things localized, but iOS 6
  // changed some of that, so they are now directly handled.
  NSUInteger numberOfSegments = segmentedControl.numberOfSegments;
  for (NSUInteger i = 0; i < numberOfSegments; ++i) {
    NSString *title = [segmentedControl titleForSegmentAtIndex:i];
    if (title) {
      title = [self localizedStringForString:title];
      if (title) {
        [segmentedControl setTitle:title forSegmentAtIndex:i];
      }
    }
  }
}

- (void)localizeView:(UIView *)view recursively:(BOOL)recursive {
  if (view) {
    // Do accessibility on views.
    [self localizeAccessibility:view];

    if (recursive) {
      for (UIView *subview in [view subviews]) {
        [self localizeObject:subview recursively:recursive];
      }
    }

    // Specific types
    if ([view isKindOfClass:[UIButton class]]) {
      [self localizeButton:(UIButton *)view];
    }

    // Then do all possible strings.
    if ([view respondsToSelector:@selector(title)]
        && [view respondsToSelector:@selector(setTitle:)]) {
      NSString *title = [view performSelector:@selector(title)];
      if (title) {
        NSString *localizedTitle = [self localizedStringForString:title];
        if (localizedTitle) {
          [view performSelector:@selector(setTitle:) withObject:localizedTitle];
        }
      }
    }

    if ([view respondsToSelector:@selector(text)]
        && [view respondsToSelector:@selector(setText:)]) {
      NSString *text = [view performSelector:@selector(text)];
      if (text) {
        NSString *localizedText = [self localizedStringForString:text];
        if (localizedText) {
          [view performSelector:@selector(setText:) withObject:localizedText];
        }
      }
    }

    if ([view respondsToSelector:@selector(placeholder)]
        && [view respondsToSelector:@selector(setPlaceholder:)]) {
      NSString *placeholder = [view performSelector:@selector(placeholder)];
      if (placeholder) {
        NSString *localizedPlaceholder =
            [self localizedStringForString:placeholder];
        if (localizedPlaceholder) {
          [view performSelector:@selector(setPlaceholder:)
                     withObject:localizedPlaceholder];
        }
      }
    }
  }
}

- (void)localizeAccessibility:(id)object {
  if ([object respondsToSelector:@selector(accessibilityHint)]
      && [object respondsToSelector:@selector(setAccessibilityHint:)]) {
    NSString *accessibilityHint =
        [object performSelector:@selector(accessibilityHint)];
    if (accessibilityHint) {
      NSString *localizedAccessibilityHint =
          [self localizedStringForString:accessibilityHint];
      if (localizedAccessibilityHint) {
        [object performSelector:@selector(setAccessibilityHint:)
                   withObject:localizedAccessibilityHint];
      }
    }
  }

  if ([object respondsToSelector:@selector(accessibilityLabel)]
      && [object respondsToSelector:@selector(setAccessibilityLabel:)]) {
    NSString *accessibilityLabel =
        [object performSelector:@selector(accessibilityLabel)];
    if (accessibilityLabel) {
      NSString *localizedAccessibilityLabel =
          [self localizedStringForString:accessibilityLabel];
      if (localizedAccessibilityLabel) {
        [object performSelector:@selector(setAccessibilityLabel:)
                   withObject:localizedAccessibilityLabel];
      }
    }
  }

  if ([object respondsToSelector:@selector(accessibilityValue)]
      && [object respondsToSelector:@selector(setAccessibilityValue:)]) {
    NSString *accessibilityValue =
        [object performSelector:@selector(accessibilityValue)];
    if (accessibilityValue) {
      NSString *localizedAccessibilityValue =
          [self localizedStringForString:accessibilityValue];
      if (localizedAccessibilityValue) {
        [object performSelector:@selector(setAccessibilityValue:)
                   withObject:localizedAccessibilityValue];
      }
    }
  }
}

- (void)localizeButton:(UIButton *)button {
  UIControlState allStates[] = { UIControlStateNormal,
                                 UIControlStateHighlighted,
                                 UIControlStateDisabled,
                                 UIControlStateSelected };
  for (size_t idx = 0; idx < (sizeof(allStates)/sizeof(allStates[0])); ++idx) {
    UIControlState state = allStates[idx];
    NSString *value = [button titleForState:state];
    if (value) {
      NSString* localizedValue = [self localizedStringForString:value];
      if (localizedValue) {
        [button setTitle:localizedValue forState:state];
      }
    }
  }
}

@end
