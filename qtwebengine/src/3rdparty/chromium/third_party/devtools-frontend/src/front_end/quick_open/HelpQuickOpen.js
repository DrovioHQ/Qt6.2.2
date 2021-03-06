// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as UI from '../ui/ui.js';

import {getRegisteredProviders, Provider, registerProvider} from './FilteredListWidget.js';
import {QuickOpenImpl} from './QuickOpen.js';


/** @type {!HelpQuickOpen} */
let helpQuickOpenInstance;

export class HelpQuickOpen extends Provider {
  /** @private */
  constructor() {
    super();
    /** @type {!Array<{prefix: string, title: string}>} */
    this._providers = [];
    getRegisteredProviders().forEach(this._addProvider.bind(this));
  }

  /**
   * @param {{forceNew: ?boolean}} opts
   */
  static instance(opts = {forceNew: null}) {
    const {forceNew} = opts;
    if (!helpQuickOpenInstance || forceNew) {
      helpQuickOpenInstance = new HelpQuickOpen();
    }
    return helpQuickOpenInstance;
  }

  /**
   * @param {!{prefix: string, title: (undefined|function():string)}} extension
   */
  _addProvider(extension) {
    if (extension.title) {
      this._providers.push({prefix: extension.prefix || '', title: extension.title()});
    }
  }

  /**
   * @override
   * @return {number}
   */
  itemCount() {
    return this._providers.length;
  }

  /**
   * @override
   * @param {number} itemIndex
   * @return {string}
   */
  itemKeyAt(itemIndex) {
    return this._providers[itemIndex].prefix;
  }

  /**
   * @override
   * @param {number} itemIndex
   * @param {string} query
   * @return {number}
   */
  itemScoreAt(itemIndex, query) {
    return -this._providers[itemIndex].prefix.length;
  }

  /**
   * @override
   * @param {number} itemIndex
   * @param {string} query
   * @param {!Element} titleElement
   * @param {!Element} subtitleElement
   */
  renderItem(itemIndex, query, titleElement, subtitleElement) {
    const provider = this._providers[itemIndex];
    const prefixElement = titleElement.createChild('span', 'monospace');
    prefixElement.textContent = (provider.prefix || '…') + ' ';
    UI.UIUtils.createTextChild(titleElement, provider.title);
  }

  /**
   * @override
   * @param {?number} itemIndex
   * @param {string} promptValue
   */
  selectItem(itemIndex, promptValue) {
    if (itemIndex !== null) {
      QuickOpenImpl.show(this._providers[itemIndex].prefix);
    }
  }

  /**
   * @override
   * @return {boolean}
   */
  renderAsTwoRows() {
    return false;
  }
}

registerProvider({
  prefix: '?',
  title: undefined,
  provider: HelpQuickOpen.instance,
});
