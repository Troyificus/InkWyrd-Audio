#include "SoundboardLayout.h"

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeySlotCount = "slotCount";
    constexpr const char* kKeySlots = "slots";
    constexpr const char* kKeyIndex = "index";
    constexpr const char* kKeyName = "name";
    constexpr const char* kKeyPath = "path";
    constexpr const char* kKeyColour = "colour";
    constexpr const char* kKeyGainDb = "gainDb";
    constexpr const char* kKeyImage = "image";
    constexpr const char* kKeyLoop = "loop";
}

SoundboardLayout::SoundboardLayout(juce::AudioFormatManager& formatManagerToUse)
    : formatManager(formatManagerToUse), layoutFile(getDefaultFile())
{
    slots.resize(kDefaultSlotCount);
}

juce::File SoundboardLayout::getDefaultFile()
{
    // Beside the settings file and the Playlists folder, so everything
    // the app persists stays in one discoverable place.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("soundboard.json");
}

void SoundboardLayout::setFile(const juce::File& file)
{
    layoutFile = file;
}

const SoundboardSlot& SoundboardLayout::getSlot(int index) const
{
    static const SoundboardSlot empty;
    return isValidIndex(index) ? slots.getReference(index) : empty;
}

void SoundboardLayout::load()
{
    slots.clear();
    loadWarnings.clear();
    fileMustNotBeOverwritten = false;

    if (!layoutFile.existsAsFile())
    {
        slots.resize(kDefaultSlotCount);
        return;
    }

    auto parsed = juce::JSON::parse(layoutFile.loadFileAsString());
    if (!parsed.isObject())
    {
        // Not rewritten on the next edit either: a hand-edited file with a
        // typo in it is still the user's board, and replacing it with an
        // empty one would lose every button in it.
        loadWarnings.add(layoutFile.getFileName() + " could not be read (not valid JSON) - "
                          "showing an empty soundboard. The file is being kept as it is, so changes to the "
                          "soundboard won't be saved until it's fixed");
        fileMustNotBeOverwritten = true;
        slots.resize(kDefaultSlotCount);
        return;
    }

    auto schemaVersion = (int) parsed.getProperty(kKeySchemaVersion, 0);
    if (schemaVersion > kCurrentSchemaVersion)
    {
        // Leave it strictly alone, exactly as PlaylistLibrary does:
        // rewriting a newer version's file with older code would quietly
        // destroy whatever it added. That includes later edits - every
        // mutator saves, so save() itself has to refuse.
        loadWarnings.add(layoutFile.getFileName() + " was made by a newer version of Inkwyrd Audio "
                          "and was not loaded. It's being kept as it is, so changes to the soundboard won't be "
                          "saved in this version");
        fileMustNotBeOverwritten = true;
        slots.resize(kDefaultSlotCount);
        return;
    }

    auto count = juce::jlimit(1, kMaxSlotCount, (int) parsed.getProperty(kKeySlotCount, kDefaultSlotCount));
    slots.resize(count);

    if (auto* slotArray = parsed.getProperty(kKeySlots, juce::var()).getArray())
    {
        for (const auto& slotVar : *slotArray)
        {
            if (!slotVar.isObject())
                continue;

            auto index = (int) slotVar.getProperty(kKeyIndex, -1);
            auto path = slotVar.getProperty(kKeyPath, "").toString();
            if (!isValidIndex(index) || path.isEmpty())
                continue;

            SoundboardSlot slot;
            slot.file = juce::File(path);
            slot.name = slotVar.getProperty(kKeyName, slot.file.getFileNameWithoutExtension()).toString();
            slot.colourArgb = (juce::uint32) (juce::int64) slotVar.getProperty(kKeyColour, (juce::int64) 0xff3a4a5a);
            slot.gainDb = juce::jlimit(kMinGainDb, kMaxGainDb,
                                        (float) (double) slotVar.getProperty(kKeyGainDb, 0.0));

            slot.loop = slotVar.getProperty(kKeyLoop, false);

            auto imagePath = slotVar.getProperty(kKeyImage, "").toString();
            if (imagePath.isNotEmpty())
                slot.imageFile = juce::File(imagePath);

            // A missing file is kept, not dropped: an unplugged drive
            // must not silently wipe someone's board layout. The GUI
            // shows it greyed and the engine simply won't register it.
            if (slot.name.isEmpty())
                slot.name = slot.file.getFileNameWithoutExtension();

            slots.set(index, slot);
        }
    }
}

juce::String SoundboardLayout::makeUniqueName(const juce::String& desiredName, int exceptIndex) const
{
    auto isTaken = [this, exceptIndex](const juce::String& candidate)
    {
        for (int i = 0; i < slots.size(); ++i)
            if (i != exceptIndex && slots.getReference(i).name.equalsIgnoreCase(candidate))
                return true;

        return false;
    };

    if (!isTaken(desiredName))
        return desiredName;

    for (int suffix = 2; suffix < 1000; ++suffix)
    {
        auto candidate = desiredName + " (" + juce::String(suffix) + ")";
        if (!isTaken(candidate))
            return candidate;
    }

    return desiredName + " " + juce::Uuid().toDashedString();
}

bool SoundboardLayout::assign(int index, const juce::File& file, const juce::String& desiredName)
{
    if (index < 0 || index >= kMaxSlotCount)
        return false;

    if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
        return false;

    // A drop onto the last row shouldn't silently do nothing.
    if (index >= slots.size())
        slots.resize(index + 1);

    SoundboardSlot slot;
    slot.file = file;
    slot.colourArgb = slots.getReference(index).colourArgb; // keep a colour already set on this slot

    auto baseName = desiredName.isNotEmpty() ? desiredName : file.getFileNameWithoutExtension();
    slot.name = makeUniqueName(baseName, index);

    slots.set(index, slot);
    save();
    return true;
}

int SoundboardLayout::assignFrom(int index, const juce::Array<juce::File>& files)
{
    if (files.isEmpty() || index < 0)
        return 0;

    int assigned = 0;
    int cursor = index;

    for (const auto& file : files)
    {
        if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
            continue;

        // The dropped-on slot is overwritten deliberately (that's what
        // aiming at it means); everything after it only fills gaps, so a
        // multi-file drop can't wipe out sounds further along the board.
        if (assigned > 0)
            while (cursor < slots.size() && !slots.getReference(cursor).isEmpty())
                ++cursor;

        if (cursor >= kMaxSlotCount)
            break;

        SoundboardSlot slot;
        slot.file = file;
        if (isValidIndex(cursor))
            slot.colourArgb = slots.getReference(cursor).colourArgb;

        if (cursor >= slots.size())
            slots.resize(cursor + 1);

        slot.name = makeUniqueName(file.getFileNameWithoutExtension(), cursor);
        slots.set(cursor, slot);

        ++assigned;
        ++cursor;
    }

    if (assigned > 0)
        save();

    return assigned;
}

void SoundboardLayout::clearSlot(int index)
{
    if (!isValidIndex(index))
        return;

    slots.set(index, SoundboardSlot());
    save();
}

bool SoundboardLayout::rename(int index, const juce::String& newName)
{
    if (!isValidIndex(index))
        return false;

    auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return false;

    for (int i = 0; i < slots.size(); ++i)
        if (i != index && slots.getReference(i).name.equalsIgnoreCase(trimmed))
            return false; // the engine keys sounds by name - a duplicate hides one of them

    auto slot = slots.getReference(index);
    auto oldName = slot.name;
    slot.name = trimmed;
    slots.set(index, slot);
    save();

    if (oldName != trimmed && onSlotRenamed != nullptr)
        onSlotRenamed(oldName, trimmed);

    return true;
}

namespace inkwyrd
{
    bool isImageFile(const juce::File& file)
    {
        return file.hasFileExtension("png;jpg;jpeg;gif;bmp;webp");
    }
}

void SoundboardLayout::setGainDb(int index, float db)
{
    if (!isValidIndex(index))
        return;

    auto slot = slots.getReference(index);
    slot.gainDb = juce::jlimit(kMinGainDb, kMaxGainDb, db);
    slots.set(index, slot);
    save();
}

float SoundboardLayout::getLinearGain(int index) const
{
    auto db = getSlot(index).gainDb;
    return db == 0.0f ? 1.0f : juce::Decibels::decibelsToGain(db);
}

bool SoundboardLayout::setImage(int index, const juce::File& imageFile)
{
    if (!isValidIndex(index) || !inkwyrd::isImageFile(imageFile))
        return false;

    auto slot = slots.getReference(index);
    slot.imageFile = imageFile;
    slots.set(index, slot);
    save();
    return true;
}

void SoundboardLayout::clearImage(int index)
{
    if (!isValidIndex(index))
        return;

    auto slot = slots.getReference(index);
    slot.imageFile = juce::File();
    slots.set(index, slot);
    save();
}

void SoundboardLayout::setColour(int index, juce::uint32 colourArgb)
{
    if (!isValidIndex(index))
        return;

    auto slot = slots.getReference(index);
    slot.colourArgb = colourArgb;
    slots.set(index, slot);
    save();
}

void SoundboardLayout::setLoop(int index, bool shouldLoop)
{
    if (!isValidIndex(index))
        return;

    auto slot = slots.getReference(index);
    slot.loop = shouldLoop;
    slots.set(index, slot);
    save();
}

bool SoundboardLayout::swapSlots(int a, int b)
{
    if (!isValidIndex(a) || !isValidIndex(b) || a == b)
        return false;

    // Both slots whole, names included - see the header for why the name
    // in particular must travel rather than being reassigned.
    auto first = slots.getReference(a);
    auto second = slots.getReference(b);
    slots.set(a, second);
    slots.set(b, first);

    save();
    return true;
}

int SoundboardLayout::setNumSlots(int count)
{
    count = juce::jlimit(1, kMaxSlotCount, count);

    // Shrinking never discards a sound: stop at the last filled slot.
    if (count < slots.size())
    {
        int lastFilled = -1;
        for (int i = 0; i < slots.size(); ++i)
            if (!slots.getReference(i).isEmpty())
                lastFilled = i;

        count = juce::jmax(count, lastFilled + 1);
    }

    slots.resize(juce::jmax(1, count));
    save();
    return slots.size();
}

int SoundboardLayout::importFolder(const juce::File& folder)
{
    if (!folder.isDirectory())
        return 0;

    // RECURSIVE, and via the same shared scanner the playlists use. The
    // old folder scan this replaces was top-level only, which is the
    // exact trap that once made a music folder look empty because every
    // track sat in per-album subfolders - no reason to leave it set for
    // sound effects too. It sorts by full path, so for the flat folder
    // an existing user has, the order is identical to before and their
    // Stream Deck buttons line up unchanged.
    juce::Array<juce::File> found;
    for (const auto& file : inkwyrd::scanFolderForAudio(folder, formatManager, true))
    {
        // Already somewhere on the board - importing the same folder
        // twice must not produce duplicate buttons.
        bool alreadyPresent = false;
        for (const auto& slot : slots)
            if (slot.file == file)
                alreadyPresent = true;

        if (!alreadyPresent)
            found.add(file);
    }

    int imported = 0;
    for (const auto& file : found)
    {
        int target = -1;
        for (int i = 0; i < slots.size() && target < 0; ++i)
            if (slots.getReference(i).isEmpty())
                target = i;

        if (target < 0)
        {
            if (slots.size() >= kMaxSlotCount)
                break;

            target = slots.size();
            slots.resize(target + 1);
        }

        SoundboardSlot slot;
        slot.file = file;
        // Unchanged from the old folder scan - this string is what a
        // Stream Deck button's payload carries, so changing it would
        // silently stop every existing button matching.
        slot.name = makeUniqueName(file.getFileNameWithoutExtension(), target);
        slots.set(target, slot);
        ++imported;
    }

    if (imported > 0)
        save();

    return imported;
}

juce::Array<SoundboardSlot> SoundboardLayout::getFilledSlots() const
{
    juce::Array<SoundboardSlot> filled;
    for (const auto& slot : slots)
        if (!slot.isEmpty())
            filled.add(slot);

    return filled;
}

void SoundboardLayout::save()
{
    if (fileMustNotBeOverwritten)
        return;

    layoutFile.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);
    root->setProperty(kKeySlotCount, slots.size());

    // Sparse: only slots with a sound are written, each carrying its own
    // index, so changing the board size can't shift everything along.
    juce::Array<juce::var> slotVars;
    for (int i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots.getReference(i);
        if (slot.isEmpty())
            continue;

        juce::DynamicObject::Ptr slotObject = new juce::DynamicObject();
        slotObject->setProperty(kKeyIndex, i);
        slotObject->setProperty(kKeyName, slot.name);
        slotObject->setProperty(kKeyPath, slot.file.getFullPathName());
        slotObject->setProperty(kKeyColour, (juce::int64) slot.colourArgb);

        // Only written when set, so an untouched board's file stays as
        // small and readable as it was before these existed.
        if (slot.gainDb != 0.0f)
            slotObject->setProperty(kKeyGainDb, (double) slot.gainDb);

        if (slot.imageFile != juce::File())
            slotObject->setProperty(kKeyImage, slot.imageFile.getFullPathName());

        if (slot.loop)
            slotObject->setProperty(kKeyLoop, true);

        slotVars.add(juce::var(slotObject.get()));
    }
    root->setProperty(kKeySlots, juce::var(slotVars));

    auto json = juce::JSON::toString(juce::var(root.get()), false);

    // Atomic, same reasoning as PlaylistLibrary::save(): a half-written
    // board left by a crash mid-save would be unrecoverable.
    juce::TemporaryFile temp(layoutFile);
    if (temp.getFile().replaceWithText(json))
        temp.overwriteTargetFileWithTemporary();
}
