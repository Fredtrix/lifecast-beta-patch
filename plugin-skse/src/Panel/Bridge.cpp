// Bridge.cpp
// Atelier

#include "Panel/Bridge.h"

#include "Panel/Panel.h"

namespace Atelier::Panel::Bridge
{
	namespace
	{
		using json = nlohmann::json;

		// A slider dragged across the panel sends many messages a second.
		// The whole text is logged up to this length and cut after it, with
		// the full length stated, so a long message is visible as long
		// rather than silently shortened.
		constexpr std::size_t kLogCap = 400;

		std::string Clip(std::string_view a_text)
		{
			if (a_text.size() <= kLogCap) {
				return std::string(a_text);
			}
			return fmt::format("{}... [{} chars total]", a_text.substr(0, kLogCap), a_text.size());
		}

		std::unordered_map<std::string, Handler>& Handlers()
		{
			static std::unordered_map<std::string, Handler> s_handlers;
			return s_handlers;
		}

		// Meridian hands every argument over as a JSON document of its own:
		// a JS object arrives as its JSON text, and a JS string arrives
		// QUOTED. A page that calls send(JSON.stringify(msg)) instead of
		// send(msg) therefore delivers a JSON string whose content is the
		// real message. Both are accepted, and the log says which one
		// arrived - the difference is exactly what a bridge defect would
		// look like.
		json Decode(const char* a_arg, bool& a_wasStringWrapped)
		{
			a_wasStringWrapped = false;

			auto outer = json::parse(a_arg, nullptr, false);
			if (outer.is_discarded()) {
				return outer;
			}

			if (outer.is_string()) {
				a_wasStringWrapped = true;
				return json::parse(outer.get<std::string>(), nullptr, false);
			}

			return outer;
		}

		void Dispatch(const json& a_message)
		{
			if (!a_message.is_object() || !a_message.contains("type") || !a_message["type"].is_string()) {
				spdlog::warn("[bridge] page->dll: not a message (no string \"type\") - ignored");
				return;
			}

			const auto type = a_message["type"].get<std::string>();

			std::uint32_t version = 0;
			if (a_message.contains("v") && a_message["v"].is_number_unsigned()) {
				version = a_message["v"].get<std::uint32_t>();
			}

			static const json kEmpty = json::object();
			const json& data = a_message.contains("data") ? a_message["data"] : kEmpty;

			// "hello" is the one type that is understood across protocol
			// versions: it is how a mismatched page learns that it is
			// mismatched. Everything else requires the same version.
			if (type != "hello" && version != kProtocolVersion) {
				spdlog::warn(
					"[bridge] page->dll: type \"{}\" speaks protocol v{}, this DLL speaks v{} - ignored",
					type, version, kProtocolVersion);
				Send("error", { { "reason", "protocol" }, { "type", type }, { "expected", kProtocolVersion } });
				return;
			}

			const auto it = Handlers().find(type);
			if (it == Handlers().end()) {
				spdlog::warn("[bridge] page->dll: no handler for type \"{}\"", type);
				Send("error", { { "reason", "unknown-type" }, { "type", type } });
				return;
			}

			it->second(data);
		}

		// The send binding. Registered with executeInGameThread = true, so
		// this runs on the game thread and the pointers in a_args are valid
		// for the duration of the call.
		void OnPageSend(const char** a_args, int a_argsCount)
		{
			if (a_argsCount < 1 || !a_args || !a_args[0]) {
				spdlog::warn("[bridge] page->dll: send() called with {} arguments - expected 1", a_argsCount);
				return;
			}

			spdlog::info("[bridge] page->dll raw ({} arg{}): {}",
				a_argsCount, a_argsCount == 1 ? "" : "s", Clip(a_args[0]));

			if (a_argsCount > 1) {
				spdlog::warn("[bridge] page->dll: {} extra arguments ignored", a_argsCount - 1);
			}

			bool wasStringWrapped = false;
			const auto message = Decode(a_args[0], wasStringWrapped);

			if (message.is_discarded()) {
				spdlog::warn("[bridge] page->dll: argument is not valid JSON - ignored");
				return;
			}

			if (wasStringWrapped) {
				spdlog::info("[bridge] page->dll: the message arrived as a JSON STRING and was decoded twice");
			}

			Dispatch(message);
		}

		Meridian::JS::JSFuncInfo& SendBinding()
		{
			static Meridian::JS::JSFuncInfo s_info = [] {
				Meridian::JS::JSFuncInfo info{};
				info.objectName = kObjectName;
				info.funcName = kSendFunction;
				info.callbackData.callback = &OnPageSend;
				info.callbackData.executeInGameThread = true;
				return info;
			}();
			return s_info;
		}

		Meridian::JS::JSFuncInfo& EventBinding()
		{
			static Meridian::JS::JSFuncInfo s_info = [] {
				Meridian::JS::JSFuncInfo info{};
				info.objectName = kObjectName;
				info.funcName = kEventFunction;
				info.callbackData.isEventFunction = true;
				return info;
			}();
			return s_info;
		}
	}

	void On(std::string_view a_type, Handler a_handler)
	{
		Handlers()[std::string(a_type)] = a_handler;
	}

	void Send(std::string_view a_type, const nlohmann::json& a_data, bool a_logWhole)
	{
		const json message = {
			{ "v", kProtocolVersion },
			{ "type", a_type },
			{ "data", a_data },
		};
		const auto text = message.dump();

		auto* browser = Panel::Browser();
		if (!browser) {
			spdlog::warn("[bridge] dll->page DROPPED, no browser: {}", a_logWhole ? text : Clip(text));
			return;
		}

		spdlog::info("[bridge] dll->page: {}", a_logWhole ? text : Clip(text));
		browser->ExecEventFunction(kEventName, text.c_str());
	}

	Meridian::JS::JSFuncInfo* const* Bindings()
	{
		static Meridian::JS::JSFuncInfo* const s_bindings[] = { &SendBinding(), &EventBinding() };
		return s_bindings;
	}

	std::uint32_t BindingCount()
	{
		return 2;
	}
}
