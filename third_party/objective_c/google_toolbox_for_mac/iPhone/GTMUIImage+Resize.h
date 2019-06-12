//
//  GTMUIImage+Resize.h
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

#import <UIKit/UIKit.h>

@interface UIImage (GTMUIImageResizeAdditions)

// Returns an image resized to |targetSize|.
//
// If |preserveAspectRatio| is YES, the original image aspect ratio is
// preserved.
//
// When |preserveAspectRatio| is YES and if |targetSize|'s aspect ratio
// is different from the image, the resulting image will be shrunken to
// a size that is within |targetSize|.
//
// To preserve the |targetSize| when |preserveAspectRatio| is YES, set
// |trimToFit| to YES. The resulting image will be the largest proportion
// of the receiver that fits in the targetSize, aligned to center of the image.
//
// Image interpolation level for resizing is set to kCGInterpolationDefault.
- (UIImage *)gtm_imageByResizingToSize:(CGSize)targetSize
                   preserveAspectRatio:(BOOL)preserveAspectRatio
                             trimToFit:(BOOL)trimToFit;

// Returns an image rotated by |orientation| where the current orientation is
// taken as UIImageOrientationUp. Nil if |orientation| is invalid.
//
// For example, UIImageOrientationRight is a 90 degree rotation clockwise,
// UIImageOrientationDown is a 180 degree rotation closewise.
//
// Supplying UIImageOrientationUp to |orientation| will return a copy of the
// image.
- (UIImage *)gtm_imageByRotating:(UIImageOrientation)orientation;

@end
