#include "core/object/object.h"
#include "core/object/signal.h"

#include <algorithm>

namespace eda {

bool Object::set(const std::string&, const Variant&) { return false; }
Variant Object::get(const std::string&) const { return Variant(); }

void Object::set_meta(const std::string& key, const Variant& value) { metadata_[key] = value; }
Variant Object::get_meta(const std::string& key, const Variant& default_value) const {
    auto it = metadata_.find(key);
    return it == metadata_.end() ? default_value : it->second;
}
bool Object::has_meta(const std::string& key) const { return metadata_.find(key) != metadata_.end(); }

Object::~Object() { _signal_disconnect_all(); }

void Object::_signal_disconnect_all() {
    for (SignalBase* s : connected_signals_) {
        s->_disconnect_object(this);
    }
    connected_signals_.clear();
}

void Object::_signal_register_connection(SignalBase* s) {
    if (std::find(connected_signals_.begin(), connected_signals_.end(), s) == connected_signals_.end()) {
        connected_signals_.push_back(s);
    }
}

void Object::_signal_unregister_connection(SignalBase* s) {
    auto it = std::find(connected_signals_.begin(), connected_signals_.end(), s);
    if (it != connected_signals_.end()) connected_signals_.erase(it);
}

}  // namespace eda
