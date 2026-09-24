#pragma once

// FileName.h
// Atelier
//
// The rule for a preset's file name, as the player types it. Pure logic, no
// engine includes, so it is compiled into the plugin and into an offline
// test alike.
//
// The name becomes <name>.jslot in RaceMenu's preset folder, and RaceMenu
// builds that path with narrow-character string formatting. So the rule is
// the conservative one: printable ASCII only, none of the characters Windows
// refuses in a file name, no leading or trailing space or dot, no reserved
// device name, and a length cap. A name outside the rule is refused with the
// reason, never silently rewritten - a preset saved under a name the player
// did not type is a preset the player cannot find.

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace Atelier::Preset::FileName
{
	inline constexpr std::size_t kMaxLength = 64;

	// Empty when a_name is acceptable as typed; otherwise the reason, in
	// words the page can show as is.
	inline std::string Problem(std::string_view a_name)
	{
		if (a_name.empty()) {
			return "the name is empty";
		}
		if (a_name.size() > kMaxLength) {
			return "the name is longer than " + std::to_string(kMaxLength) + " characters";
		}
		for (const char c : a_name) {
			const auto u = static_cast<unsigned char>(c);
			if (u < 0x20 || u > 0x7E) {
				return "only letters, digits, spaces and simple punctuation (no accents)";
			}
			if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) {
				return std::string("the character ") + c + " is not allowed in a file name";
			}
		}
		if (a_name.front() == ' ' || a_name.back() == ' ') {
			return "the name starts or ends with a space";
		}
		if (a_name.front() == '.' || a_name.back() == '.') {
			return "the name starts or ends with a dot";
		}

		std::string upper(a_name);
		std::transform(upper.begin(), upper.end(), upper.begin(),
			[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		const auto dot = upper.find('.');
		const std::string stem = dot == std::string::npos ? upper : upper.substr(0, dot);
		static constexpr std::array<std::string_view, 22> kReserved = {
			"CON", "PRN", "AUX", "NUL",
			"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
		};
		for (const auto r : kReserved) {
			if (stem == r) {
				return "the name is reserved by Windows";
			}
		}
		return {};
	}

	// Case-insensitive, which is how the file system compares them.
	inline bool Same(std::string_view a_left, std::string_view a_right)
	{
		return a_left.size() == a_right.size() &&
		       std::equal(a_left.begin(), a_left.end(), a_right.begin(), [](char l, char r) {
				   return std::tolower(static_cast<unsigned char>(l)) == std::tolower(static_cast<unsigned char>(r));
			   });
	}
}
