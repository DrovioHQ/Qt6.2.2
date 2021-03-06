// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAGE_LOAD_METRICS_BROWSER_LAYOUT_SHIFT_NORMALIZATION_H_
#define COMPONENTS_PAGE_LOAD_METRICS_BROWSER_LAYOUT_SHIFT_NORMALIZATION_H_

#include "base/time/time.h"
#include "components/page_load_metrics/browser/page_load_metrics_observer.h"

namespace page_load_metrics {

// LayoutShiftNormalization implements some experimental strategies for
// normalizing layout shift. Instead of adding all layout shift scores together
// like what we do in Cumulative Layout Shift(CLS), we aggregate layout shifts
// window by window. For more information, see go/layoutshiftnorm.
class LayoutShiftNormalization {
 public:
  LayoutShiftNormalization();
  ~LayoutShiftNormalization();
  const NormalizedCLSData& normalized_cls_data() const {
    return normalized_cls_data_;
  }

  void AddInputTimeStamps(const std::vector<base::TimeTicks>& input_timestamps);
  void AddNewLayoutShifts(
      const std::vector<page_load_metrics::mojom::LayoutShiftPtr>& new_shifts,
      base::TimeTicks current_time,
      /*Whole page CLS*/ double cumulative_layout_shift_score);

  void ClearAllLayoutShifts();

 private:
  struct SlidingWindow {
    base::TimeTicks start_time;
    double layout_shift_score = 0.0;
  };

  struct SessionWindow {
    base::TimeTicks start_time;
    base::TimeTicks last_time;
    double layout_shift_score = 0.0;
  };

  void UpdateWindowCLS(
      base::TimeTicks current_time,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator first,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator
          first_non_stale,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator last,
      double cumulative_layout_shift_score);

  void UpdateSlidingWindow(
      std::vector<SlidingWindow>* sliding_windows,
      base::TimeDelta duration,
      base::TimeTicks current_time,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator begin,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator end,
      double& max_score);

  void UpdateSessionWindow(
      SessionWindow* session_window,
      base::TimeDelta gap,
      base::TimeDelta max_duration,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator begin,
      std::vector<std::pair<base::TimeTicks, double>>::const_iterator end,
      std::vector<base::TimeTicks>& input_timestamps,
      double& max_score,
      uint32_t& count);

  // CLS normalization
  NormalizedCLSData normalized_cls_data_;

  // This vector is maintained in sorted order.
  std::vector<std::pair<base::TimeTicks, double>> recent_layout_shifts_;

  // This vector which contains input timestamps is maintained in sorted order.
  std::vector<base::TimeTicks> recent_input_timestamps_;

  // Sliding window vectors are maintained in sorted order.
  std::vector<SlidingWindow> sliding_300ms_;
  std::vector<SlidingWindow> sliding_1000ms_;
  SessionWindow session_gap1000ms_max5000ms_;
  SessionWindow session_gap1000ms_;
  SessionWindow session_gap5000ms_;
  SessionWindow session_by_inputs_gap1000ms_max5000ms_;
  // A new input in non-stale data can split the session window and make the
  // max_cls smaller. We need to store the "max_cls" calculated by the stale
  // data.
  double potential_max_cls_session_by_inputs_gap1000ms_max5000ms_ = 0.0;
  uint32_t session_gap5000ms_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(LayoutShiftNormalization);
};

}  // namespace page_load_metrics

#endif  // COMPONENTS_PAGE_LOAD_METRICS_BROWSER_LAYOUT_SHIFT_NORMALIZATION_H_
