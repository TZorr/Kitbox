//
//  DrumParams.cpp
//  Kitbox
//

#include "DrumParams.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <map>

namespace transmute
{
    const char* modelId (DrumModel model)
    {
        switch (model)
        {
            case DrumModel::kick:  return "kick";
            case DrumModel::snare: return "snare";
            case DrumModel::hat:   return "hat";
            case DrumModel::modal: return "modal";
            case DrumModel::clap:  return "clap";
        }
        return "kick";
    }

    const char* modelTitle (DrumModel model)
    {
        switch (model)
        {
            case DrumModel::kick:  return "Kick / Tom";
            case DrumModel::snare: return "Snare";
            case DrumModel::hat:   return "Hi-Hat";
            case DrumModel::modal: return "Modal";
            case DrumModel::clap:  return "Clap";
        }
        return "Kick / Tom";
    }

    std::optional<DrumModel> modelFromId (std::string_view id)
    {
        for (auto model : allModels)
            if (id == modelId (model))
                return model;

        return std::nullopt;
    }

    namespace
    {
        const char* filterId (FilterType type)
        {
            switch (type)
            {
                case FilterType::off:      return "off";
                case FilterType::lowPass:  return "lowPass";
                case FilterType::bandPass: return "bandPass";
                case FilterType::highPass: return "highPass";
            }
            return "off";
        }

        std::optional<FilterType> filterFromId (std::string_view id)
        {
            for (auto type : { FilterType::off, FilterType::lowPass, FilterType::bandPass, FilterType::highPass })
                if (id == filterId (type))
                    return type;

            return std::nullopt;
        }

        // Swift's min(max(v, lo), hi).
        double clamp (double v, double lo, double hi) { return std::min (std::max (v, lo), hi); }
    }

    //==============================================================================
    bool AttackTable::operator== (const AttackTable&) const = default;
    bool DrumParams::operator== (const DrumParams&) const = default;

    //==============================================================================
    int DrumParams::burstCount() const
    {
        return std::max ((int) std::round (clapBursts), 1);
    }

    double DrumParams::tailStart() const
    {
        return (double) (burstCount() - 1) * clapSpacing;
    }

    DrumParams::ModalMembers DrumParams::modalMembers (int k)
    {
        switch (k)
        {
            case 2:  return { &DrumParams::modalRatio2, &DrumParams::modalLevel2, &DrumParams::modalDecay2 };
            case 3:  return { &DrumParams::modalRatio3, &DrumParams::modalLevel3, &DrumParams::modalDecay3 };
            case 4:  return { &DrumParams::modalRatio4, &DrumParams::modalLevel4, &DrumParams::modalDecay4 };
            case 5:  return { &DrumParams::modalRatio5, &DrumParams::modalLevel5, &DrumParams::modalDecay5 };
            default: return { &DrumParams::modalRatio6, &DrumParams::modalLevel6, &DrumParams::modalDecay6 };
        }
    }

    std::vector<DrumParams::Mode> DrumParams::modes() const
    {
        std::vector<Mode> out { { modalTone, 1.0, ampDecay } };

        for (int k = 2; k <= 6; ++k)
        {
            const auto m = modalMembers (k);
            if (this->*m.level > 0)
                out.push_back ({ modalTone * (this->*m.ratio), this->*m.level, this->*m.decay });
        }

        return out;
    }

    DrumParams DrumParams::clamped() const
    {
        auto p = *this;
        for (const auto& s : allSpecs())
            p.*s.member = clamp (p.*s.member, s.lower, s.upper);
        return p;
    }

    double DrumParams::fadeSeconds() const
    {
        return isHat() || isModal() || isClap() ? 0.03 : std::max (0.03, 3 / fundamental);
    }

    double DrumParams::tailSeconds() const
    {
        if (isHat())   return hatTailSeconds();
        if (isModal()) return modalTailSeconds();
        if (isClap())  return clapTailSeconds();

        const auto driveGain = drive > 1e-4 ? drive / std::tanh (drive) : 1.0;
        auto tail = delay + ampAttack + ampDecay * std::pow (std::log (1000 * driveGain), 1 / ampShape);

        if (transient > 0)
            tail = std::max (tail, delay + clickDecay * std::max (std::log (1000 * transient), 0.0));

        if (noise > 0)
        {
            const auto k = isSnare() ? noiseShape : 1.0;
            tail = std::max (tail, delay + noiseDecay * std::pow (std::max (std::log (3000 * noise), 0.0), 1 / k));
        }

        if (isSnare() && mode2Level > 0)
            tail = std::max (tail, delay + ampAttack + mode2Decay * std::max (std::log (1000 * mode2Level), 0.0));

        if (attack)
            tail = std::max (tail, attack->duration() + attack->hop);

        return tail;
    }

    double DrumParams::hatTailSeconds() const
    {
        auto tail = delay + ampAttack;
        const auto level = noise + metal;

        if (level > 0)
            tail += noiseDecay * std::pow (std::max (std::log (3000 * level), 0.0), 1 / noiseShape);
        if (transient > 0)
            tail = std::max (tail, delay + clickDecay * std::max (std::log (1000 * transient), 0.0));
        if (attack)
            tail = std::max (tail, attack->duration() + attack->hop);

        return tail;
    }

    double DrumParams::modalTailSeconds() const
    {
        auto tail = delay;

        for (const auto& mode : modes())
            if (mode.level > 0)
                tail = std::max (tail, delay + ampAttack + mode.decay * std::pow (std::max (std::log (1000 * mode.level), 0.0), 1 / ampShape));

        if (transient > 0)
            tail = std::max (tail, delay + clickDecay * std::max (std::log (1000 * transient), 0.0));
        if (noise > 0)
            tail = std::max (tail, delay + noiseDecay * std::max (std::log (3000 * noise), 0.0));
        if (attack)
            tail = std::max (tail, attack->duration() + attack->hop);

        return tail;
    }

    double DrumParams::clapTailSeconds() const
    {
        auto tail = delay + tailStart() + clapBurstDecay * std::log (3000.0);

        if (noise > 0)
            tail = std::max (tail, delay + tailStart() + noiseDecay * std::pow (std::max (std::log (3000 * noise), 0.0), 1 / noiseShape));
        if (transient > 0)
            tail = std::max (tail, delay + clickDecay * std::max (std::log (1000 * transient), 0.0));
        if (attack)
            tail = std::max (tail, attack->duration() + attack->hop);

        return tail;
    }

    double DrumParams::renderLength() const
    {
        const auto& range = spec ("length");
        auto seconds = autoLength ? tailSeconds() + fadeSeconds() : length;

        if (envelopeOn)
            seconds = std::min (seconds, envelopeEnd());

        return clamp (seconds, range.lower, range.upper);
    }

    double DrumParams::pitch (double t) const
    {
        return fundamental + (pitchStart - fundamental) * std::exp (-std::max (t - delay, 0.0) / pitchDecay);
    }

    //==============================================================================
    //  .drumparams
    namespace
    {
        template <typename Number>
        std::string number (Number value)
        {
            char buffer[64];
            const auto result = std::to_chars (buffer, buffer + sizeof (buffer), value);
            return std::string (buffer, result.ptr);
        }

        std::string quoted (const std::string& s) { return "\"" + s + "\""; }

        std::string attackJson (const AttackTable& table)
        {
            std::string bands, levels;

            for (size_t i = 0; i < table.bands.size(); ++i)
                bands += (i > 0 ? ", " : "") + number (table.bands[i]);

            for (size_t b = 0; b < table.levels.size(); ++b)
            {
                std::string row;
                for (size_t f = 0; f < table.levels[b].size(); ++f)
                    row += (f > 0 ? ", " : "") + number (table.levels[b][f]);

                levels += std::string (b > 0 ? ",\n" : "") + "      [" + row + "]";
            }

            return "{\n    \"bands\" : [" + bands + "],\n    \"hop\" : " + number (table.hop)
                 + ",\n    \"levels\" : [\n" + levels + "\n    ]\n  }";
        }
    }

    std::string DrumParams::toJson() const
    {
        // Every key Transmute's Codable writes; std::map keeps them sorted as
        // its encoder's .sortedKeys does.
        std::map<std::string, std::string> fields;

        for (const auto& s : allSpecs())
            fields[s.id] = number (this->*s.member);

        fields["model"]      = quoted (modelId (model));
        fields["filterType"] = quoted (filterId (filterType));
        fields["autoLength"] = autoLength ? "true" : "false";
        fields["envelopeOn"] = envelopeOn ? "true" : "false";

        if (attack)
            fields["attack"] = attackJson (*attack);

        std::string out = "{\n";
        size_t i = 0;

        for (const auto& [key, value] : fields)
            out += "  \"" + key + "\" : " + value + (++i < fields.size() ? ",\n" : "\n");

        return out + "}\n";
    }

    namespace
    {
        /** Just enough JSON for a .drumparams file, with numbers read exactly
            (std::from_chars). juce::JSON was tried first: it reads
            0.0005045075527284687 a few bits off, and a fit in Transmute
            continued from such a value is not the fit that wrote it. */
        struct Json
        {
            enum class Type { null, boolean, number, string, array, object } type = Type::null;
            bool boolean = false;
            std::string text;                                   // a string, or a number's digits
            std::vector<Json> items;                            // array
            std::vector<std::pair<std::string, Json>> fields;   // object

            const Json* find (std::string_view key) const
            {
                for (const auto& [name, value] : fields)
                    if (name == key)
                        return &value;
                return nullptr;
            }

            template <typename Number>
            std::optional<Number> as() const
            {
                Number value {};
                if (type != Type::number
                    || std::from_chars (text.data(), text.data() + text.size(), value).ec != std::errc())
                    return std::nullopt;
                return value;
            }
        };

        class JsonReader
        {
        public:
            explicit JsonReader (std::string_view source) : s (source) {}

            std::optional<Json> document()
            {
                auto value = parse();
                skip();
                if (! value || pos != s.size())
                    return std::nullopt;
                return value;
            }

        private:
            std::string_view s;
            size_t pos = 0;

            void skip()
            {
                while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t'))
                    ++pos;
            }

            bool literal (std::string_view word)
            {
                if (s.substr (pos, word.size()) != word)
                    return false;
                pos += word.size();
                return true;
            }

            std::optional<std::string> string()
            {
                if (pos >= s.size() || s[pos] != '"')
                    return std::nullopt;
                std::string out;
                for (++pos; pos < s.size(); ++pos)
                {
                    const char c = s[pos];
                    if (c == '"') { ++pos; return out; }
                    if (c == '\\')
                    {
                        if (++pos >= s.size()) return std::nullopt;
                        const char e = s[pos];
                        out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;   // \u is not in a .drumparams
                        continue;
                    }
                    out += c;
                }
                return std::nullopt;
            }

            std::optional<Json> parse()
            {
                skip();
                if (pos >= s.size())
                    return std::nullopt;

                Json value;
                const char c = s[pos];

                if (c == '{')
                {
                    value.type = Json::Type::object;
                    ++pos;
                    skip();
                    if (pos < s.size() && s[pos] == '}') { ++pos; return value; }
                    for (;;)
                    {
                        skip();
                        auto key = string();
                        skip();
                        if (! key || pos >= s.size() || s[pos] != ':') return std::nullopt;
                        ++pos;
                        auto item = parse();
                        if (! item) return std::nullopt;
                        value.fields.emplace_back (std::move (*key), std::move (*item));
                        skip();
                        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                        if (pos < s.size() && s[pos] == '}') { ++pos; return value; }
                        return std::nullopt;
                    }
                }

                if (c == '[')
                {
                    value.type = Json::Type::array;
                    ++pos;
                    skip();
                    if (pos < s.size() && s[pos] == ']') { ++pos; return value; }
                    for (;;)
                    {
                        auto item = parse();
                        if (! item) return std::nullopt;
                        value.items.push_back (std::move (*item));
                        skip();
                        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                        if (pos < s.size() && s[pos] == ']') { ++pos; return value; }
                        return std::nullopt;
                    }
                }

                if (c == '"')
                {
                    auto text = string();
                    if (! text) return std::nullopt;
                    value.type = Json::Type::string;
                    value.text = std::move (*text);
                    return value;
                }

                if (literal ("true"))  { value.type = Json::Type::boolean; value.boolean = true;  return value; }
                if (literal ("false")) { value.type = Json::Type::boolean; value.boolean = false; return value; }
                if (literal ("null"))  { return value; }

                const size_t begin = pos;
                while (pos < s.size() && (std::isdigit ((unsigned char) s[pos]) || s[pos] == '-' || s[pos] == '+'
                                          || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E'))
                    ++pos;
                if (pos == begin)
                    return std::nullopt;

                value.type = Json::Type::number;
                value.text = std::string (s.substr (begin, pos - begin));
                return value;
            }
        };
    }

    std::optional<DrumParams> DrumParams::fromJson (const std::string& text)
    {
        const auto json = JsonReader (text).document();
        if (! json || json->type != Json::Type::object)
            return std::nullopt;

        DrumParams p;

        for (const auto& s : allSpecs())
            if (const auto* value = json->find (s.id))
                if (const auto number = value->as<double>())
                    p.*s.member = *number;

        if (const auto* value = json->find ("model"); value != nullptr && value->type == Json::Type::string)
            if (auto model = modelFromId (value->text))
                p.model = *model;

        if (const auto* value = json->find ("filterType"); value != nullptr && value->type == Json::Type::string)
            if (auto type = filterFromId (value->text))
                p.filterType = *type;

        if (const auto* value = json->find ("autoLength"); value != nullptr && value->type == Json::Type::boolean)
            p.autoLength = value->boolean;

        if (const auto* value = json->find ("envelopeOn"); value != nullptr && value->type == Json::Type::boolean)
            p.envelopeOn = value->boolean;

        if (const auto* table = json->find ("attack"); table != nullptr && table->type == Json::Type::object)
        {
            AttackTable attack;
            bool valid = true;

            if (const auto* hop = table->find ("hop"))
                attack.hop = hop->as<double>().value_or (0);

            if (const auto* bands = table->find ("bands"))
            {
                for (const auto& band : bands->items)
                {
                    if (const auto value = band.as<double>())
                        attack.bands.push_back (*value);
                    else
                        valid = false;
                }
            }

            if (const auto* rows = table->find ("levels"))
            {
                for (const auto& row : rows->items)
                {
                    std::vector<float> levels;
                    for (const auto& item : row.items)
                    {
                        if (const auto value = item.as<float>())
                            levels.push_back (*value);
                        else
                            valid = false;
                    }
                    attack.levels.push_back (std::move (levels));
                }
            }

            if (valid && attack.bands.size() == attack.levels.size() && attack.hop > 0)
                p.attack = std::move (attack);
        }

        return p.clamped();
    }

    //==============================================================================
    const std::vector<ParamSpec>& allSpecs()
    {
        using P = DrumParams;

        static const std::vector<ParamSpec> specs {
            { "delay",          &P::delay,          0,       0.03,  false },
            { "fundamental",    &P::fundamental,    20,      1000,  true },
            { "pitchStart",     &P::pitchStart,     20,      4000,  true },
            { "pitchDecay",     &P::pitchDecay,     0.001,   1,     true },
            { "ampAttack",      &P::ampAttack,      0.00005, 0.05,  true },
            { "ampDecay",       &P::ampDecay,       0.005,   4,     true },
            { "ampShape",       &P::ampShape,       0.4,     4,     true },
            { "startPhase",     &P::startPhase,     -180,    180,   false },
            { "drive",          &P::drive,          0,       8,     false },
            { "transient",      &P::transient,      0,       2,     false },
            { "clickTone",      &P::clickTone,      200,     16000, true },
            { "clickDecay",     &P::clickDecay,     0.0002,  0.02,  true },
            { "noise",          &P::noise,          0,       1,     false },
            { "noiseTone",      &P::noiseTone,      100,     16000, true },
            { "noiseDecay",     &P::noiseDecay,     0.002,   2,     true },
            { "attackLevelDB",  &P::attackLevelDB,  -40,     12,    false },
            { "gainDB",         &P::gainDB,         -60,     6,     false },
            { "length",         &P::length,         0.05,    10,    true },
            { "filterCutoff",   &P::filterCutoff,   20,      20000, true },
            { "filterQ",        &P::filterQ,        0.5,     12,    true },
            { "envHold",        &P::envHold,        0.001,   4,     true },
            { "envRelease",     &P::envRelease,     0.005,   4,     true },
            { "envCurve",       &P::envCurve,       0.25,    8,     true },
            { "mode2Level",     &P::mode2Level,     0,       2,     false },
            { "mode2Ratio",     &P::mode2Ratio,     1.1,     4,     true },
            { "mode2Decay",     &P::mode2Decay,     0.002,   1,     true },
            { "noiseWidth",     &P::noiseWidth,     0.5,     8,     false },
            { "noiseShape",     &P::noiseShape,     0.4,     4,     true },
            { "metal",          &P::metal,          0,       1.5,   false },
            { "metalTone",      &P::metalTone,      100,     1000,  true },
            { "clapBursts",     &P::clapBursts,     1,       8,     false },
            { "clapSpacing",    &P::clapSpacing,    0.003,   0.03,  true },
            { "clapBurstDecay", &P::clapBurstDecay, 0.0005,  0.03,  true },
            { "clapTailTone",   &P::clapTailTone,   100,     16000, true },
            { "clapTailWidth",  &P::clapTailWidth,  0.5,     8,     false },
            { "modalTone",      &P::modalTone,      50,      10000, true },
            { "modalRatio2",    &P::modalRatio2,    0.25,    8,     true },
            { "modalLevel2",    &P::modalLevel2,    0,       2,     false },
            { "modalDecay2",    &P::modalDecay2,    0.002,   4,     true },
            { "modalRatio3",    &P::modalRatio3,    0.25,    8,     true },
            { "modalLevel3",    &P::modalLevel3,    0,       2,     false },
            { "modalDecay3",    &P::modalDecay3,    0.002,   4,     true },
            { "modalRatio4",    &P::modalRatio4,    0.25,    8,     true },
            { "modalLevel4",    &P::modalLevel4,    0,       2,     false },
            { "modalDecay4",    &P::modalDecay4,    0.002,   4,     true },
            { "modalRatio5",    &P::modalRatio5,    0.25,    8,     true },
            { "modalLevel5",    &P::modalLevel5,    0,       2,     false },
            { "modalDecay5",    &P::modalDecay5,    0.002,   4,     true },
            { "modalRatio6",    &P::modalRatio6,    0.25,    8,     true },
            { "modalLevel6",    &P::modalLevel6,    0,       2,     false },
            { "modalDecay6",    &P::modalDecay6,    0.002,   4,     true },
        };

        return specs;
    }

    const ParamSpec& spec (std::string_view id)
    {
        for (const auto& s : allSpecs())
            if (id == s.id)
                return s;

        return allSpecs().front();
    }
}
