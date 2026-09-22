// Log.h
// Atelier
//
// Owns the plugin's spdlog setup, so every module logs the same way to the
// same file without knowing the details.
//
// Log file: Documents/My Games/Skyrim Special Edition/SKSE/Atelier.log
//
// Usage from any module (spdlog is pulled in by PCH.h):
//     spdlog::info("something happened: {}", value);

#pragma once

namespace Atelier::Core::Log
{
	// Initializes the plugin log file and installs it as the default logger.
	// Must be called once, first thing in SKSEPluginLoad, before any spdlog::
	// call.
	//
	// Returns false if the SKSE log directory could not be resolved. In that
	// case there is nothing to write to and no way to report it, so the
	// caller should abort load rather than run blind.
	bool Init();
}
