#include <stdlib.h>
#include <stdbool.h>

#include <dlfcn.h>

#include <SDL2/SDL.h>

#include <unordered_map>

extern "C" {
#include "libdetour/src/libdetour.h"
}

#include "log.h"

DETOUR_DECL_TYPE(int, SDL_WaitEventTimeout_orig, SDL_Event *event, int timeout);
detour_ctx_t SDL_WaitEventTimeout_detour_ctx;

struct controller_state_entry{
	Sint16 ltrigger_real;
	Sint16 rtrigger_real;
	SDL_GameController *controller;
};
std::unordered_map<SDL_JoystickID, controller_state_entry> controller_state;

bool is_steam_input = false;

int SDL_WaitEventTimeout_hooked(SDL_Event *event, int timeout){
	//LOG("%s: begin\n", __func__);

	int orig_result;
	DETOUR_ORIG_GET(&SDL_WaitEventTimeout_detour_ctx, orig_result, SDL_WaitEventTimeout_orig, event, timeout);
	if (is_steam_input){
		return orig_result;
	}
	if (orig_result == 0){
		return orig_result;
	}
	if (event->type != SDL_CONTROLLERAXISMOTION){
		return orig_result;
	}
	if (event->caxis.axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && event->caxis.axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT){
		return orig_result;
	}

	auto entry = controller_state.find(event->caxis.which);
	if (entry == controller_state.end()){
		controller_state.insert_or_assign(event->caxis.which, controller_state_entry{0, 0, });
		entry = controller_state.find(event->caxis.which);
	}

	if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT){
		entry->second.ltrigger_real = event->caxis.value;
	}
	if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT){
		entry->second.rtrigger_real = event->caxis.value;
	}

	event->caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
	static const Sint32 neutral = SDL_JOYSTICK_AXIS_MAX / 2;
	Sint32 combined_trigger = entry->second.ltrigger_real - entry->second.rtrigger_real;
	event->caxis.value = neutral + combined_trigger / 2;

	//LOG("%s: mixed triggers into %d\n", __func__, event->caxis.value);

	return orig_result;
}

SDL_JoystickType SDLCALL (*pSDL_JoystickGetType)(SDL_Joystick *joystick) = NULL;

DETOUR_DECL_TYPE(Uint16, SDL_JoystickGetVendor_orig, SDL_Joystick *joystick);
detour_ctx_t SDL_JoystickGetVendor_detour_ctx;
Uint16 SDL_JoystickGetVendor_hooked(SDL_Joystick *joystick){
	// XXX no thread safety with libdetour, ideally the program only do this on the event polling thread
	Uint16 orig_result = 0;
	DETOUR_ORIG_GET(&SDL_JoystickGetVendor_detour_ctx, orig_result, SDL_JoystickGetVendor_orig, joystick);

	const static Uint16 x360_vendor = 0x045e;

	if (pSDL_JoystickGetType(joystick) == SDL_JOYSTICK_TYPE_GAMECONTROLLER){
		//LOG("%s: 0x%04x -> 0x%04x\n", __func__, orig_result, x360_vendor);
		return x360_vendor;
	}

	return orig_result;

}

DETOUR_DECL_TYPE(Uint16, SDL_JoystickGetProduct_orig, SDL_Joystick *joystick);
detour_ctx_t SDL_JoystickGetProduct_detour_ctx;
Uint16 SDL_JoystickGetProduct_hooked(SDL_Joystick *joystick){
	// XXX no thread safety with libdetour, ideally the program only do this on the event polling thread
	Uint16 orig_result = 0;
	DETOUR_ORIG_GET(&SDL_JoystickGetProduct_detour_ctx, orig_result, SDL_JoystickGetProduct_orig, joystick);

	const static Uint16 x360_product = 0x028e;

	if (pSDL_JoystickGetType(joystick) == SDL_JOYSTICK_TYPE_GAMECONTROLLER){
		//LOG("%s: 0x%04x -> 0x%04x\n", __func__, orig_result, x360_product);
		return x360_product;
	}

	return orig_result;
}

__attribute__((constructor))
int init(){
	init_log();

	#define STR(s) #s
	#define DET(name, lib_name) { \
		void *lib_handle = dlopen(STR(lib_name), RTLD_NOW); \
		if (lib_handle == NULL){ \
			LOG("%s: failed loading %s\n", __func__, STR(lib_name)); \
			exit(1); \
		} \
		void *func = dlsym(lib_handle, STR(name)); \
		if (func == NULL){ \
			LOG("%s: failed fetching %s from %s", __func__, STR(name), STR(lib_name)); \
			exit(1); \
		} \
		detour_init(&name##_detour_ctx, func, (void *) name##_hooked); \
		bool enable_status = detour_enable(&name##_detour_ctx); \
		if (!enable_status){ \
			LOG("%s: failed enabling %s %s detour\n", __func__, STR(lib_name), STR(name)); \
			exit(1); \
		} \
		LOG("%s: enabled detour for %s %s\n", __func__, STR(lib_name), STR(name)); \
	}

	DET(SDL_WaitEventTimeout, libSDL2-2.0.so.0);
	DET(SDL_JoystickGetVendor, libSDL2-2.0.so.0);
	DET(SDL_JoystickGetProduct, libSDL2-2.0.so.0);

	#define FETCH(name, lib_name, out) { \
		void *lib_handle = dlopen(STR(lib_name), RTLD_NOW); \
		if (lib_handle == NULL){ \
			LOG("%s: failed loading %s\n", __func__, STR(lib_name)); \
			exit(1); \
		} \
		*(void **)&out = dlsym(lib_handle, STR(name)); \
	}

	FETCH(SDL_JoystickGetType, libSDL2-2.0.so.0, pSDL_JoystickGetType);

	is_steam_input = getenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD") != NULL;
	if (is_steam_input){
		LOG("%s: steam input detected\n", __func__);
	}

	LOG("%s: ready\n", __func__);
	return 0;
}
