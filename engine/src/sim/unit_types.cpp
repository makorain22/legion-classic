#include "slave/sim/unit_types.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace slave {

namespace {
bool parseLocomotor(const std::string& s, Locomotor& out) {
  if (s == "foot") out = Locomotor::Foot;
  else if (s == "wheeled") out = Locomotor::Wheeled;
  else if (s == "tracked") out = Locomotor::Tracked;
  else return false;
  return true;
}
}  // namespace

bool UnitTypeRegistry::loadJson(const nlohmann::json& doc, std::string& error) {
  if (!doc.contains("units") || !doc["units"].is_array()) {
    error = "missing \"units\" array";
    return false;
  }
  for (const auto& u : doc["units"]) {
    UnitTypeDef d;
    try {
      d.id = u.at("id").get<std::string>();
      d.name = u.value("name", d.id);
      d.faction = u.value("faction", "");
      d.model = u.at("model").get<std::string>();
      d.speed = u.value("speed", d.speed);
      d.turnTicks = u.value("turnTicks", d.turnTicks);
      d.radius = u.value("radius", d.radius);
      d.animTicks = u.value("animTicks", d.animTicks);
      d.maxHealth = u.value("health", d.maxHealth);
      if (!parseLocomotor(u.value("locomotor", "tracked"), d.locomotor)) {
        error = "unit '" + d.id + "': unknown locomotor";
        return false;
      }
    } catch (const nlohmann::json::exception& e) {
      error = std::string("bad unit entry: ") + e.what();
      return false;
    }
    if (d.speed <= 0 || d.speed > 256 || d.radius <= 0 || d.animTicks <= 0) {
      error = "unit '" + d.id + "': speed, radius or animTicks out of range";
      return false;
    }
    if (find(d.id) >= 0) {
      error = "duplicate unit id '" + d.id + "'";
      return false;
    }
    types_.push_back(std::move(d));
  }
  return true;
}

bool UnitTypeRegistry::loadFile(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
  if (doc.is_discarded()) {
    error = path + ": invalid JSON";
    return false;
  }
  if (!loadJson(doc, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

int UnitTypeRegistry::find(const std::string& id) const {
  for (size_t i = 0; i < types_.size(); ++i)
    if (types_[i].id == id) return static_cast<int>(i);
  return -1;
}

}  // namespace slave
