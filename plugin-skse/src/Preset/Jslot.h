#pragma once

// Jslot.h
// Lifecast
//
// Origin: copied from earlier plugin code of our own - file
// src/Core/Jslot.h at commit cb5eb7b. Changed on copy: the namespace (now
// Lifecast::Jslot), and comments that pointed to that project's documents,
// test fixtures and phase numbers, reworded to say the fact instead. The
// logic is unchanged.

// Pure-logic model of the RaceMenu .jslot preset format.
//
// The schema was mapped field by field from 21 real RaceMenu-produced .jslot
// files. Fields whose semantics are not confirmed yet are preserved
// verbatim on round-trip and never emitted by the writer.
//
// This header must stay free of engine/CommonLib includes: it is compiled
// both into the plugin and into the offline test harness.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Lifecast::Jslot
{
    using Json = nlohmann::json;

    // --- typed sections -----------------------------------------------------

    struct ActorData
    {
        std::optional<std::uint32_t> hairColor;    // packed RGB, matches CLFM colors
        std::optional<std::string>   headTexture;  // formIdentifier of a BGSTextureSet
        double                       weight{ 0.0 };
        Json                         extras = Json::object();
    };

    struct HeadPart
    {
        std::optional<std::int64_t> formId;  // FormID without load-order prefix, decimal
        std::string                 formIdentifier;  // "Plugin.esm|XXXXXX"
        std::optional<std::int64_t> type;    // semantics unconfirmed: round-trip only
        Json                        extras = Json::object();
    };

    struct FaceTexture
    {
        std::int64_t index{ 0 };
        std::string  texture;
        Json         extras = Json::object();
    };

    struct TintLayer
    {
        std::uint32_t color{ 0 };  // ARGB
        std::int64_t  index{ 0 };
        std::string   texture;
        Json          extras = Json::object();
    };

    struct BodyMorphKey
    {
        std::string key;
        double      value{ 0.0 };
        Json        extras = Json::object();
    };

    struct BodyMorph
    {
        std::vector<BodyMorphKey> keys;
        std::string               name;
        Json                      extras = Json::object();
    };

    struct ModEntry
    {
        std::int64_t index{ 0 };
        std::string  name;
        Json         extras = Json::object();
    };

    struct DefaultMorphs
    {
        std::vector<double>        morphs;   // 19 entries in every real file
        std::vector<std::uint32_t> presets;  // 4 entries; 0xFFFFFFFF = none
        Json                       extras = Json::object();
    };

    struct MorphsData
    {
        // custom / sculpt are preserved as raw JSON: their inner semantics
        // (FaceMorph named sliders, sculpt data) are outside the writer's
        // scope, and raw preservation keeps the round-trip exact even for
        // shapes we have not seen yet. nullopt = key absent in the file;
        // a stored JSON null = key present with value null (both occur).
        std::optional<Json>          custom;
        std::optional<DefaultMorphs> defaultMorphs;
        std::optional<Json>          sculpt;
        std::optional<std::int64_t>  sculptDivisor;
        Json                         extras = Json::object();
    };

    struct VersionBlock
    {
        std::int64_t formatVersion{ 0 };
        std::int64_t runtimeVersion{ 0 };
        std::int64_t signature{ 0 };  // 0x45534B53 = "SKSE" little-endian
        std::int64_t skseVersion{ 0 };
        Json         extras = Json::object();
    };

    // --- the preset ---------------------------------------------------------

    struct Preset
    {
        ActorData                               actor;
        std::optional<std::vector<BodyMorph>>   bodyMorphs;
        std::optional<std::vector<FaceTexture>> faceTextures;
        std::vector<HeadPart>                   headParts;
        std::optional<std::vector<std::string>> modNames;
        std::optional<std::vector<ModEntry>>    mods;
        MorphsData                              morphs;
        std::optional<std::vector<TintLayer>>   tintInfo;
        VersionBlock                            version;

        // Sections the writer never emits, preserved verbatim for
        // round-trip fidelity (overrides, skinOverrides, transforms today;
        // anything a future RaceMenu adds lands here too).
        std::map<std::string, Json> rawSections;

        // Parses a JSON tree read from a real .jslot. Throws Jslot::ParseError
        // on structural violations of the mapped schema.
        static Preset Parse(const Json& a_root);

        // Rebuilds the JSON tree. Parse(x).Serialize() must compare
        // semantically equal to x - that is the round-trip self-test.
        Json Serialize() const;

        // Serializes to the on-disk text form (ASCII-only escapes).
        std::string ToText() const;
    };

    class ParseError : public std::runtime_error
    {
    public:
        explicit ParseError(const std::string& a_what) :
            std::runtime_error(a_what)
        {}
    };

    // --- writer -------------------------------------------------------------

    // Index of what actually exists in the player's installation, built from
    // the installed forms (runtime: TESDataHandler enumeration; tests: fake).
    // All lookups are case-insensitive on the plugin file name.
    class CatalogIndex
    {
    public:
        void AddHeadPart(const std::string& a_formIdentifier);
        void AddTextureSet(const std::string& a_formIdentifier);

        [[nodiscard]] bool HasHeadPart(const std::string& a_formIdentifier) const;
        [[nodiscard]] bool HasTextureSet(const std::string& a_formIdentifier) const;

    private:
        static std::string Normalize(const std::string& a_identifier);

        std::set<std::string> _headParts;
        std::set<std::string> _textureSets;
    };

    // Everything the writer needs to emit a preset. Only fields whose
    // semantics are confirmed from real files are representable here.
    // A RaceMenu extended-slider value for morphs.custom. The name is the
    // SLIDER name (the ini key under facegenmorphs/<plugin>/sliders/), which
    // is what real presets carry (entries like {"name": "CME_MouthWidth",
    // "value": 0.72}).
    struct CustomMorph
    {
        std::string name;
        double      value{ 0.0 };
    };

    struct WriterChoices
    {
        std::vector<std::string>     headPartIdentifiers;
        std::optional<std::uint32_t> hairColor;
        std::optional<std::string>   headTextureIdentifier;
        double                       weight{ 50.0 };
        std::vector<double>          vanillaMorphs;   // exactly 19 when non-empty
        std::vector<std::uint32_t>   facePresets;     // exactly 4 when non-empty
        std::vector<CustomMorph>     customMorphs;    // morphs.custom entries
        std::vector<TintLayer>       tints;
        std::vector<FaceTexture>     faceTextures;
        VersionBlock                 version;
    };

    // The version block the writer emits: byte-identical to the block found
    // in all 21 real files. The skee64 source (PresetInterface.cpp) confirms
    // these are compile-time constants on RaceMenu's side too (kSignature,
    // kVersion, PACKED_SKSE_VERSION, RUNTIME_VERSION_1_4_2) - notably,
    // runtimeVersion is NOT the running game version.
    [[nodiscard]] VersionBlock ObservedVersionBlock();

    // Builds a Preset from writer choices. Emits only the minimal confirmed
    // subset of the schema; modNames is derived from the identifiers, mods is
    // emitted empty (both shapes attested by real files).
    [[nodiscard]] Preset BuildPreset(const WriterChoices& a_choices);

    // Resolvability gate: returns the list of
    // references in a_preset that do NOT resolve against a_catalog. The
    // writer refuses to write unless this comes back empty.
    [[nodiscard]] std::vector<std::string> FindUnresolvedReferences(
        const Preset&       a_preset,
        const CatalogIndex& a_catalog);
}
