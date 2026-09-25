// ObodyLink.h
// Atelier
//
// Whether OBody NG answers, and its plugin interface once it does.
//
// OBody is never a dependency: without it the body section of the panel
// simply does not exist - no error, no warning, no requirement. So the
// question asked here is not "is OBody installed" but "does OBody answer",
// and the only thing that answers it is OBody's own plugin interface:
//
//   when      kPostPostLoad, the moment OBody's header documents
//   question  a RequestPluginInterface message dispatched to "OBody"
//   answer    the pointer OBody writes back - never the Dispatch return
//             value, whose meaning for a missing receiver is not documented
//   safe      only after the OBodyIsReady event, and until
//             OBodyIsNoLongerReady. The pointer can arrive minutes before
//             the event (measured: 63 s in one boot, 3min50s in another), so
//             the wait is an event and never a timer.
//
// Before dispatching, the module checks that a module named OBody.dll is
// loaded. That check is not the answer: it only keeps the player's log clean.
// Dispatching to a receiver that is not there makes CommonLib write a
// "Failed to dispatch" warning into this plugin's log, under this plugin's
// name, for the normal case of a player who never installed OBody. The answer
// is still the pointer.
//
// The files OBody ships are also looked at, once the data is loaded, and the
// result only goes to the log: it separates "not installed" from "installed
// and did not answer", two causes of the same null. It never decides what
// the panel shows.

#pragma once

namespace OBody::API
{
	class IPluginInterface;
}

namespace Atelier::Core::ObodyLink
{
	// Asks OBody for its interface. Call once, at kPostPostLoad. Safe to call
	// twice (the second call does nothing).
	void Request();

	// Logs which of OBody's files are present - diagnostic only. Call once,
	// at kDataLoaded.
	void ReportFiles();

	// The interface while OBody says it is ready; nullptr before the
	// OBodyIsReady event, after OBodyIsNoLongerReady, and when OBody never
	// answered. Call on the game thread.
	OBody::API::IPluginInterface* Ready();

	// Runs on the game thread each time the answer of Ready() changes.
	using Callback = void (*)();
	void OnReadinessChanged(Callback a_fn);
}
