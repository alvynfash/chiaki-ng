// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CONTROLLERBACKEND_H
#define CHIAKI_CONTROLLERBACKEND_H

#include <SDL.h>

// Select the physical Sony HIDAPI report before player-slot allocation. MFI
// creates a synthetic second report during hotplug and provides neither a
// serial nor a device path that can reliably link it to the physical report.
inline bool IgnoreMacOSControllerAlias(SDL_JoystickGUID guid, SDL_GameControllerType type,
		bool ps4_hidapi_enabled, bool ps5_hidapi_enabled)
{
	return guid.data[14] == 'm'
		&& ((type == SDL_CONTROLLER_TYPE_PS4 && ps4_hidapi_enabled)
			|| (type == SDL_CONTROLLER_TYPE_PS5 && ps5_hidapi_enabled));
}

#endif
