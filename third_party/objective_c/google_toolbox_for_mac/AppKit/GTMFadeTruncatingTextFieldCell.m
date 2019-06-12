//  GTMFadeTruncatingTextFieldCell.m
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

#import "GTMFadeTruncatingTextFieldCell.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

@implementation GTMFadeTruncatingTextFieldCell
- (void)awakeFromNib {
  // Force to clipping
  [self setLineBreakMode:NSLineBreakByClipping];
}

- (id)initTextCell:(NSString *)aString {
  self = [super initTextCell:aString];
  if (self) {
    // Force to clipping
    [self setLineBreakMode:NSLineBreakByClipping];
  }
  return self;
}

- (void)drawTextGradientPart:(NSAttributedString *)attributedString
                   titleRect:(NSRect)titleRect
              backgroundRect:(NSRect)backgroundRect
                    clipRect:(NSRect)clipRect
                        mask:(NSGradient *)mask
                 fadeToRight:(BOOL)fadeToRight {
  // Draw the gradient part with a transparency layer. This makes the text look
  // suboptimal, but since it fades out, that's ok.
  [NSGraphicsContext saveGraphicsState];
  [NSBezierPath clipRect:backgroundRect];
  CGContextRef context = [[NSGraphicsContext currentContext] graphicsPort];
  CGContextBeginTransparencyLayerWithRect(context,
                                          NSRectToCGRect(backgroundRect), 0);

  if ([self drawsBackground]) {
    [[self backgroundColor] set];
    NSRectFillUsingOperation(backgroundRect,
                             NSCompositeSourceOver);
  }

  [NSGraphicsContext saveGraphicsState];
  [NSBezierPath clipRect:clipRect];
  [attributedString drawInRect:titleRect];
  [NSGraphicsContext restoreGraphicsState];

  NSPoint startPoint;
  NSPoint endPoint;
  if (fadeToRight) {
    startPoint = backgroundRect.origin;
    endPoint = NSMakePoint(NSMaxX(backgroundRect), NSMinY(backgroundRect));
  } else {
    startPoint = NSMakePoint(NSMaxX(backgroundRect), NSMinY(backgroundRect));
    endPoint = backgroundRect.origin;
  }

  // Draw the gradient mask
  CGContextSetBlendMode(context, kCGBlendModeDestinationIn);
  [mask drawFromPoint:startPoint
              toPoint:endPoint
              options:fadeToRight ? NSGradientDrawsBeforeStartingLocation :
                                    NSGradientDrawsAfterEndingLocation];

  CGContextEndTransparencyLayer(context);
  [NSGraphicsContext restoreGraphicsState];
}

- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)controlView {
  NSRect titleRect = [self titleRectForBounds:cellFrame];
  // For some reason the title rect is too far to the left.
  titleRect.origin.x += 2;
  titleRect.size.width -= 2;

  NSAttributedString *attributedString = [self attributedStringValue];
  NSSize stringSize = [attributedString size];

  // Don't complicate drawing unless we need to clip
  if (stringSize.width <= NSWidth(titleRect)) {
    [super drawInteriorWithFrame:cellFrame inView:controlView];
    return;
  }

  // Clip the string by drawing it to the left by |offsetX|.
  CGFloat offsetX = 0;
  switch (truncateMode_) {
    case GTMFadeTruncatingTail:
      break;
    case GTMFadeTruncatingHead:
      offsetX = stringSize.width - titleRect.size.width;
      break;
    case GTMFadeTruncatingHeadAndTail: {
      if (desiredCharactersToTruncateFromHead_ > 0) {
        NSAttributedString *clippedHeadString =
            [attributedString attributedSubstringFromRange:
                NSMakeRange(0, desiredCharactersToTruncateFromHead_)];
        NSSize clippedHeadSize = [clippedHeadString size];

        // This is the offset at which we start drawing. This causes the
        // beginning of the string to get clipped.
        offsetX = clippedHeadSize.width;

        // Due to the fade effect the first character is hard to see.
        // We want to make sure the first character starting at
        // |desiredCharactersToTruncateFromHead_| is readable so we reduce
        // the offset by a little bit.
        offsetX = MAX(0, offsetX - stringSize.height);

        // If the offset is so large that there's empty space at the tail
        // then reduce the offset so we can use up the empty space.
        CGFloat delta = stringSize.width - titleRect.size.width;
        if (offsetX > delta)
          offsetX = delta;
      } else {
        // Center the string and clip equal portions of the head and tail.
        offsetX = round((stringSize.width - titleRect.size.width) / 2.0);
      }
      break;
    }
  }

  NSRect offsetTitleRect = titleRect;
  offsetTitleRect.origin.x -= offsetX;
  offsetTitleRect.size.width += offsetX;
  BOOL isTruncatingHead = offsetX > 0;
  BOOL isTruncatingTail = (stringSize.width - titleRect.size.width) > offsetX;

  // Gradient is about twice our line height long
  CGFloat gradientWidth = MIN(stringSize.height * 2, round(NSWidth(cellFrame) / 4));

  // Head, solid, and tail rects for drawing the background.
  NSRect solidBackgroundPart = [self drawingRectForBounds:cellFrame];
  NSRect headBackgroundPart = NSZeroRect;
  NSRect tailBackgroundPart = NSZeroRect;
  if (isTruncatingHead)
    NSDivideRect(solidBackgroundPart, &headBackgroundPart, &solidBackgroundPart,
                 gradientWidth, NSMinXEdge);
  if (isTruncatingTail)
    NSDivideRect(solidBackgroundPart, &tailBackgroundPart, &solidBackgroundPart,
                 gradientWidth, NSMaxXEdge);

  // Head, solid and tail rects for clipping the title. This is slightly
  // smaller than the background rects.
  NSRect solidTitleClipPart = titleRect;
  NSRect headTitleClipPart = NSZeroRect;
  NSRect tailTitleClipPart = NSZeroRect;
  if (isTruncatingHead) {
    CGFloat width = NSMinX(solidBackgroundPart) - NSMinX(solidTitleClipPart);
    NSDivideRect(solidTitleClipPart, &headTitleClipPart, &solidTitleClipPart,
                 width, NSMinXEdge);
  }
  if (isTruncatingTail) {
    CGFloat width = NSMaxX(solidTitleClipPart) - NSMaxX(solidBackgroundPart);
    NSDivideRect(solidTitleClipPart, &tailTitleClipPart, &solidTitleClipPart,
                 width, NSMaxXEdge);
  }

  // Draw non-gradient part without transparency layer, as light text on a dark
  // background looks bad with a gradient layer.
  [NSGraphicsContext saveGraphicsState];
  if ([self drawsBackground]) {
    [[self backgroundColor] set];
    NSRectFillUsingOperation(solidBackgroundPart, NSCompositeSourceOver);
  }
  // We draw the text ourselves because [super drawInteriorWithFrame:inView:]
  // doesn't draw correctly if the cell draws its own background.
  [NSBezierPath clipRect:solidTitleClipPart];
  [attributedString drawInRect:offsetTitleRect];
  [NSGraphicsContext restoreGraphicsState];

  NSColor *startColor = [self textColor];;
  NSColor *endColor = [startColor colorWithAlphaComponent:0.0];
  NSGradient *mask = [[NSGradient alloc] initWithStartingColor:startColor
                                                   endingColor:endColor];

  if (isTruncatingHead)
    [self drawTextGradientPart:attributedString
                     titleRect:offsetTitleRect
                backgroundRect:headBackgroundPart
                      clipRect:headTitleClipPart
                          mask:mask
                   fadeToRight:NO];
  if (isTruncatingTail)
    [self drawTextGradientPart:attributedString
                     titleRect:offsetTitleRect
                backgroundRect:tailBackgroundPart
                      clipRect:tailTitleClipPart
                          mask:mask
                   fadeToRight:YES];

  [mask release];
}

- (void)setTruncateMode:(GTMFadeTruncateMode)mode {
  if (truncateMode_ != mode) {
    truncateMode_ = mode;
    [[self controlView] setNeedsDisplay:YES];
  }
}

- (GTMFadeTruncateMode)truncateMode {
  return truncateMode_;
}

- (void)setDesiredCharactersToTruncateFromHead:(NSUInteger)length {
  if (desiredCharactersToTruncateFromHead_ != length) {
    desiredCharactersToTruncateFromHead_ = length;
    [[self controlView] setNeedsDisplay:YES];
  }
}

- (NSUInteger)desiredCharactersToTruncateFromHead {
  return desiredCharactersToTruncateFromHead_;
}

// The faded ends of the cell are not opaque.
- (BOOL)isOpaque {
  return NO;
}

@end

#endif
