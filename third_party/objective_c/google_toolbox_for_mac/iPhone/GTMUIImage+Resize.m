//
//  GTMUIImage+Resize.m
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

#import "GTMUIImage+Resize.h"
#import "GTMDefines.h"

GTM_INLINE CGSize swapWidthAndHeight(CGSize size) {
  CGFloat  tempWidth = size.width;

  size.width  = size.height;
  size.height = tempWidth;

  return size;
}

@implementation UIImage (GTMUIImageResizeAdditions)

- (UIImage *)gtm_imageByResizingToSize:(CGSize)targetSize
                   preserveAspectRatio:(BOOL)preserveAspectRatio
                             trimToFit:(BOOL)trimToFit {
  CGSize imageSize = [self size];
  if (imageSize.height < 1 || imageSize.width < 1) {
    return nil;
  }
  if (targetSize.height < 1 || targetSize.width < 1) {
    return nil;
  }
  CGFloat aspectRatio = imageSize.width / imageSize.height;
  CGFloat targetAspectRatio = targetSize.width / targetSize.height;
  CGRect projectTo = CGRectZero;
  if (preserveAspectRatio) {
    if (trimToFit) {
      // Scale and clip image so that the aspect ratio is preserved and the
      // target size is filled.
      if (targetAspectRatio < aspectRatio) {
        // clip the x-axis.
        projectTo.size.width = targetSize.height * aspectRatio;
        projectTo.size.height = targetSize.height;
        projectTo.origin.x = (targetSize.width - projectTo.size.width) / 2;
        projectTo.origin.y = 0;
      } else {
        // clip the y-axis.
        projectTo.size.width = targetSize.width;
        projectTo.size.height = targetSize.width / aspectRatio;
        projectTo.origin.x = 0;
        projectTo.origin.y = (targetSize.height - projectTo.size.height) / 2;
      }
    } else {
      // Scale image to ensure it fits inside the specified targetSize.
      if (targetAspectRatio < aspectRatio) {
        // target is less wide than the original.
        projectTo.size.width = targetSize.width;
        projectTo.size.height = projectTo.size.width / aspectRatio;
        targetSize = projectTo.size;
      } else {
        // target is wider than the original.
        projectTo.size.height = targetSize.height;
        projectTo.size.width = projectTo.size.height * aspectRatio;
        targetSize = projectTo.size;
      }
    } // if (clip)
  } else {
    // Don't preserve the aspect ratio.
    projectTo.size = targetSize;
  }

  projectTo = CGRectIntegral(projectTo);
  // There's no CGSizeIntegral, so we fake our own.
  CGRect integralRect = CGRectZero;
  integralRect.size = targetSize;
  targetSize = CGRectIntegral(integralRect).size;

  // Resize photo. Use UIImage drawing methods because they respect
  // UIImageOrientation as opposed to CGContextDrawImage().
  UIGraphicsBeginImageContext(targetSize);
  [self drawInRect:projectTo];
  UIImage* resizedPhoto = UIGraphicsGetImageFromCurrentImageContext();
  UIGraphicsEndImageContext();
  return resizedPhoto;
}

// Based on code by Trevor Harmon:
// http://vocaro.com/trevor/blog/wp-content/uploads/2009/10/UIImage+Resize.h
// http://vocaro.com/trevor/blog/wp-content/uploads/2009/10/UIImage+Resize.m
- (UIImage *)gtm_imageByRotating:(UIImageOrientation)orientation {
  CGRect bounds = CGRectZero;
  CGRect rect = CGRectZero;
  CGAffineTransform transform = CGAffineTransformIdentity;

  bounds.size = [self size];
  rect.size = [self size];

  switch (orientation) {
    case UIImageOrientationUp:
      return [UIImage imageWithCGImage:[self CGImage]];

    case UIImageOrientationUpMirrored:
      transform = CGAffineTransformMakeTranslation(rect.size.width, 0.0);
      transform = CGAffineTransformScale(transform, -1.0, 1.0);
      break;

    case UIImageOrientationDown:
      transform = CGAffineTransformMakeTranslation(rect.size.width,
                                                   rect.size.height);
      transform = CGAffineTransformRotate(transform, M_PI);
      break;

    case UIImageOrientationDownMirrored:
      transform = CGAffineTransformMakeTranslation(0.0, rect.size.height);
      transform = CGAffineTransformScale(transform, 1.0, -1.0);
      break;

    case UIImageOrientationLeft:
      bounds.size = swapWidthAndHeight(bounds.size);
      transform = CGAffineTransformMakeTranslation(0.0, rect.size.width);
      transform = CGAffineTransformRotate(transform, -M_PI_2);
      break;

    case UIImageOrientationLeftMirrored:
      bounds.size = swapWidthAndHeight(bounds.size);
      transform = CGAffineTransformMakeTranslation(rect.size.height,
                                                   rect.size.width);
      transform = CGAffineTransformScale(transform, -1.0, 1.0);
      transform = CGAffineTransformRotate(transform, -M_PI_2);
      break;

    case UIImageOrientationRight:
      bounds.size = swapWidthAndHeight(bounds.size);
      transform = CGAffineTransformMakeTranslation(rect.size.height, 0.0);
      transform = CGAffineTransformRotate(transform, M_PI_2);
      break;

    case UIImageOrientationRightMirrored:
      bounds.size = swapWidthAndHeight(bounds.size);
      transform = CGAffineTransformMakeScale(-1.0, 1.0);
      transform = CGAffineTransformRotate(transform, M_PI_2);
      break;

    default:
      _GTMDevAssert(false, @"Invalid orientation %ld", (long)orientation);
      return nil;
  }

  UIGraphicsBeginImageContextWithOptions(bounds.size, NO, self.scale);
  CGContextRef context = UIGraphicsGetCurrentContext();

  switch (orientation) {
    case UIImageOrientationLeft:
    case UIImageOrientationLeftMirrored:
    case UIImageOrientationRight:
    case UIImageOrientationRightMirrored:
      CGContextScaleCTM(context, -1.0, 1.0);
      CGContextTranslateCTM(context, -rect.size.height, 0.0);
      break;

    default:
      CGContextScaleCTM(context, 1.0, -1.0);
      CGContextTranslateCTM(context, 0.0, -rect.size.height);
      break;
  }

  CGContextConcatCTM(context, transform);
  CGContextDrawImage(context, rect, [self CGImage]);

  UIImage *rotatedImage = UIGraphicsGetImageFromCurrentImageContext();
  UIGraphicsEndImageContext();

  return rotatedImage;
}

@end
