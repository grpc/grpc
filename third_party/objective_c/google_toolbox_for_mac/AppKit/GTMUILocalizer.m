//
//  GTMUILocalizer.m
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
#import "GTMUILocalizer.h"

@interface GTMUILocalizer (GTMUILocalizerPrivate)
- (void)localizeAccessibility:(id)object;
- (void)localizeBindings:(id)object;

// Never recursively call any of these methods. Always call
// -[self localizeObject:recursively:] otherwise bindings will not be
// localized properly.
- (void)localizeWindow:(NSWindow *)window recursively:(BOOL)recursive;
- (void)localizeToolbar:(NSToolbar *)toolbar;
- (void)localizeView:(NSView *)view recursively:(BOOL)recursive;
- (void)localizeMenu:(NSMenu *)menu recursively:(BOOL)recursive;
- (void)localizeCell:(NSCell *)cell recursively:(BOOL)recursive;

@end

@implementation GTMUILocalizer
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
  if (owner_) {
    NSBundle *newBundle = [[self class] bundleForOwner:owner_];
    bundle_ = [newBundle retain];
    [self localizeObject:owner_ recursively:YES];
    [self localizeObject:otherObjectToLocalize_ recursively:YES];
    [self localizeObject:yetAnotherObjectToLocalize_ recursively:YES];
  } else {
    _GTMDevLog(@"Expected an owner_ set for %@", self);
  }
  // Won't need these again, clear them.
  owner_ = nil;
  otherObjectToLocalize_ = nil;
  yetAnotherObjectToLocalize_ = nil;
}

+ (NSBundle *)bundleForOwner:(id)owner {
  NSBundle *newBundle = nil;
  if (owner) {
    Class class = [NSWindowController class];
    if ([owner isKindOfClass:class] && ![owner isMemberOfClass:class]) {
      newBundle = [NSBundle bundleForClass:[owner class]];
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
    } else if ([owner isKindOfClass:[NSViewController class]]) {
      newBundle = [(NSViewController *)owner nibBundle];
#endif
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
    if ([object isKindOfClass:[NSWindowController class]]) {
      NSWindow *window = [object window];
      [self localizeWindow:window recursively:recursive];
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
    } else if ([object isKindOfClass:[NSViewController class]]) {
        NSView *view = [object view];
        [self localizeView:view recursively:recursive];
#endif
    } else if ([object isKindOfClass:[NSMenu class]]) {
      [self localizeMenu:(NSMenu *)object recursively:recursive];
    } else if ([object isKindOfClass:[NSWindow class]]) {
      [self localizeWindow:(NSWindow *)object recursively:recursive];
    } else if ([object isKindOfClass:[NSView class]]) {
      [self localizeView:(NSView *)object recursively:recursive];
    } else if ([object isKindOfClass:[NSApplication class]]) {
      // Do the main menu
      NSMenu *menu = [object mainMenu];
      [self localizeMenu:menu recursively:recursive];
    } else if ([object isKindOfClass:[NSCell class]]) {
      [self localizeCell:(NSCell *)object recursively:recursive];
    } else if ([object isKindOfClass:[NSToolbar class]]) {
      [self localizeToolbar:(NSToolbar*)object];
    }
    [self localizeBindings:object];
  }
}

- (void)localizeWindow:(NSWindow *)window recursively:(BOOL)recursive {
  NSString *title = [window title];
  NSString *localizedTitle = [self localizedStringForString:title];
  if (localizedTitle) {
    [window setTitle:localizedTitle];
  }
  if (recursive) {
    [self localizeObject:[window contentView] recursively:recursive];
    [self localizeObject:[window toolbar] recursively:recursive];
  }
}

- (void)localizeToolbar:(NSToolbar *)toolbar {
  // NOTE: Like the header says, -items only gives us what is in the toolbar
  // which is usually the default items, if the toolbar supports customization
  // there is no way to fetch those possible items to tweak their contents.
  NSToolbarItem *item;
  for (item in [toolbar items]) {
    NSString *label = [item label];
    if (label) {
      label = [self localizedStringForString:label];
      if (label) {
        [item setLabel:label];
      }
    }

    NSString *paletteLabel = [item paletteLabel];
    if (paletteLabel) {
      paletteLabel = [self localizedStringForString:paletteLabel];
      if (paletteLabel) {
        [item setPaletteLabel:paletteLabel];
      }
    }

    NSString *toolTip = [item toolTip];
    if (toolTip) {
      toolTip = [self localizedStringForString:toolTip];
      if (toolTip) {
        [item setToolTip:toolTip];
      }
    }
  }
}

- (void)localizeView:(NSView *)view recursively:(BOOL)recursive {
  if (view) {
    // First do tooltips
    NSString *toolTip = [view toolTip];
    if (toolTip) {
      NSString *localizedToolTip = [self localizedStringForString:toolTip];
      if (localizedToolTip) {
        [view setToolTip:localizedToolTip];
      }
    }

    // Then do accessibility both on views directly...
    [self localizeAccessibility:view];

    // ...and on control cells (which implement accessibility for the controls
    // that contain them)
    if ([view isKindOfClass:[NSControl class]]) {
      [self localizeAccessibility:[(NSControl *)view cell]];
    }

    // Must do the menu before the titles, or else this will screw up
    // popup menus on us.
    [self localizeObject:[view menu] recursively:recursive];
    if (recursive) {
      NSArray *subviews = [view subviews];
      NSView *subview = nil;
      for (subview in subviews) {
        [self localizeObject:subview recursively:recursive];
      }
    }

    // Then do titles
    if ([view isKindOfClass:[NSTextField class]]) {
      NSString *title = [(NSTextField *)view stringValue];
      NSString *localizedTitle = [self localizedStringForString:title];
      if (localizedTitle) {
        [(NSTextField *)view setStringValue:localizedTitle];
      }
    } else if ([view respondsToSelector:@selector(title)]
        && [view respondsToSelector:@selector(setTitle:)]) {
      NSString *title = [view performSelector:@selector(title)];
      if (title) {
        NSString *localizedTitle = [self localizedStringForString:title];
        if (localizedTitle) {
          [view performSelector:@selector(setTitle:) withObject:localizedTitle];
        }
      }
      if ([view respondsToSelector:@selector(alternateTitle)]
          && [view respondsToSelector:@selector(setAlternateTitle:)]) {
        title = [view performSelector:@selector(alternateTitle)];
        if (title) {
          NSString *localizedTitle = [self localizedStringForString:title];
          if (localizedTitle) {
            [view performSelector:@selector(setAlternateTitle:)
                       withObject:localizedTitle];
          }
        }
      }
    } else if ([view respondsToSelector:@selector(tabViewItems)]) {
      NSArray *items = [view performSelector:@selector(tabViewItems)];
      NSEnumerator *itemEnum = [items objectEnumerator];
      NSTabViewItem *item = nil;
      while ((item = [itemEnum nextObject])) {
        NSString *label = [item label];
        NSString *localizedLabel = [self localizedStringForString:label];
        if (localizedLabel) {
          [item setLabel:localizedLabel];
        }
        if (recursive) {
          [self localizeObject:[item view] recursively:recursive];
        }
      }
    }
  }

  // Do NSTextField placeholders
  if ([view isKindOfClass:[NSTextField class]]) {
    NSString *placeholder = [[(NSTextField *)view cell] placeholderString];
    NSString *localizedPlaceholer = [self localizedStringForString:placeholder];
    if (localizedPlaceholer) {
      [[(NSTextField *)view cell] setPlaceholderString:localizedPlaceholer];
    }
  }

  // Do any NSMatrix placeholders
  if ([view isKindOfClass:[NSMatrix class]]) {
    NSMatrix *matrix = (NSMatrix *)view;
    // Process the prototype
    id cell = [matrix prototype];
    [self localizeObject:cell recursively:recursive];
    // Process the cells
    for (cell in [matrix cells]) {
      [self localizeObject:cell recursively:recursive];
      // The tooltip isn't on a cell, so we do it via the matrix.
      NSString *toolTip = [matrix toolTipForCell:cell];
      NSString *localizedToolTip = [self localizedStringForString:toolTip];
      if (localizedToolTip) {
        [matrix setToolTip:localizedToolTip forCell:cell];
      }
    }
  }

  // Do NSTableView column headers.
  if ([view isKindOfClass:[NSTableView class]]) {
    NSTableView *tableView = (NSTableView *)view;
    NSArray *columns = [tableView tableColumns];
    NSTableColumn *column = nil;
    for (column in columns) {
      [self localizeObject:[column headerCell] recursively:recursive];
    }
  }

  // Do NSSegmentedControl segments.
  if ([view isKindOfClass:[NSSegmentedControl class]]) {
    NSSegmentedControl *segmentedControl = (NSSegmentedControl *)view;
    for (NSInteger i = 0; i < [segmentedControl segmentCount]; ++i) {
      NSString *label = [segmentedControl labelForSegment:i];
      NSString *localizedLabel = [self localizedStringForString:label];
      if (localizedLabel) {
        [segmentedControl setLabel:localizedLabel forSegment:i];
      }
    }
  }

  // Do NSComboBox items.
  if ([view isKindOfClass:[NSComboBox class]]) {
    NSComboBox *combobox = (NSComboBox*)view;
    // Make sure it doesn't use a DataSource.
    if (![combobox usesDataSource]) {
      NSMutableArray *localizedValues = [NSMutableArray array];
      BOOL replaceValues = NO;
      NSString *value;
      for (value in [combobox objectValues]) {
        NSString *localizedValue = nil;
        if ([value isKindOfClass:[NSString class]]) {
          localizedValue = [self localizedStringForString:value];
        }
        if (localizedValue) {
          replaceValues = YES;
          [localizedValues addObject:localizedValue];
        } else {
          [localizedValues addObject:value];
        }
      }
      if (replaceValues) {
        [combobox removeAllItems];
        [combobox addItemsWithObjectValues:localizedValues];
      }
    }
  }
}

- (void)localizeMenu:(NSMenu *)menu recursively:(BOOL)recursive {
  if (menu) {
    NSString *title = [menu title];
    NSString *localizedTitle = [self localizedStringForString:title];
    if (localizedTitle) {
      [menu setTitle:localizedTitle];
    }
    NSArray *menuItems = [menu itemArray];
    NSMenuItem *menuItem = nil;
    for (menuItem in menuItems) {
      title = [menuItem title];
      localizedTitle = [self localizedStringForString:title];
      if (localizedTitle) {
        [menuItem setTitle:localizedTitle];
      }
      if (recursive) {
        [self localizeObject:[menuItem submenu] recursively:recursive];
      }
    }
  }
}

- (void)localizeCell:(NSCell *)cell recursively:(BOOL)recursive {
  if (cell) {
    NSString *title = [cell title];
    NSString *localizedTitle = [self localizedStringForString:title];
    if (localizedTitle) {
      [cell setTitle:localizedTitle];
    }
    [self localizeObject:[cell menu] recursively:recursive];
    id obj = [cell representedObject];
    [self localizeObject:obj recursively:recursive];
  }
}

- (void)localizeBindings:(id)object {
  NSArray *exposedBindings = [object exposedBindings];
  if (exposedBindings) {
    NSString *optionsToLocalize[] = {
      NSDisplayNameBindingOption,
      NSDisplayPatternBindingOption,
      NSMultipleValuesPlaceholderBindingOption,
      NSNoSelectionPlaceholderBindingOption,
      NSNotApplicablePlaceholderBindingOption,
      NSNullPlaceholderBindingOption,
    };
    Class stringClass = [NSString class];
    NSString *exposedBinding;
    for (exposedBinding in exposedBindings) {
      NSDictionary *bindingInfo = [object infoForBinding:exposedBinding];
      if (bindingInfo) {
        id observedObject = [bindingInfo objectForKey:NSObservedObjectKey];
        NSString *path = [bindingInfo objectForKey:NSObservedKeyPathKey];
        NSDictionary *options = [bindingInfo objectForKey:NSOptionsKey];
        if (observedObject && path && options) {
          NSMutableDictionary *newOptions
            = [NSMutableDictionary dictionaryWithDictionary:options];
          BOOL valueChanged = NO;
          for (size_t i = 0;
               i < sizeof(optionsToLocalize) / sizeof(optionsToLocalize[0]);
               ++i) {
            NSString *key = optionsToLocalize[i];
            NSString *value = [newOptions objectForKey:key];
            if ([value isKindOfClass:stringClass]) {
              NSString *localizedValue = [self localizedStringForString:value];
              if (localizedValue) {
                valueChanged = YES;
                [newOptions setObject:localizedValue forKey:key];
              }
            }
          }
          if (valueChanged) {
            // Only unbind and rebind if there is a change.
            [object unbind:exposedBinding];
            [object bind:exposedBinding
                toObject:observedObject
             withKeyPath:path
                 options:newOptions];
          }
        }
      }
    }
  }
}

- (void)localizeAccessibility:(id)object {
#if defined(MAC_OS_X_VERSION_10_10) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10
  id<NSAccessibility> accessible = object;
  if ([accessible conformsToProtocol:@protocol(NSAccessibility)]) {
    NSString* help = [accessible accessibilityHelp];
    NSString* localizedHelp = [self localizedStringForString:help];
    if (localizedHelp) {
      [accessible setAccessibilityHelp:localizedHelp];
    }
  }
#else
  NSArray *supportedAttrs = [object accessibilityAttributeNames];
  if ([supportedAttrs containsObject:NSAccessibilityHelpAttribute]) {
    NSString *accessibilityHelp
      = [object accessibilityAttributeValue:NSAccessibilityHelpAttribute];
    if (accessibilityHelp) {
      NSString *localizedAccessibilityHelp
        = [self localizedStringForString:accessibilityHelp];
      if (localizedAccessibilityHelp) {
        [object accessibilitySetValue:localizedAccessibilityHelp
                         forAttribute:NSAccessibilityHelpAttribute];
      }
    }
  }
#endif

  // We cannot do the same thing with NSAccessibilityDescriptionAttribute; see
  // the links in the header file for more details.
}

@end
