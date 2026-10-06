// ListSig.h
// Lifecast
//
// A stale page does not announce itself: it loads, it draws, and it does
// not know the message it was sent. So every list the DLL sends for a
// pop-up carries a stamp - the FNV-1a signature and the byte length of the
// list's canonical text - and the page answers with the stamp it computed
// from what it received. The DLL logs both ends. A page too old to answer
// is logged as such after a few seconds, which is what a mismatch looks like
// from this side.
#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Core::ListSig
{
	// FNV-1a, 32 bit, over the bytes, as 8 lower-case hex digits. The page
	// computes the same over the UTF-8 of the text it rebuilt from the list.
	std::string Fnv1a(std::string_view a_bytes);

	// A name as it goes in a list: invalid UTF-8 bytes become '?' (the bridge's
	// JSON text cannot carry them) and tabs and line breaks become a space (they
	// are the canonical text's separators). Both ends of the stamp see this form.
	std::string Sanitize(std::string_view a_text);

	// Stamps a list the DLL is about to send. a_canonical is the list as text:
	// one line per item, fields separated by a tab, every line ending in a
	// newline - the page rebuilds the identical text from the items it gets.
	// Returns { "sig", "len", "n" } to put in the message, remembers what the
	// page is expected to answer, and logs the stamp when it changed.
	nlohmann::json Stamp(std::string_view a_key, std::string_view a_canonical, std::size_t a_count);

	// Registers the page's "list-sig" answer and the check for one that
	// never comes. Call once.
	void Install();
}
