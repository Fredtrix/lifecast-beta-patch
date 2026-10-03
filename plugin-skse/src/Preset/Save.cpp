// Save.cpp
// Lifecast

#include "Preset/Save.h"

#include "Edit/Body.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Preset/Content.h"
#include "Preset/FileName.h"
#include "Preset/Guard.h"
#include "Preset/Jslot.h"
#include "Preset/Library.h"

#include <fstream>
#include <sstream>
#include <thread>

namespace Lifecast::Preset::Save
{
	namespace
	{
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		namespace fs = std::filesystem;

		// RaceMenu's native, and how long it gets to answer before the save
		// counts as not available.
		constexpr auto kClass = "CharGen";
		constexpr auto kFunction = "SaveCharacterPreset";
		constexpr auto kTimeout = std::chrono::seconds(5);

		struct FileState
		{
			bool           exists{ false };
			std::uintmax_t size{ 0 };
			fs::file_time_type written{};
		};

		enum class Kind
		{
			kSave,     // the file the player named
			kPreview,  // the preview file, before an overwrite
		};

		// One save at a time. Every field below is touched on the game
		// thread only; the VM callback and the timeout hop back to it.
		struct Pending
		{
			std::uint32_t id{ 0 };
			Kind          kind{ Kind::kSave };
			std::string   name;            // the player's name, for both kinds
			fs::path      path;            // the file this call writes
			FileState     before;          // of that file
			std::string   existing;        // the file overwritten, as read before the call; empty for a new one
			std::uint32_t replaced{ 0 };   // g_replaced when the call was made
			Clock::time_point sentAt{};
		};

		// A preview shown to the page and not answered yet.
		struct Preview
		{
			std::string name;
			std::string existing;  // the file it would overwrite, as it was
			std::string preview;   // what RaceMenu wrote for the live character
		};

		std::optional<Pending> g_pending;
		std::optional<Preview> g_preview;
		std::uint32_t          g_nextId = 0;
		std::size_t            g_saves = 0;
		std::uint32_t          g_replaced = 0;  // whole-character replacements (loads) so far

		FileState Stat(const fs::path& a_path)
		{
			FileState s;
			std::error_code ec;
			s.exists = fs::is_regular_file(a_path, ec);
			if (s.exists) {
				s.size = fs::file_size(a_path, ec);
				s.written = fs::last_write_time(a_path, ec);
			}
			return s;
		}

		fs::path PreviewPath() { return Library::Folder() / (std::string(Library::kPreviewName) + Library::kExtension); }

		json Parse(const std::string& a_text)
		{
			try {
				return json::parse(a_text);
			} catch (const std::exception&) {
				return nullptr;
			}
		}

		void Reply(const std::string& a_name, bool a_ok, const std::string& a_why, bool a_fallback,
			std::uintmax_t a_size = 0, long long a_ms = 0, json a_loss = json())
		{
			json data = {
				{ "name", a_name },
				{ "ok", a_ok },
				{ "why", a_why },
				{ "fallback", a_fallback },
				{ "size", a_size },
				{ "ms", a_ms },
			};
			if (!a_loss.is_null()) {
				data["loss"] = std::move(a_loss);
			}
			Panel::Bridge::Send("preset-saved", data, true);
		}

		// The preview file is ours to remove: it is not a preset, and the
		// player never named it. Removing is a file system operation - the
		// file is never rewritten.
		void RemovePreviewFile()
		{
			std::error_code ec;
			if (fs::remove(PreviewPath(), ec)) {
				spdlog::info("[preset-save] preview file removed");
			} else if (ec) {
				spdlog::warn("[preset-save] preview file NOT removed: {}", ec.message());
			}
		}

		// A preview waiting for an answer ends, and the page is ALWAYS told,
		// whoever ended it. The box the page may still show describes a file
		// that is gone: left on screen - the panel is only hidden when the
		// menu closes, and shows it again when the menu reopens - a click on
		// it would consent to something the DLL no longer holds.
		void Drop(const char* a_why)
		{
			if (!g_preview) {
				return;
			}
			const auto name = g_preview->name;
			g_preview.reset();
			spdlog::info("[preset-save] preview of {} dropped: {}", name, a_why);
			RemovePreviewFile();
			Panel::Bridge::Send("preset-save-preview-dropped", { { "name", name }, { "why", a_why } }, true);
		}

		// Off the game thread, after a save that landed: does the file RaceMenu
		// wrote read with the copied parser, and what did it gain and lose
		// against the file it replaced? Log only.
		void Inspect(fs::path a_path, std::string a_name, std::string a_existing, std::string a_preview)
		{
			std::string text;
			if (!Guard::ReadAll(a_path, text)) {
				spdlog::warn("[preset-save] {} cannot be read back", a_name);
				return;
			}
			const auto written = Parse(text);
			if (!a_existing.empty()) {
				const auto diff = Content::Compare(Parse(a_existing), written);
				spdlog::info("[preset-save] {} overwritten: by content {}", a_name, diff.ToJson().dump());
			}
			// The preview is what the page showed. After a confirmation the
			// file IS the preview, renamed; anything but a match says the
			// file was touched between the rename and this read.
			if (!a_preview.empty()) {
				const auto drift = Content::Compare(Parse(a_preview), written);
				if (drift.sections.empty()) {
					spdlog::info("[preset-save] {} matches the preview the page showed", a_name);
				} else {
					spdlog::warn("[preset-save] {} DIFFERS from the preview the page showed: {}", a_name,
						drift.ToJson().dump());
				}
			}
			try {
				const auto root = Jslot::Json::parse(text);
				std::string keys;
				for (const auto& [k, v] : root.items()) {
					keys += (keys.empty() ? "" : ",") + k;
				}
				const auto preset = Jslot::Preset::Parse(root);
				const bool same = preset.Serialize() == root;
				const auto& m = root.contains("morphs") ? root["morphs"] : Jslot::Json();
				spdlog::info(
					"[preset-save] {} reads: sections [{}], custom {}, sculpt {}, round trip {}",
					a_name, keys,
					m.contains("custom") && m["custom"].is_array() ? std::to_string(m["custom"].size()) : "absent",
					m.contains("sculpt") && !m["sculpt"].is_null() ? "present" : "absent",
					same ? "identical" : "DIFFERENT");
			} catch (const std::exception& e) {
				spdlog::warn("[preset-save] {} does NOT read with our parser: {}", a_name, e.what());
			}
		}

		// Game thread, after a save that landed: the file is now the live
		// character, so the overwrite guard knows it whole. Read back here
		// and not in Inspect - Inspect runs off the game thread, and the
		// guard's record is game thread only.
		void Landed(const fs::path& a_path, const std::string& a_name)
		{
			std::string written;
			if (!Guard::ReadAll(a_path, written) || written.empty()) {
				spdlog::warn("[preset-save] {} cannot be read back - the guard does not know it", a_name);
				return;
			}
			Guard::Wrote(a_name, std::move(written));
		}

		void Dispatch(Pending a_pending);

		// The file the player named. a_existing is the file it replaces as
		// read before (empty for a new one).
		void SaveNamed(const std::string& a_name, std::string a_existing)
		{
			Pending p;
			p.kind = Kind::kSave;
			p.name = a_name;
			p.path = Library::Folder() / (a_name + Library::kExtension);
			p.before = Stat(p.path);
			p.existing = std::move(a_existing);
			Dispatch(std::move(p));
		}

		// The preview landed: compare it with the file it would replace.
		// Nothing gained or lost - the save goes on at once. Otherwise the
		// page is shown both lists and the save waits for its answer.
		void Previewed(Pending a_p, long long a_ms)
		{
			std::string preview;
			if (!Guard::ReadAll(a_p.path, preview) || preview.empty()) {
				Reply(a_p.name, false, "RaceMenu did not write the save preview", true, 0, a_ms);
				return;
			}
			if (a_p.replaced != g_replaced) {
				spdlog::info("[preset-save] preview of {} discarded: a load replaced the character meanwhile", a_p.name);
				RemovePreviewFile();
				Reply(a_p.name, false, "a load replaced the character while the preview was written - save again",
					false, 0, a_ms);
				return;
			}
			// What the panel wrote itself (a body chosen here) is not a gain.
			// See Content::Compare and Edit/Body.h.
			const json written = { { "bodyMorphs", Edit::Body::Written() } };
			const auto start = Clock::now();
			const auto diff = Content::Compare(Parse(a_p.existing), Parse(preview), written);
			const auto cmpMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
			spdlog::info("[preset-save] preview of {} ({} bytes, written in {} ms) against the file ({} bytes), "
						 "compared in {} ms: {}",
				a_p.name, preview.size(), a_ms, a_p.existing.size(), cmpMs, diff.ToJson().dump());

			if (diff.NothingGainedOrLost()) {
				RemovePreviewFile();
				SaveNamed(a_p.name, std::move(a_p.existing));
				return;
			}
			g_preview = Preview{ a_p.name, std::move(a_p.existing), std::move(preview) };
			Panel::Bridge::Send("preset-save-preview", { { "name", a_p.name }, { "diff", diff.ToJson() } }, true);
		}

		void Finish(std::uint32_t a_id, bool a_returned, const std::string& a_how)
		{
			if (!g_pending || g_pending->id != a_id) {
				spdlog::info("[preset-save] #{} {} after the save was already settled - ignored", a_id, a_how);
				return;
			}
			Pending p = std::move(*g_pending);
			g_pending.reset();

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - p.sentAt).count();
			const FileState after = Stat(p.path);
			const bool changed = !p.before.exists ||
			                     after.written != p.before.written || after.size != p.before.size;

			spdlog::info(
				"[preset-save] #{} {} {} after {} ms: file {} size {} (before: {} size {}), changed {}",
				a_id, p.kind == Kind::kPreview ? "preview" : "save", a_how, ms, after.exists ? "exists" : "MISSING",
				after.size, p.before.exists ? "existed" : "absent", p.before.size, changed);

			if (a_returned && after.exists && after.size > 0 && changed) {
				if (p.kind == Kind::kPreview) {
					Previewed(std::move(p), ms);
					return;
				}
				++g_saves;
				Landed(p.path, p.name);
				Reply(p.name, true, {}, false, after.size, ms);
				Library::SendList();
				std::thread(Inspect, p.path, p.name, std::move(p.existing), std::string()).detach();
				return;
			}

			std::string why;
			if (!a_returned) {
				why = "RaceMenu did not answer the save";
			} else if (!after.exists) {
				why = "RaceMenu returned but no file was written";
			} else if (after.size == 0) {
				why = "RaceMenu wrote an empty file";
			} else {
				why = "RaceMenu returned but the existing file did not change";
			}
			if (p.kind == Kind::kPreview) {
				why += " (save preview)";
			}
			Reply(p.name, false, why, true, after.size, ms);
		}

		class Done : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Done(std::uint32_t a_id) :
				_id(a_id)
			{}

			void operator()(RE::BSScript::Variable a_result) override
			{
				spdlog::info("[preset-save] #{} {}.{} returned (result none: {})", _id, kClass, kFunction,
					a_result.IsNoneObject());
				if (auto* tasks = SKSE::GetTaskInterface()) {
					const auto id = _id;
					tasks->AddTask([id]() { Finish(id, true, "returned"); });
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::uint32_t _id;
		};

		void Dispatch(Pending a_pending)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!vm || !player) {
				spdlog::warn("[preset-save] no {} - not saved", vm ? "player" : "Papyrus VM");
				Reply(a_pending.name, false, "the game is not ready", true);
				return;
			}

			const auto id = ++g_nextId;
			a_pending.id = id;
			a_pending.sentAt = Clock::now();
			a_pending.replaced = g_replaced;
			const auto file = a_pending.kind == Kind::kPreview ? std::string(Library::kPreviewName) : a_pending.name;
			const bool overwrite = !a_pending.existing.empty();
			const auto kind = a_pending.kind;
			g_pending = std::move(a_pending);

			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Done(id));
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::string(file));
			if (!vm->DispatchStaticCall(kClass, kFunction, args, callback)) {
				spdlog::warn("[preset-save] #{} DispatchStaticCall({}.{}) refused", id, kClass, kFunction);
				Finish(id, false, "dispatch refused");
				return;
			}
			spdlog::info("[preset-save] #{} {}.{}(player, \"{}\") dispatched - {}, overwrite {}", id, kClass, kFunction,
				file, kind == Kind::kPreview ? "preview" : "save", overwrite);

			std::thread([id]() {
				std::this_thread::sleep_for(kTimeout);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id]() { Finish(id, false, "timed out"); });
				}
			}).detach();
		}

		// The page answered a preview: the preview file itself becomes the
		// preset. A .jslot does not carry its own name, so the renamed
		// preview is what a second save would have written - and it is
		// exactly what the page showed, even if the character was edited
		// while the page waited. A second save could differ from what the
		// player agreed to; a rename cannot.
		void Confirm(const std::string& a_name)
		{
			if (!g_preview || !FileName::Same(g_preview->name, a_name)) {
				Reply(a_name, false, "there is no save preview for this name any more - save again", false);
				return;
			}
			// The preview compared the character with the file as it was
			// then. A file changed on disk since is a different comparison.
			// Byte for byte on purpose, unlike the content comparisons: in
			// this window nothing of ours rewrites the file, so any
			// difference, even an order-only one, is a real outside change.
			const auto path = Library::Folder() / (a_name + Library::kExtension);
			std::string now;
			if (!Guard::ReadAll(path, now) || now != g_preview->existing) {
				Drop("the preset changed on disk since the preview");
				// The page asked to save and waits for the outcome.
				Reply(a_name, false,
					"the preset changed on disk since the preview - save again to see what it would change", false);
				return;
			}
			Preview shown = std::move(*g_preview);
			g_preview.reset();
			spdlog::info("[preset-save] preview of {} confirmed by the page", a_name);

			const auto      start = Clock::now();
			std::error_code ec;
			fs::rename(PreviewPath(), path, ec);
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
			if (ec) {
				spdlog::warn("[preset-save] preview of {} could NOT replace the preset: {}", a_name, ec.message());
				RemovePreviewFile();
				Reply(a_name, false, "the preview could not replace the preset (" + ec.message() + ") - save again",
					false, 0, ms);
				return;
			}
			const FileState after = Stat(path);
			++g_saves;
			spdlog::info("[preset-save] {} replaced by the preview the page showed, in {} ms: {} bytes", a_name, ms,
				after.size);
			Landed(path, a_name);
			Reply(a_name, true, {}, false, after.size, ms);
			Library::SendList();
			std::thread(Inspect, path, a_name, std::move(shown.existing), std::move(shown.preview)).detach();
		}

		void OnPresetSave(const json& a_data)
		{
			const auto name = a_data.contains("name") && a_data["name"].is_string() ?
			                      a_data["name"].get<std::string>() :
			                      std::string();
			const bool overwrite = a_data.value("overwrite", false);
			const bool confirm = a_data.value("confirm", false);

			if (const auto problem = FileName::Problem(name); !problem.empty()) {
				spdlog::info("[preset-save] refused \"{}\": {}", name, problem);
				Reply(name, false, problem, false);
				return;
			}
			if (FileName::Same(name, Library::kPreviewName)) {
				Reply(name, false, "this name is reserved by the Lifecast", false);
				return;
			}
			if (g_pending) {
				Reply(name, false, "a save is still running", false);
				return;
			}
			if (confirm) {
				Confirm(name);
				return;
			}
			// A new save request replaces a preview left unanswered.
			Drop("another save was asked for");

			const auto path = Library::Folder() / (name + Library::kExtension);
			const FileState before = Stat(path);
			if (before.exists && !overwrite) {
				Reply(name, false, "a preset with this name already exists", false);
				return;
			}
			if (!before.exists) {
				SaveNamed(name, {});
				return;
			}

			auto verdict = Guard::CheckOverwrite(name, path);
			spdlog::info("[preset-save] overwrite of {} {}: {} - file sections {}, at risk {}", name,
				verdict.allow ? "allowed" : "REFUSED", verdict.how, verdict.sections.dump(),
				verdict.loss.is_null() ? "[]" : verdict.loss.dump());
			if (!verdict.allow) {
				Reply(name, false, verdict.how, false, 0, 0,
					verdict.loss.is_null() ? json::array() : std::move(verdict.loss));
				return;
			}
			std::string existing;
			if (!Guard::ReadAll(path, existing) || existing.empty()) {
				Reply(name, false, "the existing preset could not be read", false);
				return;
			}

			// Before an overwrite, RaceMenu writes the live character into the
			// preview file, and the page sees what the named file would gain
			// and lose - by content - before it is touched.
			Pending p;
			p.kind = Kind::kPreview;
			p.name = name;
			p.path = PreviewPath();
			p.before = Stat(p.path);
			p.existing = std::move(existing);
			Dispatch(std::move(p));
		}

		void OnPresetSaveCancel(const json& a_data)
		{
			const auto name = a_data.contains("name") && a_data["name"].is_string() ?
			                      a_data["name"].get<std::string>() :
			                      std::string();
			if (g_preview && FileName::Same(g_preview->name, name)) {
				Drop("the page cancelled it");
			}
		}

		void OnMenuClosed() { Drop("the RaceSex Menu closed"); }

		// A load replaced the character: the preview describes someone else.
		void OnReplaced()
		{
			++g_replaced;
			Drop("a load replaced the character");
		}

		bool g_installed = false;
	}

	bool Busy() { return g_pending.has_value(); }

	void Install()
	{
		if (g_installed) {
			return;
		}
		Panel::Bridge::On("preset-save", &OnPresetSave);
		Panel::Bridge::On("preset-save-cancel", &OnPresetSaveCancel);
		Edit::Session::OnMenuClosed(&OnMenuClosed);
		Edit::Session::OnReplaced(&OnReplaced);
		g_installed = true;
		// A preview file left by a game that ended mid-preview.
		RemovePreviewFile();
		spdlog::info("[preset-save] installed - delegates to {}.{}, overwrite preview file \"{}\"", kClass, kFunction,
			Library::kPreviewName);
	}
}
