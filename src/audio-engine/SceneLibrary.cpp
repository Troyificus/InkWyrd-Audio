#include "SceneLibrary.h"

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeyScenes = "scenes";
    constexpr const char* kKeyId = "id";
    constexpr const char* kKeyName = "name";
    constexpr const char* kKeyColour = "colour";
    constexpr const char* kKeyMusic = "music";
    constexpr const char* kKeyPlaylist = "playlistId";
    constexpr const char* kKeyLoops = "loops";
    constexpr const char* kKeySetsVolume = "setsVolume";
    constexpr const char* kKeyVolume = "volume";

    // Words, not numbers, in the file: someone reading scenes.json should
    // be able to tell what a scene does without this source to hand.
    juce::String musicToString(Scene::Music music)
    {
        switch (music)
        {
            case Scene::Music::playPlaylist: return "play";
            case Scene::Music::fadeOut:      return "fadeOut";
            case Scene::Music::leave:        break;
        }

        return "leave";
    }

    Scene::Music musicFromString(const juce::String& text)
    {
        if (text == "play")    return Scene::Music::playPlaylist;
        if (text == "fadeOut") return Scene::Music::fadeOut;
        return Scene::Music::leave; // anything unknown does the least harm
    }
}

//==============================================================================
ScenePlan planScene(const Scene& scene, const SceneContext& context)
{
    ScenePlan plan;

    // --- Music -----------------------------------------------------------
    if (scene.music == Scene::Music::playPlaylist)
    {
        auto exists = scene.playlistId != juce::Uuid::null()
                       && context.playlistExists != nullptr
                       && context.playlistExists(scene.playlistId);

        if (! exists)
        {
            plan.problems.add("the playlist it plays was deleted");
        }
        else if (! (context.musicPlaying && context.activePlaylistId == scene.playlistId))
        {
            // Not playing, playing something else, or fading away: bring
            // this one in. Already playing it: leave it strictly alone.
            plan.switchPlaylist = true;
            plan.playlistId = scene.playlistId;
        }
    }
    else if (scene.music == Scene::Music::fadeOut)
    {
        // Already silent (or already fading) needs nothing.
        plan.fadeOutMusic = context.musicPlaying;
    }

    // --- Ambience --------------------------------------------------------
    juce::StringArray wanted;

    for (const auto& name : scene.loops)
    {
        if (! context.boardNames.contains(name))
            plan.problems.add("\"" + name + "\" isn't on the soundboard any more");
        else if (! context.loopingNames.contains(name))
            plan.problems.add("\"" + name + "\" isn't set to loop any more");
        else
            wanted.addIfNotAlreadyThere(name);
    }

    for (const auto& name : wanted)
        if (! context.runningLoops.contains(name))
            plan.loopsToStart.add(name);

    // The complete set: anything running that this scene doesn't want
    // goes. A loop in both is in neither list, and so is never touched.
    for (const auto& name : context.runningLoops)
        if (! wanted.contains(name))
            plan.loopsToStop.add(name);

    // --- Volume ----------------------------------------------------------
    if (scene.setsVolume)
    {
        plan.setVolume = true;
        plan.volume = juce::jlimit(0.0f, 1.0f, scene.volume);
    }

    return plan;
}

//==============================================================================
SceneLibrary::SceneLibrary() : file(getDefaultFile()) {}

juce::File SceneLibrary::getDefaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("scenes.json");
}

void SceneLibrary::setFile(const juce::File& newFile)
{
    file = newFile;
}

void SceneLibrary::load()
{
    scenes.clear();
    loadWarnings.clear();
    fileMustNotBeOverwritten = false;

    if (! file.existsAsFile())
        return; // no scenes yet is the normal first state, not a problem

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (! parsed.isObject())
    {
        // Not rewritten on the next save either: a file someone has hand-
        // edited into invalid JSON is still theirs to fix, and silently
        // replacing it with an empty list would lose every scene in it.
        loadWarnings.add(file.getFileName() + " could not be read (not valid JSON), so no scenes "
                          "were loaded. The file is being kept as it is, so scene changes won't be "
                          "saved until it's fixed");
        fileMustNotBeOverwritten = true;
        return;
    }

    if ((int) parsed.getProperty(kKeySchemaVersion, 0) > kCurrentSchemaVersion)
    {
        loadWarnings.add(file.getFileName() + " was made by a newer version of Inkwyrd Audio and was "
                          "not loaded. It's being kept as it is, so scene changes won't be saved in "
                          "this version.");
        fileMustNotBeOverwritten = true;
        return;
    }

    auto* list = parsed.getProperty(kKeyScenes, juce::var()).getArray();
    if (list == nullptr)
        return;

    for (const auto& entry : *list)
    {
        Scene scene;
        scene.id = juce::Uuid(entry.getProperty(kKeyId, "").toString());
        scene.name = entry.getProperty(kKeyName, "").toString().trim();

        // A scene with no name can't be picked by a Stream Deck button,
        // and one with no id can't be updated; neither is worth keeping.
        if (scene.name.isEmpty() || scene.id.isNull())
            continue;

        scene.colourArgb = (juce::uint32) (juce::int64) entry.getProperty(kKeyColour, (juce::int64) scene.colourArgb);
        scene.music = musicFromString(entry.getProperty(kKeyMusic, "leave").toString());
        scene.playlistId = juce::Uuid(entry.getProperty(kKeyPlaylist, "").toString());

        if (auto* loops = entry.getProperty(kKeyLoops, juce::var()).getArray())
            for (const auto& loop : *loops)
                if (loop.toString().isNotEmpty())
                    scene.loops.addIfNotAlreadyThere(loop.toString());

        scene.setsVolume = entry.getProperty(kKeySetsVolume, false);
        scene.volume = juce::jlimit(0.0f, 1.0f, (float) (double) entry.getProperty(kKeyVolume, 1.0));

        scenes.add(scene);
    }
}

void SceneLibrary::save()
{
    if (fileMustNotBeOverwritten)
        return;

    file.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);

    juce::Array<juce::var> list;
    for (const auto& scene : scenes)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty(kKeyId, scene.id.toDashedString());
        object->setProperty(kKeyName, scene.name);
        object->setProperty(kKeyColour, (juce::int64) scene.colourArgb);
        object->setProperty(kKeyMusic, musicToString(scene.music));

        if (scene.music == Scene::Music::playPlaylist)
            object->setProperty(kKeyPlaylist, scene.playlistId.toDashedString());

        juce::Array<juce::var> loops;
        for (const auto& loop : scene.loops)
            loops.add(loop);
        object->setProperty(kKeyLoops, loops);

        object->setProperty(kKeySetsVolume, scene.setsVolume);
        object->setProperty(kKeyVolume, (double) scene.volume);

        list.add(juce::var(object.get()));
    }

    root->setProperty(kKeyScenes, list);

    // Atomic, like every other store here: a crash mid-save must not leave
    // a half-written file that loses every scene.
    juce::TemporaryFile temp(file);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root.get()), false)))
        temp.overwriteTargetFileWithTemporary();
}

const Scene* SceneLibrary::getScene(int index) const
{
    return juce::isPositiveAndBelow(index, scenes.size()) ? &scenes.getReference(index) : nullptr;
}

const Scene* SceneLibrary::findById(const juce::Uuid& id) const
{
    for (auto& scene : scenes)
        if (scene.id == id)
            return &scene;

    return nullptr;
}

const Scene* SceneLibrary::findByName(const juce::String& name) const
{
    auto wanted = name.trim();

    for (auto& scene : scenes)
        if (scene.name == wanted)
            return &scene;

    for (auto& scene : scenes)
        if (scene.name.equalsIgnoreCase(wanted))
            return &scene;

    return nullptr;
}

bool SceneLibrary::isNameTaken(const juce::String& name, const juce::Uuid& exceptId) const
{
    for (auto& scene : scenes)
        if (scene.id != exceptId && scene.name.equalsIgnoreCase(name.trim()))
            return true;

    return false;
}

juce::String SceneLibrary::makeUniqueName(const juce::String& desired, const juce::Uuid& exceptId) const
{
    auto base = desired.trim().isNotEmpty() ? desired.trim() : juce::String("Scene");
    if (! isNameTaken(base, exceptId))
        return base;

    for (int n = 2;; ++n)
    {
        auto candidate = base + " (" + juce::String(n) + ")";
        if (! isNameTaken(candidate, exceptId))
            return candidate;
    }
}

juce::Uuid SceneLibrary::add(Scene scene)
{
    scene.id = juce::Uuid();
    scene.name = makeUniqueName(scene.name, scene.id);
    scenes.add(scene);
    save();
    return scene.id;
}

bool SceneLibrary::update(const Scene& scene)
{
    auto trimmed = scene.name.trim();
    if (trimmed.isEmpty() || isNameTaken(trimmed, scene.id))
        return false;

    for (auto& existing : scenes)
    {
        if (existing.id == scene.id)
        {
            existing = scene;
            existing.name = trimmed;
            save();
            return true;
        }
    }

    return false;
}

void SceneLibrary::remove(const juce::Uuid& id)
{
    for (int i = scenes.size(); --i >= 0;)
        if (scenes.getReference(i).id == id)
            scenes.remove(i);

    save();
}

void SceneLibrary::move(const juce::Uuid& id, int delta)
{
    for (int i = 0; i < scenes.size(); ++i)
    {
        if (scenes.getReference(i).id != id)
            continue;

        auto target = juce::jlimit(0, scenes.size() - 1, i + delta);
        if (target != i)
        {
            scenes.move(i, target);
            save();
        }

        return;
    }
}

int SceneLibrary::renameLoop(const juce::String& oldName, const juce::String& newName)
{
    if (oldName == newName || oldName.isEmpty() || newName.isEmpty())
        return 0;

    int changed = 0;

    for (auto& scene : scenes)
    {
        auto index = scene.loops.indexOf(oldName);
        if (index < 0)
            continue;

        scene.loops.set(index, newName);
        scene.loops.removeDuplicates(false);
        ++changed;
    }

    if (changed > 0)
        save();

    return changed;
}
