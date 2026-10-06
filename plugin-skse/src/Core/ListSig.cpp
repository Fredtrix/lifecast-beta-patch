// ListSig.cpp
// Lifecast

#include "Core/ListSig.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Core::ListSig
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// How long a page gets to answer a stamp before the silence is itself
		// the finding. The page answers on the same tick it receives the list.
		constexpr auto kAnswerWithin = std::chrono::seconds(4);

		struct Expected
		{
			std::string       sig;
			std::size_t       len = 0;
			std::size_t       count = 0;
			Clock::time_point sentAt{};
			bool              answered = false;
			bool              reportedSilence = false;
			bool              diverged = false;
		};

		std::map<std::string, Expected> g_expected;

		void OnAnswer(const nlohmann::json& a_data)
		{
			if (!a_data.is_object() || !a_data.contains("k") || !a_data["k"].is_string() || !a_data.contains("sig") ||
				!a_data["sig"].is_string() || !a_data.contains("len") || !a_data["len"].is_number_unsigned()) {
				spdlog::warn("[list-sig] answer without k, sig and len - ignored");
				return;
			}
			const auto key = a_data["k"].get<std::string>();
			const auto sig = a_data["sig"].get<std::string>();
			const auto len = a_data["len"].get<std::size_t>();
			const auto at = g_expected.find(key);
			if (at == g_expected.end()) {
				spdlog::warn("[list-sig] {}: the page answered {}/{} for a list this DLL never stamped", key, sig, len);
				return;
			}
			auto& e = at->second;
			if (sig == e.sig && len == e.len) {
				e.answered = true;
				spdlog::info("[list-sig] {}: page and DLL agree - {} / {} bytes, {} items", key, sig, len, e.count);
			} else {
				// An answer that disagrees is still an answer: the silence check must not fire after it.
				e.diverged = true;
				spdlog::warn("[list-sig] {}: DIVERGE - DLL sent {} / {} bytes, the page computed {} / {} bytes", key,
					e.sig, e.len, sig, len);
			}
		}

		void OnTick()
		{
			const auto now = Clock::now();
			for (auto& [key, e] : g_expected) {
				if (!e.answered && !e.diverged && !e.reportedSilence && now - e.sentAt >= kAnswerWithin) {
					e.reportedSilence = true;
					spdlog::warn("[list-sig] {}: NO ANSWER in {} s for {} / {} bytes - the page is older than this "
								 "DLL or has not run the list yet",
						key, std::chrono::duration_cast<std::chrono::seconds>(kAnswerWithin).count(), e.sig, e.len);
				}
			}
		}
	}

	std::string Fnv1a(std::string_view a_bytes)
	{
		std::uint32_t h = 0x811C9DC5U;
		for (const char c : a_bytes) {
			h ^= static_cast<std::uint8_t>(c);
			h *= 0x01000193U;
		}
		return fmt::format("{:08x}", h);
	}

	std::string Sanitize(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (std::size_t i = 0; i < a_text.size();) {
			const auto c = static_cast<std::uint8_t>(a_text[i]);
			if (c == 0x09 || c == 0x0A || c == 0x0D) {
				out += ' ';
				++i;
				continue;
			}
			if (c < 0x80) {
				out += static_cast<char>(c);
				++i;
				continue;
			}
			const std::size_t follow = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
			bool              ok = follow > 0 && i + follow < a_text.size();
			for (std::size_t k = 1; ok && k <= follow; ++k) {
				ok = (static_cast<std::uint8_t>(a_text[i + k]) & 0xC0) == 0x80;
			}
			if (ok) {
				out.append(a_text.substr(i, follow + 1));
				i += follow + 1;
			} else {
				out += '?';
				++i;
			}
		}
		return out;
	}

	nlohmann::json Stamp(std::string_view a_key, std::string_view a_canonical, std::size_t a_count)
	{
		const auto sig = Fnv1a(a_canonical);
		auto&      e = g_expected[std::string(a_key)];
		if (e.sig != sig || e.len != a_canonical.size()) {
			e = {};
			e.sig = sig;
			e.len = a_canonical.size();
			e.count = a_count;
			e.sentAt = Clock::now();
			spdlog::info("[list-sig] {}: stamped {} / {} bytes, {} items", a_key, sig, a_canonical.size(), a_count);
		}
		return { { "sig", sig }, { "len", a_canonical.size() }, { "n", a_count } };
	}

	void Install()
	{
		Panel::Bridge::On("list-sig", &OnAnswer);
		Edit::Session::OnTick(&OnTick);
	}
}
