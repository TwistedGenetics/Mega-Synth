#include "PatchBrowser.h"
#include "../PluginEditor.h"
#include "../Presets/Factory.h"

namespace tgui
{

const juce::String PatchCatalogue::kAll = "All", PatchCatalogue::kFavs = "Favourites", PatchCatalogue::kFactory = "Factory",
                   PatchCatalogue::kUser = "Your patches", PatchCatalogue::kDeleted = "Deleted";

juce::String PatchEntry::key() const
{
    return factory ? "factory:" + name : "file:" + file.getRelativePathFrom (MegaSynthProcessor::getPatchFolder()).replaceCharacter ('\\', '/');
}

//==============================================================================
Favourites::Favourites (juce::File f) : file (std::move (f))
{
    if (file.existsAsFile())
    {
        keys.addLines (file.loadFileAsString());
        keys.trim(); keys.removeEmptyStrings(); keys.removeDuplicates (false);
    }
}

void Favourites::toggle (const juce::String& k)
{
    if (keys.contains (k)) keys.removeString (k); else keys.add (k);
    save();
}

void Favourites::save() const { if (file != juce::File()) file.replaceWithText (keys.joinIntoString ("\n") + "\n"); }

//==============================================================================
namespace patchlib
{
    // reading a patch's information parses the whole file (it can hold samples), so it's kept per file and date
    struct MetaCache { juce::Time when; int64_t size = 0; tg::PatchMeta meta; };
    static std::map<juce::String, MetaCache>& cache() { static std::map<juce::String, MetaCache> c; return c; }

    tg::PatchMeta readMeta (const juce::File& f)
    {
        const auto key = f.getFullPathName();
        const auto when = f.getLastModificationTime();
        const auto size = f.getSize();
        auto& c = cache();
        if (auto it = c.find (key); it != c.end() && it->second.when == when && it->second.size == size) return it->second.meta;
        const auto v = juce::JSON::parse (f.loadFileAsString());
        auto m = tg::PatchMeta::fromVar (v.getProperty ("meta", {}));
        c[key] = { when, size, m };
        return m;
    }

    bool writeMeta (const juce::File& f, const tg::PatchMeta& m)
    {
        auto v = juce::JSON::parse (f.loadFileAsString());
        auto* o = v.getDynamicObject();
        if (o == nullptr) return false;
        o->setProperty ("meta", m.toVar());
        if (! f.replaceWithText (juce::JSON::toString (v, true))) return false;
        cache()[f.getFullPathName()] = { f.getLastModificationTime(), f.getSize(), m };
        return true;
    }

    bool saveFactoryMeta (int index, const tg::PatchMeta& m)
    {
        if (index < 0 || index >= (int) tg::factoryPresets().size()) return false;
        const auto f = MegaSynthProcessor::getFactoryMetaFile();
        auto all = juce::JSON::parse (f.loadFileAsString());
        if (! all.isObject()) all = juce::var (new juce::DynamicObject());
        const juce::String key = juce::String ("factory:") + tg::factoryPresets()[(size_t) index].name;
        if (m == tg::factoryMeta (index)) all.getDynamicObject()->removeProperty (key);   // back as it came: no edit kept
        else all.getDynamicObject()->setProperty (key, m.toVar());
        return f.replaceWithText (juce::JSON::toString (all, false));
    }

    juce::File uniqueFile (const juce::File& dir, const juce::String& name, const juce::String& ext)
    {
        auto base = juce::File::createLegalFileName (name.trim().isEmpty() ? juce::String ("Patch") : name.trim());
        auto f = dir.getChildFile (base + ext);
        for (int n = 2; f.exists(); ++n) f = dir.getChildFile (base + " " + juce::String (n) + ext);
        return f;
    }

    juce::File deletePatch (const juce::File& f)
    {
        const auto folder = MegaSynthProcessor::getPatchFolder(), bin = MegaSynthProcessor::getDeletedFolder();
        if (! f.existsAsFile() || f.isAChildOf (bin)) return {};
        const auto rel = f.isAChildOf (folder) ? f.getRelativePathFrom (folder) : f.getFileName();
        auto dst = bin.getChildFile (rel);
        dst.getParentDirectory().createDirectory();
        if (dst.exists()) dst = uniqueFile (dst.getParentDirectory(), dst.getFileNameWithoutExtension(), dst.getFileExtension());
        return f.moveFileTo (dst) ? dst : juce::File();
    }

    juce::String originalPath (const juce::File& deleted)
    {
        return deleted.getRelativePathFrom (MegaSynthProcessor::getDeletedFolder()).replaceCharacter ('\\', '/');
    }

    juce::File restorePatch (const juce::File& d)
    {
        const auto bin = MegaSynthProcessor::getDeletedFolder();
        if (! d.existsAsFile() || ! d.isAChildOf (bin)) return {};
        auto dst = MegaSynthProcessor::getPatchFolder().getChildFile (d.getRelativePathFrom (bin));
        dst.getParentDirectory().createDirectory();
        if (dst.exists()) dst = uniqueFile (dst.getParentDirectory(), dst.getFileNameWithoutExtension() + " (restored)", dst.getFileExtension());
        return d.moveFileTo (dst) ? dst : juce::File();
    }

    bool deleteForever (const juce::File& d)
    {
        return d.existsAsFile() && d.isAChildOf (MegaSynthProcessor::getDeletedFolder()) && d.deleteFile();
    }

    juce::Array<juce::File> deletedFiles()
    {
        auto files = MegaSynthProcessor::getDeletedFolder().findChildFiles (juce::File::findFiles, true, "*.megasynth");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }

    juce::Array<juce::File> importFiles (const juce::Array<juce::File>& in, juce::StringArray& errors, const juce::String& intoFolder)
    {
        juce::Array<juce::File> out;
        auto dir = MegaSynthProcessor::getPatchFolder();
        if (intoFolder.isNotEmpty()) { dir = dir.getChildFile (intoFolder); dir.createDirectory(); }
        for (auto& f : in)
        {
            const auto text = f.loadFileAsString();
            auto v = juce::JSON::parse (text.trim());
            if (! v.isObject() || ! (v.hasProperty ("params") || v.hasProperty ("plugin") || v.hasProperty ("version") || v.hasProperty ("name")))
            {
                errors.add (f.getFileName() + " isn't a Mega Synth patch");
                continue;
            }
            // (a patch with no information gets the Uncategorised block, so it's there to edit)
            if (! v.hasProperty ("meta")) v.getDynamicObject()->setProperty ("meta", tg::PatchMeta::fromVar ({}).toVar());
            const auto dst = uniqueFile (dir, f.getFileNameWithoutExtension());
            if (dst.replaceWithText (juce::JSON::toString (v, true))) out.add (dst);
            else errors.add ("Couldn't write " + dst.getFileName());
        }
        return out;
    }

    bool exportFactory (int index, const juce::File& to)
    {
        if (index < 0 || index >= (int) tg::factoryPresets().size()) return false;
        // built in a spare instance, so the current sound and its undo are left alone
        const bool wasInit = MegaSynthProcessor::applyInitOnCreate.load();
        MegaSynthProcessor::applyInitOnCreate = false;
        auto p = std::make_unique<MegaSynthProcessor>();
        MegaSynthProcessor::applyInitOnCreate = wasInit;
        if (! p->loadFactoryPreset (index)) return false;
        return to.replaceWithText (p->exportBrowserPatch());
    }
}

//==============================================================================
void PatchCatalogue::rescan()
{
    entries.clearQuick(); folders.clear();
    {
        PatchEntry init; init.factory = true; init.index = -1; init.name = "Init"; init.category = "Init";
        init.description = MegaSynthProcessor::hasUserInit() ? "Your saved Init (Init button > Save current patch as Init)"
                                                             : "A clean start: one saw, filter open, no effects. Mod wheel adds vibrato.";
        init.meta.category = "Init"; init.meta.note = init.description;
        entries.add (init);
    }
    const auto& fp = tg::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
    {
        PatchEntry e; e.factory = true; e.index = i;
        e.meta = MegaSynthProcessor::factoryMetaFor (i);
        e.name = fp[(size_t) i].name; e.category = e.meta.categoryShown(); e.description = fp[(size_t) i].description;
        entries.add (e);
    }
    const auto folder = MegaSynthProcessor::getPatchFolder();
    for (auto& f : MegaSynthProcessor::getPatchFiles())
    {
        PatchEntry e; e.file = f; e.name = f.getFileNameWithoutExtension();
        e.meta = patchlib::readMeta (f);
        e.category = e.meta.categoryShown();
        e.description = e.meta.note;
        const auto parent = f.getParentDirectory();
        if (parent != folder) { e.folder = parent.getRelativePathFrom (folder).replaceCharacter ('\\', '/'); folders.addIfNotAlreadyThere (e.folder); }
        entries.add (e);
    }
    for (auto& f : patchlib::deletedFiles())
    {
        PatchEntry e; e.file = f; e.name = f.getFileNameWithoutExtension(); e.deleted = true;
        e.meta = patchlib::readMeta (f);
        e.category = e.meta.categoryShown();
        e.description = "Deleted from " + patchlib::originalPath (f);
        entries.add (e);
    }
    folders.sortNatural();
}

juce::StringArray PatchCatalogue::categories() const
{
    juce::StringArray c { kAll, kFavs, "Init" };
    c.addArray (tg::patchCategories());
    // (a category typed into a patch by hand, or from elsewhere, still gets listed)
    juce::StringArray extra;
    for (auto& e : entries) if (! e.deleted && ! c.contains (e.category) && ! extra.contains (e.category)) extra.add (e.category);
    extra.sortNatural();
    c.addArray (extra);
    c.add (kFactory);
    c.add (kUser);
    for (auto& f : folders) c.add (f);
    c.add (kDeleted);
    return c;
}

juce::Array<int> PatchCatalogue::filter (const juce::String& category, const juce::String& search, const Favourites& favs,
                                         const juce::String& tag, const juce::String& author) const
{
    juce::StringArray words; words.addTokens (search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();
    juce::Array<int> out;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        bool in;
        if (category == kDeleted) in = e.deleted;
        else if (e.deleted) in = false;
        else in = category == kAll || category.isEmpty()
               || (category == kFavs && favs.contains (e.key()))
               || (category == kFactory && e.factory)
               || (category == kUser && ! e.factory)             // "Your patches" shows every saved patch, folders included
               || (! e.factory && e.folder.isNotEmpty() && e.folder == category)
               || e.category == category;
        if (! in) continue;
        if (tag.isNotEmpty() && ! e.meta.tags.contains (tag.toLowerCase())) continue;
        if (author.isNotEmpty() && e.meta.author != author) continue;
        const auto hay = (e.name + " " + e.category + " " + e.description + " " + e.meta.tags.joinIntoString (" ") + " " + e.meta.author + " " + e.meta.note
                          + (e.folder.isNotEmpty() ? " " + e.folder : juce::String())).toLowerCase();
        bool all = true;
        for (auto& w : words) if (! hay.contains (w)) { all = false; break; }
        if (all) out.add (i);
    }
    return out;
}

juce::StringArray PatchCatalogue::allTags() const
{
    juce::StringArray t;
    for (auto& e : entries) if (! e.deleted) for (auto& s : e.meta.tags) t.addIfNotAlreadyThere (s);
    t.sortNatural();
    return t;
}

juce::StringArray PatchCatalogue::allAuthors() const
{
    juce::StringArray a;
    for (auto& e : entries) if (! e.deleted && e.meta.author.isNotEmpty()) a.addIfNotAlreadyThere (e.meta.author);
    a.sortNatural();
    return a;
}

//==============================================================================
PatchBrowser::PatchBrowser (MegaSynthProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    for (auto* l : { &catList, &resList })
    {
        l->setColour (juce::ListBox::backgroundColourId, col::panel2);
        l->setColour (juce::ListBox::outlineColourId, col::border);
        l->setOutlineThickness (1);
        addAndMakeVisible (*l);
    }
    catList.setRowHeight (24);
    resList.setRowHeight (30);
    resList.setMultipleSelectionEnabled (true);
    search.setTextToShowWhenEmpty ("Search: name, category, tag, author or note (e.g. \"reese\", \"pad dark\")", col::muted);
    for (auto* t : { &search, &tagsEd, &authorEd, &noteEd })
    {
        t->setColour (juce::TextEditor::backgroundColourId, col::panel2);
        t->setColour (juce::TextEditor::textColourId, col::text);
        t->setColour (juce::TextEditor::outlineColourId, col::border);
        addAndMakeVisible (*t);
    }
    search.setFont (juce::Font (juce::FontOptions (15.0f)));
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { if (shown.size() > 0) { resList.selectRow (0); resList.grabKeyboardFocus(); } };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    noteEd.setMultiLine (true, true); noteEd.setReturnKeyStartsNewLine (true);
    tagsEd.setTextToShowWhenEmpty ("e.g. dark rolling 174", col::muted);
    authorEd.setTextToShowWhenEmpty ("your name", col::muted);
    noteEd.setTextToShowWhenEmpty ("How to play it, what the macros do...", col::muted);
    audition.setToggleState (true, juce::dontSendNotification);
    audition.setTooltip ("Loads each patch as you select it (arrow keys or click). Undo or Original goes back.");
    addAndMakeVisible (audition);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    folderBtn.onClick = [] { MegaSynthProcessor::getPatchFolder().revealToUser(); };
    importBtn.setTooltip ("Copy patch files (.megasynth or .json) into your patch folder, with their category, tags, author and note");
    exportBtn.setTooltip ("Save the selected patches (factory ones too) as patch files somewhere else, with their information");
    importBtn.onClick = [this] { importPatches(); };
    exportBtn.onClick = [this] { exportSelection(); };
    for (auto* b : { &closeBtn, &folderBtn, &importBtn, &exportBtn, &saveInfoBtn, &addTagsBtn, &removeTagsBtn, &deleteBtn, &restoreBtn, &foreverBtn })
        addAndMakeVisible (*b);
    for (auto* c : { &tagBox, &authorBox, &catBox }) addAndMakeVisible (*c);
    tagBox.onChange = [this] { tagFilter = tagBox.getSelectedId() > 1 ? tagBox.getText() : juce::String(); refilter(); };
    authorBox.onChange = [this] { authorFilter = authorBox.getSelectedId() > 1 ? authorBox.getText() : juce::String(); refilter(); };
    tagBox.setTooltip ("Only patches with this tag");
    authorBox.setTooltip ("Only patches by this author");
    saveInfoBtn.setTooltip ("Save this patch's category, tags, author and note into its file (the sound isn't touched). "
                            "For a factory preset your edits are kept alongside your patches.");
    saveInfoBtn.onClick = [this] { saveInfo(); };
    addTagsBtn.setTooltip ("Add the tags typed above to every selected patch");
    removeTagsBtn.setTooltip ("Remove the tags typed above from every selected patch");
    addTagsBtn.onClick = [this] { addTagsToSelection (tg::PatchMeta::splitTags (tagsEd.getText()), false); };
    removeTagsBtn.onClick = [this] { addTagsToSelection (tg::PatchMeta::splitTags (tagsEd.getText()), true); };
    catBox.onChange = [this]
    {
        // with several patches selected, choosing a category moves them all there at once
        if (selectedEntries().size() > 1 && catBox.getSelectedId() > 0) setCategoryForSelection (catBox.getText());
    };
    deleteBtn.setTooltip ("Move the selected patches to the Deleted folder (Restore brings them back)");
    restoreBtn.setTooltip ("Put the selected patches back where they were");
    foreverBtn.setTooltip ("Delete the selected patches for good");
    deleteBtn.onClick = [this] { deleteSelection(); };
    restoreBtn.onClick = [this] { restoreSelection(); };
    foreverBtn.onClick = [this] { deleteForeverSelection(); };
    favBtn.onClick = [this]
    {
        for (int i : selectedEntries()) favs.set (cat[i].key(), favBtn.getToggleState());
        resList.repaint();
        if (cats[juce::jmax (0, catList.getSelectedRow())] == PatchCatalogue::kFavs) refilter (true);
    };
    addAndMakeVisible (favBtn);
    for (auto* l : { &catLbl, &tagsLbl, &authorLbl, &noteLbl, &infoSub })
    {
        l->setColour (juce::Label::textColourId, col::muted);
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (*l);
    }
    infoTitle.setColour (juce::Label::textColourId, col::text);
    infoTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (infoTitle);
    hint.setText ("Arrow keys browse, Enter or double-click loads and closes, Esc closes. Shift / Cmd-click picks several (then the "
                  "category and tags apply to them all). Click the star to add a favourite.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, col::muted);
    hint.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (hint);
    selectionChanged();
}

void PatchBrowser::fillCategoryBox (juce::ComboBox& b, const juce::String& current)
{
    b.clear (juce::dontSendNotification);
    int id = 1;
    for (auto& c : tg::patchCategories()) b.addItem (c, id++);
    if (current.isNotEmpty() && ! tg::patchCategories().contains (current) && current != "Init") b.addItem (current, id++);
    for (int i = 0; i < b.getNumItems(); ++i) if (b.getItemText (i) == current) b.setSelectedItemIndex (i, juce::dontSendNotification);
}

void PatchBrowser::open()
{
    cat.rescan();
    favs = Favourites();
    const auto prevCat = catList.getSelectedRow() >= 0 ? cats[catList.getSelectedRow()] : PatchCatalogue::kAll;
    cats = cat.categories();
    catList.updateContent();
    // the tag and author filters list what's there now
    tagBox.clear (juce::dontSendNotification); tagBox.addItem ("Any tag", 1);
    int id = 2; for (auto& t : cat.allTags()) tagBox.addItem (t, id++);
    tagBox.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < tagBox.getNumItems(); ++i) if (tagFilter.isNotEmpty() && tagBox.getItemText (i) == tagFilter) tagBox.setSelectedItemIndex (i, juce::dontSendNotification);
    if (tagBox.getSelectedId() <= 1) tagFilter = {};
    authorBox.clear (juce::dontSendNotification); authorBox.addItem ("Any author", 1);
    id = 2; for (auto& a : cat.allAuthors()) authorBox.addItem (a, id++);
    authorBox.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < authorBox.getNumItems(); ++i) if (authorFilter.isNotEmpty() && authorBox.getItemText (i) == authorFilter) authorBox.setSelectedItemIndex (i, juce::dontSendNotification);
    if (authorBox.getSelectedId() <= 1) authorFilter = {};
    suppressAudition = true;
    catList.selectRow (juce::jmax (0, cats.indexOf (prevCat)));
    refilter();
    // select the current patch without reloading it
    const auto name = proc.getPatchName();
    for (int r = 0; r < shown.size(); ++r)
        if (cat[shown[r]].name == name) { resList.selectRow (r); break; }
    suppressAudition = false;
    setVisible (true);
    toFront (false);
    search.grabKeyboardFocus();
}

void PatchBrowser::rescanKeep()
{
    juce::StringArray keys;
    for (int i : selectedEntries()) keys.add (cat[i].key());
    const auto c = cats[juce::jmax (0, catList.getSelectedRow())];
    cat.rescan();
    cats = cat.categories();
    catList.updateContent();
    const bool was = suppressAudition;
    suppressAudition = true;
    catList.selectRow (juce::jmax (0, cats.indexOf (c)));
    refilter();
    juce::SparseSet<int> sel;
    for (int r = 0; r < shown.size(); ++r) if (keys.contains (cat[shown[r]].key())) sel.addRange ({ r, r + 1 });
    resList.setSelectedRows (sel);
    suppressAudition = was;
    selectionChanged();
}

void PatchBrowser::showCategory (const juce::String& c)
{
    const bool was = suppressAudition; suppressAudition = true;
    catList.selectRow (juce::jmax (0, cats.indexOf (c)));
    refilter();
    suppressAudition = was;
}

void PatchBrowser::refilter (bool keepSelection)
{
    juce::StringArray keys;
    if (keepSelection) for (int i : selectedEntries()) keys.add (cat[i].key());
    const auto c = cats[juce::jmax (0, catList.getSelectedRow())];
    shown = cat.filter (c, search.getText(), favs, tagFilter, authorFilter);
    const bool was = suppressAudition;
    suppressAudition = true;
    resList.deselectAllRows();
    resList.updateContent();
    if (keepSelection)
    {
        juce::SparseSet<int> sel;
        for (int r = 0; r < shown.size(); ++r) if (keys.contains (cat[shown[r]].key())) sel.addRange ({ r, r + 1 });
        resList.setSelectedRows (sel);
    }
    suppressAudition = was;
    selectionChanged();
    repaint();
}

juce::Array<int> PatchBrowser::selectedEntries() const
{
    juce::Array<int> out;
    const auto rows = resList.getSelectedRows();
    for (int k = 0; k < rows.size(); ++k) { const int r = rows[k]; if (r >= 0 && r < shown.size()) out.add (shown[r]); }
    return out;
}

void PatchBrowser::selectionChanged()
{
    const auto sel = selectedEntries();
    const bool one = sel.size() == 1, many = sel.size() > 1;
    const bool anyDeleted = [&] { for (int i : sel) if (cat[i].deleted) return true; return false; }();
    const bool anyUser = [&] { for (int i : sel) if (! cat[i].factory && ! cat[i].deleted) return true; return false; }();
    const bool isInit = one && cat[sel[0]].factory && cat[sel[0]].index < 0;
    if (one)
    {
        const auto& e = cat[sel[0]];
        infoTitle.setText (e.name, juce::dontSendNotification);
        infoSub.setText (e.deleted ? e.description : (e.factory ? juce::String ("Factory preset") : "Your patch" + (e.folder.isNotEmpty() ? " (" + e.folder + ")" : juce::String())),
                         juce::dontSendNotification);
        fillCategoryBox (catBox, e.meta.categoryShown());
        tagsEd.setText (e.meta.tagsText(), false);
        authorEd.setText (e.meta.author, false);
        noteEd.setText (e.meta.note, false);
        favBtn.setToggleState (favs.contains (e.key()), juce::dontSendNotification);
    }
    else
    {
        infoTitle.setText (many ? juce::String (sel.size()) + " patches selected" : juce::String ("No patch selected"), juce::dontSendNotification);
        infoSub.setText (many ? "Choose a category to move them all there; type tags to add or remove." : "", juce::dontSendNotification);
        fillCategoryBox (catBox, {});
        tagsEd.setText ({}, false); authorEd.setText ({}, false); noteEd.setText ({}, false);
        bool allFav = many; for (int i : sel) allFav = allFav && favs.contains (cat[i].key());
        favBtn.setToggleState (allFav, juce::dontSendNotification);
    }
    const bool editable = (one && ! isInit && ! anyDeleted) || (many && ! anyDeleted);
    catBox.setEnabled (editable); tagsEd.setEnabled (editable);
    authorEd.setEnabled (one && editable); noteEd.setEnabled (one && editable);
    saveInfoBtn.setVisible (one); saveInfoBtn.setEnabled (one && editable);
    addTagsBtn.setVisible (many); removeTagsBtn.setVisible (many);
    addTagsBtn.setEnabled (many && ! anyDeleted); removeTagsBtn.setEnabled (many && ! anyDeleted);
    favBtn.setEnabled ((one || many) && ! anyDeleted && ! isInit);
    deleteBtn.setVisible (! anyDeleted); deleteBtn.setEnabled (anyUser);
    restoreBtn.setVisible (anyDeleted); foreverBtn.setVisible (anyDeleted);
    exportBtn.setEnabled (sel.size() > 0 && ! isInit);
    resized();
}

void PatchBrowser::saveInfo()
{
    const auto sel = selectedEntries();
    if (sel.size() != 1) return;
    const auto& e = cat[sel[0]];
    if (e.deleted || (e.factory && e.index < 0)) return;
    tg::PatchMeta m;
    m.category = catBox.getText().trim().isEmpty() ? tg::PatchMeta::uncategorised() : catBox.getText().trim();
    m.tags = tg::PatchMeta::splitTags (tagsEd.getText());
    m.author = authorEd.getText().trim();
    m.note = noteEd.getText().trim();
    const bool ok = e.factory ? patchlib::saveFactoryMeta (e.index, m) : patchlib::writeMeta (e.file, m);
    if (ok && e.name == proc.getPatchName()) proc.setPatchMeta (m);   // (the patch that's playing: its information too)
    if (onStatus) onStatus (ok ? "Saved the information for " + e.name : "Couldn't save the information for " + e.name);
    rescanKeep();
}

void PatchBrowser::setCategoryForSelection (const juce::String& category)
{
    int n = 0;
    for (int i : selectedEntries())
    {
        const auto& e = cat[i];
        if (e.deleted || (e.factory && e.index < 0)) continue;
        auto m = e.meta; m.category = category;
        if (e.factory ? patchlib::saveFactoryMeta (e.index, m) : patchlib::writeMeta (e.file, m)) ++n;
        if (e.name == proc.getPatchName()) proc.setPatchMeta (m);
    }
    if (onStatus) onStatus (juce::String (n) + (n == 1 ? " patch" : " patches") + " moved to " + category);
    rescanKeep();
}

void PatchBrowser::addTagsToSelection (const juce::StringArray& tags, bool remove)
{
    if (tags.isEmpty()) return;
    int n = 0;
    for (int i : selectedEntries())
    {
        const auto& e = cat[i];
        if (e.deleted || (e.factory && e.index < 0)) continue;
        auto m = e.meta;
        for (auto& t : tags) { if (remove) m.tags.removeString (t); else m.tags.addIfNotAlreadyThere (t); }
        if (e.factory ? patchlib::saveFactoryMeta (e.index, m) : patchlib::writeMeta (e.file, m)) ++n;
        if (e.name == proc.getPatchName()) proc.setPatchMeta (m);
    }
    if (onStatus) onStatus (juce::String (remove ? "Tags removed from " : "Tags added to ") + juce::String (n) + (n == 1 ? " patch" : " patches"));
    rescanKeep();
}

void PatchBrowser::deleteSelection()
{
    int n = 0;
    for (int i : selectedEntries())
        if (! cat[i].factory && ! cat[i].deleted && patchlib::deletePatch (cat[i].file) != juce::File()) ++n;
    if (n == 0) return;
    if (onStatus) onStatus (juce::String (n) + (n == 1 ? " patch" : " patches") + " moved to Deleted (Restore brings them back)");
    rescanKeep();
}

void PatchBrowser::restoreSelection()
{
    int n = 0;
    for (int i : selectedEntries()) if (cat[i].deleted && patchlib::restorePatch (cat[i].file) != juce::File()) ++n;
    if (onStatus) onStatus (juce::String (n) + (n == 1 ? " patch" : " patches") + " restored");
    rescanKeep();
}

void PatchBrowser::deleteForeverSelection()
{
    juce::Array<juce::File> files;
    for (int i : selectedEntries()) if (cat[i].deleted) files.add (cat[i].file);
    if (files.isEmpty()) return;
    auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Delete for good?")
                    .withMessage ("Delete " + juce::String (files.size()) + (files.size() == 1 ? " patch" : " patches") + " for good? This can't be undone.")
                    .withButton ("Delete").withButton ("Cancel").withAssociatedComponent (this);
    juce::AlertWindow::showAsync (opts, [safe = juce::Component::SafePointer<PatchBrowser> (this), files] (int r)
    {
        if (safe == nullptr || r != 1) return;
        for (auto& f : files) patchlib::deleteForever (f);
        safe->rescanKeep();
    });
}

void PatchBrowser::importPatches()
{
    chooser = std::make_unique<juce::FileChooser> ("Import patches", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.megasynth;*.json;*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::StringArray errors;
                              const auto in = patchlib::importFiles (fc.getResults(), errors);
                              if (onStatus) onStatus (juce::String (in.size()) + (in.size() == 1 ? " patch" : " patches") + " imported"
                                                      + (errors.isEmpty() ? juce::String() : " (" + errors.joinIntoString ("; ") + ")"));
                              rescanKeep();
                          });
}

void PatchBrowser::exportSelection()
{
    const auto sel = selectedEntries();
    if (sel.isEmpty()) return;
    chooser = std::make_unique<juce::FileChooser> ("Export to a folder", juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, sel] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File() || ! dir.isDirectory()) return;
                              int n = 0;
                              for (int i : sel)
                              {
                                  const auto& e = cat[i];
                                  if (e.factory && e.index < 0) continue;
                                  const auto dst = patchlib::uniqueFile (dir, e.name);
                                  if (e.factory ? patchlib::exportFactory (e.index, dst) : e.file.copyFileTo (dst)) ++n;
                              }
                              if (onStatus) onStatus (juce::String (n) + (n == 1 ? " patch" : " patches") + " exported to " + dir.getFullPathName());
                          });
}

void PatchBrowser::selectRowsForTests (const juce::Array<int>& rows)
{
    juce::SparseSet<int> s; for (int r : rows) s.addRange ({ r, r + 1 });
    const bool was = suppressAudition; suppressAudition = true;
    resList.setSelectedRows (s);
    suppressAudition = was;
    selectionChanged();
}

void PatchBrowser::load (int row)
{
    if (row < 0 || row >= shown.size()) return;
    const auto& e = cat[shown[row]];
    if (e.deleted) return;   // (restore it first)
    if (e.factory && e.index < 0) { if (onLoadInit) onLoadInit(); }
    else if (e.factory) { if (onLoadFactory) onLoadFactory (e.index); }
    else if (onLoadFile) onLoadFile (e.file);
}

void PatchBrowser::CatModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (sel) { g.setColour (col::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    const auto& c = b.cats[row];
    const bool heading = c == PatchCatalogue::kAll || c == PatchCatalogue::kFavs || c == PatchCatalogue::kFactory || c == PatchCatalogue::kUser || c == PatchCatalogue::kDeleted;
    const bool folder = b.cat.categories().indexOf (c) > b.cat.categories().indexOf (PatchCatalogue::kUser) && c != PatchCatalogue::kDeleted;
    g.setColour (sel ? col::text : col::text.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (13.0f, heading ? juce::Font::bold : juce::Font::plain)));
    const auto label = (c == PatchCatalogue::kFavs ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + (folder ? "   " + c : c);
    g.drawText (label, 10, 0, w - 44, h, juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (juce::String (b.cat.count (c, b.favs)), w - 40, 0, 32, h, juce::Justification::centredRight);
}

void PatchBrowser::ResultModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row < 0 || row >= b.shown.size()) return;
    const auto& e = b.cat[b.shown[row]];
    if (sel) { g.setColour (col::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    else if (row % 2) { g.setColour (col::panel3.withAlpha (0.4f)); g.fillRect (0, 0, w, h); }
    const bool fav = b.favs.contains (e.key());
    g.setColour (fav ? col::supersaw : col::muted.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (16.0f)));
    if (! e.deleted) g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"), 6, 0, 24, h, juce::Justification::centred);
    g.setColour (e.deleted ? col::muted : col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawFittedText (e.name, 36, 0, 190, h, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ((e.factory && e.index >= 0 ? "Factory - " : "") + e.category, 232, 0, 150, h, juce::Justification::centredLeft);
    const auto tags = e.meta.tags.joinIntoString (" ");
    g.drawFittedText (e.description + (tags.isNotEmpty() ? "   #" + e.meta.tags.joinIntoString (" #") : juce::String()), 388, 0, w - 394, h, juce::Justification::centredLeft, 1, 0.9f);
}

void PatchBrowser::ResultModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.x < 32 && row >= 0 && row < b.shown.size() && ! b.cat[b.shown[row]].deleted)
    {
        b.favs.toggle (b.cat[b.shown[row]].key());
        if (b.cats[juce::jmax (0, b.catList.getSelectedRow())] == PatchCatalogue::kFavs) b.refilter (true);
        b.resList.repaint();
        b.selectionChanged();
    }
}

void PatchBrowser::ResultModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x < 32) return;
    if (row >= 0 && row < b.shown.size() && b.cat[b.shown[row]].deleted) return;
    if (! b.audition.getToggleState()) b.load (row);
    if (b.onClose) b.onClose();
}

void PatchBrowser::ResultModel::selectedRowsChanged (int row)
{
    b.selectionChanged();
    if (! b.suppressAudition && b.audition.getToggleState() && b.resList.getNumSelectedRows() == 1) b.load (row);
}

void PatchBrowser::ResultModel::returnKeyPressed (int row)
{
    if (! b.audition.getToggleState()) b.load (row);
    if (b.onClose) b.onClose();
}

bool PatchBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void PatchBrowser::paint (juce::Graphics& g)
{
    g.fillAll (col::bg.withAlpha (0.97f));
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("Patches", 16, 10, 200, 30, juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (juce::String (shown.size()) + (shown.size() == 1 ? " patch" : " patches"), getWidth() - 470, 50, 130, 28, juce::Justification::centredRight);
    // the information panel's frame
    const auto r = juce::Rectangle<int> (getWidth() - 330, 84, 314, getHeight() - 120).toFloat();
    g.setColour (col::panel2); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::border); g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void PatchBrowser::resized()
{
    const int W = getWidth(), H = getHeight();
    search.setBounds (110, 12, W - 520, 30);
    audition.setBounds (W - 400, 12, 170, 30);
    folderBtn.setBounds (W - 222, 12, 100, 30);
    closeBtn.setBounds (W - 114, 12, 100, 30);
    tagBox.setBounds (226, 52, 150, 26);
    authorBox.setBounds (384, 52, 150, 26);
    importBtn.setBounds (W - 330, 52, 100, 26);
    exportBtn.setBounds (W - 222, 52, 100, 26);
    catList.setBounds (16, 54, 200, H - 90);
    resList.setBounds (226, 84, W - 226 - 340, H - 120);
    // the information panel
    const int px = W - 320, pw = 294;
    int y = 92;
    infoTitle.setBounds (px, y, pw, 22); y += 22;
    infoSub.setBounds (px, y, pw, 18); y += 24;
    catLbl.setBounds (px, y, 70, 24); catBox.setBounds (px + 72, y, pw - 72, 24); y += 30;
    tagsLbl.setBounds (px, y, 70, 24); tagsEd.setBounds (px + 72, y, pw - 72, 24); y += 30;
    authorLbl.setBounds (px, y, 70, 24); authorEd.setBounds (px + 72, y, pw - 72, 24); y += 30;
    noteLbl.setBounds (px, y, 70, 24); noteEd.setBounds (px + 72, y, pw - 72, 96); y += 102;
    favBtn.setBounds (px, y, 110, 26);
    saveInfoBtn.setBounds (px + pw - 110, y, 110, 26);
    addTagsBtn.setBounds (px + 112, y, 90, 26); removeTagsBtn.setBounds (px + 204, y, 90, 26);
    y += 40;
    deleteBtn.setBounds (px, y, 110, 26);
    restoreBtn.setBounds (px, y, 110, 26); foreverBtn.setBounds (px + 118, y, 130, 26);
    hint.setBounds (16, H - 32, W - 32, 24);
}

} // namespace tgui
