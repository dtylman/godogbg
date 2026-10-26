/*
 * GDExtension entry point: registers the Gnubg class and its singleton.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>

#include "gnubg_godot.h"

static Gnubg *gnubg_singleton = nullptr;

static void
initialize_godogbg(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;
    GDREGISTER_CLASS(Gnubg);
    gnubg_singleton = memnew(Gnubg);
    Engine::get_singleton()->register_singleton("Gnubg", gnubg_singleton);
}

static void
uninitialize_godogbg(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;
    Engine::get_singleton()->unregister_singleton("Gnubg");
    memdelete(gnubg_singleton);
    gnubg_singleton = nullptr;
}

extern "C" {

GDExtensionBool GDE_EXPORT
godogbg_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                     GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
{
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_godogbg);
    init_obj.register_terminator(uninitialize_godogbg);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
