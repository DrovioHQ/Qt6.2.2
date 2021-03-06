// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/renderer/fake_page_timing_sender.h"

#include <algorithm>

#include "testing/gtest/include/gtest/gtest.h"

namespace page_load_metrics {

FakePageTimingSender::FakePageTimingSender(PageTimingValidator* validator)
    : validator_(validator) {}

FakePageTimingSender::~FakePageTimingSender() {}

void FakePageTimingSender::SendTiming(
    const mojom::PageLoadTimingPtr& timing,
    const mojom::FrameMetadataPtr& metadata,
    mojom::PageLoadFeaturesPtr new_features,
    std::vector<mojom::ResourceDataUpdatePtr> resources,
    const mojom::FrameRenderDataUpdate& render_data,
    const mojom::CpuTimingPtr& cpu_timing,
    mojom::DeferredResourceCountsPtr new_deferred_resource_data,
    const mojom::InputTimingPtr new_input_timing,
    const blink::MobileFriendliness& mobile_friendliness) {
  validator_->UpdateTiming(timing, metadata, new_features, resources,
                           render_data, cpu_timing, new_deferred_resource_data,
                           new_input_timing, mobile_friendliness);
}

void FakePageTimingSender::SetUpSmoothnessReporting(
    base::ReadOnlySharedMemoryRegion shared_memory) {}

FakePageTimingSender::PageTimingValidator::PageTimingValidator()
    : expected_input_timing(mojom::InputTiming::New()),
      actual_input_timing(mojom::InputTiming::New()) {}

FakePageTimingSender::PageTimingValidator::~PageTimingValidator() {
  VerifyExpectedTimings();
}

void FakePageTimingSender::PageTimingValidator::ExpectPageLoadTiming(
    const mojom::PageLoadTiming& timing) {
  VerifyExpectedTimings();
  expected_timings_.push_back(timing.Clone());
}

void FakePageTimingSender::PageTimingValidator::ExpectCpuTiming(
    const base::TimeDelta& timing) {
  VerifyExpectedCpuTimings();
  expected_cpu_timings_.push_back(mojom::CpuTiming(timing).Clone());
}

void FakePageTimingSender::PageTimingValidator::VerifyExpectedTimings() const {
  // Ideally we'd just call ASSERT_EQ(actual_timings_, expected_timings_) here,
  // but this causes the generated gtest code to fail to build on Windows. See
  // the comments in the header file for additional details.
  ASSERT_EQ(actual_timings_.size(), expected_timings_.size());
  for (size_t i = 0; i < actual_timings_.size(); ++i) {
    if (actual_timings_.at(i)->Equals(*expected_timings_.at(i)))
      continue;
    ADD_FAILURE() << "Actual timing != expected timing at index " << i;
  }
}

void FakePageTimingSender::PageTimingValidator::UpdateExpectedInputTiming(
    const base::TimeDelta input_delay) {
  expected_input_timing->num_input_events++;
  expected_input_timing->total_input_delay += input_delay;
  expected_input_timing->total_adjusted_input_delay +=
      base::TimeDelta::FromMilliseconds(
          std::max(int64_t(0), input_delay.InMilliseconds() - int64_t(50)));
}
void FakePageTimingSender::PageTimingValidator::VerifyExpectedInputTiming()
    const {
  ASSERT_EQ(expected_input_timing.is_null(), actual_input_timing.is_null());
  ASSERT_EQ(expected_input_timing->num_input_events,
            actual_input_timing->num_input_events);
  ASSERT_EQ(expected_input_timing->total_input_delay,
            actual_input_timing->total_input_delay);
  ASSERT_EQ(expected_input_timing->total_adjusted_input_delay,
            actual_input_timing->total_adjusted_input_delay);
}

void FakePageTimingSender::PageTimingValidator::
    UpdateExpectedMobileFriendliness(
        const blink::MobileFriendliness& mobile_friendliness) {
  expected_mobile_friendliness = mobile_friendliness;
}

void FakePageTimingSender::PageTimingValidator::
    VerifyExpectedMobileFriendliness() const {
  ASSERT_EQ(expected_mobile_friendliness, actual_mobile_friendliness);
}

void FakePageTimingSender::PageTimingValidator::VerifyExpectedCpuTimings()
    const {
  ASSERT_EQ(actual_cpu_timings_.size(), expected_cpu_timings_.size());
  for (size_t i = 0; i < actual_cpu_timings_.size(); ++i) {
    if (actual_cpu_timings_.at(i)->task_time ==
        expected_cpu_timings_.at(i)->task_time)
      continue;
    ADD_FAILURE() << "Actual cpu timing != expected cpu timing at index " << i;
  }
}

void FakePageTimingSender::PageTimingValidator::UpdateExpectPageLoadFeatures(
    const blink::mojom::WebFeature feature) {
  expected_features_.insert(feature);
}

void FakePageTimingSender::PageTimingValidator::
    UpdateExpectPageLoadCssProperties(
        blink::mojom::CSSSampleId css_property_id) {
  expected_css_properties_.insert(css_property_id);
}

void FakePageTimingSender::PageTimingValidator::VerifyExpectedFeatures() const {
  ASSERT_EQ(actual_features_.size(), expected_features_.size());
  std::vector<blink::mojom::WebFeature> diff;
  std::set_difference(actual_features_.begin(), actual_features_.end(),
                      expected_features_.begin(), expected_features_.end(),
                      diff.begin());
  EXPECT_TRUE(diff.empty())
      << "Expected more features than the actual features observed";

  std::set_difference(expected_features_.begin(), expected_features_.end(),
                      actual_features_.begin(), actual_features_.end(),
                      diff.begin());
  EXPECT_TRUE(diff.empty())
      << "More features are actually observed than expected";
}

void FakePageTimingSender::PageTimingValidator::VerifyExpectedCssProperties()
    const {
  ASSERT_EQ(actual_css_properties_.size(), expected_css_properties_.size());
  std::vector<blink::mojom::CSSSampleId> diff;
  std::set_difference(actual_css_properties_.begin(),
                      actual_css_properties_.end(),
                      expected_css_properties_.begin(),
                      expected_css_properties_.end(), diff.begin());
  EXPECT_TRUE(diff.empty())
      << "Expected more CSS properties than the actual features observed";

  std::set_difference(expected_css_properties_.begin(),
                      expected_css_properties_.end(),
                      actual_css_properties_.begin(),
                      actual_css_properties_.end(), diff.begin());
  EXPECT_TRUE(diff.empty())
      << "More CSS Properties are actually observed than expected";
}

void FakePageTimingSender::PageTimingValidator::VerifyExpectedRenderData()
    const {
  EXPECT_FLOAT_EQ(expected_render_data_.is_null()
                      ? 0.0
                      : expected_render_data_->layout_shift_delta,
                  actual_render_data_.layout_shift_delta);
}

void FakePageTimingSender::PageTimingValidator::
    VerifyExpectedFrameIntersectionUpdate() const {
  if (!expected_frame_intersection_update_.is_null()) {
    EXPECT_FALSE(actual_frame_intersection_update_.is_null());
    EXPECT_TRUE(expected_frame_intersection_update_->Equals(
        *actual_frame_intersection_update_));
  }
}

void FakePageTimingSender::PageTimingValidator::UpdateTiming(
    const mojom::PageLoadTimingPtr& timing,
    const mojom::FrameMetadataPtr& metadata,
    const mojom::PageLoadFeaturesPtr& new_features,
    const std::vector<mojom::ResourceDataUpdatePtr>& resources,
    const mojom::FrameRenderDataUpdate& render_data,
    const mojom::CpuTimingPtr& cpu_timing,
    const mojom::DeferredResourceCountsPtr& new_deferred_resource_data,
    const mojom::InputTimingPtr& new_input_timing,
    const blink::MobileFriendliness& mobile_friendliness) {
  actual_timings_.push_back(timing.Clone());
  if (!cpu_timing->task_time.is_zero()) {
    actual_cpu_timings_.push_back(cpu_timing.Clone());
  }
  for (const auto feature : new_features->features) {
    EXPECT_EQ(actual_features_.find(feature), actual_features_.end())
        << "Feature " << feature << "has been sent more than once";
    actual_features_.insert(feature);
  }
  for (const auto css_property_id : new_features->css_properties) {
    EXPECT_EQ(actual_css_properties_.find(css_property_id),
              actual_css_properties_.end())
        << "CSS Property ID " << css_property_id
        << "has been sent more than once";
    actual_css_properties_.insert(css_property_id);
  }
  actual_render_data_.layout_shift_delta = render_data.layout_shift_delta;
  actual_frame_intersection_update_ = metadata->intersection_update.Clone();

  actual_input_timing->num_input_events += new_input_timing->num_input_events;
  actual_input_timing->total_input_delay += new_input_timing->total_input_delay;
  actual_input_timing->total_adjusted_input_delay +=
      new_input_timing->total_adjusted_input_delay;
  actual_mobile_friendliness = mobile_friendliness;

  VerifyExpectedTimings();
  VerifyExpectedCpuTimings();
  VerifyExpectedFeatures();
  VerifyExpectedCssProperties();
  VerifyExpectedRenderData();
  VerifyExpectedFrameIntersectionUpdate();
  VerifyExpectedMobileFriendliness();
}

}  // namespace page_load_metrics
