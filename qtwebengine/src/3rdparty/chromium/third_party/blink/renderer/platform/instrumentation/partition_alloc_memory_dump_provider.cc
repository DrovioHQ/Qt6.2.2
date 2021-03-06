// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/instrumentation/partition_alloc_memory_dump_provider.h"

#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/format_macros.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/malloc_dump_provider.h"
#include "base/trace_event/process_memory_dump.h"
#include "third_party/blink/renderer/platform/wtf/allocator/partitions.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

namespace {

const char kPartitionAllocDumpName[] = "partition_alloc";
const char kPartitionsDumpName[] = "partitions";

std::string GetPartitionDumpName(const char* partition_name) {
  return base::StringPrintf("%s/%s/%s", kPartitionAllocDumpName,
                            kPartitionsDumpName, partition_name);
}

// This class is used to invert the dependency of PartitionAlloc on the
// PartitionAllocMemoryDumpProvider. This implements an interface that will
// be called with memory statistics for each bucket in the allocator.
class PartitionStatsDumperImpl final : public base::PartitionStatsDumper {
  DISALLOW_NEW();

 public:
  PartitionStatsDumperImpl(
      base::trace_event::ProcessMemoryDump* memory_dump,
      base::trace_event::MemoryDumpLevelOfDetail level_of_detail)
      : memory_dump_(memory_dump), uid_(0), total_active_bytes_(0) {}

  // PartitionStatsDumper implementation.
  void PartitionDumpTotals(const char* partition_name,
                           const base::PartitionMemoryStats*) override;
  void PartitionsDumpBucketStats(
      const char* partition_name,
      const base::PartitionBucketMemoryStats*) override;

  size_t TotalActiveBytes() const { return total_active_bytes_; }

 private:
  base::trace_event::ProcessMemoryDump* memory_dump_;
  uint64_t uid_;
  size_t total_active_bytes_;

  DISALLOW_COPY_AND_ASSIGN(PartitionStatsDumperImpl);
};

void PartitionStatsDumperImpl::PartitionDumpTotals(
    const char* partition_name,
    const base::PartitionMemoryStats* memory_stats) {
  total_active_bytes_ += memory_stats->total_active_bytes;
  std::string dump_name = GetPartitionDumpName(partition_name);
  base::trace_event::MemoryAllocatorDump* allocator_dump =
      memory_dump_->CreateAllocatorDump(dump_name);
  allocator_dump->AddScalar("size", "bytes",
                            memory_stats->total_resident_bytes);
  allocator_dump->AddScalar("allocated_objects_size", "bytes",
                            memory_stats->total_active_bytes);
  allocator_dump->AddScalar("virtual_size", "bytes",
                            memory_stats->total_mmapped_bytes);
  allocator_dump->AddScalar("virtual_committed_size", "bytes",
                            memory_stats->total_committed_bytes);
  allocator_dump->AddScalar("decommittable_size", "bytes",
                            memory_stats->total_decommittable_bytes);
  allocator_dump->AddScalar("discardable_size", "bytes",
                            memory_stats->total_discardable_bytes);
  if (memory_stats->has_thread_cache) {
    const auto& thread_cache_stats = memory_stats->current_thread_cache_stats;
    auto* thread_cache_dump = memory_dump_->CreateAllocatorDump(
        dump_name + "/thread_cache/main_thread");
    base::trace_event::ReportPartitionAllocThreadCacheStats(thread_cache_dump,
                                                            thread_cache_stats);

    const auto& all_thread_caches_stats = memory_stats->all_thread_caches_stats;
    auto* all_thread_caches_dump =
        memory_dump_->CreateAllocatorDump(dump_name + "/thread_cache");
    base::trace_event::ReportPartitionAllocThreadCacheStats(
        all_thread_caches_dump, all_thread_caches_stats);

    if (all_thread_caches_stats.alloc_count) {
      int hit_rate_percent =
          static_cast<int>((100 * all_thread_caches_stats.alloc_hits) /
                           all_thread_caches_stats.alloc_count);
      base::UmaHistogramPercentage("Memory.PartitionAlloc.ThreadCache.HitRate",
                                   hit_rate_percent);

      int batch_fill_rate_percent =
          static_cast<int>((100 * all_thread_caches_stats.batch_fill_count) /
                           all_thread_caches_stats.alloc_count);
      base::UmaHistogramPercentage(
          "Memory.PartitionAlloc.ThreadCache.BatchFillRate",
          batch_fill_rate_percent);
    }

    if (thread_cache_stats.alloc_count) {
      int hit_rate_percent =
          static_cast<int>((100 * thread_cache_stats.alloc_hits) /
                           thread_cache_stats.alloc_count);
      base::UmaHistogramPercentage(
          "Memory.PartitionAlloc.ThreadCache.HitRate.MainThread",
          hit_rate_percent);

      int batch_fill_rate_percent =
          static_cast<int>((100 * thread_cache_stats.batch_fill_count) /
                           thread_cache_stats.alloc_count);
      base::UmaHistogramPercentage(
          "Memory.PartitionAlloc.ThreadCache.BatchFillRate.MainThread",
          batch_fill_rate_percent);
    }
  }
}

void PartitionStatsDumperImpl::PartitionsDumpBucketStats(
    const char* partition_name,
    const base::PartitionBucketMemoryStats* memory_stats) {
  DCHECK(memory_stats->is_valid);
  std::string dump_name = GetPartitionDumpName(partition_name);
  if (memory_stats->is_direct_map) {
    dump_name.append(base::StringPrintf("/buckets/directMap_%" PRIu64, ++uid_));
  } else {
    dump_name.append(base::StringPrintf("/buckets/bucket_%" PRIu32,
                                        memory_stats->bucket_slot_size));
  }

  base::trace_event::MemoryAllocatorDump* allocator_dump =
      memory_dump_->CreateAllocatorDump(dump_name);
  allocator_dump->AddScalar("size", "bytes", memory_stats->resident_bytes);
  allocator_dump->AddScalar("allocated_objects_size", "bytes",
                            memory_stats->active_bytes);
  allocator_dump->AddScalar("slot_size", "bytes",
                            memory_stats->bucket_slot_size);
  allocator_dump->AddScalar("decommittable_size", "bytes",
                            memory_stats->decommittable_bytes);
  allocator_dump->AddScalar("discardable_size", "bytes",
                            memory_stats->discardable_bytes);
  // TODO(bartekn): Rename the scalar names.
  allocator_dump->AddScalar("total_slot_span_size", "bytes",
                            memory_stats->allocated_slot_span_size);
  allocator_dump->AddScalar("active_slot_spans", "objects",
                            memory_stats->num_active_slot_spans);
  allocator_dump->AddScalar("full_slot_spans", "objects",
                            memory_stats->num_full_slot_spans);
  allocator_dump->AddScalar("empty_slot_spans", "objects",
                            memory_stats->num_empty_slot_spans);
  allocator_dump->AddScalar("decommitted_slot_spans", "objects",
                            memory_stats->num_decommitted_slot_spans);
}

}  // namespace

PartitionAllocMemoryDumpProvider* PartitionAllocMemoryDumpProvider::Instance() {
  DEFINE_STATIC_LOCAL(PartitionAllocMemoryDumpProvider, instance, ());
  return &instance;
}

bool PartitionAllocMemoryDumpProvider::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* memory_dump) {
  using base::trace_event::MemoryDumpLevelOfDetail;

  MemoryDumpLevelOfDetail level_of_detail = args.level_of_detail;
  PartitionStatsDumperImpl partition_stats_dumper(memory_dump, level_of_detail);

  base::trace_event::MemoryAllocatorDump* partitions_dump =
      memory_dump->CreateAllocatorDump(base::StringPrintf(
          "%s/%s", kPartitionAllocDumpName, kPartitionsDumpName));

  // This method calls memoryStats.partitionsDumpBucketStats with memory
  // statistics.
  WTF::Partitions::DumpMemoryStats(
      level_of_detail != MemoryDumpLevelOfDetail::DETAILED,
      &partition_stats_dumper);

  base::trace_event::MemoryAllocatorDump* allocated_objects_dump =
      memory_dump->CreateAllocatorDump(
          WTF::Partitions::kAllocatedObjectPoolName);
  allocated_objects_dump->AddScalar("size", "bytes",
                                    partition_stats_dumper.TotalActiveBytes());
  memory_dump->AddOwnershipEdge(allocated_objects_dump->guid(),
                                partitions_dump->guid());

  return true;
}

PartitionAllocMemoryDumpProvider::PartitionAllocMemoryDumpProvider() = default;
PartitionAllocMemoryDumpProvider::~PartitionAllocMemoryDumpProvider() = default;

}  // namespace blink
