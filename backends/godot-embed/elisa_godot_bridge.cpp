// Hand-written GDExtension binding ElisaMaze to the Elisa-compiled maze game.
//
// godot-cpp ships no branch for this Godot, so the bridge binds the C
// interface directly against the version-matched header the installed binary
// dumps itself (see scripts/fetch_gdextension_header.py). All Variants crossing
// the boundary are INT (int64); the int32 narrowing happens at the libmaze ABI
// call, whose values are small by construction.
#include <stddef.h>
#include <stdint.h>

#include "gdextension_interface.h"
#include "libmaze.h"

// ---------------------------------------------------------------- interface

static GDExtensionInterfaceClassdbRegisterExtensionClass6 FnRegisterClass;
static GDExtensionInterfaceClassdbRegisterExtensionClassMethod FnRegisterMethod;
static GDExtensionInterfaceClassdbUnregisterExtensionClass FnUnregisterClass;
static GDExtensionInterfacePrintError FnPrintError;
static GDExtensionInterfaceVariantGetType FnVariantType;
static GDExtensionInterfaceGetVariantFromTypeConstructor FnGetVariantCtor;
static GDExtensionInterfaceGetVariantToTypeConstructor FnGetTypeCtor;
static GDExtensionInterfaceStringNameNewWithUtf8Chars FnNameNew;
static GDExtensionInterfaceStringNewWithUtf8Chars FnStringNew;
static GDExtensionInterfaceVariantGetPtrDestructor FnGetDestructor;

static GDExtensionVariantFromTypeConstructorFunc CtorIntFrom;
static GDExtensionTypeFromVariantConstructorFunc CtorIntTo;
static GDExtensionPtrDestructor DtorStringName;
static GDExtensionPtrDestructor DtorString;

// Godot's String and StringName are one pointer wide on 64-bit; these are the
// storage the interface constructors write into, not data this library reads.
struct HostString {
	uint8_t bytes[8];
};
struct HostStringName {
	uint8_t bytes[8];
};

static HostStringName BoundClassName;
static int NamesLive = 0;
static GDExtensionClassLibraryPtr BoundLibrary = nullptr;

static void report(const char *what, const char *file, int32_t line) {
	if (FnPrintError != nullptr) {
		FnPrintError("elisa_godot_bridge", what, file, line, 0);
	}
}

static int load_api(GDExtensionInterfaceGetProcAddress get_proc) {
	FnRegisterClass = (GDExtensionInterfaceClassdbRegisterExtensionClass6)get_proc("classdb_register_extension_class6");
	FnRegisterMethod = (GDExtensionInterfaceClassdbRegisterExtensionClassMethod)get_proc("classdb_register_extension_class_method");
	FnUnregisterClass = (GDExtensionInterfaceClassdbUnregisterExtensionClass)get_proc("classdb_unregister_extension_class");
	FnPrintError = (GDExtensionInterfacePrintError)get_proc("print_error");
	FnVariantType = (GDExtensionInterfaceVariantGetType)get_proc("variant_get_type");
	FnGetVariantCtor = (GDExtensionInterfaceGetVariantFromTypeConstructor)get_proc("get_variant_from_type_constructor");
	FnGetTypeCtor = (GDExtensionInterfaceGetVariantToTypeConstructor)get_proc("get_variant_to_type_constructor");
	FnNameNew = (GDExtensionInterfaceStringNameNewWithUtf8Chars)get_proc("string_name_new_with_utf8_chars");
	FnStringNew = (GDExtensionInterfaceStringNewWithUtf8Chars)get_proc("string_new_with_utf8_chars");
	FnGetDestructor = (GDExtensionInterfaceVariantGetPtrDestructor)get_proc("variant_get_ptr_destructor");
	if (FnRegisterClass == nullptr || FnRegisterMethod == nullptr || FnUnregisterClass == nullptr ||
	    FnVariantType == nullptr ||
	    FnGetVariantCtor == nullptr || FnGetTypeCtor == nullptr || FnNameNew == nullptr ||
	    FnStringNew == nullptr || FnGetDestructor == nullptr) {
		return 0;
	}
	CtorIntFrom = FnGetVariantCtor(GDEXTENSION_VARIANT_TYPE_INT);
	CtorIntTo = FnGetTypeCtor(GDEXTENSION_VARIANT_TYPE_INT);
	DtorStringName = FnGetDestructor(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
	DtorString = FnGetDestructor(GDEXTENSION_VARIANT_TYPE_STRING);
	return CtorIntFrom != nullptr && CtorIntTo != nullptr && DtorStringName != nullptr && DtorString != nullptr;
}

// ------------------------------------------------------------------ methods

struct BoundMethod {
	const char *name;
	int32_t argc;
	const char *arg_names[2];
	int32_t (*fn0)();
	int32_t (*fn1)(int32_t);
	int32_t (*fn2)(int32_t, int32_t);
};

static int32_t dispatch_bound(const BoundMethod *method, int64_t a, int64_t b) {
	if (method->argc == 0 && method->fn0 != nullptr) {
		return method->fn0();
	}
	if (method->argc == 1 && method->fn1 != nullptr) {
		return method->fn1((int32_t)a);
	}
	if (method->argc == 2 && method->fn2 != nullptr) {
		return method->fn2((int32_t)a, (int32_t)b);
	}
	return 0;
}

static void bound_call(void *method_userdata, GDExtensionClassInstancePtr /*instance*/,
                       const GDExtensionConstVariantPtr *args, GDExtensionInt argument_count,
                       GDExtensionVariantPtr ret, GDExtensionCallError *err) {
	const BoundMethod *method = (const BoundMethod *)method_userdata;
	if (argument_count < method->argc) {
		err->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		err->argument = (int32_t)argument_count;
		err->expected = method->argc;
		return;
	}
	if (argument_count > method->argc) {
		err->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		err->argument = method->argc;
		err->expected = method->argc;
		return;
	}
	int64_t values[2] = {0, 0};
	for (int32_t i = 0; i < method->argc; ++i) {
		if (FnVariantType(args[i]) != GDEXTENSION_VARIANT_TYPE_INT) {
			err->error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			err->argument = i;
			err->expected = (int32_t)GDEXTENSION_VARIANT_TYPE_INT;
			return;
		}
		CtorIntTo(&values[i], (GDExtensionVariantPtr)args[i]);
	}
	int64_t out = dispatch_bound(method, values[0], values[1]);
	CtorIntFrom(ret, &out);
	err->error = GDEXTENSION_CALL_OK;
	err->argument = 0;
	err->expected = 0;
}

static void bound_ptrcall(void *method_userdata, GDExtensionClassInstancePtr /*instance*/,
                          const GDExtensionConstTypePtr *args, GDExtensionTypePtr ret) {
	const BoundMethod *method = (const BoundMethod *)method_userdata;
	int64_t values[2] = {0, 0};
	for (int32_t i = 0; i < method->argc; ++i) {
		values[i] = *(const int64_t *)args[i];
	}
	int64_t out = dispatch_bound(method, values[0], values[1]);
	*(int64_t *)ret = out;
}

static const BoundMethod kMethods[] = {
	{"maze_start", 0, {nullptr, nullptr}, maze_start, nullptr, nullptr},
	{"maze_step", 1, {"direction", nullptr}, nullptr, maze_step, nullptr},
	{"maze_player_x", 0, {nullptr, nullptr}, maze_player_x, nullptr, nullptr},
	{"maze_player_y", 0, {nullptr, nullptr}, maze_player_y, nullptr, nullptr},
	{"maze_status", 0, {nullptr, nullptr}, maze_status, nullptr, nullptr},
	{"maze_lives", 0, {nullptr, nullptr}, maze_lives, nullptr, nullptr},
	{"maze_width", 0, {nullptr, nullptr}, maze_width, nullptr, nullptr},
	{"maze_height", 0, {nullptr, nullptr}, maze_height, nullptr, nullptr},
	{"maze_wall_count", 0, {nullptr, nullptr}, maze_wall_count, nullptr, nullptr},
	{"maze_goal_x", 0, {nullptr, nullptr}, maze_goal_x, nullptr, nullptr},
	{"maze_goal_y", 0, {nullptr, nullptr}, maze_goal_y, nullptr, nullptr},
	{"maze_is_wall", 2, {"x", "y"}, nullptr, nullptr, maze_is_wall},
};
static const int32_t kMethodCount = (int32_t)(sizeof(kMethods) / sizeof(kMethods[0]));

// -------------------------------------------------------------------- class

static void make_name(HostStringName *out, const char *text) {
	FnNameNew(out, text);
}

static void destroy_name(HostStringName *name) {
	DtorStringName(name);
}

static void register_one_method(const BoundMethod *method) {
	HostStringName empty_name;
	FnNameNew((GDExtensionUninitializedStringNamePtr)&empty_name, "");
	HostString empty_text;
	FnStringNew((GDExtensionUninitializedStringPtr)&empty_text, "");
	GDExtensionPropertyInfo ret_info;
	ret_info.type = GDEXTENSION_VARIANT_TYPE_INT;
	HostStringName ret_name;
	make_name(&ret_name, "");
	ret_info.name = &ret_name;
	HostStringName ret_class;
	make_name(&ret_class, "");
	ret_info.class_name = &ret_class;
	ret_info.hint = 0;
	ret_info.hint_string = (GDExtensionStringPtr)&empty_text;
	ret_info.usage = 6; // PROPERTY_USAGE_DEFAULT
	GDExtensionPropertyInfo arg_infos[2];
	HostStringName arg_names[2];
	for (int32_t i = 0; i < method->argc; ++i) {
		make_name(&arg_names[i], method->arg_names[i]);
		arg_infos[i].type = GDEXTENSION_VARIANT_TYPE_INT;
		arg_infos[i].name = &arg_names[i];
		arg_infos[i].class_name = &ret_class;
		arg_infos[i].hint = 0;
		arg_infos[i].hint_string = (GDExtensionStringPtr)&empty_text;
		arg_infos[i].usage = 6;
	}
	GDExtensionClassMethodArgumentMetadata arg_meta[2] = {
		GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64,
		GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64,
	};
	HostStringName method_name;
	make_name(&method_name, method->name);
	GDExtensionClassMethodInfo info;
	info.name = &method_name;
	info.method_userdata = (void *)method;
	info.call_func = &bound_call;
	info.ptrcall_func = &bound_ptrcall;
	info.method_flags = GDEXTENSION_METHOD_FLAG_STATIC;
	info.has_return_value = 1;
	info.return_value_info = &ret_info;
	info.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64;
	info.argument_count = (uint32_t)method->argc;
	info.arguments_info = method->argc == 0 ? nullptr : arg_infos;
	info.arguments_metadata = method->argc == 0 ? nullptr : arg_meta;
	info.default_argument_count = 0;
	info.default_arguments = nullptr;
	FnRegisterMethod(BoundLibrary, &BoundClassName, &info);
	destroy_name(&method_name);
	for (int32_t i = 0; i < method->argc; ++i) {
		destroy_name(&arg_names[i]);
	}
	destroy_name(&ret_name);
	destroy_name(&ret_class);
	destroy_name(&empty_name);
	DtorString((GDExtensionTypePtr)&empty_text);
}

static void on_initialize(void * /*userdata*/, GDExtensionInitializationLevel level) {
	if (level != GDEXTENSION_INITIALIZATION_SCENE) {
		return;
	}
	make_name(&BoundClassName, "ElisaMaze");
	HostStringName parent_name;
	make_name(&parent_name, "RefCounted");
	NamesLive = 1;
	GDExtensionClassCreationInfo6 info;
	for (size_t i = 0; i < sizeof(info); ++i) {
		((unsigned char *)&info)[i] = 0;
	}
	info.is_virtual = 0;
	info.is_abstract = 1;
	info.is_exposed = 1;
	info.is_runtime = 0;
	info.icon_path = nullptr;
	info.create_instance_func = nullptr;
	info.free_instance_func = nullptr;
	info.get_virtual_func = nullptr;
	info.class_userdata = nullptr;
	FnRegisterClass(BoundLibrary, &BoundClassName, &parent_name, &info);
	destroy_name(&parent_name);
	for (int32_t i = 0; i < kMethodCount; ++i) {
		register_one_method(&kMethods[i]);
	}
}

static void on_deinitialize(void * /*userdata*/, GDExtensionInitializationLevel level) {
	if (level != GDEXTENSION_INITIALIZATION_SCENE) {
		return;
	}
	FnUnregisterClass(BoundLibrary, &BoundClassName);
	if (NamesLive != 0) {
		destroy_name(&BoundClassName);
		NamesLive = 0;
	}
}

extern "C" {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("default")))
#endif
GDExtensionBool
gdextension_init(GDExtensionInterfaceGetProcAddress get_proc, GDExtensionClassLibraryPtr library,
                 GDExtensionInitialization *init) {
	if (get_proc == nullptr || library == nullptr || init == nullptr) {
		return 0;
	}
	if (load_api(get_proc) == 0) {
		report("missing GDExtension interface functions", __FILE__, __LINE__);
		return 0;
	}
	BoundLibrary = library;
	init->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
	init->userdata = nullptr;
	init->initialize = &on_initialize;
	init->deinitialize = &on_deinitialize;
	return 1;
}
} // extern "C"
