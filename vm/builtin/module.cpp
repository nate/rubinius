#include "builtin/class.hpp"
#include "builtin/module.hpp"
#include "objectmemory.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/lookuptable.hpp"
#include "builtin/methodtable.hpp"
#include "builtin/symbol.hpp"
#include "builtin/string.hpp"

namespace rubinius {

  Module* Module::create(STATE) {
    Module* mod = (Module*)state->new_object(G(module));

    SET(mod, name, Qnil);
    SET(mod, superclass, Qnil);

    mod->setup(state);

    return mod;
  }

  Module* Module::new_instance(STATE, OBJECT self) {
    Module* module = Module::create(state);

    SET(module, klass, self);

    return module;
  }

  void Module::setup(STATE) {
    SET(this, constants, LookupTable::create(state));
    SET(this, method_table, MethodTable::create(state));
  }

  void Module::setup(STATE, const char* str, Module* under) {
    setup(state, state->symbol(str), under);
  }

  void Module::setup(STATE, SYMBOL name, Module* under) {
    if(!under) under = G(object);

    SET(this, constants, LookupTable::create(state));
    SET(this, method_table, MethodTable::create(state));
    SET(this, name, name);
    under->set_const(state, name, this);
  }

  void Module::set_name(STATE, Module* under, SYMBOL name) {
    String* cur = under->name->to_str(state);
    String* str_name = cur->add(state, "::")->append(state,
        name->to_str(state));

    SET(this, name, str_name->to_sym(state));
  }

  void Module::set_const(STATE, OBJECT sym, OBJECT val) {
    constants->store(state, sym, val);
  }

  void Module::set_const(STATE, const char* name, OBJECT val) {
    constants->store(state, state->symbol(name), val);
  }

  OBJECT Module::get_const(STATE, SYMBOL sym) {
    return constants->fetch(state, sym);
  }

  OBJECT Module::get_const(STATE, SYMBOL sym, bool* found) {
    return constants->fetch(state, sym, found);
  }

  OBJECT Module::get_const(STATE, const char* sym) {
    return get_const(state, state->symbol(sym));
  }
}