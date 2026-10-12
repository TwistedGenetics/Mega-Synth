#pragma once
// Patch browser: search, categories (the jungle / drum & bass categories, your patch folders), tags,
// authors, favourites and audition; each patch's information (category, tags, author, note) edited here,
// for one patch or many at once; deleting into a Deleted folder (and restoring); importing and exporting
// patch files with their information. It sits over the tab pages; loading goes through the editor so
// status / undo match the header's patch menu.
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../PatchMeta.h"

namespace tgui
{

struct PatchEntry
{
    juce::String name, category, description;   // category: the browser category ("Init" for the Init entry)
    tg::PatchMeta meta;
    bool factory = false, deleted = false;
    int index = -1;          // factory preset index (-1 with factory: the Init patch)
    juce::File file;         // user patch (or, deleted, where it waits in the Deleted folder)
    juce::String folder;     // user patch: its folder inside the patch folder ("" at the top)
    juce::String key() const;   // favourites key: "factory:<name>" or "file:<path relative to the patch folder>"
};

// Favourites live in favourites.txt in the patch folder, one key per line
class Favourites
{
public:
    explicit Favourites (juce::File f = MegaSynthProcessor::getPatchFolder().getChildFile ("favourites.txt"));
    bool contains (const juce::String& k) const { return keys.contains (k); }
    void toggle (const juce::String& k);
    void set (const juce::String& k, bool on) { if (on != contains (k)) toggle (k); }
    const juce::StringArray& all() const { return keys; }
private:
    void save() const;
    juce::File file;
    juce::StringArray keys;
};

// Patch files and their information, with no UI (tested on its own)
namespace patchlib
{
    tg::PatchMeta readMeta (const juce::File&);                       // (none: Uncategorised)
    bool writeMeta (const juce::File&, const tg::PatchMeta&);         // only the information changes, never the sound
    bool saveFactoryMeta (int index, const tg::PatchMeta&);           // your edits to a factory preset's information
    juce::File deletePatch (const juce::File&);                       // into the Deleted folder (its folder path kept); where it went
    juce::File restorePatch (const juce::File& deleted);              // back where it was (a new name if that's taken)
    bool deleteForever (const juce::File& deleted);
    juce::Array<juce::File> deletedFiles();
    juce::String originalPath (const juce::File& deleted);            // its path in the patch folder before it was deleted
    // patch files from anywhere copied into the patch folder, keeping their information; errors for any that aren't patches
    juce::Array<juce::File> importFiles (const juce::Array<juce::File>&, juce::StringArray& errors, const juce::String& intoFolder = {});
    bool exportFactory (int index, const juce::File& to);             // a factory preset as a patch file, with its information
    juce::File uniqueFile (const juce::File& dir, const juce::String& name, const juce::String& ext = ".megasynth");
}

// All the entries and the filtering, with no UI (tested on its own)
class PatchCatalogue
{
public:
    void rescan();
    // "All", "Favourites", the categories, "Factory", "Your patches", your folders, "Deleted"
    juce::StringArray categories() const;
    // entries shown for a category and a search (words match name, category, tags, author, note or description, in
    // any order), optionally only with a tag and / or by an author
    juce::Array<int> filter (const juce::String& category, const juce::String& search, const Favourites&,
                             const juce::String& tag = {}, const juce::String& author = {}) const;
    juce::StringArray allTags() const;
    juce::StringArray allAuthors() const;
    int count (const juce::String& category, const Favourites& f) const { return filter (category, {}, f).size(); }
    const PatchEntry& operator[] (int i) const { return entries.getReference (i); }
    int size() const { return entries.size(); }
    static const juce::String kAll, kFavs, kFactory, kUser, kDeleted;
private:
    juce::Array<PatchEntry> entries;
    juce::StringArray folders;
};

class PatchBrowser : public juce::Component
{
public:
    explicit PatchBrowser (MegaSynthProcessor&);
    std::function<void (int)> onLoadFactory;
    std::function<void (const juce::File&)> onLoadFile;
    std::function<void()> onLoadInit;
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;

    void open();   // rescans and shows
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // (for tests) select rows in the results; apply a category / tags to the selection
    void selectRowsForTests (const juce::Array<int>& rows);
    void setCategoryForSelection (const juce::String& category);
    void addTagsToSelection (const juce::StringArray& tags, bool remove);
    void setAuthorForSelection (const juce::String& author);
    void setNoteForSelection (const juce::String& note);
    void showCategory (const juce::String& c);
    void refresh() { rescanKeep(); }             // patches changed elsewhere (e.g. the header's Save): list them again
    // (for tests) what the panel and the filter menus show
    juce::StringArray authorFilterItems() const { juce::StringArray a; for (int i = 0; i < authorBox.getNumItems(); ++i) a.add (authorBox.getItemText (i)); return a; }
    juce::StringArray tagFilterItems() const { juce::StringArray a; for (int i = 0; i < tagBox.getNumItems(); ++i) a.add (tagBox.getItemText (i)); return a; }
    juce::StringArray authorChoices() const { juce::StringArray a; for (int i = 0; i < authorCb.getNumItems(); ++i) a.add (authorCb.getItemText (i)); return a; }
    void setAuthorFilterForTests (const juce::String& a) { for (int i = 0; i < authorBox.getNumItems(); ++i) if (authorBox.getItemText (i) == a) authorBox.setSelectedItemIndex (i, juce::sendNotificationSync); }
    juce::String authorFilterValue() const { return authorFilter; }
    juce::String panelCategory() const { return catBox.getText(); }
    juce::String panelAuthor() const { return authorCb.getText(); }
    juce::String panelNote() const { return noteEd.getText(); }
    int shownCount() const { return shown.size(); }
    const PatchEntry& shownEntry (int row) const { return cat[shown[row]]; }

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
        void deleteKeyPressed (int) override { b.deleteSelection(); }
    };
    void refilter (bool keepSelection = false);
    void load (int row);
    void selectionChanged();
    juce::Array<int> selectedEntries() const;    // catalogue indexes of the selected rows
    void saveInfo();
    void deleteSelection();
    void restoreSelection();
    void deleteForeverSelection();
    void importPatches();
    void exportSelection();
    void rescanKeep();                           // rescan, keeping the category, filters and selection
    void fillCategoryBox (juce::ComboBox&, const juce::String& current);
    void fillAuthorBox (const juce::String& current);
    void refreshFilterMenus();                   // the tag / author filters: what's there now (a vanished one goes back to Any)

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
    juce::TextButton closeBtn { "Close" }, folderBtn { "Show folder" }, importBtn { "Import..." }, exportBtn { "Export..." };
    juce::ComboBox tagBox, authorBox;
    juce::Label hint;
    // the information panel: one patch, or several at once
    juce::Label infoTitle, infoSub;
    juce::ComboBox catBox;
    juce::TextEditor tagsEd, noteEd;
    juce::ComboBox authorCb;                      // type a name, or pick one used before
    juce::Label catLbl { {}, "Category" }, tagsLbl { {}, "Tags" }, authorLbl { {}, "Author" }, noteLbl { {}, "Note" };
    juce::ToggleButton favBtn { "Favourite" };
    juce::TextButton saveInfoBtn { "Save info" }, addTagsBtn { "Add tags" }, removeTagsBtn { "Remove tags" },
                     deleteBtn { "Delete" }, restoreBtn { "Restore" }, foreverBtn { "Delete forever" },
                     setAuthorBtn { "Set author" }, setNoteBtn { "Set note" }, forgetAuthorBtn { "Forget" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool suppressAudition = false;
    juce::String tagFilter, authorFilter;
};

} // namespace tgui
