// Panel.h
// Lifecast
//
// The panel's browser: created through Meridian UI's platform API
// (IUIPlatformAPI::AddOrGetBrowser), not through the Meridian.View extension.
// The platform browser is what carries the two-key focus and visibility
// chords (IBrowser::ToggleBrowserFocusByKeys / ToggleBrowserVisibleByKeys),
// which Meridian evaluates before a focused browser can swallow a key - the
// one way back to the game that does not depend on the page itself working.
// Meridian.View does not expose them.
//
// Lifetime: the browser is created once, on first use, and then shown and
// hidden - never released and re-created while the game runs. A re-created
// browser is a new instance that silently loses its chords and bindings, and
// the state of what the player has already edited lives in the page.

#pragma once

#include "MeridianUIAPI/IBrowser.h"

namespace Lifecast::Panel
{
	// The mod:// host is the folder under Data/MeridianUI/, and the browser
	// name is a process-wide key shared with every other Meridian consumer.
	inline constexpr auto kBrowserName = "Lifecast.Panel";
	inline constexpr auto kStartUrl = "mod://Lifecast/index.html";

	// Registers the panel's own bridge handlers. Call once from
	// SKSEPluginLoad; creates nothing.
	void Init();

	// Creates the browser if it does not exist yet, hidden. Returns false
	// when Meridian is unavailable, refused, or already shutting down. Call
	// on the game thread.
	bool Ensure();

	// Shows or hides the browser. Hiding also releases focus - Meridian's
	// own rule for SetBrowserVisible(false). Does nothing without a browser.
	void SetVisible(bool a_visible);
	bool IsVisible();

	// Registers the focus chord on the current browser: up to two DirectInput
	// scan codes, the second one 0 for a single key. Meridian evaluates it
	// before a focused browser can swallow the key, and consumes it - it
	// reaches neither the page nor the game. Registrations belong to one
	// browser instance, so this is called again every time the panel is
	// shown rather than once.
	void ArmFocusChord(std::uint32_t a_key1, std::uint32_t a_key2);

	// Removes the chord, so the keys reach the game again.
	void DisarmFocusChord();

	// Asks Meridian for focus. Meridian applies it at page load if the page
	// is not loaded yet, and it deposes whichever browser held focus before.
	// Only call this with a way out already armed.
	void RequestFocus();

	// Gives Meridian's focus up, so the keyboard and the mouse reach the game again.
	// Applied on the next frame (the read-back right after the call still says focused).
	void ReleaseFocus();

	// Whether this panel currently owns Meridian's focus.
	bool IsFocused();

	// Null before Ensure() succeeds and after Meridian's shutdown callback.
	Meridian::CEF::IBrowser* Browser();
}
