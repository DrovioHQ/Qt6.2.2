/* Copyright 2019 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. */
import 'chrome://resources/cr_elements/shared_vars_css.m.js';
import './signin_vars_css.js';

import 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

const styleElement = document.createElement('dom-module');
styleElement.setAttribute('assetpath', 'chrome://resources/');
styleElement.innerHTML = `{__html_template__}`;
styleElement.register('signin-dialog-shared');
