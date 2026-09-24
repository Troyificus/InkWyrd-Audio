#pragma once

#include <functional>
#include <map>
#include <vector>

#include <juce_core/juce_core.h>

// Scenes: one press that sets the whole room - which playlist, which
// ambience loops, and optionally the master volume. "Tavern", "Combat",
// "Storm".
//
// Two halves, both deliberately free of any window or audio device so
// the self-test can drive them:
//
//   - SceneLibrary, the list itself, saved to
//     %APPDATA%\Inkwyrd Audio\scenes.json.
//   - planScene(), which works out what pressing a scene should CHANGE,
//     given what is playing right now. Every rule that makes scenes feel
//     right lives there - see its comment - and the app only carries out
//     the plan it returns.

// How one sound fades when a scene starts or stops it. A scene keeps its
// own copy per sound, so the same rain can swell in slowly in "Storm" and
// cut in at once in "Combat".
struct SceneFades
{
    // "Use the scene transition" - the one length every scene used before
    // per-sound fades existed, and still the default for a loop whose
    // button has no fade of its own. Resolved by the app at press time.
    static constexpr double kSceneTransition = -1.0;

    double fadeInSeconds = kSceneTransition;
    double fadeOutSeconds = kSceneTransition;

    bool operator==(const SceneFades& other) const
    {
        return juce::approximatelyEqual(fadeInSeconds, other.fadeInSeconds)
                && juce::approximatelyEqual(fadeOutSeconds, other.fadeOutSeconds);
    }
};

// A one-shot the scene sets playing randomly - the seagull over the waves.
struct SceneRandom
{
    juce::String name;
    int frequency = 2; // 1 low, 2 medium, 3 high - RandomFrequency's values

    bool operator==(const SceneRandom& other) const
    {
        return name == other.name && frequency == other.frequency;
    }
};

struct Scene
{
    juce::Uuid id;
    juce::String name;

    // Plain ARGB, like a soundboard slot's colour, so this stays free of
    // juce_graphics.
    juce::uint32 colourArgb = 0xff2f4a8c;

    // What happens to the music. "Leave" lets an ambience-only scene -
    // "it starts raining" - change the room without interrupting the
    // track; "fade out" gives a deliberate silence.
    enum class Music { leave, playPlaylist, fadeOut };
    Music music = Music::leave;
    juce::Uuid playlistId; // only meaningful with playPlaylist

    // The looping soundboard buttons that should be running, by NAME -
    // the same key the engine and a Stream Deck use. This is the
    // COMPLETE set: pressing the scene stops any loop not listed.
    juce::StringArray loops;

    // One-shots this scene plays randomly, and how often. Like loops, the
    // COMPLETE set: pressing the scene switches random play off for any
    // sound not listed.
    std::vector<SceneRandom> randoms;

    // Per sound (loops and randoms alike), how it fades. A sound with no
    // entry uses the scene transition both ways - which is also exactly
    // how every scene saved before this existed behaves.
    std::map<juce::String, SceneFades> soundFades;

    SceneFades fadesFor(const juce::String& soundName) const
    {
        auto it = soundFades.find(soundName);
        return it == soundFades.end() ? SceneFades {} : it->second;
    }

    // Optional, and off by default: the master fader is also what Discord
    // hears, so a scene only moves it when told to.
    bool setsVolume = false;
    float volume = 1.0f; // 0..1, same scale as the master fader
};

// What pressing a scene should change. Built by planScene; carried out by
// the app.
struct ScenePlan
{
    bool switchPlaylist = false;
    juce::Uuid playlistId;

    bool fadeOutMusic = false;

    juce::StringArray loopsToStart, loopsToStop;

    // Random play to switch on (or change the pace of), and to switch off.
    std::vector<SceneRandom> randomsToStart;
    juce::StringArray randomsToStop;

    bool setVolume = false;
    float volume = 1.0f;

    // Things the scene refers to that aren't there any more, in words a
    // user would recognise ("the playlist it plays was deleted"). The
    // scene still does everything else it can.
    juce::StringArray problems;

    bool changesAnything() const
    {
        return switchPlaylist || fadeOutMusic || ! loopsToStart.isEmpty()
                || ! loopsToStop.isEmpty() || ! randomsToStart.empty()
                || ! randomsToStop.isEmpty() || setVolume;
    }
};

// What is true right now, as far as a scene cares.
struct SceneContext
{
    juce::Uuid activePlaylistId;

    // Playing AND not already fading out. A fading playlist counts as not
    // playing: it is on its way to silence, so a scene that wants it has
    // to bring music back.
    bool musicPlaying = false;

    std::function<bool(const juce::Uuid&)> playlistExists;

    // Looping soundboard sounds running now, NOT counting ones already
    // fading out (SoundboardEngine::getPlayingLoopNames).
    juce::StringArray runningLoops;

    // Sounds playing randomly right now, and how often.
    std::vector<SceneRandom> runningRandoms;

    // Every name on the board that is set to loop, and every name on the
    // board at all - the difference is how "isn't a loop any more" is
    // told apart from "isn't on the board".
    juce::StringArray loopingNames;
    juce::StringArray boardNames;
};

// The rules, in one place:
//
//   - Music: a scene's playlist is switched to only if it isn't ALREADY
//     the one playing. Re-activating the running playlist would crossfade
//     and jump track, so "Combat" pressed during combat must leave the
//     fight music alone.
//   - Ambience: the scene's loops are the complete set. Loops it lists
//     start; loops it doesn't list stop; a loop BOTH scenes share is in
//     neither list, so it runs on untouched - rain carries from "Road"
//     into "Storm" without a hiccup.
//   - Pressing the scene already in effect PUTS IT BACK: anything that
//     has drifted (a loop killed by the Killswitch, music stopped) is
//     restored, and anything already right is left alone.
//   - Random play follows the same "complete set" rule: sounds the scene
//     lists start playing randomly (or change pace); any other sound
//     playing randomly is switched off. One already at the right pace is
//     left alone, so re-pressing a scene doesn't reset its timing.
//   - Anything missing is skipped and reported; the rest still happens.
ScenePlan planScene(const Scene& scene, const SceneContext& context);

class SceneLibrary
{
public:
    SceneLibrary();

    static juce::File getDefaultFile();

    // Overridable so tests can point at a scratch file.
    void setFile(const juce::File& file);

    void load();
    void save();

    // A newer version's file, or one that can't be read. Surfaced in the
    // UI rather than thrown away.
    juce::StringArray getLoadWarnings() const { return loadWarnings; }

    int getNumScenes() const { return scenes.size(); }
    const Scene* getScene(int index) const;
    const Scene* findById(const juce::Uuid& id) const;

    // What a Stream Deck button sends. Exact first, then ignoring case -
    // a scene called "Combat" should answer to "combat" typed into a
    // Stream Deck setting, since names are unique ignoring case anyway.
    const Scene* findByName(const juce::String& name) const;

    // Adds at the end, with a fresh id and a name made unique (ignoring
    // case) by " (2)", " (3)"... Returns the id it was given.
    juce::Uuid add(Scene scene);

    // Replaces the scene with the same id. False if there isn't one, or
    // if the new name clashes with another scene's.
    bool update(const Scene& scene);

    void remove(const juce::Uuid& id);

    // Moves a scene one place earlier (-1) or later (+1) in the list.
    void move(const juce::Uuid& id, int delta);

    // A soundboard button was renamed. Every scene that used the old name
    // follows it - loops, random sounds and fade settings alike - so
    // renaming a button never quietly breaks a scene.
    // Returns how many scenes changed.
    int renameLoop(const juce::String& oldName, const juce::String& newName);

    bool isNameTaken(const juce::String& name, const juce::Uuid& exceptId) const;

    // 2 adds random sounds and per-sound fades.
    static constexpr int kCurrentSchemaVersion = 2;

private:
    juce::String makeUniqueName(const juce::String& desired, const juce::Uuid& exceptId) const;

    juce::File file;
    juce::Array<Scene> scenes;
    juce::StringArray loadWarnings;

    // Set when the file on disk came from a NEWER version, or couldn't be
    // read at all. Saving is then refused outright, not just skipped at
    // load: an edit made here would otherwise rewrite that file with this
    // version's understanding of it and quietly throw away whatever it
    // held.
    bool fileMustNotBeOverwritten = false;
};
