#pragma once

#include <chrono>
#include <set>
#include <vector>

#include "cista/reflection/comparable.h"

#include "nigiri/routing/query.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct start {
  CISTA_FRIEND_COMPARABLE(start)
  unixtime_t time_at_start_;
  unixtime_t time_at_stop_;
  location_idx_t stop_;
};

void get_starts(timetable const&,
                rt_timetable const*,
                direction,
                query const&,
                start_time_t const&,
                bool add_ontrip,
                std::vector<start>&);

void collect_destinations(timetable const&,
                          std::vector<offset> const& destinations,
                          location_match_mode const,
                          bitvec& is_destination,
                          std::vector<std::uint16_t>& dist_to_dest);

}  // namespace nigiri::routing
