// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#define SDL_MAIN_HANDLED
#include "../gui/include/controllerbackend.h"
#include <cstdio>

int main()
{
	struct Case {
		const char *name;
		const char *guid;
		SDL_GameControllerType type;
		bool ps4, ps5, ignored;
	};
	const Case cases[] = {
		// These GUIDs come from the reconnect that created pads 1 and 2.
		{"physical DS4 retained", "03008fe54c050000c405000000016800", SDL_CONTROLLER_TYPE_PS4, true, true, false},
		{"synthetic DS4 rejected before physical report arrives", "05009a3b4c050000cc090000c5b96d04", SDL_CONTROLLER_TYPE_PS4, true, true, true},
		{"physical DualSense retained", "030057564c050000e60c000000016800", SDL_CONTROLLER_TYPE_PS5, true, true, false},
		{"synthetic DualSense rejected", "050057564c050000e60c00001be36d04", SDL_CONTROLLER_TYPE_PS5, true, true, true},
		{"Apple Xbox report retained", "050000005e040000e002000000006d03", SDL_CONTROLLER_TYPE_XBOXONE, true, true, false},
		{"Apple fallback when all HIDAPI is disabled", "05009a3b4c050000cc090000c5b96d04", SDL_CONTROLLER_TYPE_PS4, false, false, false},
		{"Apple fallback when DS4 HIDAPI is disabled", "05009a3b4c050000cc090000c5b96d04", SDL_CONTROLLER_TYPE_PS4, false, true, false},
		{"Apple fallback when DualSense HIDAPI is disabled", "050057564c050000e60c00001be36d04", SDL_CONTROLLER_TYPE_PS5, true, false, false},
		{"IOKit fallback retained", "030000004c050000c405000000010000", SDL_CONTROLLER_TYPE_PS4, true, true, false},
		// Backend selection must not collapse separate physical pads of the same model.
		{"second physical DS4 retained", "03008fe54c050000c405000000016800", SDL_CONTROLLER_TYPE_PS4, true, true, false},
	};
	for(const Case &test : cases)
	{
		bool ignored = IgnoreMacOSControllerAlias(SDL_JoystickGetGUIDFromString(test.guid), test.type,
				test.ps4, test.ps5);
		if(ignored != test.ignored)
		{
			std::fprintf(stderr, "Failed controller backend regression: %s\n", test.name);
			return 1;
		}
	}
	std::puts("10 controller backend regressions passed");
	return 0;
}
