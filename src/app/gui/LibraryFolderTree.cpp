#include "LibraryFolderTree.h"

#include <algorithm>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shlwapi.h> // StrCmpLogicalW, the ordering Explorer itself uses
#endif

#include "InkwyrdTheme.h"

namespace inkwyrd
{
    namespace
    {
        // The same ordering Windows Explorer shows, which is the point of a
        // view of the folders on disk: the user should see their album in
        // the order their file manager does.
        //
        // NOT juce::String::compareNatural - it deliberately falls back to
        // plain text comparison as soon as either number carries a leading
        // zero, so a folder mixing "1 Seance" with "02 Alone" comes out in
        // an order no file manager would show.
        int compareForDisplay(const juce::String& a, const juce::String& b)
        {
           #if JUCE_WINDOWS
            return StrCmpLogicalW(a.toWideCharPointer(), b.toWideCharPointer());
           #else
            return a.compareNatural(b);
           #endif
        }

        // Case-insensitive, because Windows paths are: the same folder
        // reached two ways must not become two rows. juce::File's own
        // comparison already does this on Windows.
        FolderNode* findChild(FolderNode& parent, const juce::File& folder)
        {
            for (auto* child : parent.children)
                if (child->folder == folder)
                    return child;

            return nullptr;
        }

        FolderNode& findOrCreateChild(FolderNode& parent, const juce::File& folder)
        {
            if (auto* existing = findChild(parent, folder))
                return *existing;

            auto* created = parent.children.add(new FolderNode());
            created->folder = folder;

            // A drive root has no filename of its own, so fall back to the
            // whole path rather than showing a blank row.
            created->name = folder.getFileName();
            if (created->name.isEmpty())
                created->name = folder.getFullPathName();

            return *created;
        }

        // Every folder from the top-most one down to `folder` itself.
        juce::Array<juce::File> chainFromRoot(const juce::File& folder)
        {
            juce::Array<juce::File> chain;

            for (auto current = folder; current != juce::File();)
            {
                chain.insert(0, current);

                auto parent = current.getParentDirectory();
                if (parent == current) // a drive root is its own parent
                    break;

                current = parent;
            }

            return chain;
        }

        int computeTotals(FolderNode& node)
        {
            auto total = node.files.size();

            for (auto* child : node.children)
                total += computeTotals(*child);

            node.totalTrackCount = total;
            return total;
        }

        struct ChildSorter
        {
            static int compareElements(FolderNode* a, FolderNode* b)
            {
                return compareForDisplay(a->name, b->name);
            }
        };

        void sortNode(FolderNode& node)
        {
            // Explorer's own order, so "Track 2" comes before "Track 10"
            // the way it does in the folder itself.
            std::sort(node.files.begin(), node.files.end(),
                       [](const juce::File& a, const juce::File& b)
            {
                return compareForDisplay(a.getFileName(), b.getFileName()) < 0;
            });

            ChildSorter sorter;
            node.children.sort(sorter, true);

            for (auto* child : node.children)
                sortNode(*child);
        }

        // Collapses a run of folders that only lead to one other folder
        // into a single row, so a path like Music\Artist\Album isn't three
        // clicks deep when nothing branches along the way.
        void collapseSingleChildChains(FolderNode& node)
        {
            while (node.children.size() == 1 && node.files.isEmpty())
            {
                std::unique_ptr<FolderNode> only(node.children.removeAndReturn(0));

                node.name = node.name + juce::File::getSeparatorString() + only->name;
                node.folder = only->folder;
                node.files.swapWith(only->files);
                node.children.swapWith(only->children);
            }

            for (auto* child : node.children)
                collapseSingleChildChains(*child);
        }
    }

    std::unique_ptr<FolderNode> buildFolderTree(const juce::Array<juce::File>& tracks)
    {
        auto root = std::make_unique<FolderNode>();

        for (const auto& file : tracks)
        {
            if (file.getFullPathName().isEmpty())
                continue;

            auto* node = root.get();
            for (const auto& folder : chainFromRoot(file.getParentDirectory()))
                node = &findOrCreateChild(*node, folder);

            node->files.addIfNotAlreadyThere(file);
        }

        // Not the root itself - it is invisible, and collapsing into it
        // would swallow the top-level folder's own row.
        for (auto* child : root->children)
            collapseSingleChildChains(*child);

        sortNode(*root);
        computeTotals(*root);

        return root;
    }
}

//==============================================================================
namespace
{
    constexpr int kItemHeight = 22;
}

// Common ground for both kinds of row: "what tracks does this row stand
// for", which is what makes selecting a folder mean selecting an album.
class LibraryFolderTree::ItemBase : public juce::TreeViewItem
{
public:
    explicit ItemBase(LibraryFolderTree& ownerToUse) : owner(ownerToUse) {}

    virtual void collectTracks(juce::Array<juce::File>& into) const = 0;

    int getItemHeight() const override { return kItemHeight; }

    void itemSelectionChanged(bool) override
    {
        if (owner.onSelectionChanged != nullptr)
            owner.onSelectionChanged();
    }

protected:
    LibraryFolderTree& owner;
};

//==============================================================================
class LibraryFolderTree::TrackItem : public LibraryFolderTree::ItemBase
{
public:
    TrackItem(LibraryFolderTree& ownerToUse, const juce::File& fileToUse)
        : ItemBase(ownerToUse), file(fileToUse) {}

    bool mightContainSubItems() override { return false; }

    // Openness and selection are restored across rebuilds BY NAME, so this
    // has to identify the track rather than its position.
    juce::String getUniqueName() const override { return file.getFullPathName().toLowerCase(); }

    juce::File getFile() const { return file; }

    void collectTracks(juce::Array<juce::File>& into) const override { into.addIfNotAlreadyThere(file); }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        auto playing = file == owner.engine.getCurrentTrackFile();
        auto missing = ! file.existsAsFile();

        if (playing)
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }

        auto text = owner.trackMetadata.get(file).displayTitle(file);
        if (missing)
            text += "   (missing)";

        g.setColour(missing ? inkwyrd::theme::warning
                            : (playing ? inkwyrd::theme::accent : inkwyrd::theme::text));
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(text, juce::Rectangle<int>(6, 0, width - 12, height),
                    juce::Justification::centredLeft, true);
    }

    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        if (owner.onTracksDoubleClicked != nullptr)
            owner.onTracksDoubleClicked();
    }

private:
    juce::File file;
};

//==============================================================================
class LibraryFolderTree::FolderItem : public LibraryFolderTree::ItemBase
{
public:
    FolderItem(LibraryFolderTree& ownerToUse, const inkwyrd::FolderNode& nodeToUse, bool isRootToUse)
        : ItemBase(ownerToUse), node(nodeToUse), isRoot(isRootToUse)
    {
        // The invisible root's children are what the user sees first, so
        // they cannot wait for an openness change that never comes.
        if (isRoot)
            createSubItems();
    }

    bool mightContainSubItems() override
    {
        return node.children.size() > 0 || node.files.size() > 0;
    }

    juce::String getUniqueName() const override
    {
        return isRoot ? juce::String("root") : node.folder.getFullPathName().toLowerCase();
    }

    void collectTracks(juce::Array<juce::File>& into) const override
    {
        // Walks the MODEL rather than the sub-items: a folder that has
        // never been opened has no sub-items yet, and selecting it still
        // means every track under it.
        collectFrom(node, into);
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isRoot)
            return;

        auto area = juce::Rectangle<int>(6, 0, width - 12, height);

        // The count sits on the right, so a folder says how much is inside
        // it without being opened.
        auto countArea = area.removeFromRight(52);
        g.setColour(inkwyrd::theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(juce::String(node.totalTrackCount), countArea,
                    juce::Justification::centredRight, false);

        g.setColour(inkwyrd::theme::text);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(node.name, area, juce::Justification::centredLeft, true);
    }

    void itemOpennessChanged(bool isNowOpen) override
    {
        // Built on first open rather than up front: a library of a few
        // thousand tracks would otherwise pay for every row in every folder
        // before showing anything.
        if (isNowOpen && getNumSubItems() == 0)
            createSubItems();
    }

private:
    static void collectFrom(const inkwyrd::FolderNode& from, juce::Array<juce::File>& into)
    {
        for (const auto& file : from.files)
            into.addIfNotAlreadyThere(file);

        for (auto* child : from.children)
            collectFrom(*child, into);
    }

    void createSubItems()
    {
        for (auto* child : node.children)
            addSubItem(new FolderItem(owner, *child, false));

        for (const auto& file : node.files)
            addSubItem(new TrackItem(owner, file));
    }

    const inkwyrd::FolderNode& node;
    bool isRoot;
};

//==============================================================================
LibraryFolderTree::LibraryFolderTree(TrackMetadataStore& trackMetadataToUse, PlaylistEngine& engineToUse)
    : trackMetadata(trackMetadataToUse), engine(engineToUse)
{
    tree.setDefaultOpenness(false);
    tree.setRootItemVisible(false);
    tree.setMultiSelectEnabled(true);
    tree.setIndentSize(14);
    addAndMakeVisible(tree);

    setTracks({});
}

LibraryFolderTree::~LibraryFolderTree()
{
    // The items point at the model and at this object, both of which the
    // TreeView has to let go of first.
    tree.setRootItem(nullptr);
}

void LibraryFolderTree::resized()
{
    tree.setBounds(getLocalBounds());
}

void LibraryFolderTree::setTracks(const juce::Array<juce::File>& tracks)
{
    // Which folders were open, and which tracks were selected: a rebuild
    // (a track added, or the tag scan finishing) must not collapse the
    // tree back to the top and drop the selection.
    std::unique_ptr<juce::XmlElement> openness;
    if (rootItem != nullptr)
        openness = tree.getOpennessState(true);

    auto previousSelection = getSelectedTracks();

    tree.setRootItem(nullptr);
    rootItem.reset();

    model = inkwyrd::buildFolderTree(tracks);
    rootItem = std::make_unique<FolderItem>(*this, *model, true);
    tree.setRootItem(rootItem.get());

    if (openness != nullptr)
        tree.restoreOpennessState(*openness, false);

    if (! previousSelection.isEmpty())
        reselect(previousSelection);
}

void LibraryFolderTree::reselect(const juce::Array<juce::File>& tracks)
{
    // Only rows that EXIST can be selected, which means the ones inside
    // folders the openness restore above has re-opened. A track whose
    // folder is closed is not on screen to be selected.
    std::function<void(juce::TreeViewItem&)> visit = [&](juce::TreeViewItem& item)
    {
        for (int i = 0; i < item.getNumSubItems(); ++i)
        {
            auto* sub = item.getSubItem(i);
            if (sub == nullptr)
                continue;

            if (auto* track = dynamic_cast<TrackItem*>(sub))
                if (tracks.contains(track->getFile()))
                    track->setSelected(true, false, juce::dontSendNotification);

            visit(*sub);
        }
    };

    if (rootItem != nullptr)
        visit(*rootItem);
}

juce::Array<juce::File> LibraryFolderTree::getSelectedTracks() const
{
    juce::Array<juce::File> selected;

    for (int i = 0; i < tree.getNumSelectedItems(); ++i)
        if (auto* item = dynamic_cast<ItemBase*>(tree.getSelectedItem(i)))
            item->collectTracks(selected);

    return selected;
}
