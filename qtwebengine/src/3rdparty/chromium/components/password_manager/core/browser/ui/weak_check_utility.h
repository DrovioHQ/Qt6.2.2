// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_WEAK_CHECK_UTILITY_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_WEAK_CHECK_UTILITY_H_

#include "base/containers/flat_set.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece_forward.h"
#include "base/types/strong_alias.h"

namespace password_manager {

using IsWeakPassword = base::StrongAlias<class IsWeakPasswordTag, bool>;

// Returns whether `password` is weak.
IsWeakPassword IsWeak(base::StringPiece16 password);

// Checks each password for weakness and removes strong passwords from the
// |passwords|.
base::flat_set<base::string16> BulkWeakCheck(
    base::flat_set<base::string16> passwords);

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_UI_WEAK_CHECK_UTILITY_H_
