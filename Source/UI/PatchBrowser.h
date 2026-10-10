#pragma once
// Patch browser: search, categories (factory categories and your patch folders), favourites and
// audition. It sits over the tab pages; loading goes through the editor so status / undo match the
// header's patch menu.
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

struct PatchEntry
{
    juce::String name, category, description;
    bool factory = false;
    int index = -1;          // factory preset index
    juce::File file;         // user patch
    juce::String key() const;   // favourites key: "factory:<name>" or "file:<path relative to the patch folder>"
};

// Favourites live in favourites.txt in the patch folder, one key per line
class Favourites
{
public:
    explicit Favourites (juce::File f = MegaSynthProcessor::getPatchFolder().getChildFile ("favourites.txt"));
    bool contains (const juce::String& k) const { return keys.contains (k); }
    void toggle (const juce::String& k);
    const juce::StringArray& all() const { return keys; }
private:
    void save() const;
    juce::File file;
    juce::StringArray keys;
};

// All the entries and the filtering, with no UI (tested on its own)
class PatchCatalogue
{
public:
    void rescan();
    juce::StringArray categories() const;     // "All", "Favourites", factory categories, "Your patches", folders
    // entries shown for a category and a search (words match name, category or description, in any order)
    juce::Array<int> filter (const juce::String& category, const juce::String& search, const Favourites&) const;
    const PatchEntry& operator[] (int i) const { return entries.getReference (i); }
    int size() const { return entries.size(); }
private:
    juce::Array<PatchEntry> entries;
    juce::StringArray factoryCats, folders;
};

class PatchBrowser : public juce::Component
{
public:
    explicit PatchBrowser (MegaSynthProcessor&);
    std::function<void (int)> onLoadFactory;
    std::function<void (const juce::File&)> onLoadFile;
    std::function<void()> onClose;

    void open();   // rescans and shows
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct CatModel : juce::ListBoxModel
    {
        PatchBrowser& b; explicit CatModel (PatchBrowser& o) : b (o) {}
        int getNumRows() override { return b.cats.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override { b.refilter(); }
    };
    struct ResultModel : juce::ListBoxModel
    {
        PatchBrowser& b; explicit ResultModel (PatchBrowser& o) : b (o) {}
        int getNumRows() override { return b.shown.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
        void selectedRowsChanged (int) override;
        void returnKeyPressed (int) override;
    };
    void refilter();
    void load (int row);

    MegaSynthProcessor& proc;
    PatchCatalogue cat;
    Favourites favs;
    juce::StringArray cats;
    juce::Array<int> shown;
    CatModel catModel { *this };
    ResultModel resModel { *this };
    juce::ListBox catList { "Categories", &catModel }, resList { "Patches", &resModel };
    juce::TextEditor search;
    juce::ToggleButton audition { "Load as you browse" };
    juce::TextButton closeBtn { "Close" }, folderBtn { "Show folder" };
    juce::Label hint;
    bool suppressAudition = false;
};

} // namespace tgui
