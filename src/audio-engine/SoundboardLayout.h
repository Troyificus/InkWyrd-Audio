#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "PlaylistLibrary.h" // for inkwyrd::scanFolderForAudio

// The SFX board as a fixed set of assignable, Stream-Deck-style slots,
// persisted to %APPDATA%\Inkwyrd Audio\soundboard.json.
//
// Before this, the board was simply "whatever audio files are in the
// soundboard folder, alphabetically" - so adding one file could shift
// every button along by one, and there was nowhere to put a name or a
// colour. A slot instead holds a specific sound at a specific position
// and stays put.
//
// A slot's NAME is what SoundboardEngine registers it under, which is
// also the string a Stream Deck button's payload carries. It therefore
// defaults to the file's name without its extension - exactly what the
// old folder scan produced - so existing Stream Deck buttons keep
// matching after the one-time migration. Names must stay unique for the
// same reason: the engine keys sounds by name.

struct SoundboardSlot
{
    juce::String name;   // the trigger key; empty for an unassigned slot
    juce::File file;     // {} for an unassigned slot

    // Plain ARGB rather than a juce::Colour, so the model stays inside
    // juce_core/juce_audio_* and the AudioEngine library doesn't have to
    // drag in juce_graphics. The GUI wraps it in juce::Colour.
    juce::uint32 colourArgb = 0xff3a4a5a;

    // Per-button volume trim in dB. A soundboard is a pile of clips from
    // all over the place, so their levels rarely match; 0 dB means
    // untouched and is what every existing button gets.
    float gainDb = 0.0f;

    // Optional picture drawn as the button's background. {} for none.
    juce::File imageFile;

    // Repeats until pressed again, instead of playing once - for an
    // ambience bed rather than an effect. See SoundboardEngine::trigger.
    bool loop = false;

    bool isEmpty() const { return file == juce::File(); }
};

namespace inkwyrd
{
    // Extension check only - deliberately in the model so the UI and the
    // stored layout can never disagree about what counts as an image.
    bool isImageFile(const juce::File& file);
}

class SoundboardLayout
{
public:
    explicit SoundboardLayout(juce::AudioFormatManager& formatManagerToUse);

    static juce::File getDefaultFile();

    // Overridable so tests can point at a scratch file.
    void setFile(const juce::File& file);
    juce::File getFile() const { return layoutFile; }

    void load();

    // Unreadable or newer-schema files. Surfaced in the UI rather than
    // thrown away.
    juce::StringArray getLoadWarnings() const { return loadWarnings; }

    int getNumSlots() const { return slots.size(); }
    const SoundboardSlot& getSlot(int index) const;
    bool isValidIndex(int index) const { return juce::isPositiveAndBelow(index, slots.size()); }

    // Grows the board if index is past the end, so a drop onto the last
    // row never silently does nothing. Returns false if the file isn't
    // playable. The name defaults to the filename without extension and
    // is de-duplicated with " (2)", " (3)" etc.
    bool assign(int index, const juce::File& file, const juce::String& desiredName = {});

    // Assigns files to index, then to each following EMPTY slot, growing
    // the board as needed. Returns how many were actually assigned.
    int assignFrom(int index, const juce::Array<juce::File>& files);

    void clearSlot(int index);

    // False if the name is empty or already used by another slot - the
    // engine keys sounds by name, so a duplicate would make one of them
    // untriggerable.
    bool rename(int index, const juce::String& newName);

    void setColour(int index, juce::uint32 colourArgb);

    void setLoop(int index, bool shouldLoop);

    // Swaps two slots, contents and all. Moving a button is a swap
    // rather than an insert so nothing else on the board shifts - a
    // board is arranged by where things ARE, and a rearranging insert
    // would move buttons the user never touched.
    //
    // Everything travels with the slot, INCLUDING ITS NAME: the name is
    // what the engine keys a sound by and what a Stream Deck button
    // sends, so a move must never renumber or rename anything.
    bool swapSlots(int a, int b);

    // A trim, like the per-track one: mostly "pull this clip down".
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = 6.0f;
    void setGainDb(int index, float db);
    float getLinearGain(int index) const;

    // Returns false if the file isn't an image this app recognises.
    bool setImage(int index, const juce::File& imageFile);
    void clearImage(int index);

    // Never removes a slot that has a sound in it: returns the count it
    // actually settled on.
    int setNumSlots(int count);

    // Files in the folder that aren't already on the board, dropped into
    // the free slots (growing it if needed). Used both for the one-time
    // migration from the old folder-scan behaviour and whenever the
    // sound-effects folder is changed in Settings - non-destructive
    // either way, so a board someone has arranged by hand is never
    // rearranged behind their back.
    int importFolder(const juce::File& folder);

    void save();

    // Slots with a sound in them, in board order.
    juce::Array<SoundboardSlot> getFilledSlots() const;

    // 2 adds per-button gain and background images; 3 adds the loop
    // flag. An older build reads this as "newer version", leaves the
    // file strictly alone and reports it, rather than rewriting it and
    // silently dropping what it doesn't understand.
    static constexpr int kCurrentSchemaVersion = 3;
    static constexpr int kDefaultSlotCount = 24;
    static constexpr int kMaxSlotCount = 256;

private:
    juce::String makeUniqueName(const juce::String& desiredName, int exceptIndex) const;

    juce::AudioFormatManager& formatManager;
    juce::File layoutFile;
    juce::Array<SoundboardSlot> slots;
    juce::StringArray loadWarnings;
};
