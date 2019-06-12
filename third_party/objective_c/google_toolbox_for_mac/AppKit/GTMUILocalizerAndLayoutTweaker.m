//
//  GTMUILocalizerAndLayoutTweaker.m
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

#import "GTMUILocalizerAndLayoutTweaker.h"
#import "GTMUILocalizer.h"

// Controls if +wrapString:width:font: uses a subclassed TypeSetter to do
// its work in one pass.
#define GTM_USE_TYPESETTER 1

// Helper that will try to do a SizeToFit on any UI items and do the special
// case handling we also need to end up with a usable UI item.  It also takes
// an offset so we can slide the item if we need to.
// Returns the change in the view's size.
static NSSize SizeToFit(NSView *view, NSPoint offset);
// Compare function for -[NSArray sortedArrayUsingFunction:context:]
static NSInteger CompareFrameX(id view1, id view2, void *context);
// Check if the view is anchored on the right (fixed right, flexible left).
static BOOL IsRightAnchored(NSView *view);

// Constant for a forced string wrap in button cells (Opt-Return in IB inserts
// this into the string).
NSString * const kForcedWrapString = @"\xA";
// Radio and Checkboxes (NSButtonCell) appears to use two different layout
// algorithms for sizeToFit calls and drawing calls when there is a forced word
// wrap in the title.  The result is a sizeToFit can tell you it all fits N
// lines in the given rect, but at draw time, it draws as >N lines and never
// gets as wide, resulting in a clipped control.  This fudge factor is what is
// added to try and avoid these by giving the size calls just enough slop to
// handle the differences.
// radar://7831901 different wrapping between sizeToFit and drawing
static const CGFloat kWrapperStringSlop = 0.9;

#if GTM_USE_TYPESETTER

@interface GTMBreakRecordingTypeSetter : NSATSTypesetter {
 @private
  NSMutableArray *array_;
}
@end

@implementation GTMBreakRecordingTypeSetter
- (id)init {
  if ((self = [super init])) {
    array_ = [[NSMutableArray alloc] init];
  }
  return self;
}

- (void)dealloc {
  [array_ release];
  [super dealloc];
}

- (BOOL)shouldBreakLineByWordBeforeCharacterAtIndex:(NSUInteger)charIndex {
  [array_ addObject:[NSNumber numberWithUnsignedInteger:charIndex]];
  return YES;
}

- (NSArray*)breakArray {
  return array_;
}

@end

#endif  // GTM_USE_TYPESETTER

@interface GTMUILocalizerAndLayoutTweaker (PrivateMethods)
// Recursively walk the UI triggering Tweakers.
- (void)tweakView:(NSView *)view;
// Insert newlines so the string wraps to the given width using the requested
// font.
+ (NSString*)wrapString:(NSString *)string
                  width:(CGFloat)width
                   font:(NSFont *)font;
@end

@interface GTMWidthBasedTweaker (InternalMethods)
// Does the actual work to size and adjust the views within this Tweaker.  The
// offset is the amount this view should shift as part of it's resize.
// Returns change in this view's width.
- (CGFloat)tweakLayoutWithOffset:(NSPoint)offset;
@end

@implementation GTMUILocalizerAndLayoutTweaker

- (void)awakeFromNib {
  if (uiObject_) {
    GTMUILocalizer *localizer = localizer_;
    if (!localizer) {
      NSBundle *bundle = [GTMUILocalizer bundleForOwner:localizerOwner_];
      localizer = [[[GTMUILocalizer alloc] initWithBundle:bundle] autorelease];
    }
    [self applyLocalizer:localizer tweakingUI:uiObject_];
  }
}

- (void)applyLocalizer:(GTMUILocalizer *)localizer
            tweakingUI:(id)uiObject {
  // Localize first
  [localizer localizeObject:uiObject recursively:YES];

  // Then tweak!
  [self tweakUI:uiObject];
}

- (void)tweakUI:(id)uiObject {
  // Figure out where we start
  NSView *startView;
  if ([uiObject isKindOfClass:[NSWindow class]]) {
    startView = [(NSWindow *)uiObject contentView];
  } else {
    _GTMDevAssert([uiObject isKindOfClass:[NSView class]],
                  @"should have been a subclass of NSView");
    startView = (NSView *)uiObject;
  }

  // Tweak away!
  [self tweakView:startView];
}

- (void)tweakView:(NSView *)view {
  // If it's a alignment box, let it do its thing...
  if ([view isKindOfClass:[GTMWidthBasedTweaker class]]) {
    [(GTMWidthBasedTweaker *)view tweakLayoutWithOffset:NSZeroPoint];
  // Do our best to support TabViews. If the tabs need to resize, you are
  // probably better off manually running them through a tweaker and then fixing
  // up the parent view (and other tabs) to look right.
  } else if ([view isKindOfClass:[NSTabView class]]) {
    NSArray *tabViewItems = [(NSTabView *)view tabViewItems];
    NSTabViewItem *item = nil;
    for (item in tabViewItems) {
      [self tweakView:[item view]];
    }
  // Generically look for subviews...
  } else {
    NSArray *subviews = [view subviews];
    NSView *subview = nil;
    for (subview in subviews) {
      [self tweakView:subview];
    }
  }
}

+ (NSString*)wrapString:(NSString *)string
                  width:(CGFloat)width
                   font:(NSFont *)font {
  // Set up the objects needed for the layout work.
  NSRect targetRect = NSMakeRect(0, 0, width, CGFLOAT_MAX);
  NSTextContainer* textContainer =
    [[[NSTextContainer alloc] initWithContainerSize:targetRect.size]
     autorelease];
  NSLayoutManager* layoutManager = [[[NSLayoutManager alloc] init] autorelease];
  NSTextStorage* textStorage =
    [[[NSTextStorage alloc] initWithString:string] autorelease];

  [textStorage addLayoutManager:layoutManager];
  [layoutManager addTextContainer:textContainer];
  // From playing in interface builder, the padding seems to be 2 on the line
  // fragments to get the same wrapping as what the NSCell will do in the end.
  [textContainer setLineFragmentPadding:2.0f];

  if (font == NULL) {
    // If the font is NULL, it is the System font.
    font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
  }
  // Apply the font.
  [textStorage setFont:font];

  // Get the mutable string for the layout, remove any forced wraps in it.
  NSMutableString* workerStr = [textStorage mutableString];
  [workerStr replaceOccurrencesOfString:kForcedWrapString
                             withString:@""
                                options:NSLiteralSearch
                                  range:NSMakeRange(0, [workerStr length])];

#if GTM_USE_TYPESETTER
  // Put in the recording type setter.
  GTMBreakRecordingTypeSetter *typeSetter =
    [[[GTMBreakRecordingTypeSetter alloc] init] autorelease];
  [layoutManager setTypesetter:typeSetter];
  // Make sure things are layed out (10.5 has a clean API for this, 10.4
  // doesn't).
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
  [layoutManager ensureLayoutForCharacterRange:NSMakeRange(0,
                                                           [textStorage length])];
#else
  [layoutManager lineFragmentRectForGlyphAtIndex:[layoutManager numberOfGlyphs]-1
                                  effectiveRange:NULL];
#endif

  // Insert the breaks everywere the type setter got asked about breaks.
  NSEnumerator *reverseEnumerator =
    [[typeSetter breakArray] reverseObjectEnumerator];
  NSNumber *number;
  while ((number = [reverseEnumerator nextObject]) != nil) {
    [workerStr insertString:kForcedWrapString
                    atIndex:[number unsignedIntegerValue]];
  }
#else
  // Find out how tall lines would be for the layout loop.
  CGFloat lineHeight = [layoutManager defaultLineHeightForFont:font];
  targetRect.size.height = lineHeight;

  // Loop until all glyphs are layout out.
  NSUInteger numGlyphsUsed = 0;
  while (numGlyphsUsed < [layoutManager numberOfGlyphs]) {
    // See what fits in the current rect
    NSRange range = [layoutManager glyphRangeForBoundingRect:targetRect
                                             inTextContainer:textContainer];
    numGlyphsUsed = NSMaxRange(range);
    if (numGlyphsUsed < [layoutManager numberOfGlyphs]) {
      // Didn't all fit, add a break, and grow the rect to try again.
      NSRange charRange = [layoutManager glyphRangeForCharacterRange:range
                                                actualCharacterRange:nil];
      [workerStr insertString:kForcedWrapString atIndex:NSMaxRange(charRange)];
      targetRect.size.height += lineHeight;
    }
  }
#endif  // GTM_USE_TYPESETTER

  // Return the string with forced wraps
  return [[workerStr copy] autorelease];
}

+ (NSSize)sizeToFitView:(NSView *)view {
  return SizeToFit(view, NSZeroPoint);
}

+ (CGFloat)sizeToFitFixedWidthTextField:(NSTextField *)textField {
  NSRect initialFrame = [textField frame];
  NSRect sizeRect = NSMakeRect(0, 0, NSWidth(initialFrame), CGFLOAT_MAX);
  NSSize newSize = [[textField cell] cellSizeForBounds:sizeRect];
  newSize.width = NSWidth(initialFrame);
  [textField setFrameSize:newSize];
  return newSize.height - NSHeight(initialFrame);
}

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

+ (CGFloat)sizeToFitFixedHeightTextField:(NSTextField *)textField {
  return [self sizeToFitFixedHeightTextField:textField minWidth:(CGFloat)0];
}

+ (CGFloat)sizeToFitFixedHeightTextField:(NSTextField *)textField
                                minWidth:(NSUInteger)minWidth {
  NSRect initialRect = [textField frame];
  NSCell *cell = [textField cell];
  NSSize titleSize = [cell titleRectForBounds:initialRect].size;

  // Find linebreak point, and keep trying them until we're under the height
  // requested.
  NSString *str = [textField stringValue];
  CFStringTokenizerRef tokenizer =
      CFStringTokenizerCreate(NULL,
                              (CFStringRef)str,
                              CFRangeMake(0, [str length]),
                              kCFStringTokenizerUnitLineBreak,
                              NULL);
  if (!tokenizer) {
    _GTMDevAssert(tokenizer, @"failed to get a tokenizer");
    return 0.0;
  }
  NSCell *workerCell = [[cell copy] autorelease];

  // Loop trying line break points until the height fits.
  while (1) {
    CFStringTokenizerTokenType tokenType =
        CFStringTokenizerAdvanceToNextToken(tokenizer);
    if (tokenType == kCFStringTokenizerTokenNone) {
      // Reached the end without ever find a good width, how?
      _GTMDevAssert(0, @"Failed to find a good size?!");
      [textField sizeToFit];
      break;
    }
    CFRange tokenRange = CFStringTokenizerGetCurrentTokenRange(tokenizer);

    NSRange subStringRange =
      NSMakeRange(0, tokenRange.location + tokenRange.length);
    NSString *subString = [str substringWithRange:subStringRange];

    // Find how wide the cell would be for this sub string.
    [workerCell setStringValue:subString];
    CGFloat testWidth = [workerCell cellSize].width;

    // Find the overall size if wrapped to this width.
    NSRect sizeRect = NSMakeRect(0, 0, testWidth, CGFLOAT_MAX);
    NSSize newSize = [cell cellSizeForBounds:sizeRect];
    if (newSize.height <= titleSize.height) {
      [textField setFrameSize:newSize];
      break;
    }
  }

  CFRelease(tokenizer);

  NSSize fixedSize = [textField frame].size;
  NSSize finalSize = NSMakeSize(fixedSize.width, NSHeight(initialRect));

  // Enforce the minWidth
  if (minWidth > fixedSize.width) {
    finalSize.width = minWidth;
  }
  // Make integral.
  finalSize.width = ceil(fixedSize.width);
  finalSize.height = ceil(fixedSize.height);
  if (!NSEqualSizes(fixedSize, finalSize)) {
    [textField setFrameSize:finalSize];
  }

  // Return how much things changed
  return finalSize.width - NSWidth(initialRect);
}

#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

+ (void)wrapButtonTitleForWidth:(NSButton *)button {
  NSCell *cell = [button cell];
  NSRect frame = [button frame];

  NSRect titleFrame = [cell titleRectForBounds:frame];

  NSString* newTitle = [self wrapString:[button title]
                                  width:NSWidth(titleFrame)
                                   font:[button font]];
  [button setTitle:newTitle];
}

+ (void)wrapRadioGroupForWidth:(NSMatrix *)radioGroup {
  NSSize cellSize = [radioGroup cellSize];
  NSRect tmpRect = NSMakeRect(0, 0, cellSize.width, cellSize.height);
  NSFont *font = [radioGroup font];

  NSCell *cell;
  for (cell in [radioGroup cells]) {
    NSRect titleFrame = [cell titleRectForBounds:tmpRect];
    NSString* newTitle = [self wrapString:[cell title]
                                    width:NSWidth(titleFrame)
                                     font:font];
    [cell setTitle:newTitle];
  }
}

+ (void)resizeWindowWithoutAutoResizingSubViews:(NSWindow*)window
                                          delta:(NSSize)delta {
  NSView *contentView = [window contentView];

  // Clear autosizesSubviews (saving the state).
  BOOL autoresizesSubviews = [contentView autoresizesSubviews];
  if (autoresizesSubviews) {
    [contentView setAutoresizesSubviews:NO];
  }

  NSRect rect = [contentView convertRect:[window frame] fromView:nil];
  rect.size.width += delta.width;
  rect.size.height += delta.height;
  rect = [contentView convertRect:rect toView:nil];
  [window setFrame:rect display:NO];
  // For some reason the content view is resizing, but some times not adjusting
  // its origin, so correct it manually.
  [contentView setFrameOrigin:NSMakePoint(0, 0)];

  // Restore autosizesSubviews.
  if (autoresizesSubviews) {
    [contentView setAutoresizesSubviews:YES];
  }
}

+ (void)resizeViewWithoutAutoResizingSubViews:(NSView*)view
                                        delta:(NSSize)delta {
  // Clear autosizesSubviews (saving the state).
  BOOL autoresizesSubviews = [view autoresizesSubviews];
  if (autoresizesSubviews) {
    [view setAutoresizesSubviews:NO];
  }

  NSRect rect = [view frame];
  rect.size.width += delta.width;
  rect.size.height += delta.height;
  [view setFrame:rect];

  // Restore autosizesSubviews.
  if (autoresizesSubviews) {
    [view setAutoresizesSubviews:YES];
  }
}

@end

@implementation GTMWidthBasedTweaker

- (CGFloat)changedWidth {
  return widthChange_;
}

- (CGFloat)tweakLayoutWithOffset:(NSPoint)offset {
  NSArray *subviews = [self subviews];
  if (![subviews count]) {
    widthChange_ = 0.0;
    return widthChange_;
  }

  BOOL sumMode = NO;
  NSMutableArray *rightAlignedSubViews = nil;
  NSMutableArray *rightAlignedSubViewDeltas = nil;
  if ([subviews count] > 1) {
    // Check if the frames are in a row by seeing if when they are left aligned
    // they overlap.  If they don't overlap in this case, it means they are
    // probably stacked instead.
    NSRect rect1 = [[subviews objectAtIndex:0] frame];
    NSRect rect2 = [[subviews objectAtIndex:1] frame];
    rect1.origin.x = rect2.origin.x = 0;
    if (NSIntersectsRect(rect1, rect2)) {
      // No, so walk them x order moving them along so they don't overlap.
      sumMode = YES;
      subviews = [subviews sortedArrayUsingFunction:CompareFrameX context:NULL];
    } else {
      // Since they are vertical, any views pinned to the right will have to be
      // shifted after we finish figuring out the final size.
      rightAlignedSubViews = [NSMutableArray array];
      rightAlignedSubViewDeltas = [NSMutableArray array];
    }
  }

  // Size our subviews
  NSView *subView;
  CGFloat finalDelta = sumMode ? 0 : -CGFLOAT_MAX;
  NSPoint subViewOffset = NSZeroPoint;
  for (subView in subviews) {
    if (sumMode) {
      subViewOffset.x = finalDelta;
    }
    CGFloat delta = SizeToFit(subView, subViewOffset).width;
    if (sumMode) {
      finalDelta += delta;
    } else {
      if (delta > finalDelta) {
        finalDelta = delta;
      }
    }
    // Track the right anchored subviews size changes so we can update them
    // once we know this view's size.
    if (IsRightAnchored(subView)) {
      [rightAlignedSubViews addObject:subView];
#if CGFLOAT_IS_DOUBLE
      NSNumber *nsDelta = [NSNumber numberWithDouble:delta];
#else
      NSNumber *nsDelta = [NSNumber numberWithFloat:delta];
#endif
      [rightAlignedSubViewDeltas addObject:nsDelta];
    }
  }

  // Are we pinned to the right of our parent?
  BOOL rightAnchored = IsRightAnchored(self);

  // Adjust our size (turn off auto resize, because we just fixed up all the
  // objects within us).
  BOOL autoresizesSubviews = [self autoresizesSubviews];
  if (autoresizesSubviews) {
    [self setAutoresizesSubviews:NO];
  }
  NSRect selfFrame = [self frame];
  selfFrame.size.width += finalDelta;
  if (rightAnchored) {
    // Right side is anchored, so we need to slide back to the left.
    selfFrame.origin.x -= finalDelta;
  }
  selfFrame.origin.x += offset.x;
  selfFrame.origin.y += offset.y;
  [self setFrame:selfFrame];
  if (autoresizesSubviews) {
    [self setAutoresizesSubviews:autoresizesSubviews];
  }

  // Now spin over the list of right aligned view and their size changes
  // fixing up their positions so they are still right aligned in our final
  // view.
  for (NSUInteger lp = 0; lp < [rightAlignedSubViews count]; ++lp) {
    subView = [rightAlignedSubViews objectAtIndex:lp];
    CGFloat delta = [[rightAlignedSubViewDeltas objectAtIndex:lp] doubleValue];
    NSRect viewFrame = [subView frame];
    viewFrame.origin.x += -delta + finalDelta;
    [subView setFrame:viewFrame];
  }

  if (viewToSlideAndResize_) {
    NSRect viewFrame = [viewToSlideAndResize_ frame];
    if (!rightAnchored) {
      // If our right wasn't anchored, this view slides (we push it right).
      // (If our right was anchored, the assumption is the view is in front of
      // us so its x shouldn't move.)
      viewFrame.origin.x += finalDelta;
    }
    viewFrame.size.width -= finalDelta;
    [viewToSlideAndResize_ setFrame:viewFrame];
  }
  if (viewToSlide_) {
    NSRect viewFrame = [viewToSlide_ frame];
    // Move the view the same direction we moved.
    if (rightAnchored) {
      viewFrame.origin.x -= finalDelta;
    } else {
      viewFrame.origin.x += finalDelta;
    }
    [viewToSlide_ setFrame:viewFrame];
  }
  if (viewToResize_) {
    if ([viewToResize_ isKindOfClass:[NSWindow class]]) {
      NSWindow *window = (NSWindow *)viewToResize_;
      NSView *contentView = [window contentView];
      NSRect windowFrame = [contentView convertRect:[window frame]
                                           fromView:nil];
      windowFrame.size.width += finalDelta;
      windowFrame = [contentView convertRect:windowFrame toView:nil];
      [window setFrame:windowFrame display:YES];
      // For some reason the content view is resizing, but not adjusting its
      // origin, so correct it manually.
      [contentView setFrameOrigin:NSMakePoint(0, 0)];
      // TODO: should we update min size?
    } else {
      NSRect viewFrame = [viewToResize_ frame];
      viewFrame.size.width += finalDelta;
      [viewToResize_ setFrame:viewFrame];
      // TODO: should we check if this view is right anchored, and adjust its
      // x position also?
    }
  }

  widthChange_ = finalDelta;
  return widthChange_;
}

@end

#pragma mark -

static NSSize SizeToFit(NSView *view, NSPoint offset) {

  // If we've got one of us within us, recurse (for grids)
  if ([view isKindOfClass:[GTMWidthBasedTweaker class]]) {
    GTMWidthBasedTweaker *widthAlignmentBox = (GTMWidthBasedTweaker *)view;
    return NSMakeSize([widthAlignmentBox tweakLayoutWithOffset:offset], 0);
  }

  NSRect oldFrame = [view frame];
  NSRect fitFrame = oldFrame;
  NSRect newFrame = oldFrame;

  if ([view isKindOfClass:[NSTextField class]] &&
      [(NSTextField *)view isEditable]) {
    // Don't try to sizeToFit because edit fields really don't want to be sized
    // to what is in them as they are for users to enter things so honor their
    // current size.
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
  } else if ([view isKindOfClass:[NSPathControl class]]) {
    // Don't try to sizeToFit because NSPathControls usually need to be able
    // to display any path, so they shouldn't tight down to whatever they
    // happen to be listing at the moment.
#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
  } else {
    // Genericaly fire a sizeToFit if it has one.
    if ([view respondsToSelector:@selector(sizeToFit)]) {
      [view performSelector:@selector(sizeToFit)];
      fitFrame = [view frame];
      newFrame = fitFrame;

      if ([view isKindOfClass:[NSMatrix class]]) {
        NSMatrix *matrix = (NSMatrix *)view;
        // See note on kWrapperStringSlop for why this is done.
        NSCell *cell;
        for (cell in [matrix cells]) {
          if ([[cell title] rangeOfString:kForcedWrapString].location !=
              NSNotFound) {
            newFrame.size.width += kWrapperStringSlop;
            break;
          }
        }
      }

    }

    if ([view isKindOfClass:[NSButton class]]) {
      NSButton *button = (NSButton *)view;
      // -[NSButton sizeToFit] gives much worse results than IB's Size to Fit
      // option for standard push buttons.
      if (([button bezelStyle] == NSRoundedBezelStyle) &&
          ([[button cell] controlSize] == NSRegularControlSize)) {
        // This is the amount of padding IB adds over a sizeToFit, empirically
        // determined.
        const CGFloat kExtraPaddingAmount = 12.0;
        // Width is tricky, new buttons in IB are 96 wide, Carbon seems to have
        // defaulted to 70, Cocoa seems to like 82.  But we go with 96 since
        // that's what IB is doing these days.
        const CGFloat kMinButtonWidth = (CGFloat)96.0;
        newFrame.size.width = NSWidth(newFrame) + kExtraPaddingAmount;
        if (NSWidth(newFrame) < kMinButtonWidth) {
          newFrame.size.width = kMinButtonWidth;
        }
      } else if ([button bezelStyle] == NSTexturedRoundedBezelStyle &&
                 [[button cell] controlSize] == NSRegularControlSize) {
        // The round textured style needs to have a little extra padding,
        // otherwise the baseline of the text sinks by a few pixels.
        const CGFloat kExtraPaddingAmount = 4.0;
        newFrame.size.width += kExtraPaddingAmount;
      } else {
        // See note on kWrapperStringSlop for why this is done.
        NSString *title = [button title];
        if ([title rangeOfString:kForcedWrapString].location != NSNotFound) {
          newFrame.size.width += kWrapperStringSlop;
        }
      }
    }
  }

  // Apply the offset, and see if we need to change the frame (again).
  newFrame.origin.x += offset.x;
  newFrame.origin.y += offset.y;
  if (!NSEqualRects(fitFrame, newFrame)) {
    [view setFrame:newFrame];
  }

  // Return how much we changed size.
  return NSMakeSize(NSWidth(newFrame) - NSWidth(oldFrame),
                    NSHeight(newFrame) - NSHeight(oldFrame));
}

static NSInteger CompareFrameX(id view1, id view2, void *context) {
  CGFloat x1 = [view1 frame].origin.x;
  CGFloat x2 = [view2 frame].origin.x;
  if (x1 < x2)
    return NSOrderedAscending;
  else if (x1 > x2)
    return NSOrderedDescending;
  else
    return NSOrderedSame;
}

static BOOL IsRightAnchored(NSView *view) {
  NSUInteger autoresizing = [view autoresizingMask];
  BOOL viewRightAnchored =
   ((autoresizing & (NSViewMinXMargin | NSViewMaxXMargin)) == NSViewMinXMargin);
  return viewRightAnchored;
}
