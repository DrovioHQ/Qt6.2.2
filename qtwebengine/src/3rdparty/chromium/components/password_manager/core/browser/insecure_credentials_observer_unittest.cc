// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/insecure_credentials_observer.h"

#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "components/password_manager/core/browser/password_store_change.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace password_manager {
namespace {

constexpr char kHistogramName[] =
    "PasswordManager.RemoveCompromisedCredentials";
constexpr char kSite[] = "https://example.com/path";
constexpr char kUsername[] = "peter";
constexpr char kUsernameNew[] = "ana";

PasswordForm TestForm(base::StringPiece username) {
  PasswordForm form;
  form.url = GURL(kSite);
  form.signon_realm = form.url.GetOrigin().spec();
  form.username_value = base::ASCIIToUTF16(username);
  form.password_value = base::ASCIIToUTF16("12345");
  return form;
}

class InsecureCredentialsObserverTest : public testing::Test {
 public:
  base::HistogramTester& histogram_tester() { return histogram_tester_; }
  base::MockCallback<RemoveInsecureCallback>& remove_callback() {
    return remove_callback_;
  }

 private:
  base::HistogramTester histogram_tester_;
  base::MockCallback<RemoveInsecureCallback> remove_callback_;
};

TEST_F(InsecureCredentialsObserverTest, DeletePassword) {
  const PasswordForm form = TestForm(kUsername);
  EXPECT_CALL(remove_callback(), Run(form.signon_realm, form.username_value,
                                     RemoveInsecureCredentialsReason::kRemove));
  ProcessLoginsChanged({PasswordStoreChange(PasswordStoreChange::REMOVE, form)},
                       remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::REMOVE, 1);
}

TEST_F(InsecureCredentialsObserverTest, UpdateFormNoPasswordChange) {
  const PasswordForm form = TestForm(kUsername);
  EXPECT_CALL(remove_callback(), Run).Times(0);
  ProcessLoginsChanged({PasswordStoreChange(PasswordStoreChange::UPDATE, form,
                                            FormPrimaryKey(1000), false)},
                       remove_callback().Get());
  histogram_tester().ExpectTotalCount(kHistogramName, 0);
}

TEST_F(InsecureCredentialsObserverTest, UpdatePassword) {
  const PasswordForm form = TestForm(kUsername);
  EXPECT_CALL(remove_callback(), Run(form.signon_realm, form.username_value,
                                     RemoveInsecureCredentialsReason::kUpdate));
  ProcessLoginsChanged({PasswordStoreChange(PasswordStoreChange::UPDATE, form,
                                            FormPrimaryKey(1000), true)},
                       remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::UPDATE, 1);
}

TEST_F(InsecureCredentialsObserverTest, UpdateTwice) {
  const PasswordForm form = TestForm(kUsername);
  EXPECT_CALL(remove_callback(), Run(form.signon_realm, form.username_value,
                                     RemoveInsecureCredentialsReason::kUpdate));
  ProcessLoginsChanged(
      {PasswordStoreChange(PasswordStoreChange::UPDATE, TestForm(kUsernameNew),
                           FormPrimaryKey(1000), false),
       PasswordStoreChange(PasswordStoreChange::UPDATE, form,
                           FormPrimaryKey(1001), true)},
      remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::UPDATE, 1);
}

TEST_F(InsecureCredentialsObserverTest, AddPassword) {
  const PasswordForm form = TestForm(kUsername);
  EXPECT_CALL(remove_callback(), Run).Times(0);
  ProcessLoginsChanged({PasswordStoreChange(PasswordStoreChange::ADD, form)},
                       remove_callback().Get());
  histogram_tester().ExpectTotalCount(kHistogramName, 0);
}

TEST_F(InsecureCredentialsObserverTest, AddReplacePassword) {
  PasswordForm form = TestForm(kUsername);
  PasswordStoreChange remove(PasswordStoreChange::REMOVE, form);
  form.password_value = base::ASCIIToUTF16("new_password_12345");
  PasswordStoreChange add(PasswordStoreChange::ADD, form);
  EXPECT_CALL(remove_callback(), Run(form.signon_realm, form.username_value,
                                     RemoveInsecureCredentialsReason::kUpdate));
  ProcessLoginsChanged({remove, add}, remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::UPDATE, 1);
}

TEST_F(InsecureCredentialsObserverTest, UpdateWithPrimaryKey) {
  const PasswordForm old_form = TestForm(kUsername);
  PasswordStoreChange remove(PasswordStoreChange::REMOVE, old_form);
  PasswordStoreChange add(PasswordStoreChange::ADD, TestForm(kUsernameNew));
  EXPECT_CALL(remove_callback(),
              Run(old_form.signon_realm, old_form.username_value,
                  RemoveInsecureCredentialsReason::kUpdate));
  ProcessLoginsChanged({remove, add}, remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::UPDATE, 1);
}

TEST_F(InsecureCredentialsObserverTest, UpdateWithPrimaryKey_RemoveTwice) {
  const PasswordForm old_form = TestForm(kUsername);
  PasswordStoreChange remove_old(PasswordStoreChange::REMOVE, old_form);
  const PasswordForm conflicting_new_form = TestForm(kUsernameNew);
  PasswordStoreChange remove_conflicting(PasswordStoreChange::REMOVE,
                                         conflicting_new_form);
  PasswordStoreChange add(PasswordStoreChange::ADD, TestForm(kUsernameNew));
  EXPECT_CALL(remove_callback(),
              Run(old_form.signon_realm, old_form.username_value,
                  RemoveInsecureCredentialsReason::kUpdate));
  EXPECT_CALL(remove_callback(), Run(conflicting_new_form.signon_realm,
                                     conflicting_new_form.username_value,
                                     RemoveInsecureCredentialsReason::kUpdate));
  ProcessLoginsChanged({remove_old, remove_conflicting, add},
                       remove_callback().Get());
  histogram_tester().ExpectUniqueSample(kHistogramName,
                                        PasswordStoreChange::UPDATE, 2);
}

}  // namespace
}  // namespace password_manager
