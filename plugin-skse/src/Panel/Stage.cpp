// Stage.cpp
// Atelier

#include "Panel/Stage.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

namespace Atelier::Panel::Stage
{
	namespace
	{
		using json = nlohmann::json;
		using Rot = std::array<double, 9>;  // flat, row after row - RaceMenu's order
		using Clock = std::chrono::steady_clock;

		constexpr auto   kCharGen = "_global.skse.plugins.CharGen";
		constexpr double kPi = 3.14159265358979323846;

		// How much of the gap that is left the current position covers in one
		// tick, and how close to the target counts as arrived.
		//
		// WORKING VALUES, not chosen ones. The only evidence that exists about
		// what this camera sliding should feel like is the engine's own zoom
		// delegate, and that is one grandeza, one boot, and a movement with no
		// value to read - not a population to derive a rate from. The number
		// is the author's to pick once he has seen it move.
		//
		// The snap epsilons are in the units each grandeza is measured in:
		// game units for distance and height, degrees for turn and tilt.
		constexpr double kEase = 0.25;
		constexpr double kSnapUnits = 0.05;
		constexpr double kSnapDegrees = 0.05;

		// The five RaceMenu calls this module needs; any one missing and the
		// view is refused, since a half-working view is worse than none.
		constexpr std::array<const char*, 5> kCalls{ "GetRaceSexCameraPos", "SetRaceSexCameraPos", "GetPlayerPosition",
			"GetPlayerRotation", "SetPlayerRotation" };

		// ----- what the menu showed when it opened (UI thread only) -----------

		struct Frame
		{
			bool          have{ false };
			RE::NiPoint3  camera;     // camera position, as RaceMenu reports it
			RE::NiPoint3  player;     // player position, as RaceMenu reports it
			Rot           body{};     // the body's rotation
			bool          node{ false };
			RE::NiMatrix3 cameraRot;  // the camera node's local rotation
			double        distance{ 0.0 };
			double        dirX{ 0.0 };  // unit vector, player to camera, ground plane
			double        dirY{ 0.0 };
		};

		struct At
		{
			double turn{ 0.0 };
			double distance{ 0.0 };
			double lift{ 0.0 };
			double tilt{ 0.0 };
		};

		Frame       g_frame;
		At          g_target;  // where the page asked the view to be
		At          g_at;      // where it is now, which is what was last written
		std::string g_why;     // why the view is not ready, when it is not

		// Read by the tick on the game thread, written by the work on the UI
		// thread. A stale read costs one frame of the slide and nothing else,
		// so it is relaxed.
		std::atomic<bool> g_moving{ false };

		// The slide under way, for the log: this is the measurement the change
		// exists to produce, and there is nowhere else to take it from.
		std::uint32_t     g_ticks = 0;
		Clock::time_point g_slideAt{};
		double            g_workUs = 0.0;
		double            g_worstUs = 0.0;

		// ----- requests from the page (game thread), folded ----------------------

		struct Pending
		{
			double turn{ 0.0 };
			double distance{ 0.0 };
			double lift{ 0.0 };
			double tilt{ 0.0 };
			bool   reset{ false };
			bool   any{ false };
			// Whether the page is waiting for an answer. A tick is not: the
			// state it would send is the target, and the target is exactly
			// what a tick does not change.
			bool answer{ false };
		};

		bool g_installed = false;

		std::mutex g_lock;
		Pending    g_pending;
		bool       g_queued = false;

		// ----- small helpers ------------------------------------------------------

		std::string P(const RE::NiPoint3& a_p)
		{
			return fmt::format("({:.2f}, {:.2f}, {:.2f})", a_p.x, a_p.y, a_p.z);
		}

		double Clamp(double a_v, double a_lo, double a_hi)
		{
			return a_v < a_lo ? a_lo : (a_v > a_hi ? a_hi : a_v);
		}

		double Wrap(double a_deg)
		{
			double d = std::fmod(a_deg, 360.0);
			if (d > 180.0) {
				d -= 360.0;
			} else if (d <= -180.0) {
				d += 360.0;
			}
			return d;
		}

		// Clamps, and says so. A target that is silently clipped leaves the
		// page showing a value nobody asked for with nothing in the log to
		// explain where it came from.
		double ClampSaid(double a_v, double a_lo, double a_hi, const char* a_what)
		{
			const double c = Clamp(a_v, a_lo, a_hi);
			if (c != a_v) {
				spdlog::info("[stage] {} target {:.2f} is outside {:.2f} .. {:.2f} - clamped to {:.2f}", a_what, a_v,
					a_lo, a_hi, c);
			}
			return c;
		}

		// One step of the slide. Returns whether the value moved, which is
		// what decides if this grandeza gets written this tick.
		bool Step(double& a_cur, double a_target, double a_snap)
		{
			const double gap = a_target - a_cur;
			if (std::abs(gap) <= a_snap) {
				if (a_cur == a_target) {
					return false;
				}
				a_cur = a_target;  // exactly, so arrival is a comparison and not a guess
				return true;
			}
			a_cur += gap * kEase;
			return true;
		}

		// The same, the short way round: turn lives on a circle, so a target
		// just past 180 degrees is a small move and not an almost whole turn.
		bool StepAngle(double& a_cur, double a_target, double a_snap)
		{
			const double gap = Wrap(a_target - a_cur);
			if (std::abs(gap) <= a_snap) {
				if (a_cur == a_target) {
					return false;
				}
				a_cur = a_target;
				return true;
			}
			a_cur = Wrap(a_cur + gap * kEase);
			return true;
		}

		RE::GPtr<RE::RaceSexMenu> Menu()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui ? ui->GetMenu<RE::RaceSexMenu>() : RE::GPtr<RE::RaceSexMenu>();
		}

		bool CharGen(RE::GFxMovieView* a_movie, RE::GFxValue& a_out)
		{
			return a_movie && a_movie->GetVariable(&a_out, kCharGen) && a_out.IsObject();
		}

		bool GetPoint(RE::GFxValue& a_cg, const char* a_fn, RE::NiPoint3& a_out)
		{
			RE::GFxValue result;
			if (!a_cg.Invoke(a_fn, &result) || !result.IsObject()) {
				spdlog::warn("[stage] CharGen.{} gave no object", a_fn);
				return false;
			}
			const std::array<const char*, 3> names{ "x", "y", "z" };
			std::array<double, 3>            xyz{};
			for (std::size_t i = 0; i < names.size(); ++i) {
				RE::GFxValue v;
				if (!result.GetMember(names[i], &v) || !v.IsNumber()) {
					spdlog::warn("[stage] CharGen.{}: no number '{}'", a_fn, names[i]);
					return false;
				}
				xyz[i] = v.GetNumber();
			}
			a_out = RE::NiPoint3(static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2]));
			return true;
		}

		bool SetCameraPos(RE::GFxMovieView* a_movie, RE::GFxValue& a_cg, const RE::NiPoint3& a_p)
		{
			RE::GFxValue obj;
			a_movie->CreateObject(&obj);
			if (!obj.IsObject()) {
				spdlog::warn("[stage] CreateObject failed - SetRaceSexCameraPos not called");
				return false;
			}
			obj.SetMember("x", RE::GFxValue(static_cast<double>(a_p.x)));
			obj.SetMember("y", RE::GFxValue(static_cast<double>(a_p.y)));
			obj.SetMember("z", RE::GFxValue(static_cast<double>(a_p.z)));
			return a_cg.Invoke("SetRaceSexCameraPos", nullptr, &obj, 1);
		}

		bool GetBody(RE::GFxValue& a_cg, Rot& a_out)
		{
			RE::GFxValue result;
			if (!a_cg.Invoke("GetPlayerRotation", &result) || !result.IsArray() || result.GetArraySize() != 9) {
				spdlog::warn("[stage] CharGen.GetPlayerRotation gave no array of 9");
				return false;
			}
			for (std::uint32_t i = 0; i < 9; ++i) {
				RE::GFxValue v;
				if (!result.GetElement(i, &v) || !v.IsNumber()) {
					spdlog::warn("[stage] CharGen.GetPlayerRotation: element {} is not a number", i);
					return false;
				}
				a_out[i] = v.GetNumber();
			}
			return true;
		}

		bool SetBody(RE::GFxMovieView* a_movie, RE::GFxValue& a_cg, const Rot& a_r)
		{
			RE::GFxValue arr;
			a_movie->CreateArray(&arr);
			if (!arr.IsArray()) {
				spdlog::warn("[stage] CreateArray failed - SetPlayerRotation not called");
				return false;
			}
			for (const double v : a_r) {
				arr.PushBack(RE::GFxValue(v));
			}
			// RaceMenu asserts exactly 9, and its assert does not stop a
			// release build: refuse here instead of there.
			if (arr.GetArraySize() != 9) {
				spdlog::warn("[stage] rotation array has {} elements - SetPlayerRotation not called", arr.GetArraySize());
				return false;
			}
			return a_cg.Invoke("SetPlayerRotation", nullptr, &arr, 1);
		}

		// Rz(a_deg) * a_m, both flat row after row.
		Rot TurnedBy(const Rot& a_m, double a_deg)
		{
			const double                c = std::cos(a_deg * kPi / 180.0);
			const double                s = std::sin(a_deg * kPi / 180.0);
			const std::array<double, 9> z{ c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0 };
			Rot                         out{};
			for (int r = 0; r < 3; ++r) {
				for (int col = 0; col < 3; ++col) {
					double sum = 0.0;
					for (int k = 0; k < 3; ++k) {
						sum += z[r * 3 + k] * a_m[k * 3 + col];
					}
					out[r * 3 + col] = sum;
				}
			}
			return out;
		}

		// A rotation of a_deg about the unit axis (a_x, a_y, a_z), column
		// vectors (Rodrigues).
		RE::NiMatrix3 AxisAngle(double a_x, double a_y, double a_z, double a_deg)
		{
			const double  t = a_deg * kPi / 180.0;
			const double  c = std::cos(t);
			const double  s = std::sin(t);
			const double  k = 1.0 - c;
			RE::NiMatrix3 m;
			m.entry[0][0] = static_cast<float>(c + a_x * a_x * k);
			m.entry[0][1] = static_cast<float>(a_x * a_y * k - a_z * s);
			m.entry[0][2] = static_cast<float>(a_x * a_z * k + a_y * s);
			m.entry[1][0] = static_cast<float>(a_y * a_x * k + a_z * s);
			m.entry[1][1] = static_cast<float>(c + a_y * a_y * k);
			m.entry[1][2] = static_cast<float>(a_y * a_z * k - a_x * s);
			m.entry[2][0] = static_cast<float>(a_z * a_x * k - a_y * s);
			m.entry[2][1] = static_cast<float>(a_z * a_y * k + a_x * s);
			m.entry[2][2] = static_cast<float>(c + a_z * a_z * k);
			return m;
		}

		double LiftMin()
		{
			const double room = g_frame.camera.z - g_frame.player.z - kFloorMargin;
			return room > 0.0 ? -room : 0.0;
		}

		double DistanceMin()
		{
			// Never force the camera further away than the menu itself put it.
			return g_frame.distance < kMinDistance ? g_frame.distance : kMinDistance;
		}

		// ----- the work (UI thread) -------------------------------------------------

		bool Capture(RE::RaceSexMenu* a_menu, RE::GFxValue& a_cg)
		{
			for (const char* fn : kCalls) {
				if (!a_cg.HasMember(fn)) {
					g_why = fmt::format("RaceMenu's CharGen.{} is missing", fn);
					return false;
				}
			}
			Frame f;
			if (!GetPoint(a_cg, "GetRaceSexCameraPos", f.camera) || !GetPoint(a_cg, "GetPlayerPosition", f.player) ||
				!GetBody(a_cg, f.body)) {
				g_why = "RaceMenu did not report the camera, the player or the body";
				return false;
			}
			const double dx = static_cast<double>(f.camera.x) - f.player.x;
			const double dy = static_cast<double>(f.camera.y) - f.player.y;
			f.distance = std::sqrt(dx * dx + dy * dy);
			if (f.distance < 1.0) {
				g_why = fmt::format("the camera is {:.2f} from the player in the ground plane - no direction to move along",
					f.distance);
				return false;
			}
			f.dirX = dx / f.distance;
			f.dirY = dy / f.distance;
			if (auto* node = a_menu->GetRuntimeData().camera.cameraRoot.get()) {
				f.node = true;
				f.cameraRot = node->local.rotate;
			}
			f.have = true;
			g_frame = f;
			g_at = At{};
			g_at.distance = f.distance;
			g_target = g_at;
			g_moving.store(false, std::memory_order_relaxed);
			g_why.clear();
			spdlog::info("[stage] frame kept: camera {} player {} - {:.2f} apart in the ground plane, {:.2f} above; "
						 "camera node {}",
				P(f.camera), P(f.player), f.distance, static_cast<double>(f.camera.z) - f.player.z,
				f.node ? "present" : "ABSENT (no tilt)");
			return true;
		}

		bool WriteCameraPos(RE::GFxMovieView* a_movie, RE::GFxValue& a_cg)
		{
			const RE::NiPoint3 p(
				static_cast<float>(g_frame.player.x + g_frame.dirX * g_at.distance),
				static_cast<float>(g_frame.player.y + g_frame.dirY * g_at.distance),
				static_cast<float>(g_frame.camera.z + g_at.lift));
			return SetCameraPos(a_movie, a_cg, p);
		}

		bool WriteTilt(RE::RaceSexMenu* a_menu)
		{
			auto* node = a_menu->GetRuntimeData().camera.cameraRoot.get();
			if (!node || !g_frame.node) {
				return false;
			}
			// A world-space rotation about the horizontal axis across the line
			// of sight, brought into the node's parent space. Which way a
			// positive angle turns the view is read on screen, not assumed.
			const RE::NiMatrix3 world = AxisAngle(-g_frame.dirY, g_frame.dirX, 0.0, g_at.tilt);
			RE::NiMatrix3       local;
			if (auto* parent = node->parent) {
				const RE::NiMatrix3 p = parent->world.rotate;
				local = p.Transpose() * (world * (p * g_frame.cameraRot));
			} else {
				local = world * g_frame.cameraRot;
			}
			node->local.rotate = local;
			RE::NiUpdateData data{};
			node->Update(data);
			return true;
		}

		// The TARGET, not where the camera has got to. The page disables a
		// button at a limit from this, and the readout is a destination: both
		// have to answer the click that was just made, not the frame the
		// slide happens to be in.
		json State()
		{
			if (!g_frame.have) {
				return { { "ready", false }, { "why", g_why.empty() ? std::string("not measured yet") : g_why } };
			}
			return { { "ready", true }, { "tilt", g_frame.node }, { "turn", g_target.turn },
				{ "distance", g_target.distance }, { "lift", g_target.lift }, { "tiltDeg", g_target.tilt },
				{ "limits", { { "minDistance", DistanceMin() }, { "maxDistance", kMaxDistance }, { "liftMin", LiftMin() },
								{ "liftMax", kLiftUp }, { "tiltMax", kTiltMax } } } };
		}

		void Answer(json a_state)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([state = std::move(a_state)]() { Panel::Bridge::Send("stage", state); });
		}

		void Refuse(std::string a_why, bool a_answer)
		{
			g_frame = Frame{};
			g_why = std::move(a_why);
			g_moving.store(false, std::memory_order_relaxed);
			if (a_answer) {
				Answer(State());
			}
		}

		void Work()
		{
			const auto startedAt = Clock::now();

			Pending p;
			{
				std::scoped_lock lock(g_lock);
				p = g_pending;
				g_pending = Pending{};
				g_queued = false;
			}

			auto         menu = Menu();
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue cg;
			if (!movie) {
				Refuse("the RaceSex Menu is not open", p.answer);
				return;
			}
			if (!CharGen(movie, cg)) {
				Refuse("RaceMenu's CharGen object is not there", p.answer);
				spdlog::warn("[stage] {} - the view is refused", g_why);
				return;
			}
			if (!g_frame.have && !Capture(menu.get(), cg)) {
				spdlog::warn("[stage] {} - the view is refused", g_why);
				g_moving.store(false, std::memory_order_relaxed);
				if (p.answer) {
					Answer(State());
				}
				return;
			}

			// The move goes on the target, and the clamp with it.
			if (p.reset) {
				g_target = At{};
				g_target.distance = g_frame.distance;
			}
			if (p.turn != 0.0) {
				g_target.turn = Wrap(g_target.turn + p.turn);
			}
			if (p.distance != 0.0) {
				g_target.distance = ClampSaid(g_target.distance + p.distance, DistanceMin(), kMaxDistance, "distance");
			}
			if (p.lift != 0.0) {
				g_target.lift = ClampSaid(g_target.lift + p.lift, LiftMin(), kLiftUp, "height");
			}
			if (p.tilt != 0.0 && g_frame.node) {
				g_target.tilt = ClampSaid(g_target.tilt + p.tilt, -kTiltMax, kTiltMax, "tilt");
			}

			// And the tick moves what is written, one step closer.
			const bool turnMoved = StepAngle(g_at.turn, g_target.turn, kSnapDegrees);
			const bool distanceMoved = Step(g_at.distance, g_target.distance, kSnapUnits);
			const bool liftMoved = Step(g_at.lift, g_target.lift, kSnapUnits);
			const bool tiltMoved = g_frame.node && Step(g_at.tilt, g_target.tilt, kSnapDegrees);

			// A reframe writes all three even when nothing moved: RaceMenu's
			// own tabs move this camera, and the kept frame is what the
			// player asked to see again.
			const bool body = turnMoved || p.reset;
			const bool camera = distanceMoved || liftMoved || p.reset;
			const bool tilt = g_frame.node && (tiltMoved || p.reset);

			const bool bodyOk = !body || SetBody(movie, cg, TurnedBy(g_frame.body, g_at.turn));
			const bool cameraOk = !camera || WriteCameraPos(movie, cg);
			const bool tiltOk = !tilt || WriteTilt(menu.get());

			const bool wasMoving = g_moving.load(std::memory_order_relaxed);
			const bool moving = g_at.turn != g_target.turn || g_at.distance != g_target.distance ||
			                    g_at.lift != g_target.lift || g_at.tilt != g_target.tilt;
			g_moving.store(moving, std::memory_order_relaxed);

			if (p.any) {
				spdlog::info("[stage] {}target turn {:.1f} deg, distance {:.2f}, lift {:.2f}, tilt {:.1f} deg{}{}{}",
					p.reset ? "reframe: " : "", g_target.turn, g_target.distance, g_target.lift, g_target.tilt,
					bodyOk ? "" : " - body NOT written", cameraOk ? "" : " - camera NOT written",
					tiltOk ? "" : " - tilt NOT written");
			}

			// The slide, tick by tick. At trace because it is one line a
			// frame while something is moving: raising the level silences it
			// without touching this file.
			if (wasMoving || moving) {
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - startedAt).count();
				if (!wasMoving) {
					g_ticks = 0;
					g_workUs = 0.0;
					g_worstUs = 0.0;
					g_slideAt = startedAt;
				}
				++g_ticks;
				g_workUs += us;
				if (us > g_worstUs) {
					g_worstUs = us;
				}
				spdlog::trace("[stage] tick {}: at turn {:.2f} distance {:.2f} lift {:.2f} tilt {:.2f} -> target "
							  "{:.2f} {:.2f} {:.2f} {:.2f} - {:.0f} us",
					g_ticks, g_at.turn, g_at.distance, g_at.lift, g_at.tilt, g_target.turn, g_target.distance,
					g_target.lift, g_target.tilt, us);
				if (!moving) {
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_slideAt);
					spdlog::info("[stage] arrived after {} ticks in {} ms - {:.0f} us of work in all, worst tick "
								 "{:.0f} us",
						g_ticks, ms.count(), g_workUs, g_worstUs);
				}
			}

			if (p.answer) {
				Answer(State());
			}
		}

		void Queue(const Pending& a_add)
		{
			bool queue = false;
			{
				std::scoped_lock lock(g_lock);
				g_pending.turn += a_add.turn;
				g_pending.distance += a_add.distance;
				g_pending.lift += a_add.lift;
				g_pending.tilt += a_add.tilt;
				g_pending.reset = g_pending.reset || a_add.reset;
				g_pending.any = g_pending.any || a_add.any;
				g_pending.answer = g_pending.answer || a_add.answer;
				if (!g_queued) {
					g_queued = true;
					queue = true;
				}
			}
			if (!queue) {
				return;
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddUITask([]() { Work(); });
			} else {
				std::scoped_lock lock(g_lock);
				g_queued = false;
			}
		}

		double Number(const json& a_data, const char* a_key)
		{
			if (!a_data.is_object() || !a_data.contains(a_key) || !a_data[a_key].is_number()) {
				return 0.0;
			}
			const double v = a_data[a_key].get<double>();
			return std::isfinite(v) ? v : 0.0;
		}

		// ----- bridge and session (game thread) -----------------------------------

		Pending Asked()
		{
			Pending p;
			p.answer = true;
			return p;
		}

		void OnGet(const json&)
		{
			Queue(Asked());
		}

		void OnMove(const json& a_data)
		{
			Move(Number(a_data, "turn"), Number(a_data, "distance"), Number(a_data, "lift"), Number(a_data, "tilt"));
		}

		void OnReset(const json&)
		{
			Reset();
		}

		void OnShown()
		{
			Queue(Asked());
		}

		void OnMenuClosed()
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddUITask([]() {
					if (g_frame.have) {
						spdlog::info("[stage] the menu closed - the kept frame is dropped");
					}
					g_frame = Frame{};
					g_at = At{};
					g_target = At{};
					g_moving.store(false, std::memory_order_relaxed);
					g_why.clear();
				});
			}
		}
	}

	void Move(double a_turn, double a_distance, double a_lift, double a_tilt)
	{
		Pending p;
		p.turn = a_turn;
		p.distance = a_distance;
		p.lift = a_lift;
		p.tilt = a_tilt;
		p.any = p.turn != 0.0 || p.distance != 0.0 || p.lift != 0.0 || p.tilt != 0.0;
		p.answer = true;
		Queue(p);
	}

	void Reset()
	{
		Pending p;
		p.reset = true;
		p.any = true;
		p.answer = true;
		Queue(p);
	}

	void Tick()
	{
		if (!g_moving.load(std::memory_order_relaxed)) {
			return;
		}
		Queue(Pending{});
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Edit::Session::OnPanelShown(&OnShown);
		Edit::Session::OnMenuClosed(&OnMenuClosed);
		Panel::Bridge::On("stage-get", &OnGet);
		Panel::Bridge::On("stage-move", &OnMove);
		Panel::Bridge::On("stage-reset", &OnReset);
		g_installed = true;
		spdlog::info("[stage] installed");
	}
}
