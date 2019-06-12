//
//  GTMUIImage+ResizeTest.m
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

#import "GTMSenTestCase.h"
#import "GTMUIImage+Resize.h"

@interface GTMUIImage_ResizeTest : GTMTestCase
- (UIImage *)testImageNamed:(NSString *)imageName;
@end

@implementation GTMUIImage_ResizeTest

- (UIImage *)testImageNamed:(NSString *)imageName {
  NSBundle *myBundle = [NSBundle bundleForClass:[self class]];
  NSString *imagePath = [myBundle pathForResource:imageName ofType:@"png"];
  UIImage *result = [UIImage imageWithContentsOfFile:imagePath];
  return result;
}

- (void)testNilImage {
  UIImage *image = [[UIImage alloc] init];
  UIImage *actual = [image gtm_imageByResizingToSize:CGSizeMake(100, 100)
                                 preserveAspectRatio:YES
                                           trimToFit:NO];
  XCTAssertNil(actual, @"Invalid inputs should return nil");
}

- (void)testInvalidInput {
  UIImage *actual;
  UIImage *image
      = [UIImage imageNamed:@"GTMUIImage+Resize_100x50.png"];
  actual = [image gtm_imageByResizingToSize:CGSizeZero
                        preserveAspectRatio:YES
                                  trimToFit:NO];
  XCTAssertNil(actual, @"CGSizeZero resize should be ignored.");

  actual = [image gtm_imageByResizingToSize:CGSizeMake(0.1, 0.1)
                        preserveAspectRatio:YES
                                  trimToFit:NO];
  XCTAssertNil(actual, @"Invalid size should be ignored.");

  actual = [image gtm_imageByResizingToSize:CGSizeMake(-100, -100)
                        preserveAspectRatio:YES
                                  trimToFit:NO];
  XCTAssertNil(actual, @"Invalid size should be ignored.");
}

- (void)testImageByResizingWithoutPreservingAspectRatio {
  UIImage *actual = nil;
  // Square image.
  UIImage *originalImage = [self testImageNamed:@"GTMUIImage+Resize_100x100"];
  XCTAssertNotNil(originalImage, @"Unable to read image.");

  // Resize with same aspect ratio.
  CGSize size50x50 = CGSizeMake(50, 50);
  actual = [originalImage gtm_imageByResizingToSize:size50x50
                                preserveAspectRatio:NO
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size50x50),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size50x50),
                NSStringFromCGSize([actual size]));

  // Resize with different aspect ratio
  CGSize size60x40 = CGSizeMake(60, 40);
  actual = [originalImage gtm_imageByResizingToSize:size60x40
                                preserveAspectRatio:NO
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size60x40),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size60x40),
                NSStringFromCGSize([actual size]));

  CGSize size40x60 = CGSizeMake(40, 60);
  actual = [originalImage gtm_imageByResizingToSize:size40x60
                                preserveAspectRatio:NO
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size40x60),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size40x60),
                NSStringFromCGSize([actual size]));
}

- (void)testImageByResizingPreservingAspectRatioWithoutClip {
  UIImage *actual = nil;
  UIImage *landscapeImage = [self testImageNamed:@"GTMUIImage+Resize_100x50"];
  XCTAssertNotNil(landscapeImage, @"Unable to read image.");

  // Landscape resize to 50x50, but clipped to 50x25.
  CGSize size50x50 = CGSizeMake(50, 50);
  CGSize expected50x25 = CGSizeMake(50, 25);
  actual = [landscapeImage gtm_imageByResizingToSize:size50x50
                                 preserveAspectRatio:YES
                                           trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected50x25),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected50x25),
                NSStringFromCGSize([actual size]));

  // Landscape resize to 60x40, but clipped to 60x30.
  CGSize size60x40 = CGSizeMake(60, 40);
  CGSize expected60x30 = CGSizeMake(60, 30);

  actual = [landscapeImage gtm_imageByResizingToSize:size60x40
                                 preserveAspectRatio:YES
                                           trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected60x30),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected60x30),
                NSStringFromCGSize([actual size]));

  // Landscape resize to 40x60, but clipped to 40x20.
  CGSize expected40x20 = CGSizeMake(40, 20);
  CGSize size40x60 = CGSizeMake(40, 60);
  actual = [landscapeImage gtm_imageByResizingToSize:size40x60
                                 preserveAspectRatio:YES
                                           trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected40x20),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected40x20),
                NSStringFromCGSize([actual size]));

  // Portrait Image
  UIImage *portraitImage = [self testImageNamed:@"GTMUIImage+Resize_50x100"];

  // Portrait resize to 50x50, but clipped to 25x50.
  CGSize expected25x50 = CGSizeMake(25, 50);
  actual = [portraitImage gtm_imageByResizingToSize:size50x50
                                preserveAspectRatio:YES
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected25x50),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected25x50),
                NSStringFromCGSize([actual size]));

  // Portrait resize to 60x40, but clipped to 20x40.
  CGSize expected20x40 = CGSizeMake(20, 40);
  actual = [portraitImage gtm_imageByResizingToSize:size60x40
                                preserveAspectRatio:YES
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected20x40),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected20x40),
                NSStringFromCGSize([actual size]));

  // Portrait resize to 40x60, but clipped to 30x60.
  CGSize expected30x60 = CGSizeMake(30, 60);
  actual = [portraitImage gtm_imageByResizingToSize:size40x60
                                preserveAspectRatio:YES
                                          trimToFit:NO];
  XCTAssertTrue(CGSizeEqualToSize([actual size], expected30x60),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(expected30x60),
                NSStringFromCGSize([actual size]));
}

- (void)testImageByResizingPreservingAspectRatioWithClip {
  UIImage *actual = nil;
  UIImage *landscapeImage = [self testImageNamed:@"GTMUIImage+Resize_100x50"];
  XCTAssertNotNil(landscapeImage, @"Unable to read image.");

  // Landscape resize to 50x50
  CGSize size50x50 = CGSizeMake(50, 50);
  actual = [landscapeImage gtm_imageByResizingToSize:size50x50
                                 preserveAspectRatio:YES
                                           trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size50x50),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size50x50),
                NSStringFromCGSize([actual size]));

  // Landscape resize to 60x40
  CGSize size60x40 = CGSizeMake(60, 40);
  actual = [landscapeImage gtm_imageByResizingToSize:size60x40
                                 preserveAspectRatio:YES
                                           trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size60x40),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size60x40),
                NSStringFromCGSize([actual size]));

  // Landscape resize to 40x60
  CGSize size40x60 = CGSizeMake(40, 60);
  actual = [landscapeImage gtm_imageByResizingToSize:size40x60
                                 preserveAspectRatio:YES
                                           trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size40x60),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size40x60),
                NSStringFromCGSize([actual size]));

  // Portrait Image.
  UIImage *portraitImage = [self testImageNamed:@"GTMUIImage+Resize_50x100"];

  // Portrait resize to 50x50
  actual = [portraitImage gtm_imageByResizingToSize:size50x50
                                  preserveAspectRatio:YES
                                             trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size50x50),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size50x50),
                NSStringFromCGSize([actual size]));

  // Portrait resize to 60x40
  actual = [portraitImage gtm_imageByResizingToSize:size60x40
                                preserveAspectRatio:YES
                                          trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size60x40),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size60x40),
                NSStringFromCGSize([actual size]));

  // Portrait resize to 40x60.
  actual = [portraitImage gtm_imageByResizingToSize:size40x60
                                preserveAspectRatio:YES
                                          trimToFit:YES];
  XCTAssertTrue(CGSizeEqualToSize([actual size], size40x60),
                @"Resized image should equal size: %@ actual: %@",
                NSStringFromCGSize(size40x60),
                NSStringFromCGSize([actual size]));
}

- (void)testImageByRotating {
  UIImage *actual = nil;
  UIImage *landscapeImage = [self testImageNamed:@"GTMUIImage+Resize_100x50"];
  XCTAssertNotNil(landscapeImage, @"Unable to read image.");

  // Rotate 90 degrees.
  actual = [landscapeImage gtm_imageByRotating:UIImageOrientationRight];

  // Rotate 180 degrees.
  actual = [landscapeImage gtm_imageByRotating:UIImageOrientationDown];

  // Rotate 270 degrees.
  actual = [landscapeImage gtm_imageByRotating:UIImageOrientationLeft];

  // Rotate 360 degrees.
  actual = [landscapeImage gtm_imageByRotating:UIImageOrientationUp];
}

@end
