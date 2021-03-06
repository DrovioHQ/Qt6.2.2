// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/prerender/prerender_processor.h"

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/test_browser_context.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "mojo/public/cpp/system/functions.h"
#include "third_party/blink/public/common/features.h"

namespace content {
namespace {

class PrerenderProcessorTest : public RenderViewHostImplTestHarness {
 public:
  void SetUp() override {
    RenderViewHostImplTestHarness::SetUp();

    scoped_feature_list_.InitAndEnableFeature(blink::features::kPrerender2);
    browser_context_ = std::make_unique<TestBrowserContext>();

    web_contents_ = TestWebContents::Create(
        browser_context_.get(),
        SiteInstanceImpl::Create(browser_context_.get()));
    web_contents_->NavigateAndCommit(GURL("https://example.com"));
  }

  void TearDown() override {
    web_contents_.reset();
    browser_context_.reset();
    RenderViewHostImplTestHarness::TearDown();
  }

  void DestroyPrerenderProcessors() {
    // Resetting the web contents destroys render frame hosts, which in turn
    // destroy `RenderFrameHostImpl::prerender_processor_receivers_`.
    web_contents_.reset();
  }

  RenderFrameHostImpl* GetRenderFrameHost() {
    DCHECK(web_contents_);
    return web_contents_->GetMainFrame();
  }

  GURL GetSameOriginUrl(const std::string& path) {
    return GURL("https://example.com" + path);
  }

  GURL GetCrossOriginUrl(const std::string& path) {
    return GURL("https://other.example.com" + path);
  }

  PrerenderHostRegistry* GetPrerenderHostRegistry() const {
    return static_cast<StoragePartitionImpl*>(
               BrowserContext::GetDefaultStoragePartition(
                   browser_context_.get()))
        ->GetPrerenderHostRegistry();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  std::unique_ptr<TestBrowserContext> browser_context_;
  std::unique_ptr<TestWebContents> web_contents_;
};

TEST_F(PrerenderProcessorTest, StartCancel) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes = blink::mojom::PrerenderAttributes::New();
  attributes->url = kPrerenderingUrl;
  attributes->referrer = blink::mojom::Referrer::New();

  // Start() call should register a new prerender host.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes));
  remote.FlushForTesting();
  EXPECT_TRUE(registry->FindHostByUrlForTesting(kPrerenderingUrl));

  // Cancel() call should abandon the prerender host.
  remote->Cancel();
  remote.FlushForTesting();
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
}

TEST_F(PrerenderProcessorTest, StartDisconnect) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes = blink::mojom::PrerenderAttributes::New();
  attributes->url = kPrerenderingUrl;
  attributes->referrer = blink::mojom::Referrer::New();

  // Start() call should register a new prerender host.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes));
  remote.FlushForTesting();
  EXPECT_TRUE(registry->FindHostByUrlForTesting(kPrerenderingUrl));

  // Connection lost should abandon the prerender host.
  remote.reset();
  // FlushForTesting() is no longer available. Instead, use base::RunLoop.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
}

TEST_F(PrerenderProcessorTest, CancelOnDestruction) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes = blink::mojom::PrerenderAttributes::New();
  attributes->url = kPrerenderingUrl;
  attributes->referrer = blink::mojom::Referrer::New();

  // Start() call should register a new prerender host.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes));
  remote.FlushForTesting();
  EXPECT_TRUE(registry->FindHostByUrlForTesting(kPrerenderingUrl));

  // The destructor of PrerenderProcessor should abandon the prerender host.
  DestroyPrerenderProcessors();
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
}

TEST_F(PrerenderProcessorTest, StartTwice) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  // Set up the error handler for bad mojo messages.
  std::string bad_message_error;
  mojo::SetDefaultProcessErrorHandler(
      base::BindLambdaForTesting([&](const std::string& error) {
        EXPECT_TRUE(bad_message_error.empty());
        bad_message_error = error;
      }));

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes1 = blink::mojom::PrerenderAttributes::New();
  attributes1->url = kPrerenderingUrl;
  attributes1->referrer = blink::mojom::Referrer::New();

  // Start() call should register a new prerender host.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes1));
  remote.FlushForTesting();
  EXPECT_TRUE(registry->FindHostByUrlForTesting(kPrerenderingUrl));

  auto attributes2 = blink::mojom::PrerenderAttributes::New();
  attributes2->url = kPrerenderingUrl;
  attributes2->referrer = blink::mojom::Referrer::New();

  // Call Start() again. This should be reported as a bad mojo message.
  ASSERT_TRUE(bad_message_error.empty());
  remote->Start(std::move(attributes2));
  remote.FlushForTesting();
  EXPECT_EQ(bad_message_error, "PP_START_TWICE");
}

TEST_F(PrerenderProcessorTest, CancelBeforeStart) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  // Set up the error handler for bad mojo messages.
  std::string bad_message_error;
  mojo::SetDefaultProcessErrorHandler(
      base::BindLambdaForTesting([&](const std::string& error) {
        EXPECT_TRUE(bad_message_error.empty());
        bad_message_error = error;
      }));

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes1 = blink::mojom::PrerenderAttributes::New();
  attributes1->url = kPrerenderingUrl;
  attributes1->referrer = blink::mojom::Referrer::New();

  // Call Cancel() before Start(). This should be reported as a bad mojo
  // message.
  ASSERT_TRUE(bad_message_error.empty());
  remote->Cancel();
  remote.FlushForTesting();
  EXPECT_EQ(bad_message_error, "PP_CANCEL_BEFORE_START");
}

// Tests that prerendering a cross-origin URL is aborted. Cross-origin
// prerendering is not supported for now, but we plan to support it later
// (https://crbug.com/1176054).
TEST_F(PrerenderProcessorTest, CrossOrigin) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  // Set up the error handler for bad mojo messages.
  std::string bad_message_error;
  mojo::SetDefaultProcessErrorHandler(
      base::BindLambdaForTesting([&](const std::string& error) {
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(bad_message_error.empty());
        bad_message_error = error;
      }));

  const GURL kPrerenderingUrl = GetCrossOriginUrl("/next");
  auto attributes = blink::mojom::PrerenderAttributes::New();
  attributes->url = kPrerenderingUrl;
  attributes->referrer = blink::mojom::Referrer::New();

  // Start() call with the cross-origin URL should be reported as a bad message.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes));
  remote.FlushForTesting();
  EXPECT_EQ(bad_message_error, "PP_CROSS_ORIGIN");
}

// Tests that prerendering triggered by <link rel=next> is aborted. This trigger
// is not supported for now, but we may want to support it if NoStatePrefetch
// re-enables it again. See https://crbug.com/1161545.
TEST_F(PrerenderProcessorTest, RelTypeNext) {
  RenderFrameHostImpl* render_frame_host = GetRenderFrameHost();
  PrerenderHostRegistry* registry = GetPrerenderHostRegistry();

  mojo::Remote<blink::mojom::PrerenderProcessor> remote;
  render_frame_host->BindPrerenderProcessor(
      remote.BindNewPipeAndPassReceiver());

  // Set up the error handler for bad mojo messages.
  std::string bad_message_error;
  mojo::SetDefaultProcessErrorHandler(
      base::BindLambdaForTesting([&](const std::string& error) {
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(bad_message_error.empty());
        bad_message_error = error;
      }));

  const GURL kPrerenderingUrl = GetSameOriginUrl("/next");
  auto attributes = blink::mojom::PrerenderAttributes::New();
  attributes->url = kPrerenderingUrl;
  // Set kNext instead of the default kPrerender.
  attributes->rel_type = blink::mojom::PrerenderRelType::kNext;
  attributes->referrer = blink::mojom::Referrer::New();

  // Start() call with kNext should be aborted.
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));
  remote->Start(std::move(attributes));
  remote.FlushForTesting();
  EXPECT_FALSE(registry->FindHostByUrlForTesting(kPrerenderingUrl));

  // Start() call with kNext is a valid request, currently it's not supported
  // though. The request shouldn't result in a bad message failure.
  EXPECT_TRUE(bad_message_error.empty());

  // Cancel() call should not be reported as a bad mojo message as well.
  remote->Cancel();
  remote.FlushForTesting();
  EXPECT_TRUE(bad_message_error.empty());
}

}  // namespace
}  // namespace content
