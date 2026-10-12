#pragma once
// Patch information that isn't sound: a category, tags, the author and a note. It travels inside every
// patch file (the "meta" block), so a patch keeps it when it's copied, exported or imported. Patches from
// before this have none and show as Uncategorised. The factory presets have theirs built in; edits to a
// factory preset's information are kept in factory_meta.json in the patch folder (the presets themselves
// can't change).
#include <JuceHeader.h>

namespace tg
{

struct PatchMeta
{
    juce::String category;     // one of patchCategories() (anything else shows as it is, under its own name)
    juce::StringArray tags;    // free words, lower case
    juce::String author, note;

    static const juce::String& uncategorised() { static const juce::String u ("Uncategorised"); return u; }
    juce::String categoryShown() const { return category.trim().isEmpty() ? uncategorised() : category.trim(); }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("category", categoryShown());
        juce::Array<juce::var> t; for (auto& s : tags) t.add (s);
        o->setProperty ("tags", t);
        o->setProperty ("author", author);
        o->setProperty ("note", note);
        return juce::var (o);
    }
    static PatchMeta fromVar (const juce::var& v)
    {
        PatchMeta m;
        if (! v.isObject()) { m.category = uncategorised(); return m; }
        m.category = v.getProperty ("category", uncategorised()).toString().trim();
        if (m.category.isEmpty()) m.category = uncategorised();
        if (auto* a = v.getProperty ("tags", {}).getArray()) for (auto& t : *a) m.tags.add (t.toString());
        else m.tags = splitTags (v.getProperty ("tags", {}).toString());
        m.tags = cleanTags (m.tags);
        m.author = v.getProperty ("author", {}).toString().trim();
        m.note = v.getProperty ("note", {}).toString().trim();
        return m;
    }
    // tags typed as "dark, reese  wide" (commas or spaces)
    static juce::StringArray splitTags (const juce::String& s)
    {
        juce::StringArray t; t.addTokens (s.replaceCharacter (',', ' '), " ", "\"");
        return cleanTags (t);
    }
    static juce::StringArray cleanTags (juce::StringArray t)
    {
        juce::StringArray out;
        for (auto s : t) { s = s.trim().toLowerCase(); if (s.isNotEmpty()) out.addIfNotAlreadyThere (s); }
        return out;
    }
    juce::String tagsText() const { return tags.joinIntoString (", "); }
    bool operator== (const PatchMeta& o) const { return categoryShown() == o.categoryShown() && tags == o.tags && author == o.author && note == o.note; }
};

// The categories, in the order the browser lists them
inline const juce::StringArray& patchCategories()
{
    static const juce::StringArray c { "Reese", "Sub & Bass", "Neuro", "Hoover & Rave", "Stabs & Chords", "Pads & Atmos",
                                       "Leads", "Breaks & Drums", "FX & Risers", "Keys & Plucks", PatchMeta::uncategorised() };
    return c;
}

} // namespace tg
