//
//  GTMFadeTruncatingLabel.m
//
//  Copyright 2012 Google Inc.
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
#import "GTMFadeTruncatingLabel.h"

@interface GTMFadeTruncatingLabel ()
- (void)setup;
@end

@implementation GTMFadeTruncatingLabel

@synthesize truncateMode = truncateMode_;

- (void)setup {
  self.backgroundColor = [UIColor clearColor];
  truncateMode_ = GTMFadeTruncatingTail;
}

- (id)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    // Use clip as a default value.
    self.lineBreakMode = NSLineBreakByClipping;
    [self setup];
  }
  return self;
}

- (void)awakeFromNib {
  [super awakeFromNib];
  [self setup];
}

// Draw fade gradient mask if text is wider than rect.
- (void)drawTextInRect:(CGRect)requestedRect {
  CGContextRef context = UIGraphicsGetCurrentContext();
  CGContextSaveGState(context);

#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_7_0
  // |sizeWithFont:| is deprecated in iOS 7, replaced by |sizeWithAttributes:|
  CGSize size = [self.text sizeWithFont:self.font];
#else
  CGSize size = CGSizeZero;
  if (self.font) {
    size = [self.text sizeWithAttributes:@{NSFontAttributeName:self.font}];
    // sizeWithAttributes: may return fractional values, so ceil the width and
    // height to preserve the behavior of sizeWithFont:.
    size = CGSizeMake(ceil(size.width), ceil(size.height));
  }
#endif
  if (size.width > requestedRect.size.width) {
    UIImage* image = [[self class]
        getLinearGradient:requestedRect
                 fadeHead:((self.truncateMode & GTMFadeTruncatingHead) > 0)
                 fadeTail:((self.truncateMode & GTMFadeTruncatingTail) > 0)];
    CGContextClipToMask(context, self.bounds, image.CGImage);
  }

  if (self.shadowColor) {
    CGRect shadowRect = CGRectOffset(requestedRect, self.shadowOffset.width,
                                     self.shadowOffset.height);
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_7_0
    // |drawInRect:withFont:lineBreakMode:alignment:| is deprecated in iOS 7,
    // replaced by |drawInRect:withAttributes:|
    CGContextSetFillColorWithColor(context, self.shadowColor.CGColor);
    [self.text drawInRect:shadowRect
                 withFont:self.font
            lineBreakMode:self.lineBreakMode
                alignment:self.textAlignment];
#else
    if (self.font) {
      NSMutableParagraphStyle* textStyle =
          [[[NSParagraphStyle defaultParagraphStyle] mutableCopy] autorelease];
      textStyle.lineBreakMode = self.lineBreakMode;
      textStyle.alignment = self.textAlignment;
      NSDictionary* attributes = @{
          NSFontAttributeName:self.font,
          NSParagraphStyleAttributeName:textStyle,
          NSForegroundColorAttributeName:self.shadowColor
      };
      [self.text drawInRect:shadowRect
             withAttributes:attributes];
    }
#endif
  }

  // We check for nilness of shadowColor above, but there's no need to do so
  // for textColor here because  UILabel's textColor property cannot be nil.
  // The UILabel docs say the default textColor is black and experimentation
  // shows that calling -textColor will return the cached [UIColor blackColor]
  // when called on a freshly alloc/init-ed UILabel, or a UILabel whose
  // textColor has been set to nil.
  //
  // @see https://developer.apple.com/Library/ios/documentation/UIKit/Reference/UILabel_Class/Reference/UILabel.html#//apple_ref/occ/instp/UILabel/textColor
  // (NOTE(bgoodwin): interesting side-note. These docs also say setting
  // textColor to nil will result in an exception. In my testing, that did not
  // happen.)
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_7_0
  // |drawInRect:withFont:lineBreakMode:alignment:| is deprecated in iOS 7,
  // replaced by |drawInRect:withAttributes:|
  CGContextSetFillColorWithColor(context, self.textColor.CGColor);
  [self.text drawInRect:requestedRect
               withFont:self.font
          lineBreakMode:self.lineBreakMode
              alignment:self.textAlignment];
#else
    if (self.font) {
      NSMutableParagraphStyle* textStyle =
          [[[NSParagraphStyle defaultParagraphStyle] mutableCopy] autorelease];
      textStyle.lineBreakMode = self.lineBreakMode;
      textStyle.alignment = self.textAlignment;
      NSDictionary* attributes = @{
          NSFontAttributeName:self.font,
          NSParagraphStyleAttributeName:textStyle,
          NSForegroundColorAttributeName:self.textColor
      };
      [self.text drawInRect:requestedRect
             withAttributes:attributes];
    }
#endif
  CGContextRestoreGState(context);
}

// Create gradient opacity mask based on direction.
+ (UIImage*)getLinearGradient:(CGRect)rect
                     fadeHead:(BOOL)fadeHead
                     fadeTail:(BOOL)fadeTail {
  // Create an opaque context.
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
  CGContextRef context = CGBitmapContextCreate(NULL,
                                               rect.size.width,
                                               rect.size.height,
                                               8,
                                               4*rect.size.width,
                                               colorSpace,
                                               (CGBitmapInfo)kCGImageAlphaNone);

  // White background will mask opaque, black gradient will mask transparent.
  CGContextSetFillColorWithColor(context, [UIColor whiteColor].CGColor);
  CGContextFillRect(context, rect);

  // Create gradient from white to black.
  CGFloat locs[2] = { 0.0f, 1.0f };
  CGFloat components[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
  CGGradientRef gradient =
      CGGradientCreateWithColorComponents(colorSpace, components, locs, 2);
  CGColorSpaceRelease(colorSpace);

  // Draw head and/or tail gradient.
  CGFloat fadeWidth = MIN(rect.size.height * 2, floor(rect.size.width / 4));
  CGFloat minX = CGRectGetMinX(rect);
  CGFloat maxX = CGRectGetMaxX(rect);
  if (fadeTail) {
    CGFloat startX = maxX - fadeWidth;
    CGPoint startPoint = CGPointMake(startX, CGRectGetMidY(rect));
    CGPoint endPoint = CGPointMake(maxX, CGRectGetMidY(rect));
    CGContextDrawLinearGradient(context, gradient, startPoint, endPoint, 0);
  }
  if (fadeHead) {
    CGFloat startX = minX + fadeWidth;
    CGPoint startPoint = CGPointMake(startX, CGRectGetMidY(rect));
    CGPoint endPoint = CGPointMake(minX, CGRectGetMidY(rect));
    CGContextDrawLinearGradient(context, gradient, startPoint, endPoint, 0);
  }
  CGGradientRelease(gradient);

  // Clean up, return image.
  CGImageRef ref = CGBitmapContextCreateImage(context);
  UIImage* image = [UIImage imageWithCGImage:ref];
  CGImageRelease(ref);
  CGContextRelease(context);
  return image;
}

@end
