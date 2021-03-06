// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_FEEDBACK_FEEDBACK_UI_H_
#define CHROME_BROWSER_UI_WEBUI_FEEDBACK_FEEDBACK_UI_H_

#include "base/macros.h"
#include "content/public/browser/web_ui_controller.h"

// The implementation for the chrome://feedback page.
class FeedbackUI : public content::WebUIController {
 public:
  explicit FeedbackUI(content::WebUI* web_ui);
  ~FeedbackUI() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FeedbackUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_FEEDBACK_FEEDBACK_UI_H_
