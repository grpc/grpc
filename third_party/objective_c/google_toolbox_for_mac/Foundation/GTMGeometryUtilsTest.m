//
//  GTMGeometryUtilsTest.m
//
//  Copyright 2006-2008 Google Inc.
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
#import "GTMGeometryUtils.h"

@interface GTMGeometryUtilsTest : GTMTestCase
@end

@implementation GTMGeometryUtilsTest

#if !GTM_IPHONE_SDK
- (void)testGTMNSRectToCGRect {
  NSRect nsRect = NSMakeRect(4.6,3.2,22.1,45.0);
  CGRect cgRect = GTMNSRectToCGRect(nsRect);
  XCTAssertTrue(CGRectEqualToRect(cgRect, *(CGRect*)&nsRect));
}

- (void)testGTMNSSizeToCGSize {
  NSSize nsSize = {22,15};
  CGSize cgSize = GTMNSSizeToCGSize(nsSize);
  XCTAssertTrue(CGSizeEqualToSize(cgSize, *(CGSize*)&nsSize));
}

- (void)testGTMNSPointsOnRect {
  NSRect rect = NSMakeRect(0, 0, 2, 2);

  NSPoint point = GTMNSMidMinX(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 0.0, 0.01);

  point = GTMNSMidMaxX(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 2.0, 0.01);

  point = GTMNSMidMaxY(rect);
  XCTAssertEqualWithAccuracy(point.y, 2.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);

  point = GTMNSMidMinY(rect);
  XCTAssertEqualWithAccuracy(point.y, 0.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);

  point = GTMNSCenter(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);
}

- (void)testGTMNSRectSize {
  NSSize nsSize = GTMNSRectSize(NSMakeRect(1, 1, 10, 5));
  XCTAssertEqualWithAccuracy(nsSize.width, 10.0, 0.01);
  XCTAssertEqualWithAccuracy(nsSize.height, 5.0, 0.01);
}

- (void)testGTMNSRectOfSize {
  NSRect outRect = GTMNSRectOfSize(NSMakeSize(10, 5));
  NSRect expectedRect = NSMakeRect(0, 0, 10, 5);
  XCTAssertTrue(NSEqualRects(outRect, expectedRect));
}

- (void)testGTMNSAlignRectangles {
  typedef struct  {
    NSPoint expectedOrigin;
    GTMRectAlignment alignment;
  } TestData;

  TestData data[] = {
    { {1,2}, GTMRectAlignTop },
    { {0,2}, GTMRectAlignTopLeft },
    { {2,2}, GTMRectAlignTopRight },
    { {0,1}, GTMRectAlignLeft },
    { {1,0}, GTMRectAlignBottom },
    { {0,0}, GTMRectAlignBottomLeft },
    { {2,0}, GTMRectAlignBottomRight },
    { {2,1}, GTMRectAlignRight },
    { {1,1}, GTMRectAlignCenter },
  };

  NSRect rect1 = NSMakeRect(0, 0, 4, 4);
  NSRect rect2 = NSMakeRect(0, 0, 2, 2);

  NSRect expectedRect;
  expectedRect.size = NSMakeSize(2, 2);

  for (size_t i = 0; i < sizeof(data) / sizeof(TestData); i++) {
    expectedRect.origin = data[i].expectedOrigin;
    NSRect outRect = GTMNSAlignRectangles(rect2, rect1, data[i].alignment);
    XCTAssertTrue(NSEqualRects(outRect, expectedRect));
  }
}

- (void)testGTMNSScaleRectangleToSize {
  NSRect rect = NSMakeRect(0.0f, 0.0f, 10.0f, 10.0f);
  typedef struct {
    NSSize size_;
    NSSize newSize_;
  } Test;
  Test tests[] = {
    { { 5.0, 10.0 }, { 5.0, 5.0 } },
    { { 10.0, 5.0 }, { 5.0, 5.0 } },
    { { 10.0, 10.0 }, { 10.0, 10.0 } },
    { { 11.0, 11.0, }, { 10.0, 10.0 } },
    { { 5.0, 2.0 }, { 2.0, 2.0 } },
    { { 2.0, 5.0 }, { 2.0, 2.0 } },
    { { 2.0, 2.0 }, { 2.0, 2.0 } },
    { { 0.0, 10.0 }, { 0.0, 0.0 } }
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(Test); ++i) {
    NSRect result = GTMNSScaleRectangleToSize(rect, tests[i].size_,
                                              GTMScaleProportionally);
    XCTAssertTrue(NSEqualRects(result, GTMNSRectOfSize(tests[i].newSize_)),
                  @"failed on test %zu", i);
  }

  NSRect result = GTMNSScaleRectangleToSize(NSZeroRect, tests[0].size_,
                                            GTMScaleProportionally);
  XCTAssertTrue(NSEqualRects(result, NSZeroRect));

  result = GTMNSScaleRectangleToSize(rect, tests[0].size_,
                                     GTMScaleToFit);
  XCTAssertTrue(NSEqualRects(result, GTMNSRectOfSize(tests[0].size_)));

  result = GTMNSScaleRectangleToSize(rect, tests[0].size_,
                                     GTMScaleNone);
  XCTAssertTrue(NSEqualRects(result, rect));
}


- (void)testGTMNSScaleRectToRect {
  typedef struct  {
    NSRect expectedRect;
    GTMScaling scaling;
    GTMRectAlignment alignment;
  } TestData;

  NSRect rect1 = NSMakeRect(0, 0, 4, 4);
  NSRect rect2 = NSMakeRect(0, 0, 2, 1);

  TestData data[] = {
    { NSMakeRect(2, 3, 2, 1), GTMScaleToFillProportionally, GTMRectAlignTopRight },
    { NSMakeRect(0, 0, 4, 4), GTMScaleToFit, GTMRectAlignCenter },
    { NSMakeRect(1, 1.5, 2, 1), GTMScaleNone, GTMRectAlignCenter },
    { NSMakeRect(1, 0, 2, 1), GTMScaleProportionally, GTMRectAlignBottom },
  };

  for (size_t i = 0; i < sizeof(data) / sizeof(TestData); i++) {
    NSRect outRect = GTMNSScaleRectToRect(rect2, rect1, data[i].scaling, data[i].alignment);
    XCTAssertTrue(NSEqualRects(outRect, data[i].expectedRect));
  }
}


- (void)testGTMNSDistanceBetweenPoints {
  NSPoint pt1 = NSMakePoint(0, 0);
  NSPoint pt2 = NSMakePoint(3, 4);
  XCTAssertEqualWithAccuracy(GTMNSDistanceBetweenPoints(pt1, pt2), 5.0, 0.01);
  XCTAssertEqualWithAccuracy(GTMNSDistanceBetweenPoints(pt2, pt1), 5.0, 0.01);
  pt1 = NSMakePoint(1, 1);
  pt2 = NSMakePoint(1, 1);
  XCTAssertEqualWithAccuracy(GTMNSDistanceBetweenPoints(pt1, pt2), 0.0, 0.01);
}

- (void)testGTMNSRectScaling {
  NSRect rect = NSMakeRect(1.0f, 2.0f, 5.0f, 10.0f);
  NSRect rect2 = NSMakeRect(1.0, 2.0, 1.0, 12.0);
  XCTAssertTrue(NSEqualRects(GTMNSRectScale(rect, 0.2, 1.2), rect2));
}

#endif //  !GTM_IPHONE_SDK

- (void)testGTMCGDistanceBetweenPoints {
  CGPoint pt1 = CGPointMake(0, 0);
  CGPoint pt2 = CGPointMake(3, 4);
  XCTAssertEqualWithAccuracy(GTMCGDistanceBetweenPoints(pt1, pt2), 5.0, 0.01);
  XCTAssertEqualWithAccuracy(GTMCGDistanceBetweenPoints(pt2, pt1), 5.0, 0.01);
  pt1 = CGPointMake(1, 1);
  pt2 = CGPointMake(1, 1);
  XCTAssertEqualWithAccuracy(GTMCGDistanceBetweenPoints(pt1, pt2), 0.0, 0.01);
}

- (void)testGTMCGAlignRectangles {
  typedef struct  {
    CGPoint expectedOrigin;
    GTMRectAlignment alignment;
  } TestData;

  TestData data[] = {
    { {1,2}, GTMRectAlignTop },
    { {0,2}, GTMRectAlignTopLeft },
    { {2,2}, GTMRectAlignTopRight },
    { {0,1}, GTMRectAlignLeft },
    { {1,0}, GTMRectAlignBottom },
    { {0,0}, GTMRectAlignBottomLeft },
    { {2,0}, GTMRectAlignBottomRight },
    { {2,1}, GTMRectAlignRight },
    { {1,1}, GTMRectAlignCenter },
  };

  CGRect rect1 = CGRectMake(0, 0, 4, 4);
  CGRect rect2 = CGRectMake(0, 0, 2, 2);

  CGRect expectedRect;
  expectedRect.size = CGSizeMake(2, 2);

  for (size_t i = 0; i < sizeof(data) / sizeof(TestData); i++) {
    expectedRect.origin = data[i].expectedOrigin;
    CGRect outRect = GTMCGAlignRectangles(rect2, rect1, data[i].alignment);
    XCTAssertTrue(CGRectEqualToRect(outRect, expectedRect));
  }
}

- (void)testGTMCGPointsOnRect {
  CGRect rect = CGRectMake(0, 0, 2, 2);

  CGPoint point = GTMCGMidMinX(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 0.0, 0.01);

  point = GTMCGMidMaxX(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 2.0, 0.01);

  point = GTMCGMidMaxY(rect);
  XCTAssertEqualWithAccuracy(point.y, 2.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);

  point = GTMCGMidMinY(rect);
  XCTAssertEqualWithAccuracy(point.y, 0.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);

  point = GTMCGCenter(rect);
  XCTAssertEqualWithAccuracy(point.y, 1.0, 0.01);
  XCTAssertEqualWithAccuracy(point.x, 1.0, 0.01);
}

- (void)testGTMCGRectSize {
  CGSize cgSize = GTMCGRectSize(CGRectMake(1, 1, 10, 5));
  XCTAssertEqualWithAccuracy(cgSize.width, 10.0, 0.01);
  XCTAssertEqualWithAccuracy(cgSize.height, 5.0, 0.01);
}

- (void)testGTMCGRectOfSize {
  CGRect outRect = GTMCGRectOfSize(CGSizeMake(10, 5));
  CGRect expectedRect = CGRectMake(0, 0, 10, 5);
  XCTAssertTrue(CGRectEqualToRect(outRect, expectedRect));
}

- (void)testGTMCGRectScaling {
  CGRect rect = CGRectMake(1.0f, 2.0f, 5.0f, 10.0f);
  CGRect rect2 = CGRectMake(1.0, 2.0, 1.0, 12.0);
  XCTAssertTrue(CGRectEqualToRect(GTMCGRectScale(rect, 0.2, 1.2), rect2));
}

- (void)testGTMCGScaleRectangleToSize {
  CGRect rect = CGRectMake(0.0f, 0.0f, 10.0f, 10.0f);
  typedef struct {
    CGSize size_;
    CGSize newSize_;
  } Test;
  Test tests[] = {
    { { 5.0, 10.0 }, { 5.0, 5.0 } },
    { { 10.0, 5.0 }, { 5.0, 5.0 } },
    { { 10.0, 10.0 }, { 10.0, 10.0 } },
    { { 11.0, 11.0, }, { 10.0, 10.0 } },
    { { 5.0, 2.0 }, { 2.0, 2.0 } },
    { { 2.0, 5.0 }, { 2.0, 2.0 } },
    { { 2.0, 2.0 }, { 2.0, 2.0 } },
    { { 0.0, 10.0 }, { 0.0, 0.0 } }
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(Test); ++i) {
    CGRect result = GTMCGScaleRectangleToSize(rect, tests[i].size_,
                                              GTMScaleProportionally);
    XCTAssertTrue(CGRectEqualToRect(result, GTMCGRectOfSize(tests[i].newSize_)),
                  @"failed on test %zu", i);
  }

  CGRect result = GTMCGScaleRectangleToSize(CGRectZero, tests[0].size_,
                                            GTMScaleProportionally);
  XCTAssertTrue(CGRectEqualToRect(result, CGRectZero));

  result = GTMCGScaleRectangleToSize(rect, tests[0].size_,
                                     GTMScaleToFit);
  XCTAssertTrue(CGRectEqualToRect(result, GTMCGRectOfSize(tests[0].size_)));

  result = GTMCGScaleRectangleToSize(rect, tests[0].size_,
                                     GTMScaleNone);
  XCTAssertTrue(CGRectEqualToRect(result, rect));
}

@end
