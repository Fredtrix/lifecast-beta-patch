// Stage.cpp
// Atelier

#include "Panel/Stage.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"

#include <array>
#include <cmath>
#include <mutex>

namespace Atelier::Panel::Stage
{
	namespace
	{
		using json = nlohmann::json;
		using Rot = std::array<double, 9>;  // flat, row after row - RaceMenu's order

		constexpr auto   kCharGen = "_global.skse.plugins.CharGen";
		constexpr double kPi = 3.14159265358979323846;

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
		At          g_at;
		std::string g_why;  // why the view is not ready, when it is not

		// ----- requests from the page (game thread), folded ----------------------

		struct Pending
		{
			double turn{ 0.0 };
			double distance{ 0.0 };
			double lift{ 0.0 };
			double tilt{ 0.0 };
			bool   reset{ false };
			bool   any{ false };
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

		json State()
		{
			if (!g_frame.have) {
				return { { "ready", false }, { "why", g_why.empty() ? std::string("not measured yet") : g_why } };
			}
			return { { "ready", true }, { "tilt", g_frame.node }, { "turn", g_at.turn }, { "distance", g_at.distance },
				{ "lift", g_at.lift }, { "tiltDeg", g_at.tilt },
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

		void Work()
		{
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
				g_frame = Frame{};
				g_why = "the RaceSex Menu is not open";
				Answer(State());
				return;
			}
			if (!CharGen(movie, cg)) {
				g_frame = Frame{};
				g_why = "RaceMenu's CharGen object is not there";
				spdlog::warn("[stage] {} - the view is refused", g_why);
				Answer(State());
				return;
			}
			if (!g_frame.have && !Capture(menu.get(), cg)) {
				spdlog::warn("[stage] {} - the view is refused", g_why);
				Answer(State());
				return;
			}

			bool body = false;
			bool camera = false;
			bool tilt = false;
			if (p.reset) {
				g_at = At{};
				g_at.distance = g_frame.distance;
				body = camera = tilt = true;
			}
			if (p.turn != 0.0) {
				g_at.turn = Wrap(g_at.turn + p.turn);
				body = true;
			}
			if (p.distance != 0.0) {
				g_at.distance = Clamp(g_at.distance + p.distance, DistanceMin(), kMaxDistance);
				camera = true;
			}
			if (p.lift != 0.0) {
				g_at.lift = Clamp(g_at.lift + p.lift, LiftMin(), kLiftUp);
				camera = true;
			}
			if (p.tilt != 0.0 && g_frame.node) {
				g_at.tilt = Clamp(g_at.tilt + p.tilt, -kTiltMax, kTiltMax);
				tilt = true;
			}

			const bool bodyOk = !body || SetBody(movie, cg, TurnedBy(g_frame.body, g_at.turn));
			const bool cameraOk = !camera || WriteCameraPos(movie, cg);
			const bool tiltOk = !tilt || WriteTilt(menu.get());
			if (p.any) {
				spdlog::info("[stage] {}turn {:.1f} deg, distance {:.2f}, lift {:.2f}, tilt {:.1f} deg{}{}{}",
					p.reset ? "reframe: " : "", g_at.turn, g_at.distance, g_at.lift, g_at.tilt,
					bodyOk ? "" : " - body NOT written", cameraOk ? "" : " - camera NOT written",
					tiltOk ? "" : " - tilt NOT written");
			}
			Answer(State());
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

		void OnGet(const json&)
		{
			Queue(Pending{});
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
			Queue(Pending{});
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
		Queue(p);
	}

	void Reset()
	{
		Pending p;
		p.reset = true;
		p.any = true;
		Queue(p);
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
