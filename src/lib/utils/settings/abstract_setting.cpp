#include "abstract_setting.hpp"

#include <memory>
#include <string>

#include "hyrise.hpp"

namespace hyrise {

AbstractSetting::AbstractSetting(const std::string& init_name) : name(init_name) {}

void AbstractSetting::register_at_settings_manager() {
  Hyrise::get().settings_manager._add(std::static_pointer_cast<AbstractSetting>(shared_from_this()));
}

void AbstractSetting::unregister_at_settings_manager() {
  Hyrise::get().settings_manager._remove(name);
}

}  // namespace hyrise
