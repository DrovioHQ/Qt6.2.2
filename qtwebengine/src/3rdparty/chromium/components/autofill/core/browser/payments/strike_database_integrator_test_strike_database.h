// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_STRIKE_DATABASE_INTEGRATOR_TEST_STRIKE_DATABASE_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_STRIKE_DATABASE_INTEGRATOR_TEST_STRIKE_DATABASE_H_

#include <stdint.h>
#include <string>

#include "components/autofill/core/browser/payments/strike_database.h"
#include "components/autofill/core/browser/payments/strike_database_integrator_base.h"

namespace autofill {

// Mock per-project implementation of StrikeDatabase to test the functions in
// StrikeDatabaseIntegrator.
class StrikeDatabaseIntegratorTestStrikeDatabase
    : public StrikeDatabaseIntegratorBase {
 public:
  StrikeDatabaseIntegratorTestStrikeDatabase(
      StrikeDatabase* strike_database,
      base::Optional<int64_t> expiry_time_micros);
  explicit StrikeDatabaseIntegratorTestStrikeDatabase(
      StrikeDatabase* strike_database);
  ~StrikeDatabaseIntegratorTestStrikeDatabase() override;

  std::string GetProjectPrefix() override;
  int GetMaxStrikesLimit() override;
  base::Optional<int64_t> GetExpiryTimeMicros() override;
  bool UniqueIdsRequired() override;

  void SetUniqueIdsRequired(bool unique_ids_required);

 private:
  bool unique_ids_required_ = false;
  base::Optional<int64_t> expiry_time_micros_ =
      static_cast<int64_t>(1000000) * 60 * 60 * 24 *
      365;  // Default expiry time is 1 year.
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_STRIKE_DATABASE_INTEGRATOR_TEST_STRIKE_DATABASE_H_
