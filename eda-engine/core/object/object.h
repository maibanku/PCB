#pragma once
#include "core/object/variant.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

class SignalBase;  // 前向：析构解绑用（定义在 signal.h）

class Object {
public:
    static constexpr int NOTIFICATION_PREDELETE = 0;
    static constexpr int NOTIFICATION_POSTINITIALIZE = 1;

    Object() = default;
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    virtual void notification(int) {}

    virtual std::string get_class() const { return "Object"; }
    virtual bool is_class(const std::string& name) const { return name == "Object"; }
    static std::string get_class_static() { return "Object"; }

    virtual bool set(const std::string& name, const Variant& value);
    virtual Variant get(const std::string& name) const;

    void set_meta(const std::string& key, const Variant& value);
    Variant get_meta(const std::string& key, const Variant& default_value = Variant()) const;
    bool has_meta(const std::string& key) const;

    virtual void _signal_disconnect_all();
    void _signal_register_connection(SignalBase* s);
    void _signal_unregister_connection(SignalBase* s);

private:
    std::unordered_map<std::string, Variant> metadata_;
    std::vector<SignalBase*> connected_signals_;
};

}  // namespace eda
