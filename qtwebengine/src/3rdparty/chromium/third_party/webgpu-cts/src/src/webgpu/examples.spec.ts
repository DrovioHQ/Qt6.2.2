export const description = `
Examples of writing CTS tests with various features.

Start here when looking for examples of basic framework usage.
`;

import { params, pbool, poptions } from '../common/framework/params_builder.js';
import { makeTestGroup } from '../common/framework/test_group.js';

import { GPUTest } from './gpu_test.js';

// To run these tests in the standalone runner, run `grunt build` or `grunt pre` then open:
// - http://localhost:8080/?runnow=1&q=webgpu:examples:
// To run in WPT, copy/symlink the out-wpt/ directory as the webgpu/ directory in WPT, then open:
// - (wpt server url)/webgpu/cts.html?q=webgpu:examples:
//
// Tests here can be run individually or in groups:
// - ?q=webgpu:examples:basic/async=
// - ?q=webgpu:examples:basic/
// - ?q=webgpu:examples:

export const g = makeTestGroup(GPUTest);

// Note: spaces in test names are replaced with underscores: webgpu:examples:test_name=
/* eslint-disable-next-line  @typescript-eslint/no-unused-vars */
g.test('test_name').fn(t => {});

g.test('not_implemented_yet,without_plan').unimplemented();
g.test('not_implemented_yet,with_plan')
  .desc(
    `
Plan for this test. What it tests. Summary of how it tests that functionality.
- Description of cases, by describing parameters {a, b, c}
- x= more parameters {x, y, z}
`
  )
  .unimplemented();

g.test('basic').fn(t => {
  t.expect(true);
  t.expect(true, 'true should be true');

  t.shouldThrow(
    // The expected '.name' of the thrown error.
    'TypeError',
    // This function is run inline inside shouldThrow, and is expected to throw.
    () => {
      throw new TypeError();
    },
    // Log message.
    'function should throw Error'
  );
});

g.test('basic,async').fn(async t => {
  // shouldReject must be awaited to ensure it can wait for the promise before the test ends.
  t.shouldReject(
    // The expected '.name' of the thrown error.
    'TypeError',
    // Promise expected to reject.
    Promise.reject(new TypeError()),
    // Log message.
    'Promise.reject should reject'
  );

  // Promise can also be an IIFE.
  t.shouldReject(
    'TypeError',
    (async () => {
      throw new TypeError();
    })(),
    'Promise.reject should reject'
  );
});

// A test can be parameterized with a simple array of objects.
//
// Parameters can be public (x, y) which means they're part of the case name.
// They can also be private by starting with an underscore (_result), which passes
// them into the test but does not make them part of the case name:
//
// - webgpu:examples:basic/params={"x":2,"y":4}    runs with t.params = {x: 2, y: 5, _result: 6}.
// - webgpu:examples:basic/params={"x":-10,"y":18} runs with t.params = {x: -10, y: 18, _result: 8}.
g.test('basic,params')
  .params([
    { x: 2, y: 4, _result: 6 }, //
    { x: -10, y: 18, _result: 8 },
  ])
  .fn(t => {
    t.expect(t.params.x + t.params.y === t.params._result);
  });
// (note the blank comment above to enforce newlines on autoformat)

// Runs the following cases:
// { x: 2, y: 2 }
// { x: 2, z: 3 }
// { x: 3, y: 2 }
// { x: 3, z: 3 }
g.test('basic,params_builder')
  .params(
    params()
      .combine(poptions('x', [2, 3]))
      .combine([{ y: 2 }, { z: 3 }])
  )
  .fn(() => {});

g.test('gpu,async').fn(async t => {
  const fence = t.queue.createFence();
  t.queue.signal(fence, 2);
  await fence.onCompletion(1);
  t.expect(fence.getCompletedValue() === 2);
});

g.test('gpu,buffers').fn(async t => {
  const data = new Uint32Array([0, 1234, 0]);
  const src = t.device.createBuffer({
    mappedAtCreation: true,
    size: 12,
    usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
  });
  new Uint32Array(src.getMappedRange()).set(data);
  src.unmap();

  // Use the expectContents helper to check the actual contents of a GPUBuffer.
  // Like shouldReject, it must be awaited.
  t.expectContents(src, data);
});

// One of the following two tests should be skipped on most platforms.

g.test('gpu,with_texture_compression,bc')
  .desc(
    `Example of a test using a device descriptor.
Tests that a BC format passes validation iff the feature is enabled.`
  )
  .params(pbool('textureCompressionBC'))
  .fn(async t => {
    const { textureCompressionBC } = t.params;

    if (textureCompressionBC) {
      await t.selectDeviceOrSkipTestCase({ extensions: ['texture-compression-bc'] });
    }

    const shouldError = !textureCompressionBC;
    t.expectGPUError(
      'validation',
      () => {
        t.device.createTexture({
          format: 'bc1-rgba-unorm',
          size: [4, 4, 1],
          usage: GPUTextureUsage.SAMPLED,
        });
      },
      shouldError
    );
  });

g.test('gpu,with_texture_compression,etc')
  .desc(
    `Example of a test using a device descriptor.

TODO: Test that an ETC format passes validation iff the feature is enabled.`
  )
  .params(pbool('textureCompressionETC'))
  .fn(async t => {
    const { textureCompressionETC } = t.params;

    if (textureCompressionETC) {
      await t.selectDeviceOrSkipTestCase({
        extensions: ['texture-compression-etc' as GPUExtensionName],
      });
    }

    // TODO: Should actually test createTexture with an ETC format here.
  });
