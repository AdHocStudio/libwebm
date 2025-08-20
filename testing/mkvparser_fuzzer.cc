// Copyright (c) 2022 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>

#include "mkvparser/mkvparser.h"
#include "mkvparser/mkvreader.h"

namespace {

class MemoryReader : public adhoc::mkvparser::IMkvReader {
 public:
  MemoryReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  int Read(long long pos, long len, unsigned char* buf) override {
    if (pos < 0 || len < 0) {
      abort();
    }
    if (pos >= size_ || size_ - pos < len) {
      return -1;
    }
    memcpy(buf, data_ + pos, len);
    return 0;
  }

  int Length(long long* total, long long* available) override {
    if (total != nullptr) {
      *total = size_;
    }
    if (available != nullptr) {
      *available = size_;
    }
    return 0;
  }

 private:
  const uint8_t* data_;
  size_t size_;
};

void ParseCues(const adhoc::mkvparser::Segment& segment) {
  const adhoc::mkvparser::Cues* const cues = segment.GetCues();
  if (cues == nullptr) {
    return;
  }

  while (!cues->DoneParsing()) {
    cues->LoadCuePoint();
  }
}

const adhoc::mkvparser::BlockEntry* GetBlockEntryFromCues(
    const void* ctx, const adhoc::mkvparser::CuePoint* cue,
    const adhoc::mkvparser::CuePoint::TrackPosition* track_pos) {
  const auto* const cues = static_cast<const adhoc::mkvparser::Cues*>(ctx);
  return cues->GetBlock(cue, track_pos);
}

const adhoc::mkvparser::BlockEntry* GetBlockEntryFromCluster(
    const void* ctx, const adhoc::mkvparser::CuePoint* cue,
    const adhoc::mkvparser::CuePoint::TrackPosition* track_pos) {
  if (track_pos == nullptr) {
    return nullptr;
  }
  const auto* const cluster = static_cast<const adhoc::mkvparser::Cluster*>(ctx);
  const adhoc::mkvparser::BlockEntry* block_entry =
      cluster->GetEntry(*cue, *track_pos);
  return block_entry;
}

void WalkCues(const adhoc::mkvparser::Segment& segment,
              std::function<const adhoc::mkvparser::BlockEntry*(
                  const void*, const adhoc::mkvparser::CuePoint*,
                  const adhoc::mkvparser::CuePoint::TrackPosition*)>
                  get_block_entry,
              const void* ctx) {
  const adhoc::mkvparser::Cues* const cues = segment.GetCues();
  const adhoc::mkvparser::Tracks* tracks = segment.GetTracks();
  if (cues == nullptr || tracks == nullptr) {
    return;
  }
  const unsigned long num_tracks = tracks->GetTracksCount();

  for (const adhoc::mkvparser::CuePoint* cue = cues->GetFirst(); cue != nullptr;
       cue = cues->GetNext(cue)) {
    for (unsigned long track_num = 0; track_num < num_tracks; ++track_num) {
      const adhoc::mkvparser::Track* const track = tracks->GetTrackByIndex(track_num);
      const adhoc::mkvparser::CuePoint::TrackPosition* const track_pos =
          cue->Find(track);
      const adhoc::mkvparser::BlockEntry* block_entry =
          get_block_entry(ctx, cue, track_pos);
      static_cast<void>(block_entry);
    }
  }
}

void ParseCluster(const adhoc::mkvparser::Cluster& cluster) {
  const adhoc::mkvparser::BlockEntry* block_entry;
  long status = cluster.GetFirst(block_entry);
  if (status != 0) {
    return;
  }

  while (block_entry != nullptr && !block_entry->EOS()) {
    const adhoc::mkvparser::Block* const block = block_entry->GetBlock();
    if (block == nullptr) {
      return;
    }

    status = cluster.GetNext(block_entry, block_entry);
    if (status != 0) {
      return;
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  MemoryReader reader(data, size);

  long long int pos = 0;
  std::unique_ptr<adhoc::mkvparser::EBMLHeader> ebml_header(
      new (std::nothrow) adhoc::mkvparser::EBMLHeader());  // NOLINT
  if (ebml_header->Parse(&reader, pos) < 0) {
    return 0;
  }

  adhoc::mkvparser::Segment* temp_segment;
  if (adhoc::mkvparser::Segment::CreateInstance(&reader, pos, temp_segment) != 0) {
    return 0;
  }
  std::unique_ptr<adhoc::mkvparser::Segment> segment(temp_segment);

  if (segment->Load() < 0) {
    return 0;
  }

  ParseCues(*segment);
  WalkCues(*segment, GetBlockEntryFromCues, segment->GetCues());

  const adhoc::mkvparser::Cluster* cluster = segment->GetFirst();
  while (cluster != nullptr && !cluster->EOS()) {
    ParseCluster(*cluster);
    WalkCues(*segment, GetBlockEntryFromCluster, cluster);
    cluster = segment->GetNext(cluster);
  }

  return 0;
}
