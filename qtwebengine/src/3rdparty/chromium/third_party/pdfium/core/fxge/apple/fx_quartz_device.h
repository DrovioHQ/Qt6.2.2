// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FXGE_APPLE_FX_QUARTZ_DEVICE_H_
#define CORE_FXGE_APPLE_FX_QUARTZ_DEVICE_H_


#include "core/fxcrt/fx_system.h"
#include "core/fxge/cfx_gemodule.h"
#include "core/fxge/dib/fx_dib.h"

#if !defined(OS_IOS)
#include <Carbon/Carbon.h>
#else
class CGPoint;
#endif

class CFX_DIBitmap;
class CFX_Matrix;

class CQuartz2D {
 public:
  void* CreateGraphics(const RetainPtr<CFX_DIBitmap>& bitmap);
  void DestroyGraphics(void* graphics);

  void* CreateFont(const uint8_t* pFontData, uint32_t dwFontSize);
  void DestroyFont(void* pFont);
  void SetGraphicsTextMatrix(void* graphics, const CFX_Matrix& matrix);
  bool DrawGraphicsString(void* graphics,
                          void* font,
                          float fontSize,
                          uint16_t* glyphIndices,
                          CGPoint* glyphPositions,
                          int32_t chars,
                          FX_ARGB argb);
};

#endif  // CORE_FXGE_APPLE_FX_QUARTZ_DEVICE_H_
