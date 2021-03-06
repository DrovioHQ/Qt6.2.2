// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/url_util.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "components/payments/content/android/jni_headers/UrlUtil_jni.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"

namespace payments {
namespace android {

// static
jboolean JNI_UrlUtil_IsOriginAllowedToUseWebPaymentApis(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& j_url) {
  std::unique_ptr<GURL> url = url::GURLAndroid::ToNativeGURL(env, j_url);
  return UrlUtil::IsOriginAllowedToUseWebPaymentApis(*url);
}

// static
jboolean JNI_UrlUtil_IsLocalDevelopmentUrl(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& j_url) {
  std::unique_ptr<GURL> url = url::GURLAndroid::ToNativeGURL(env, j_url);
  return UrlUtil::IsLocalDevelopmentUrl(*url);
}

}  // namespace android
}  // namespace payments
