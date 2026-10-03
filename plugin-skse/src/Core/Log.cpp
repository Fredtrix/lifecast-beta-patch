// Log.cpp
// Lifecast

#include "Log.h"

#include "Version.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <format>

namespace Lifecast::Core::Log
{
	bool Init()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			// No log directory - nothing to write to, and no way to report it.
			return false;
		}

		*path /= std::format("{}.log", Version::kProjectName);

		// Keep exactly one previous session as .log.1. The truncate flag
		// below wipes the file on every game launch, and development compares
		// one in-game round against the next - the baseline has to survive the
		// relaunch that produces the round it is compared with. One
		// generation is enough for that; errors are swallowed because
		// logging must never stop the plugin from loading.
		std::error_code ec;
		if (std::filesystem::exists(*path, ec)) {
			auto previous = *path;
			previous += ".1";
			std::filesystem::remove(previous, ec);
			std::filesystem::rename(*path, previous, ec);
		}

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

		// This log is the raw output of the plugin: every handshake answer
		// lands here, with its moment. Trace level even in release builds,
		// because while the project is being built the log IS the result.
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

		return true;
	}
}
