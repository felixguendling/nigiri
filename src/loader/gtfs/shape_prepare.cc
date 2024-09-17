#include "nigiri/loader/gtfs/shape_prepare.h"

#include <algorithm>

#include "geo/latlng.h"
#include "geo/polyline.h"

#include "utl/get_or_create.h"
#include "utl/progress_tracker.h"
#include "utl/zip.h"

#include "nigiri/loader/gtfs/trip.h"
#include "nigiri/shape.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

namespace nigiri::loader::gtfs {

auto get_closest(geo::latlng coordinate,
                 std::span<geo::latlng const> const& shape) {
  if (shape.size() < 2) {
    return 0U;
  }
  auto const best = geo::distance_to_polyline(coordinate, shape);
  auto const segment = best.segment_idx_;
  auto const segment_from = shape[segment];
  auto const segment_to = shape[segment + 1];
  auto const offset = geo::distance(coordinate, segment_from) <=
                              geo::distance(coordinate, segment_to)
                          ? segment
                          : segment + 1;
  return static_cast<unsigned>(offset);
}

std::vector<location_idx_t> get_interior_locations(stop_seq_t const& stops) {
  auto const length = stops.length();
  assert(length >= 2U);
  auto locations = std::vector<location_idx_t>(length - 2);
  for (auto i = 0U; i < locations.size(); ++i) {
    locations[i] = stop(stops[i + 1]).location_idx();
  }
  return locations;
}

std::vector<shape_offset_t> split_shape(timetable const& tt,
                                        std::span<geo::latlng const> shape,
                                        stop_seq_t const& stops) {
  if (shape.empty()) {
    return {};
  }
  auto offsets = std::vector<shape_offset_t>(stops.size());
  auto offset = shape_offset_t{0};

  auto index = 0U;
  for (auto const& location_index : stops) {
    if (index == 0U) {
      offsets[0] = shape_offset_t{0};
    } else if (index == stops.size() - 1U) {
      offsets[index] = shape_offset_t{shape.size() - 1U};
    } else {
      auto const location =
          tt.locations_.get(stop(location_index).location_idx());
      offsets[index] = offset += get_closest(
          location.pos_, shape.subspan(static_cast<std::size_t>(offset.v_)));
    }
    ++index;
  }

  return offsets;
}

void calculate_shape_offsets(timetable const& tt,
                             shapes_storage& shapes_data,
                             vector_map<gtfs_trip_idx_t, trip> const& trips) {
  auto const progress_tracker = utl::get_active_progress_tracker();
  progress_tracker->status("Calculating shape offsets")
      .out_bounds(98.F, 100.F)
      .in_high(trips.size());
  struct hash_stop_seq_t {
    auto operator()(
        std::pair<shape_idx_t, stop_seq_t const*> const& pair) const noexcept {
      return cista::hashing<std::pair<shape_idx_t, stop_seq_t>>{}(
          std::make_pair(pair.first, *pair.second));
    }
  };

  struct equals_stop_seq_t {
    auto operator()(
        std::pair<shape_idx_t, stop_seq_t const*> const& lhs,
        std::pair<shape_idx_t, stop_seq_t const*> const& rhs) const noexcept {
      if (lhs.first != rhs.first || lhs.second->size() != rhs.second->size()) {
        return false;
      }
      auto const& zip = utl::zip(*lhs.second, *rhs.second);
      return std::all_of(begin(zip), end(zip), [](auto const pair) {
        return std::get<0>(pair) == std::get<1>(pair);
      });
    }
  };
  auto offsets_cache =
      hash_map<std::pair<shape_idx_t, stop_seq_t const*>, trip_idx_t,
               hash_stop_seq_t, equals_stop_seq_t>{};
  for (auto const& trip : trips) {
    progress_tracker->update_fn();
    auto const trip_index = trip.trip_idx_;
    if (trip.stop_seq_.size() < 2U) {
      shapes_data.add_offsets(trip_index, {});
      continue;
    }
    auto const cached_trip_index = utl::get_or_create(
        offsets_cache, std::make_pair(trip.shape_idx_, &trip.stop_seq_), [&]() {
          auto const shape = shapes_data.get_shape(trip.shape_idx_);
          auto const offsets = split_shape(tt, shape, trip.stop_seq_);
          shapes_data.add_offsets(trip_index, offsets);
          return trip_index;
        });
    if (cached_trip_index != trip_index) {
      shapes_data.duplicate_offsets(cached_trip_index, trip_index);
    }
  }
}

}  // namespace nigiri::loader::gtfs