#include "pioneer/sim/determinism.hpp"

#include <algorithm>
#include <cstdio>

#include "pioneer/core/profiler.hpp"
#include "pioneer/sim/simulation.hpp"

namespace pioneer {

void HashTrace::record(Simulation& sim) {
  const uint32_t t = sim.tickCount();
  if (t % interval_ != 0) return;
  if (!samples_.empty() && samples_.back().tick >= t) return;
  samples_.push_back({t, 0, sim.stateHash()});
}

HashTrace::Mismatch HashTrace::check(Simulation& sim) const {
  Mismatch m;
  const uint32_t t = sim.tickCount();
  auto it = std::lower_bound(samples_.begin(), samples_.end(), t,
                             [](const HashSample& s, uint32_t tick) { return s.tick < tick; });
  if (it == samples_.end() || it->tick != t) return m;
  const uint64_t got = sim.stateHash();
  if (got != it->hash) m = {true, t, it->hash, got};
  return m;
}

HashTrace::Mismatch HashTrace::compare(const HashTrace& expected, const HashTrace& got) {
  size_t i = 0, j = 0;
  const auto& a = expected.samples_;
  const auto& b = got.samples_;
  while (i < a.size() && j < b.size()) {
    if (a[i].tick < b[j].tick) ++i;
    else if (b[j].tick < a[i].tick) ++j;
    else {
      if (a[i].hash != b[j].hash) return {true, a[i].tick, a[i].hash, b[j].hash};
      ++i;
      ++j;
    }
  }
  return {};
}

void HashTrace::write(ByteWriter& w) const {
  w.u32(interval_);
  w.podVector(samples_);
}

bool HashTrace::read(ByteReader& r) {
  interval_ = std::max<uint32_t>(1, r.u32());
  samples_ = r.podVector<HashSample>();
  for (size_t i = 1; i < samples_.size(); ++i)
    if (samples_[i].tick <= samples_[i - 1].tick) return false;
  return r.ok();
}

TwinReport twinCheck(Simulation& sim, uint32_t ticks, const std::function<void(Simulation&, uint32_t)>& drive) {
  PNR_PROFILE("Sim: twin check");
  TwinReport rep;
  ByteWriter saved;
  sim.saveState(saved);
  Map copy = sim.map();
  Simulation twin(copy, sim.types(), sim.weapons(), 0);
  {
    ByteReader r(saved.bytes());
    std::string err;
    if (!twin.loadState(r, err)) {
      rep.ok = false;
      rep.text = "the saved state did not load: " + err;
      return rep;
    }
  }
  auto names = [](const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& n : v) s += (s.empty() ? "" : ", ") + n;
    return s;
  };
  // What was saved and what the copy holds must be the same thing.
  rep.sections = Simulation::digestDifference(sim.digest(), twin.digest());
  if (!rep.sections.empty()) {
    rep.ok = rep.saveRoundTrip = false;
    rep.tick = sim.tickCount();
    rep.text = "a loaded state differs from the one saved, in: " + names(rep.sections);
    return rep;
  }
  for (uint32_t i = 0; i < ticks; ++i) {
    if (drive) {
      drive(sim, sim.tickCount());
      drive(twin, twin.tickCount());
    }
    sim.tick();
    twin.tick();
    ++rep.ticksRun;
    if (sim.stateHash() == twin.stateHash()) continue;
    rep.ok = false;
    rep.tick = sim.tickCount();
    rep.sections = Simulation::digestDifference(sim.digest(), twin.digest());
    char buf[96];
    std::snprintf(buf, sizeof(buf), "the battle and its loaded copy part ways at tick %u (after %u ticks), in: ", rep.tick,
                  rep.ticksRun);
    rep.text = buf + names(rep.sections);
    return rep;
  }
  // The checksum covers the units; the digest covers everything saved.
  rep.sections = Simulation::digestDifference(sim.digest(), twin.digest());
  if (!rep.sections.empty()) {
    rep.ok = false;
    rep.tick = sim.tickCount();
    rep.text = "after " + std::to_string(ticks) + " ticks the units agree but these sections differ: " + names(rep.sections);
    return rep;
  }
  rep.text = "the battle and its loaded copy stayed identical for " + std::to_string(ticks) + " ticks";
  return rep;
}

}  // namespace pioneer
