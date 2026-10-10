#include "PatchBrowser.h"
#include "../PluginEditor.h"
#include "../Presets/Factory.h"

namespace tgui
{

static const juce::String kAll = "All", kFavs = "Favourites", kUser = "Your patches";

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

void Favourites::save() const { file.replaceWithText (keys.joinIntoString ("\n") + "\n"); }

//==============================================================================
void PatchCatalogue::rescan()
{
    entries.clearQuick(); factoryCats.clear(); folders.clear();
    const auto& fp = tg::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
    {
        PatchEntry e; e.factory = true; e.index = i;
        e.name = fp[(size_t) i].name; e.category = fp[(size_t) i].category; e.description = fp[(size_t) i].description;
        factoryCats.addIfNotAlreadyThere (e.category);
        entries.add (e);
    }
    const auto folder = MegaSynthProcessor::getPatchFolder();
    for (auto& f : MegaSynthProcessor::getPatchFiles())
    {
        PatchEntry e; e.file = f; e.name = f.getFileNameWithoutExtension();
        const auto parent = f.getParentDirectory();
        e.category = parent == folder ? kUser : parent.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
        if (parent != folder) folders.addIfNotAlreadyThere (e.category);
        entries.add (e);
    }
    folders.sortNatural();
}

juce::StringArray PatchCatalogue::categories() const
{
    juce::StringArray c { kAll, kFavs };
    c.addArray (factoryCats);
    c.add (kUser);
    for (auto& f : folders) c.add (f);
    return c;
}

juce::Array<int> PatchCatalogue::filter (const juce::String& category, const juce::String& search, const Favourites& favs) const
{
    juce::StringArray words; words.addTokens (search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();
    juce::Array<int> out;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        bool in = category == kAll || category.isEmpty()
               || (category == kFavs && favs.contains (e.key()))
               || (category == kUser && ! e.factory)          // "Your patches" shows every saved patch, folders included
               || e.category == category;
        if (! in) continue;
        const auto hay = (e.name + " " + e.category + " " + e.description).toLowerCase();
        bool all = true;
        for (auto& w : words) if (! hay.contains (w)) { all = false; break; }
        if (all) out.add (i);
    }
    return out;
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
    catList.setRowHeight (26);
    resList.setRowHeight (30);
    search.setTextToShowWhenEmpty ("Search: name, category or sound (e.g. \"reese\", \"pad dark\")", col::muted);
    search.setColour (juce::TextEditor::backgroundColourId, col::panel2);
    search.setColour (juce::TextEditor::textColourId, col::text);
    search.setColour (juce::TextEditor::outlineColourId, col::border);
    search.setFont (juce::Font (juce::FontOptions (15.0f)));
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { if (shown.size() > 0) { resList.selectRow (0); resList.grabKeyboardFocus(); } };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (search);
    audition.setToggleState (true, juce::dontSendNotification);
    audition.setTooltip ("Loads each patch as you select it (arrow keys or click). Undo or Original goes back.");
    addAndMakeVisible (audition);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    folderBtn.onClick = [] { MegaSynthProcessor::getPatchFolder().revealToUser(); };
    addAndMakeVisible (closeBtn);
    addAndMakeVisible (folderBtn);
    hint.setText ("Arrow keys browse, Enter or double-click loads and closes, Esc closes. Click the star to add a favourite. "
                  "Folders inside your patch folder appear as categories.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, col::muted);
    hint.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (hint);
}

void PatchBrowser::open()
{
    cat.rescan();
    favs = Favourites();
    const auto prevCat = catList.getSelectedRow() >= 0 ? cats[catList.getSelectedRow()] : kAll;
    cats = cat.categories();
    catList.updateContent();
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

void PatchBrowser::refilter()
{
    const auto c = cats[juce::jmax (0, catList.getSelectedRow())];
    shown = cat.filter (c, search.getText(), favs);
    const bool was = suppressAudition;
    suppressAudition = true;
    resList.deselectAllRows();
    resList.updateContent();
    suppressAudition = was;
    repaint();
}

void PatchBrowser::load (int row)
{
    if (row < 0 || row >= shown.size()) return;
    const auto& e = cat[shown[row]];
    if (e.factory) { if (onLoadFactory) onLoadFactory (e.index); }
    else if (onLoadFile) onLoadFile (e.file);
}

void PatchBrowser::CatModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (sel) { g.setColour (col::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    const auto& c = b.cats[row];
    const bool heading = c == kAll || c == kFavs || c == kUser;
    g.setColour (sel ? col::text : col::text.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (13.5f, heading ? juce::Font::bold : juce::Font::plain)));
    g.drawText ((c == kFavs ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + c, 10, 0, w - 14, h, juce::Justification::centredLeft);
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
    g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"), 6, 0, 24, h, juce::Justification::centred);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (e.name, 36, 0, 230, h, juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (e.factory ? "Factory - " + e.category : e.category, 270, 0, 160, h, juce::Justification::centredLeft);
    g.drawText (e.description, 436, 0, w - 444, h, juce::Justification::centredLeft);
}

void PatchBrowser::ResultModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.x < 32 && row >= 0 && row < b.shown.size())
    {
        b.favs.toggle (b.cat[b.shown[row]].key());
        if (b.cats[juce::jmax (0, b.catList.getSelectedRow())] == kFavs) b.refilter();
        b.resList.repaint();
    }
}

void PatchBrowser::ResultModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x < 32) return;
    if (! b.audition.getToggleState()) b.load (row);
    if (b.onClose) b.onClose();
}

void PatchBrowser::ResultModel::selectedRowsChanged (int row)
{
    if (! b.suppressAudition && b.audition.getToggleState()) b.load (row);
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
    g.drawText (juce::String (shown.size()) + (shown.size() == 1 ? " patch" : " patches"), getWidth() - 360, 50, 130, 30, juce::Justification::centredRight);
}

void PatchBrowser::resized()
{
    const int W = getWidth(), H = getHeight();
    search.setBounds (110, 12, W - 520, 30);
    audition.setBounds (W - 400, 12, 170, 30);
    folderBtn.setBounds (W - 222, 12, 100, 30);
    closeBtn.setBounds (W - 114, 12, 100, 30);
    catList.setBounds (16, 54, 200, H - 90);
    resList.setBounds (226, 84, W - 242, H - 120);
    hint.setBounds (16, H - 32, W - 32, 24);
}

} // namespace tgui
