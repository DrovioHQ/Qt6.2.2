// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the enterprise-related command-line switches used on several
// platforms.

#ifndef COMPONENTS_ENTERPRISE_BROWSER_ENTERPRISE_SWITCHES_H_
#define COMPONENTS_ENTERPRISE_BROWSER_ENTERPRISE_SWITCHES_H_

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace switches {

#if !BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OS_ANDROID)
extern const char kEnableChromeBrowserCloudManagement[];
#endif

}  // namespace switches

#endif  // COMPONENTS_ENTERPRISE_BROWSER_ENTERPRISE_SWITCHES_H_
