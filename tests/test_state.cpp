// Pioneer part 3: saved state in tagged sections, canonical bytes, and the
// determinism checker.
#include <doctest/doctest.h>

#include <cstring>

#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/core/chunks.hpp"
#include "pioneer/core/reflect.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/map/map_io.hpp"
#include "pioneer/sim/determinism.hpp"
#include "pioneer/sim/savefile.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
constexpr int T = 256;
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
};

void spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at, int count) {
  Command s;
  s.type = CommandType::Spawn;
  s.player = owner;
  s.unitType = type;
  s.count = static_cast<uint16_t>(count);
  s.target = at;
  sim.queue(s);
}

// Two small armies that meet in the middle of a 64 x 64 map.
void skirmish(Simulation& sim, const Rules& R) {
  spawn(sim, R.t("ger_rifleman"), 0, {14 * T, 30 * T}, 16);
  spawn(sim, R.t("ger_medium_tank"), 0, {12 * T, 34 * T}, 3);
  spawn(sim, R.t("rus_rifleman"), 1, {50 * T, 32 * T}, 18);
  spawn(sim, R.t("rus_medium_tank"), 1, {52 * T, 28 * T}, 3);
  sim.tick();
  for (uint8_t p = 0; p < 2; ++p) {
    Command c;
    c.type = CommandType::AttackMove;
    c.player = p;
    c.target = {32 * T, 32 * T};
    for (uint32_t i : sim.registry().pool<CUnit>().entities())
      if (sim.registry().pool<CUnit>().get(i).owner == p) c.units.push_back(sim.registry().handle(i));
    sim.queue(c);
  }
}

struct Padded {
  uint8_t a = 1;
  // three bytes of padding
  uint32_t b = 2;
  uint16_t c = 3;
  // two more
};
struct Nested {
  uint8_t tag = 9;
  Padded inner;
  std::array<Padded, 2> list;
  Vec2i at;
};
}  // namespace

TEST_CASE("reflect: fields are counted and visited in order, padding is found") {
  CHECK(reflect::fieldCount<Padded> == 3);
  CHECK(reflect::fieldCount<Nested> == 4);
  CHECK(reflect::fieldCount<Vec2i> == 2);
  int n = 0;
  uint64_t sum = 0;
  reflect::forEachField(Padded{}, [&](const auto& f) {
    ++n;
    sum = sum * 10 + static_cast<uint64_t>(f);
  });
  CHECK(n == 3);
  CHECK(sum == 123);

  CHECK_FALSE(reflect::isPacked<Padded>);
  CHECK(reflect::isPacked<Vec2i>);
  const auto& m = reflect::podMask<Padded>();
  REQUIRE(sizeof(Padded) == 12);
  const uint8_t want[12] = {0xFF, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};
  CHECK(std::memcmp(m.data(), want, 12) == 0);
  // Nested structs and arrays of them are followed.
  const auto& nm = reflect::podMask<Nested>();
  int data = 0;
  for (uint8_t b : nm) data += b ? 1 : 0;
  CHECK(data == 1 + 7 * 3 + 8);
}

TEST_CASE("the byte writer blanks padding, so equal values give equal bytes") {
  alignas(Padded) uint8_t dirty1[sizeof(Padded)], dirty2[sizeof(Padded)];
  std::memset(dirty1, 0xAA, sizeof(dirty1));
  std::memset(dirty2, 0x55, sizeof(dirty2));
  Padded* a = new (dirty1) Padded;  // default-initialised: the padding keeps the rubbish
  Padded* b = new (dirty2) Padded;
  a->b = b->b = 77;
  ByteWriter wa, wb;
  wa.pod(*a);
  wb.pod(*b);
  std::vector<Padded> va{*a, *a}, vb{*b, *b};
  std::memcpy(static_cast<void*>(va.data()), dirty1, sizeof(Padded));
  std::memcpy(static_cast<void*>(vb.data()), dirty2, sizeof(Padded));
  va[0].b = vb[0].b = 5;
  va[0].a = vb[0].a = 1;
  va[0].c = vb[0].c = 3;
  wa.podVector(va);
  wb.podVector(vb);
  CHECK(wa.bytes() == wb.bytes());
  // And it still reads back.
  ByteReader r(wa.bytes());
  const Padded back = r.pod<Padded>();
  CHECK(back.a == 1);
  CHECK(back.b == 77);
  CHECK(back.c == 3);
  const std::vector<Padded> list = r.podVector<Padded>();
  REQUIRE(list.size() == 2);
  CHECK(list[0].b == 5);
  CHECK(r.atEnd());
}

TEST_CASE("chunks: sections are found by tag, unknown ones are stepped over, damage is refused") {
  ByteWriter w;
  w.u32(0xC0FFEE);
  ChunkWriter cw(w);
  cw.begin(fourcc("ONE "), 1);
  w.str("first");
  cw.end();
  cw.begin(fourcc("NEW!"), 7, chunk::kRequired);
  for (int i = 0; i < 100; ++i) w.u32(static_cast<uint32_t>(i));
  cw.end();
  cw.begin(fourcc("TWO "), 2);
  w.i32(-5);
  cw.end();
  w.u8(42);  // whatever follows the chunks

  ByteReader r(w.bytes());
  CHECK(r.u32() == 0xC0FFEE);
  ChunkReader cr;
  REQUIRE(cr.parse(r));
  CHECK(r.u8() == 42);
  CHECK(r.atEnd());
  REQUIRE(cr.chunks().size() == 3);
  const ChunkInfo* one = cr.find(fourcc("ONE "));
  const ChunkInfo* two = cr.find(fourcc("TWO "));
  const ChunkInfo* nw = cr.find(fourcc("NEW!"));
  REQUIRE(one);
  REQUIRE(two);
  REQUIRE(nw);
  CHECK(cr.find(fourcc("NONE")) == nullptr);
  ByteReader r1 = one->reader();
  CHECK(r1.str() == "first");
  CHECK(two->version == 2);
  ByteReader r2 = two->reader();
  CHECK(r2.i32() == -5);
  CHECK(nw->size == 400);
  CHECK((nw->flags & chunk::kRequired) != 0);
  CHECK(nw->name() == "NEW!");
  CHECK(fourccName(fourcc("AI  ")) == "AI");

  // Cut short: refused, never read past the end.
  std::vector<uint8_t> cut(w.bytes().begin(), w.bytes().begin() + 60);
  ByteReader rc(cut);
  rc.u32();
  ChunkReader bad;
  CHECK_FALSE(bad.parse(rc));
}

TEST_CASE("a state saved by state version 8 (before sections) still loads and carries on") {
  Rules R;
  SaveFile f;
  std::string err;
  REQUIRE_MESSAGE(readSaveFile(std::string(LEGION_CONTENT_DIR) + "../tests/data/sim8_three_sides.lsav", f, false, err), err);
  const nlohmann::json meta = nlohmann::json::parse(f.metaJson);
  MapFile mf;
  REQUIRE(decodeMap(f.mapBytes, mf, err));
  std::vector<std::string> savedTypes;
  for (const auto& t : meta["types"]) savedTypes.push_back(t.get<std::string>());
  Simulation sim(mf.map, R.types, R.weapons, 1);
  ByteReader r(f.stateBytes);
  bool remapped = false;
  REQUIRE_MESSAGE(sim.loadState(r, err, &savedTypes, &remapped), err);
  CHECK(r.atEnd());
  CHECK(sim.tickCount() == meta["tick"].get<uint32_t>());
  CHECK(sim.unitCount() == meta["units"].get<size_t>());
  // The checksum counts type numbers: only comparable while the rules still number the types the same way.
  if (!remapped) CHECK(std::to_string(sim.stateHash()) == meta["hash"].get<std::string>());
  CHECK(sim.ai().player(0).enabled);
  const size_t before = sim.unitCount();
  for (int i = 0; i < 15 * 20; ++i) sim.tick();
  CHECK(sim.unitCount() > 0);
  MESSAGE("old save: ", before, " units at load, ", sim.unitCount(), " twenty seconds on");
  // A full battle with three computer players, saved and loaded in mid-flight:
  // nothing may be missing from the saved state.
  const TwinReport twin = twinCheck(sim, 15 * 90);
  CHECK_MESSAGE(twin.ok, twin.text);
  // Saved again it is in sections, and that loads to the same state.
  ByteWriter w;
  sim.saveState(w);
  Map copy = mf.map;
  Simulation again(copy, R.types, R.weapons, 2);
  ByteReader r2(w.bytes());
  REQUIRE_MESSAGE(again.loadState(r2, err), err);
  CHECK(again.stateHash() == sim.stateHash());
  CHECK(again.digest().total == sim.digest().total);
}

TEST_CASE("saved state is canonical: save, load, save gives the same bytes, section by section") {
  Rules R;
  Map map(64, 64);
  map.generateCountryside(7);
  Simulation sim(map, R.types, R.weapons, 3);
  skirmish(sim, R);
  for (int i = 0; i < 15 * 25; ++i) sim.tick();
  ByteWriter a;
  sim.saveState(a);
  Map copy = map;
  Simulation loaded(copy, R.types, R.weapons, 99);
  ByteReader r(a.bytes());
  std::string err;
  REQUIRE_MESSAGE(loaded.loadState(r, err), err);
  CHECK(r.atEnd());
  CHECK(loaded.loadNotes().empty());
  ByteWriter b;
  loaded.saveState(b);
  CHECK(a.bytes() == b.bytes());
  const auto da = sim.digest(), db = loaded.digest();
  CHECK(da.total == db.total);
  CHECK(Simulation::digestDifference(da, db).empty());
  CHECK(da.sections.size() == Simulation::stateSections().size());
  // A change shows up in the section of the system it belongs to, and nowhere else.
  Entity victim = sim.registry().handle(sim.registry().pool<CUnit>().entities().front());
  sim.debugDamage(victim, 100);
  const auto diff = Simulation::digestDifference(sim.digest(), db);
  REQUIRE(diff.size() == 1);
  CHECK(diff[0] == "ECS");
}

TEST_CASE("a state with sections and components from a newer version loads; what it must understand it refuses") {
  Rules R;
  Map map(64, 64);
  map.generateCountryside(11);
  Simulation sim(map, R.types, R.weapons, 3);
  skirmish(sim, R);
  for (int i = 0; i < 15 * 8; ++i) sim.tick();
  ByteWriter w;
  sim.saveState(w);

  // Take the saved state apart and put it together again with additions.
  ByteReader r(w.bytes());
  const uint32_t magic = r.u32();
  ChunkReader cr;
  REQUIRE(cr.parse(r));
  auto rebuild = [&](bool unknownRequired) {
    ByteWriter out;
    out.u32(magic);
    ChunkWriter cw(out);
    for (const ChunkInfo& c : cr.chunks()) {
      cw.begin(c.tag, c.version, c.flags);
      if (c.tag == fourcc("ECS ")) {
        // The registry, then the pools: add one nobody knows, and shrink the
        // animation component to two bytes as if its third field were new.
        ByteReader er = c.reader();
        out.podVector(er.podVector<uint32_t>());
        out.podVector(er.podVector<uint8_t>());
        out.podVector(er.podVector<uint32_t>());
        out.u64(er.u64());
        const uint32_t pools = er.u32();
        out.u32(pools + 1);
        out.u32(fourcc("CXYZ"));
        out.u32(6);
        out.podVector(std::vector<uint32_t>{1, 2, 3});
        for (int i = 0; i < 18; ++i) out.u8(0xEE);
        for (uint32_t i = 0; i < pools; ++i) {
          const uint32_t tag = er.u32();
          const uint32_t size = er.u32();
          const std::vector<uint32_t> ents = er.podVector<uint32_t>();
          std::vector<uint8_t> data(ents.size() * size);
          er.raw(data.data(), data.size());
          out.u32(tag);
          if (tag == fourcc("CANI")) {
            REQUIRE(size == 3);
            out.u32(2);
            out.podVector(ents);
            for (size_t k = 0; k < ents.size(); ++k) out.raw(&data[k * 3], 2);
          } else {
            out.u32(size);
            out.podVector(ents);
            out.raw(data.data(), data.size());
          }
        }
        REQUIRE(er.ok());
      } else {
        out.raw(c.data, c.size);
      }
      cw.end();
    }
    cw.begin(fourcc("FUTR"), 3, unknownRequired ? chunk::kRequired : 0);
    out.str("something a later version keeps here");
    cw.end();
    return out;
  };

  {
    const ByteWriter newer = rebuild(false);
    Map copy = map;
    Simulation loaded(copy, R.types, R.weapons, 5);
    ByteReader lr(newer.bytes());
    std::string err;
    REQUIRE_MESSAGE(loaded.loadState(lr, err), err);
    CHECK(lr.atEnd());
    CHECK(loaded.loadNotes().size() == 2);  // the pool and the section
    CHECK(loaded.unitCount() == sim.unitCount());
    CHECK(loaded.stateHash() == sim.stateHash());
    for (int i = 0; i < 60; ++i) loaded.tick();
  }
  {
    const ByteWriter newer = rebuild(true);
    Map copy = map;
    Simulation loaded(copy, R.types, R.weapons, 5);
    ByteReader lr(newer.bytes());
    std::string err;
    CHECK_FALSE(loaded.loadState(lr, err));
    CHECK(err.find("newer version") != std::string::npos);
  }
  {  // A required section missing.
    ByteWriter out;
    out.u32(magic);
    ChunkWriter cw(out);
    for (const ChunkInfo& c : cr.chunks()) {
      if (c.tag == fourcc("SIDE")) continue;
      cw.begin(c.tag, c.version, c.flags);
      out.raw(c.data, c.size);
      cw.end();
    }
    Map copy = map;
    Simulation loaded(copy, R.types, R.weapons, 5);
    ByteReader lr(out.bytes());
    std::string err;
    CHECK_FALSE(loaded.loadState(lr, err));
    CHECK(err.find("SIDE") != std::string::npos);
  }
  {  // An optional one missing: that system starts fresh.
    ByteWriter out;
    out.u32(magic);
    ChunkWriter cw(out);
    for (const ChunkInfo& c : cr.chunks()) {
      if (c.tag == fourcc("LOGI") || c.tag == fourcc("TUTR")) continue;
      cw.begin(c.tag, c.version, c.flags);
      out.raw(c.data, c.size);
      cw.end();
    }
    Map copy = map;
    Simulation loaded(copy, R.types, R.weapons, 5);
    ByteReader lr(out.bytes());
    std::string err;
    REQUIRE_MESSAGE(loaded.loadState(lr, err), err);
    CHECK(loaded.stateHash() == sim.stateHash());
  }
}

TEST_CASE("twin check: a battle and its loaded copy stay identical; a difference is found and placed") {
  Rules R;
  Map map(64, 64);
  map.generateCountryside(21);
  Simulation sim(map, R.types, R.weapons, 8);
  skirmish(sim, R);
  for (int i = 0; i < 15 * 10; ++i) sim.tick();
  const uint32_t at = sim.tickCount();
  const TwinReport ok = twinCheck(sim, 15 * 30);
  CHECK_MESSAGE(ok.ok, ok.text);
  CHECK(ok.saveRoundTrip);
  CHECK(ok.ticksRun == 15 * 30);
  CHECK(sim.tickCount() == at + 15 * 30);

  // Now one of the two is told something the other isn't: caught on that tick.
  int calls = 0;
  const TwinReport bad = twinCheck(sim, 15 * 5, [&](Simulation& s, uint32_t tick) {
    if (tick == at + 15 * 30 + 20 && calls++ == 0) {
      Entity e = s.registry().handle(s.registry().pool<CUnit>().entities().front());
      s.debugDamage(e, 300);
    }
  });
  CHECK_FALSE(bad.ok);
  CHECK(bad.ticksRun == 21);
  REQUIRE_FALSE(bad.sections.empty());
  CHECK(bad.sections.front() == "ECS");
  MESSAGE(bad.text);
}

TEST_CASE("hash traces: recorded once a second, compared, and carried by replays") {
  Rules R;
  Map map(64, 64);
  map.generateCountryside(5);
  auto run = [&](uint64_t seed, int extraAt) {
    Map m = map;
    Simulation sim(m, R.types, R.weapons, seed);
    sim.startRecording();
    skirmish(sim, R);
    HashTrace trace(15);
    for (int i = 0; i < 15 * 20; ++i) {
      if (i == extraAt) spawn(sim, R.t("ger_rifleman"), 0, {20 * T, 20 * T}, 1);
      sim.tick();
      trace.record(sim);
    }
    CHECK(sim.hashLog().size() == trace.samples().size());
    CHECK(sim.hashLog().back().hash == trace.samples().back().hash);
    return trace;
  };
  const HashTrace a = run(4, -1), b = run(4, -1), c = run(4, 15 * 7 + 3);
  CHECK(a.samples().size() == 20);
  CHECK_FALSE(HashTrace::compare(a, b).found);
  const HashTrace::Mismatch m = HashTrace::compare(a, c);
  REQUIRE(m.found);
  CHECK(m.tick == 15 * 8);  // the first whole second after the extra man arrived

  ByteWriter w;
  a.write(w);
  ByteReader r(w.bytes());
  HashTrace back;
  REQUIRE(back.read(r));
  CHECK(back.samples().size() == a.samples().size());
  CHECK_FALSE(HashTrace::compare(a, back).found);

  // In a replay file.
  SaveFile f;
  f.metaJson = "{}";
  f.endTick = 300;
  f.hashes = a.samples();
  SaveFile g;
  std::string err;
  REQUIRE(decodeSaveFile(encodeSaveFile(f, true), g, true, err));
  REQUIRE(g.hashes.size() == 20);
  CHECK(g.hashes[7].hash == a.samples()[7].hash);
  CHECK(g.hashes[7].tick == a.samples()[7].tick);
}
