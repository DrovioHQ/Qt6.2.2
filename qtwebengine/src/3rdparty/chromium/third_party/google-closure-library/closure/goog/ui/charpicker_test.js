// Copyright 2013 The Closure Library Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

goog.module('goog.ui.CharPickerTest');
goog.setTestOnly();

const CharPicker = goog.require('goog.ui.CharPicker');
const CharPickerData = goog.require('goog.i18n.CharPickerData');
const EventType = goog.require('goog.events.EventType');
const FlatButtonRenderer = goog.require('goog.ui.FlatButtonRenderer');
const GoogEvent = goog.require('goog.events.Event');
const MockControl = goog.require('goog.testing.MockControl');
const NameFetcher = goog.require('goog.i18n.uChar.NameFetcher');
const State = goog.require('goog.a11y.aria.State');
const aria = goog.require('goog.a11y.aria');
const dispose = goog.require('goog.dispose');
const dom = goog.require('goog.dom');
const events = goog.require('goog.testing.events');
const mockmatchers = goog.require('goog.testing.mockmatchers');
const testSuite = goog.require('goog.testing.testSuite');

let charPicker;
let charPickerData;
let charPickerElement;

let charNameFetcherMock;
let mockControl;

testSuite({
  setUp() {
    mockControl = new MockControl();
    charNameFetcherMock = mockControl.createLooseMock(
        NameFetcher, true /* opt_ignoreUnexpectedCalls */);

    charPickerData = new CharPickerData();
    charPickerElement = dom.getElement('charpicker');

    charPicker = new CharPicker(charPickerData, charNameFetcherMock);
  },

  tearDown() {
    dispose(charPicker);
    dom.removeChildren(charPickerElement);
    mockControl.$tearDown();
  },

  testAriaLabelIsUpdatedOnFocus() {
    const character = '???';
    const characterName = 'right arrow';

    charNameFetcherMock.getName(character, mockmatchers.isFunction)
        .$does((c, callback) => {
          callback(characterName);
        });

    mockControl.$replayAll();

    charPicker.decorate(charPickerElement);

    // Get the first button elements within the grid div and override its
    // char attribute so the test doesn't depend on the actual grid content.
    const gridElement = dom.getElementByClass(
        goog.getCssName('goog-char-picker-grid'), charPickerElement);
    const buttonElement =
        dom.getElementsByClass(FlatButtonRenderer.CSS_CLASS, gridElement)[0];
    buttonElement.setAttribute('char', character);

    // Trigger a focus event on the button element.
    events.fireBrowserEvent(new GoogEvent(EventType.FOCUS, buttonElement));

    mockControl.$verifyAll();

    const ariaLabel = aria.getState(buttonElement, State.LABEL);
    assertEquals(
        'The aria label should be updated when the button' +
            'gains focus.',
        characterName, ariaLabel);
  },
});
