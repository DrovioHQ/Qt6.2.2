// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is included from autogenerated files based on
// chrome/common/extensions/api/enterprise_device_attributes.idl.

#ifndef CHROME_BROWSER_EXTENSIONS_API_ENTERPRISE_DEVICE_ATTRIBUTES_ENTERPRISE_DEVICE_ATTRIBUTES_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_ENTERPRISE_DEVICE_ATTRIBUTES_ENTERPRISE_DEVICE_ATTRIBUTES_API_H_

#include "build/chromeos_buildflags.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/extensions/api/enterprise_device_attributes/enterprise_device_attributes_api_lacros.h"
#else
#include "chrome/browser/extensions/api/enterprise_device_attributes/enterprise_device_attributes_api_ash.h"
#endif

#endif  // CHROME_BROWSER_EXTENSIONS_API_ENTERPRISE_DEVICE_ATTRIBUTES_ENTERPRISE_DEVICE_ATTRIBUTES_API_H_
