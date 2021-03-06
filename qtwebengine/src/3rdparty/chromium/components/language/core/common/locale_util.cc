// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/language/core/common/locale_util.h"

#include <stddef.h>

#include <algorithm>

#include "build/build_config.h"
#include "ui/base/l10n/l10n_util.h"

namespace language {

namespace {

#if defined(OS_IOS)
// iOS uses a different name for es-419 (es-MX).
const char* const kEs419 = "es-MX";
#else
const char* const kEs419 = "es-419";
#endif

// Pair of locales, where the first element should fallback to the second one.
struct LocaleUIFallbackPair {
  const char* const chosen_locale;
  const char* const fallback_locale;
};

// This list MUST be sorted by the first element in the pair, because we perform
// binary search on it.
// Note that no (Norwegian) is an alias, and should fallback to Norwegian
// Bokmål (nb)
const LocaleUIFallbackPair kLocaleUIFallbackTable[] = {
    // clang-format off
    {"en", "en-US"},
    {"en-AU", "en-GB"},
    {"en-CA", "en-GB"},
    {"en-GB-oxendict", "en-GB"},
    {"en-IN", "en-GB"},
    {"en-NZ", "en-GB"},
    {"en-ZA", "en-GB"},
    {"es-AR", kEs419},
    {"es-CL", kEs419},
    {"es-CO", kEs419},
    {"es-CR", kEs419},
    {"es-HN", kEs419},
    {"es-MX", kEs419},
    {"es-PE", kEs419},
    {"es-US", kEs419},
    {"es-UY", kEs419},
    {"es-VE", kEs419},
    {"it-CH", "it"},
    {"no", "nb"},
    {"pt", "pt-PT"}
    // clang-format on
};

base::StringPiece GetUIFallbackLocale(base::StringPiece input) {
  const auto* it = std::lower_bound(
      std::begin(kLocaleUIFallbackTable), std::end(kLocaleUIFallbackTable),
      input, [](const LocaleUIFallbackPair& p1, base::StringPiece p2) {
        return p1.chosen_locale < p2;
      });
  if (it != std::end(kLocaleUIFallbackTable) && it->chosen_locale == input)
    return it->fallback_locale;
  return input;
}

// Checks if |locale| is one of the actual locales supported as a UI languages.
bool IsAvailableUILocale(base::StringPiece locale) {
  for (const auto& ui_locale : l10n_util::GetLocalesWithStrings()) {
    if (ui_locale == locale)
      return true;
  }
  return false;
}

bool ConvertToFallbackUILocale(std::string* input_locale) {
  // 1) Convert input to a fallback, if available.
  base::StringPiece fallback = GetUIFallbackLocale(*input_locale);

  // 2) Check if input is part of the UI languages.
  if (IsAvailableUILocale(fallback)) {
    *input_locale = std::string(fallback);
    return true;
  }

  return false;
}

}  // namespace

std::pair<base::StringPiece, base::StringPiece> SplitIntoMainAndTail(
    base::StringPiece locale) {
  size_t hyphen_pos = static_cast<size_t>(
      std::find(locale.begin(), locale.end(), '-') - locale.begin());
  return std::make_pair(locale.substr(0U, hyphen_pos),
                        locale.substr(hyphen_pos));
}

base::StringPiece ExtractBaseLanguage(base::StringPiece language_code) {
  return SplitIntoMainAndTail(language_code).first;
}

// TODO(mlcui): Replace this with l10n_util::CheckAndResolveLocale.
// Note that CheckAndResolveLocale has different behaviour for "pt".
bool ConvertToActualUILocale(std::string* input_locale) {
  if (ConvertToFallbackUILocale(input_locale))
    return true;

  // Check if the base language of the input is part of the UI languages.
  base::StringPiece base = ExtractBaseLanguage(*input_locale);
  if (base != *input_locale && IsAvailableUILocale(base)) {
    *input_locale = std::string(base);
    return true;
  }

  return false;
}

}  // namespace language
