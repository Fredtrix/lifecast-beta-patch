// Jslot.cpp
// Lifecast
//
// Origin: copied from earlier plugin code of our own - file
// src/Core/Jslot.cpp at commit cb5eb7b. Changed on copy: the namespace (now
// Lifecast::Jslot), and comments that pointed to that project's documents,
// test fixtures and phase numbers, reworded to say the fact instead. The
// logic is unchanged.

#include "Jslot.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Lifecast::Jslot
{
    namespace
    {
        [[noreturn]] void Fail(const std::string& a_path, const std::string& a_what)
        {
            throw ParseError(std::format("{}: {}", a_path, a_what));
        }

        void RequireObject(const Json& a_value, const std::string& a_path)
        {
            if (!a_value.is_object()) {
                Fail(a_path, "expected an object, got " + std::string(a_value.type_name()));
            }
        }

        void RequireArray(const Json& a_value, const std::string& a_path)
        {
            if (!a_value.is_array()) {
                Fail(a_path, "expected an array, got " + std::string(a_value.type_name()));
            }
        }

        // Extracts a_key from the (mutable copy of an) object if present,
        // converting to T; the caller ends up with only unknown keys left,
        // which become the extras blob.
        template <class T>
        std::optional<T> TakeOpt(Json& a_obj, const char* a_key, const std::string& a_path)
        {
            const auto it = a_obj.find(a_key);
            if (it == a_obj.end()) {
                return std::nullopt;
            }
            try {
                T value = it->template get<T>();
                a_obj.erase(it);
                return value;
            } catch (const Json::exception& e) {
                Fail(a_path + "." + a_key, e.what());
            }
        }

        template <class T>
        T TakeRequired(Json& a_obj, const char* a_key, const std::string& a_path)
        {
            auto value = TakeOpt<T>(a_obj, a_key, a_path);
            if (!value) {
                Fail(a_path, std::format("required key '{}' is missing", a_key));
            }
            return *std::move(value);
        }

        // Takes the raw JSON value (any shape, including null) without
        // conversion. nullopt = key absent.
        std::optional<Json> TakeRaw(Json& a_obj, const char* a_key)
        {
            const auto it = a_obj.find(a_key);
            if (it == a_obj.end()) {
                return std::nullopt;
            }
            Json value = *it;
            a_obj.erase(it);
            return value;
        }

        ActorData ParseActor(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json        rest = a_json;
            ActorData   out;
            out.hairColor   = TakeOpt<std::uint32_t>(rest, "hairColor", a_path);
            out.headTexture = TakeOpt<std::string>(rest, "headTexture", a_path);
            out.weight      = TakeRequired<double>(rest, "weight", a_path);
            out.extras      = std::move(rest);
            return out;
        }

        HeadPart ParseHeadPart(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json     rest = a_json;
            HeadPart out;
            out.formId         = TakeOpt<std::int64_t>(rest, "formId", a_path);
            out.formIdentifier = TakeRequired<std::string>(rest, "formIdentifier", a_path);
            out.type           = TakeOpt<std::int64_t>(rest, "type", a_path);
            out.extras         = std::move(rest);
            return out;
        }

        FaceTexture ParseFaceTexture(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json        rest = a_json;
            FaceTexture out;
            out.index   = TakeRequired<std::int64_t>(rest, "index", a_path);
            out.texture = TakeRequired<std::string>(rest, "texture", a_path);
            out.extras  = std::move(rest);
            return out;
        }

        TintLayer ParseTintLayer(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json      rest = a_json;
            TintLayer out;
            out.color   = TakeRequired<std::uint32_t>(rest, "color", a_path);
            out.index   = TakeRequired<std::int64_t>(rest, "index", a_path);
            out.texture = TakeRequired<std::string>(rest, "texture", a_path);
            out.extras  = std::move(rest);
            return out;
        }

        BodyMorphKey ParseBodyMorphKey(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json         rest = a_json;
            BodyMorphKey out;
            out.key    = TakeRequired<std::string>(rest, "key", a_path);
            out.value  = TakeRequired<double>(rest, "value", a_path);
            out.extras = std::move(rest);
            return out;
        }

        BodyMorph ParseBodyMorph(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json      rest = a_json;
            BodyMorph out;
            if (auto keys = TakeRaw(rest, "keys")) {
                RequireArray(*keys, a_path + ".keys");
                for (std::size_t i = 0; i < keys->size(); ++i) {
                    out.keys.push_back(
                        ParseBodyMorphKey((*keys)[i], std::format("{}.keys[{}]", a_path, i)));
                }
            } else {
                Fail(a_path, "required key 'keys' is missing");
            }
            out.name   = TakeRequired<std::string>(rest, "name", a_path);
            out.extras = std::move(rest);
            return out;
        }

        ModEntry ParseModEntry(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json     rest = a_json;
            ModEntry out;
            out.index  = TakeRequired<std::int64_t>(rest, "index", a_path);
            out.name   = TakeRequired<std::string>(rest, "name", a_path);
            out.extras = std::move(rest);
            return out;
        }

        DefaultMorphs ParseDefaultMorphs(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json          rest = a_json;
            DefaultMorphs out;
            out.morphs  = TakeRequired<std::vector<double>>(rest, "morphs", a_path);
            out.presets = TakeRequired<std::vector<std::uint32_t>>(rest, "presets", a_path);
            out.extras  = std::move(rest);
            return out;
        }

        MorphsData ParseMorphs(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json       rest = a_json;
            MorphsData out;
            out.custom = TakeRaw(rest, "custom");
            if (auto def = TakeRaw(rest, "default")) {
                out.defaultMorphs = ParseDefaultMorphs(*def, a_path + ".default");
            }
            out.sculpt        = TakeRaw(rest, "sculpt");
            out.sculptDivisor = TakeOpt<std::int64_t>(rest, "sculptDivisor", a_path);
            out.extras        = std::move(rest);
            return out;
        }

        VersionBlock ParseVersion(const Json& a_json, const std::string& a_path)
        {
            RequireObject(a_json, a_path);
            Json         rest = a_json;
            VersionBlock out;
            out.formatVersion  = TakeRequired<std::int64_t>(rest, "formatVersion", a_path);
            out.runtimeVersion = TakeRequired<std::int64_t>(rest, "runtimeVersion", a_path);
            out.signature      = TakeRequired<std::int64_t>(rest, "signature", a_path);
            out.skseVersion    = TakeRequired<std::int64_t>(rest, "skseVersion", a_path);
            out.extras         = std::move(rest);
            return out;
        }

        template <class T, class Fn>
        std::vector<T> ParseArray(const Json& a_json, const std::string& a_path, Fn a_parse)
        {
            RequireArray(a_json, a_path);
            std::vector<T> out;
            out.reserve(a_json.size());
            for (std::size_t i = 0; i < a_json.size(); ++i) {
                out.push_back(a_parse(a_json[i], std::format("{}[{}]", a_path, i)));
            }
            return out;
        }

        // Serialization helpers: start from the extras blob so unknown keys
        // survive, then add the typed fields on top.

        Json BaseObject(const Json& a_extras)
        {
            return a_extras.is_object() ? a_extras : Json::object();
        }

        Json EmitActor(const ActorData& a_actor)
        {
            Json out = BaseObject(a_actor.extras);
            if (a_actor.hairColor) {
                out["hairColor"] = *a_actor.hairColor;
            }
            if (a_actor.headTexture) {
                out["headTexture"] = *a_actor.headTexture;
            }
            out["weight"] = a_actor.weight;
            return out;
        }

        Json EmitHeadPart(const HeadPart& a_part)
        {
            Json out = BaseObject(a_part.extras);
            if (a_part.formId) {
                out["formId"] = *a_part.formId;
            }
            out["formIdentifier"] = a_part.formIdentifier;
            if (a_part.type) {
                out["type"] = *a_part.type;
            }
            return out;
        }

        Json EmitFaceTexture(const FaceTexture& a_tex)
        {
            Json out       = BaseObject(a_tex.extras);
            out["index"]   = a_tex.index;
            out["texture"] = a_tex.texture;
            return out;
        }

        Json EmitTintLayer(const TintLayer& a_tint)
        {
            Json out       = BaseObject(a_tint.extras);
            out["color"]   = a_tint.color;
            out["index"]   = a_tint.index;
            out["texture"] = a_tint.texture;
            return out;
        }

        Json EmitBodyMorph(const BodyMorph& a_morph)
        {
            Json out  = BaseObject(a_morph.extras);
            Json keys = Json::array();
            for (const auto& key : a_morph.keys) {
                Json k     = BaseObject(key.extras);
                k["key"]   = key.key;
                k["value"] = key.value;
                keys.push_back(std::move(k));
            }
            out["keys"] = std::move(keys);
            out["name"] = a_morph.name;
            return out;
        }

        Json EmitModEntry(const ModEntry& a_mod)
        {
            Json out     = BaseObject(a_mod.extras);
            out["index"] = a_mod.index;
            out["name"]  = a_mod.name;
            return out;
        }

        Json EmitMorphs(const MorphsData& a_morphs)
        {
            Json out = BaseObject(a_morphs.extras);
            if (a_morphs.custom) {
                out["custom"] = *a_morphs.custom;
            }
            if (a_morphs.defaultMorphs) {
                Json def       = BaseObject(a_morphs.defaultMorphs->extras);
                def["morphs"]  = a_morphs.defaultMorphs->morphs;
                def["presets"] = a_morphs.defaultMorphs->presets;
                out["default"] = std::move(def);
            }
            if (a_morphs.sculpt) {
                out["sculpt"] = *a_morphs.sculpt;
            }
            if (a_morphs.sculptDivisor) {
                out["sculptDivisor"] = *a_morphs.sculptDivisor;
            }
            return out;
        }

        Json EmitVersion(const VersionBlock& a_version)
        {
            Json out              = BaseObject(a_version.extras);
            out["formatVersion"]  = a_version.formatVersion;
            out["runtimeVersion"] = a_version.runtimeVersion;
            out["signature"]      = a_version.signature;
            out["skseVersion"]    = a_version.skseVersion;
            return out;
        }
    }

    // --- Preset -------------------------------------------------------------

    Preset Preset::Parse(const Json& a_root)
    {
        RequireObject(a_root, "$");
        Json   rest = a_root;
        Preset out;

        if (auto actor = TakeRaw(rest, "actor")) {
            out.actor = ParseActor(*actor, "$.actor");
        } else {
            Fail("$", "required key 'actor' is missing");
        }

        if (auto bodyMorphs = TakeRaw(rest, "bodyMorphs")) {
            out.bodyMorphs = ParseArray<BodyMorph>(*bodyMorphs, "$.bodyMorphs", ParseBodyMorph);
        }
        if (auto faceTextures = TakeRaw(rest, "faceTextures")) {
            out.faceTextures =
                ParseArray<FaceTexture>(*faceTextures, "$.faceTextures", ParseFaceTexture);
        }

        if (auto headParts = TakeRaw(rest, "headParts")) {
            out.headParts = ParseArray<HeadPart>(*headParts, "$.headParts", ParseHeadPart);
        } else {
            Fail("$", "required key 'headParts' is missing");
        }

        if (auto modNames = TakeRaw(rest, "modNames")) {
            RequireArray(*modNames, "$.modNames");
            try {
                out.modNames = modNames->get<std::vector<std::string>>();
            } catch (const Json::exception& e) {
                Fail("$.modNames", e.what());
            }
        }
        if (auto mods = TakeRaw(rest, "mods")) {
            out.mods = ParseArray<ModEntry>(*mods, "$.mods", ParseModEntry);
        }

        if (auto morphs = TakeRaw(rest, "morphs")) {
            out.morphs = ParseMorphs(*morphs, "$.morphs");
        } else {
            Fail("$", "required key 'morphs' is missing");
        }

        if (auto tintInfo = TakeRaw(rest, "tintInfo")) {
            out.tintInfo = ParseArray<TintLayer>(*tintInfo, "$.tintInfo", ParseTintLayer);
        }

        if (auto version = TakeRaw(rest, "version")) {
            out.version = ParseVersion(*version, "$.version");
        } else {
            Fail("$", "required key 'version' is missing");
        }

        // Everything left over (overrides, skinOverrides, transforms, or any
        // future addition) is preserved verbatim.
        for (auto& [key, value] : rest.items()) {
            out.rawSections.emplace(key, value);
        }
        return out;
    }

    Json Preset::Serialize() const
    {
        Json out = Json::object();
        for (const auto& [key, value] : rawSections) {
            out[key] = value;
        }

        out["actor"] = EmitActor(actor);
        if (bodyMorphs) {
            Json arr = Json::array();
            for (const auto& morph : *bodyMorphs) {
                arr.push_back(EmitBodyMorph(morph));
            }
            out["bodyMorphs"] = std::move(arr);
        }
        if (faceTextures) {
            Json arr = Json::array();
            for (const auto& tex : *faceTextures) {
                arr.push_back(EmitFaceTexture(tex));
            }
            out["faceTextures"] = std::move(arr);
        }
        {
            Json arr = Json::array();
            for (const auto& part : headParts) {
                arr.push_back(EmitHeadPart(part));
            }
            out["headParts"] = std::move(arr);
        }
        if (modNames) {
            out["modNames"] = *modNames;
        }
        if (mods) {
            Json arr = Json::array();
            for (const auto& mod : *mods) {
                arr.push_back(EmitModEntry(mod));
            }
            out["mods"] = std::move(arr);
        }
        out["morphs"] = EmitMorphs(morphs);
        if (tintInfo) {
            Json arr = Json::array();
            for (const auto& tint : *tintInfo) {
                arr.push_back(EmitTintLayer(tint));
            }
            out["tintInfo"] = std::move(arr);
        }
        out["version"] = EmitVersion(version);
        return out;
    }

    std::string Preset::ToText() const
    {
        // indent 3, ensure_ascii - matches the style and the pure-ASCII
        // property observed in every real file.
        return Serialize().dump(3, ' ', true) + "\n";
    }

    // --- CatalogIndex -------------------------------------------------------

    std::string CatalogIndex::Normalize(const std::string& a_identifier)
    {
        std::string out = a_identifier;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char a_ch) {
            return static_cast<char>(std::tolower(a_ch));
        });
        return out;
    }

    void CatalogIndex::AddHeadPart(const std::string& a_formIdentifier)
    {
        _headParts.insert(Normalize(a_formIdentifier));
    }

    void CatalogIndex::AddTextureSet(const std::string& a_formIdentifier)
    {
        _textureSets.insert(Normalize(a_formIdentifier));
    }

    bool CatalogIndex::HasHeadPart(const std::string& a_formIdentifier) const
    {
        return _headParts.contains(Normalize(a_formIdentifier));
    }

    bool CatalogIndex::HasTextureSet(const std::string& a_formIdentifier) const
    {
        return _textureSets.contains(Normalize(a_formIdentifier));
    }

    // --- writer -------------------------------------------------------------

    namespace
    {
        std::optional<std::string> PluginNameOf(const std::string& a_identifier)
        {
            const auto bar = a_identifier.rfind('|');
            if (bar == std::string::npos || bar == 0) {
                return std::nullopt;
            }
            return a_identifier.substr(0, bar);
        }
    }

    VersionBlock ObservedVersionBlock()
    {
        VersionBlock version;
        version.formatVersion  = 3;           // PresetHeader::kVersion
        version.runtimeVersion = 17039392;    // RUNTIME_VERSION_1_4_2 (constant, not the running game)
        version.signature      = 1163086675;  // 0x45534B53, "SKSE" little-endian
        version.skseVersion    = 33685600;    // PACKED_SKSE_VERSION as of the observed corpus
        return version;
    }

    Preset BuildPreset(const WriterChoices& a_choices)
    {
        if (!a_choices.vanillaMorphs.empty() && a_choices.vanillaMorphs.size() != 19) {
            throw ParseError(std::format(
                "writer: vanillaMorphs must have exactly 19 entries, got {}",
                a_choices.vanillaMorphs.size()));
        }
        if (!a_choices.facePresets.empty() && a_choices.facePresets.size() != 4) {
            throw ParseError(std::format(
                "writer: facePresets must have exactly 4 entries, got {}",
                a_choices.facePresets.size()));
        }

        Preset out;
        out.actor.hairColor   = a_choices.hairColor;
        out.actor.headTexture = a_choices.headTextureIdentifier;
        out.actor.weight      = a_choices.weight;

        // Only formIdentifier is emitted per head part. The skee64 source
        // confirms formId is the raw runtime FormID (load-order dependent)
        // and the loader resolves by formIdentifier; real presets attest
        // that formId/type may be absent.
        for (const auto& identifier : a_choices.headPartIdentifiers) {
            HeadPart part;
            part.formIdentifier = identifier;
            out.headParts.push_back(std::move(part));
        }

        // modNames = sorted unique plugin names derived from every emitted
        // identifier; mods = [] - both shapes attested by real files.
        std::vector<std::string> names;
        for (const auto& identifier : a_choices.headPartIdentifiers) {
            if (auto name = PluginNameOf(identifier)) {
                names.push_back(*std::move(name));
            }
        }
        if (a_choices.headTextureIdentifier) {
            if (auto name = PluginNameOf(*a_choices.headTextureIdentifier)) {
                names.push_back(*std::move(name));
            }
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        out.modNames = std::move(names);
        out.mods     = std::vector<ModEntry>{};

        // custom: null and sculpt: null are both attested in real files;
        // sculpt is out of scope by design. When extended
        // slider values are present, custom becomes the attested array shape
        // [{"name": ..., "value": ...}].
        out.morphs.custom        = Json(nullptr);
        if (!a_choices.customMorphs.empty()) {
            Json custom = Json::array();
            for (const auto& morph : a_choices.customMorphs) {
                custom.push_back(Json{ { "name", morph.name },
                    { "value", morph.value } });
            }
            out.morphs.custom = std::move(custom);
        }
        out.morphs.sculpt        = Json(nullptr);
        out.morphs.sculptDivisor = 10000;
        if (!a_choices.vanillaMorphs.empty() || !a_choices.facePresets.empty()) {
            DefaultMorphs def;
            def.morphs  = a_choices.vanillaMorphs;
            def.presets = a_choices.facePresets;
            out.morphs.defaultMorphs = std::move(def);
        }

        out.faceTextures = a_choices.faceTextures;
        out.tintInfo     = a_choices.tints;
        out.version      = a_choices.version;
        return out;
    }

    std::vector<std::string> FindUnresolvedReferences(
        const Preset&       a_preset,
        const CatalogIndex& a_catalog)
    {
        std::vector<std::string> unresolved;
        for (const auto& part : a_preset.headParts) {
            if (!a_catalog.HasHeadPart(part.formIdentifier)) {
                unresolved.push_back("headPart: " + part.formIdentifier);
            }
        }
        if (a_preset.actor.headTexture &&
            !a_catalog.HasTextureSet(*a_preset.actor.headTexture)) {
            unresolved.push_back("headTexture: " + *a_preset.actor.headTexture);
        }
        return unresolved;
    }
}
