// Bridge.h
// Atelier
//
// The message channel between the panel page and this DLL, in both
// directions. Every section of the panel talks through here, so the shape of
// a message is fixed once and not per feature:
//
//     { "v": <protocol version>, "type": "<name>", "data": <any JSON> }
//
// Page to DLL: the page calls window.Atelier.send(message) - a fire-and-forget
// binding. It returns undefined at once; the page never waits on the DLL.
//
// DLL to page: Send() below pushes the same shape through a Meridian event
// function. The page subscribes once with window.Atelier.on("message", fn)
// and receives the message as JSON text.
//
// Why fire-and-forget both ways and no promise natives: a request that
// expects an answer becomes two messages, and the page stays responsive
// while the second one is on its way. Nothing on either side can block on
// the other.
//
// Every message that crosses is logged, raw, before it is interpreted. When
// a future section misbehaves, the log is where "what did the page actually
// send" gets answered.

#pragma once

#include "MeridianUIAPI/JSTypes.h"

#include <nlohmann/json.hpp>

namespace Atelier::Panel::Bridge
{
	// Bumped when the message shape or the meaning of an existing type
	// changes. The page carries its own copy and says which one it speaks in
	// its "hello"; the DLL and the page ship together, but a player can
	// update one without the other.
	inline constexpr std::uint32_t kProtocolVersion = 1;

	// window.<kObjectName>.send(...) and window.<kObjectName>.on(...).
	inline constexpr auto kObjectName = "Atelier";
	inline constexpr auto kSendFunction = "send";
	inline constexpr auto kEventFunction = "on";

	// The single event name every DLL-to-page message travels under. The
	// page dispatches on the "type" field, not on the event name.
	inline constexpr auto kEventName = "message";

	// Handles one message type from the page. Runs on the game thread: the
	// send binding is registered with executeInGameThread, so a handler may
	// touch Skyrim state directly.
	using Handler = void (*)(const nlohmann::json& a_data);

	// Registers the handler for a message type. A second registration for
	// the same type replaces the first.
	void On(std::string_view a_type, Handler a_handler);

	// Pushes a message to the page. Dropped, with a log line, when there is
	// no browser to push to.
	void Send(std::string_view a_type, const nlohmann::json& a_data = nlohmann::json::object());

	// The bindings the browser has to be created with. The event function in
	// particular can only be passed at creation time - AddFunctionCallback
	// cannot add one later. The array and what it points to live for the
	// whole process.
	Meridian::JS::JSFuncInfo* const* Bindings();
	std::uint32_t BindingCount();
}
