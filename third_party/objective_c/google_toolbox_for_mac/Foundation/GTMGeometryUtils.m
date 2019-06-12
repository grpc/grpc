//
//  GTMGeometryUtils.m
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

#import "GTMGeometryUtils.h"

/// Align rectangles
//
//  Args:
//    alignee - rect to be aligned
//    aligner - rect to be aligned to
//    alignment - alignment to be applied to alignee based on aligner

CGRect GTMCGAlignRectangles(CGRect alignee, CGRect aligner, GTMRectAlignment alignment) {
  switch (alignment) {
    case GTMRectAlignTop:
      alignee.origin.x = aligner.origin.x + (CGRectGetWidth(aligner) * .5f - CGRectGetWidth(alignee) * .5f);
      alignee.origin.y = aligner.origin.y + CGRectGetHeight(aligner) - CGRectGetHeight(alignee);
      break;

    case GTMRectAlignTopLeft:
      alignee.origin.x = aligner.origin.x;
      alignee.origin.y = aligner.origin.y + CGRectGetHeight(aligner) - CGRectGetHeight(alignee);
    break;

    case GTMRectAlignTopRight:
      alignee.origin.x = aligner.origin.x + CGRectGetWidth(aligner) - CGRectGetWidth(alignee);
      alignee.origin.y = aligner.origin.y + CGRectGetHeight(aligner) - CGRectGetHeight(alignee);
      break;

    case GTMRectAlignLeft:
      alignee.origin.x = aligner.origin.x;
      alignee.origin.y = aligner.origin.y + (CGRectGetHeight(aligner) * .5f - CGRectGetHeight(alignee) * .5f);
      break;

    case GTMRectAlignBottomLeft:
      alignee.origin.x = aligner.origin.x;
      alignee.origin.y = aligner.origin.y;
      break;

    case GTMRectAlignBottom:
      alignee.origin.x = aligner.origin.x + (CGRectGetWidth(aligner) * .5f - CGRectGetWidth(alignee) * .5f);
      alignee.origin.y = aligner.origin.y;
      break;

    case GTMRectAlignBottomRight:
      alignee.origin.x = aligner.origin.x + CGRectGetWidth(aligner) - CGRectGetWidth(alignee);
      alignee.origin.y = aligner.origin.y;
      break;

    case GTMRectAlignRight:
      alignee.origin.x = aligner.origin.x + CGRectGetWidth(aligner) - CGRectGetWidth(alignee);
      alignee.origin.y = aligner.origin.y + (CGRectGetHeight(aligner) * .5f - CGRectGetHeight(alignee) * .5f);
      break;

    default:
    case GTMRectAlignCenter:
      alignee.origin.x = aligner.origin.x + (CGRectGetWidth(aligner) * .5f - CGRectGetWidth(alignee) * .5f);
      alignee.origin.y = aligner.origin.y + (CGRectGetHeight(aligner) * .5f - CGRectGetHeight(alignee) * .5f);
      break;
  }
  return alignee;
}

CGRect GTMCGScaleRectangleToSize(CGRect scalee, CGSize size, GTMScaling scaling) {
  switch (scaling) {

    case GTMScaleToFillProportionally:
    case GTMScaleProportionally: {
      CGFloat height = CGRectGetHeight(scalee);
      CGFloat width = CGRectGetWidth(scalee);
      if (isnormal(height) && isnormal(width) &&
          (height > size.height || width > size.width)) {
        CGFloat horiz = size.width / width;
        CGFloat vert = size.height / height;
        BOOL expand = (scaling == GTMScaleToFillProportionally);
        // We use the smaller scale unless expand is true. In that case, larger.
        CGFloat newScale = ((horiz < vert) ^ expand) ? horiz : vert;
        scalee = GTMCGRectScale(scalee, newScale, newScale);
      }
      break;
    }

    case GTMScaleToFit:
      scalee.size = size;
      break;

    case GTMScaleNone:
    default:
      // Do nothing
      break;
  }
  return scalee;
}
