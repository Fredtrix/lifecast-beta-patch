// Finish.h
// Atelier
//
// The character's name, and the button that ends the RaceSex Menu.
//
// Finishing is the one step of the mod that cannot be taken back: at the
// start of a new game, closing the menu is the end of character creation. So
// the button does ONE thing, the same in every way the menu can be opened -
// it gives the name and closes the menu - on a single click, with no
// confirmation step on the page. What changes from one way to another is only
// the words the page shows while it happens:
//
//   - at the start of a new game, closing is final;
//   - from the console, closing keeps the game going, and a race changed in
//     the menu costs what a race change costs.
//
// Nothing destructive depends on telling the two apart, and the name is
// always required; so a wrong guess only changes the words, never what
// happens.
//
// HOW IT GUESSES, and every sign is logged on its own, because none of them
// is proof and a round measures how often each one is right:
//
//   saving      the player's byCharGenFlag has kDisableSaving. The vanilla
//               start sets it before the menu (QF_MQ101 stage 10) and clears
//               it after - and it is far from the only one: in the vanilla
//               and DLC script sources, ten SetInChargen calls turn it on, in
//               nine scripts (the carriage ride, the werewolf and Vampire Lord
//               changes, Dawnguard VQ06, Dragonborn TTR4b, TG08, MQ206 and a
//               College scene among them). Mods add more. A hit rate measured
//               only in character creation says nothing about false positives.
//   mq101       the stage of the vanilla start quest (Skyrim.esm 0003372B).
//               Logged only.
//   alternate   Alternate Start - Live Another Life's start quest (000DAF in
//               its plugin) is running and has not reached stage 10, which it
//               sets right after its menu closes. Only when that mod is
//               installed.
//
//   newGame = saving or alternate
//
// THE NAME. Written through the menu itself - RaceSexMenu::ChangeName, the
// game's own function for it - and read back from the actor base. If the
// base does not carry the name afterwards, it is written on the record
// (TESFullName::SetFullName, a path already measured to work in game) and the log
// says the menu's path did not take. Which of the two lands, and what the
// next save is called, is what the round measures.
//
// THE CLOSE is a kHide message to the RaceSex Menu - measured in game to
// close RaceMenu's film and the panel together, in 13 ms. Nothing of
// the film is called.
//
// THE NAME THAT CANNOT BE READ is a state of its own, and not the empty
// name. The name is always required, and the page's field starts with the
// name read from the character; if that read fails, the field would start
// empty and the button would refuse as if the player had cleared it - a dead
// end that only shows to someone who already has a problem. So "name" says
// whether the name could be read ("readable"), and when it cannot, the page
// does not offer the finish at all and points to RaceMenu's own Done button.
// The DLL refuses a finish in that state as well: what it writes is checked
// by reading it back, and an unreadable name cannot be checked.
//
// REFUSED, with the reason sent to the page, and nothing done:
//
//   - the menu is not open;
//   - the name is empty, or only spaces;
//   - the name on the actor base cannot be read;
//   - a preset save or load is still running (Preset/Save.h, Load.h);
//   - a finish was already accepted in this menu session - a second click,
//     or a second message, does nothing.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "name-get"       {}
//   dll -> page  "name"           { "name": text, "readable": bool,
//                                   "newGame": bool, "session": number }
//   page -> dll  "finish"         { "name": text }
//   dll -> page  "finish-result"  { "ok": bool, "why": text }
//
// "name" is also sent every time the panel comes on screen, so what the page
// shows is read from the character and not remembered from before - with one
// exception the page keeps: a name the player typed and has not finished
// with yet survives an F4 swap inside the same menu session ("session" is the
// count of RaceSex Menu openings, and a new one drops it).

#pragma once

namespace Atelier::Edit::Finish
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
