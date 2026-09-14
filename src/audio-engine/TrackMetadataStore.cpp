#include "TrackMetadataStore.h"

#include "MediaFoundationAudioFormat.h"
#include "TagEditor.h"
#include "Mp3AudioFormat.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <propsys.h>
 #include <propvarutil.h>
 #include <shlobj.h>
#endif

namespace
{
    constexpr const char* kSchemaKey = "schemaVersion";
    constexpr const char* kTracksKey = "tracks";

    // How often the scan thread tells the UI something changed. Every
    // file would post a message per file and swamp the message queue on
    // a big library; never would leave the list blank until the end.
    constexpr int kProgressEveryNFiles = 12;

#if JUCE_WINDOWS
    // Looked up by NAME rather than hand-written GUID/PID pairs. The
    // canonical names are stable and self-documenting, and typing
    // {56A3372E-CE9C-11D2-...} from memory is exactly the kind of thing
    // that fails silently by returning the wrong property.
    bool propertyKeyFor(const wchar_t* canonicalName, PROPERTYKEY& key)
    {
        return SUCCEEDED(PSGetPropertyKeyFromName(canonicalName, &key));
    }

    juce::String readStringProperty(IPropertyStore& store, const wchar_t* canonicalName)
    {
        PROPERTYKEY key {};
        if (! propertyKeyFor(canonicalName, key))
            return {};

        PROPVARIANT value {};
        PropVariantInit(&value);

        juce::String result;

        if (SUCCEEDED(store.GetValue(key, &value)) && value.vt != VT_EMPTY)
        {
            // Handles both plain strings and the VT_VECTOR forms that
            // multi-value tags (artist, genre) come back as, joining
            // them rather than silently taking the first.
            PWSTR text = nullptr;
            if (SUCCEEDED(PropVariantToStringAlloc(value, &text)) && text != nullptr)
            {
                result = juce::String(text);
                CoTaskMemFree(text);
            }
        }

        PropVariantClear(&value);
        return result.trim();
    }

    int readIntProperty(IPropertyStore& store, const wchar_t* canonicalName)
    {
        PROPERTYKEY key {};
        if (! propertyKeyFor(canonicalName, key))
            return 0;

        PROPVARIANT value {};
        PropVariantInit(&value);

        ULONG number = 0;
        if (SUCCEEDED(store.GetValue(key, &value)))
            PropVariantToUInt32(value, &number);

        PropVariantClear(&value);
        return (int) number;
    }

    bool readWithPropertyStore(const juce::File& file, TrackMetadata& out)
    {
        IPropertyStore* store = nullptr;

        auto path = file.getFullPathName().toWideCharPointer();
        if (FAILED(SHGetPropertyStoreFromParsingName(path, nullptr, GPS_READWRITE | GPS_OPENSLOWITEM,
                                                       IID_PPV_ARGS(&store)))
             || store == nullptr)
        {
            // GPS_READWRITE fails on read-only files and on volumes the
            // user can't write to; the default flags still read fine.
            if (FAILED(SHGetPropertyStoreFromParsingName(path, nullptr, GPS_DEFAULT,
                                                           IID_PPV_ARGS(&store)))
                 || store == nullptr)
                return false;
        }

        out.title       = readStringProperty(*store, L"System.Title");
        out.artist      = readStringProperty(*store, L"System.Music.Artist");
        out.album       = readStringProperty(*store, L"System.Music.AlbumTitle");
        out.albumArtist = readStringProperty(*store, L"System.Music.AlbumArtist");
        out.genre       = readStringProperty(*store, L"System.Music.Genre");
        out.trackNumber = readIntProperty(*store, L"System.Music.TrackNumber");
        out.year        = readIntProperty(*store, L"System.Media.Year");

        store->Release();
        return true;
    }
#endif

    // JUCE's readers use different key names per format, so this looks
    // for any of the spellings that actually turn up.
    juce::String firstOf(const juce::StringPairArray& values,
                          std::initializer_list<const char*> keys)
    {
        for (auto* key : keys)
        {
            auto value = values.getValue(key, {}).trim();
            if (value.isNotEmpty())
                return value;
        }

        return {};
    }

    bool readWithJuce(const juce::File& file, juce::AudioFormatManager& formatManager,
                       TrackMetadata& out)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return false;

        const auto& values = reader->metadataValues;
        if (values.size() == 0)
            return false;

        out.title       = firstOf(values, { "title", "TITLE", "IART/title" });
        out.artist      = firstOf(values, { "artist", "ARTIST" });
        out.album       = firstOf(values, { "album", "ALBUM" });
        out.albumArtist = firstOf(values, { "albumartist", "ALBUMARTIST", "album artist" });
        out.genre       = firstOf(values, { "genre", "GENRE" });
        out.year        = firstOf(values, { "date", "DATE", "year", "YEAR" }).getIntValue();
        out.trackNumber = firstOf(values, { "tracknumber", "TRACKNUMBER", "track" }).getIntValue();

        return out.title.isNotEmpty() || out.artist.isNotEmpty() || out.album.isNotEmpty();
    }
}

//==============================================================================
juce::String TrackMetadata::displayTitle(const juce::File& file) const
{
    return title.isNotEmpty() ? title : file.getFileNameWithoutExtension();
}

juce::String TrackMetadata::sortKeyFor(const juce::String& value) const
{
    // A high code point in front of empties pushes them to the end under
    // an ordinary ascending compare, without every call site needing to
    // special-case blanks.
    return value.isNotEmpty() ? value.toLowerCase() : juce::String::fromUTF8("\xef\xbf\xbf");
}

//==============================================================================
class TrackMetadataStore::ScanThread : public juce::Thread
{
public:
    ScanThread(TrackMetadataStore& ownerToUse,
                juce::Array<juce::File> filesToScan,
                std::function<void()> onProgressToUse,
                std::function<void()> onFinishedToUse)
        : juce::Thread("Track metadata scan"),
          owner(ownerToUse),
          files(std::move(filesToScan)),
          onProgress(std::move(onProgressToUse)),
          onFinished(std::move(onFinishedToUse))
    {
    }

    void run() override
    {
#if JUCE_WINDOWS
        // Per-thread, and required before any property-store call. STA
        // because shell property handlers are overwhelmingly apartment-
        // threaded and MTA leaves them being marshalled or refusing.
        auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool comInitialised = SUCCEEDED(comResult);
#endif

        int sinceProgress = 0;
        int scannedCount = 0;

        for (const auto& file : files)
        {
            if (threadShouldExit())
                break;

            auto metadata = TrackMetadataStore::readFromFile(file, owner.formatManager);
            metadata.scanned = true;
            ++scannedCount;

            {
                const juce::ScopedLock scope(owner.lock);
                auto& entry = owner.entries[TrackMetadataStore::keyFor(file)];
                entry.metadata = metadata;
                entry.fileSize = file.getSize();
                entry.modifiedMs = file.getLastModificationTime().toMilliseconds();
            }

            owner.remaining.store(juce::jmax(0, owner.remaining.load() - 1));

            if (++sinceProgress >= kProgressEveryNFiles)
            {
                sinceProgress = 0;
                if (onProgress)
                    juce::MessageManager::callAsync(onProgress);
            }
        }

        owner.remaining.store(0);

#if JUCE_WINDOWS
        if (comInitialised)
            CoUninitialize();
#endif

        // Saving from this thread is safe: the store file is only ever
        // written here and from the message thread's save(), and both go
        // through the same lock.
        //
        // Saved even when cancelled part-way. Every entry written is a
        // complete, valid read of one file, and scans now get cancelled
        // routinely - adding tracks restarts the scan - so skipping the
        // save meant the restarted scan found nothing left to do, never
        // saved either, and the whole lot was re-read next launch.
        if (scannedCount > 0)
            owner.save();

        if (onFinished)
            juce::MessageManager::callAsync(onFinished);
    }

private:
    TrackMetadataStore& owner;
    juce::Array<juce::File> files;
    std::function<void()> onProgress, onFinished;
};

//==============================================================================
TrackMetadataStore::TrackMetadataStore()
{
    formatManager.registerBasicFormats();
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false);
}

TrackMetadataStore::~TrackMetadataStore()
{
    cancelScan();
}

juce::File TrackMetadataStore::getDefaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Inkwyrd Audio")
               .getChildFile("track-metadata.json");
}

void TrackMetadataStore::setFile(const juce::File& file)
{
    storeFile = file;
}

juce::String TrackMetadataStore::keyFor(const juce::File& file)
{
    // Same convention as TrackSettingsStore and TrackLibrary: the
    // lowercased full path, because Windows paths are case-insensitive
    // and two spellings of one file must not become two entries.
    return file.getFullPathName().toLowerCase();
}

TrackMetadata TrackMetadataStore::readFromFile(const juce::File& file,
                                                juce::AudioFormatManager& formatManager)
{
    TrackMetadata metadata;

    // TagLib FIRST, because it is what the tag editor WRITES with. Two
    // different readers would mean the lists and the editor disagreeing
    // about a file the moment someone saved a change to it - and it
    // needs no COM, unlike the property store below.
    auto tags = inkwyrd::TagEditor::read(file);
    if (tags.ok && (tags.title.isNotEmpty() || tags.artist.isNotEmpty() || tags.album.isNotEmpty()))
    {
        metadata.title       = tags.title;
        metadata.artist      = tags.artist;
        metadata.album       = tags.album;
        metadata.albumArtist = tags.albumArtist;
        metadata.genre       = tags.genre;
        metadata.year        = tags.year.getIntValue();
        metadata.trackNumber = tags.trackNumber.getIntValue();
        return metadata;
    }

    // Then JUCE's own readers, then Windows. Both stay as fallbacks for
    // anything TagLib declines to open - dropping them would trade a
    // working path for a tidier one.
    if (readWithJuce(file, formatManager, metadata))
        return metadata;

#if JUCE_WINDOWS
    readWithPropertyStore(file, metadata);
#endif

    return metadata;
}

TrackMetadata TrackMetadataStore::get(const juce::File& file) const
{
    const juce::ScopedLock scope(lock);

    auto it = entries.find(keyFor(file));
    return it != entries.end() ? it->second.metadata : TrackMetadata {};
}

bool TrackMetadataStore::isStale(const juce::File& file, const Entry& entry) const
{
    return ! entry.metadata.scanned
            || entry.fileSize != file.getSize()
            || entry.modifiedMs != file.getLastModificationTime().toMilliseconds();
}

void TrackMetadataStore::scanAsync(const juce::Array<juce::File>& files,
                                    std::function<void()> onProgress,
                                    std::function<void()> onFinished)
{
    cancelScan();

    juce::Array<juce::File> work;
    {
        const juce::ScopedLock scope(lock);

        // Callers pass the library AND every playlist, which overlap
        // heavily - without this a track in three playlists is read four
        // times.
        std::set<juce::String> queued;

        for (const auto& file : files)
        {
            auto key = keyFor(file);
            if (! queued.insert(key).second || ! file.existsAsFile())
                continue;

            auto it = entries.find(key);
            if (it == entries.end() || isStale(file, it->second))
                work.add(file);
        }
    }

    if (work.isEmpty())
    {
        if (onFinished)
            juce::MessageManager::callAsync(onFinished);
        return;
    }

    remaining.store(work.size());
    scanThread = std::make_unique<ScanThread>(*this, std::move(work),
                                               std::move(onProgress), std::move(onFinished));
    scanThread->startThread();
}

void TrackMetadataStore::cancelScan()
{
    if (scanThread != nullptr)
    {
        scanThread->signalThreadShouldExit();
        scanThread->stopThread(4000);
        scanThread.reset();
    }

    remaining.store(0);
}

bool TrackMetadataStore::isScanning() const
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

void TrackMetadataStore::load()
{
    const juce::ScopedLock scope(lock);
    entries.clear();

    if (! storeFile.existsAsFile())
        return;

    auto parsed = juce::JSON::parse(storeFile.loadFileAsString());
    if (! parsed.isObject())
        return;

    // Same skip-if-newer discipline as every other store here: a file
    // written by a later version is left alone rather than half-read and
    // then overwritten.
    if ((int) parsed.getProperty(kSchemaKey, 0) > kCurrentSchemaVersion)
        return;

    auto tracks = parsed.getProperty(kTracksKey, {});
    if (auto* array = tracks.getArray())
    {
        for (const auto& item : *array)
        {
            auto path = item.getProperty("path", {}).toString();
            if (path.isEmpty())
                continue;

            Entry entry;
            entry.metadata.title       = item.getProperty("title", {}).toString();
            entry.metadata.artist      = item.getProperty("artist", {}).toString();
            entry.metadata.album       = item.getProperty("album", {}).toString();
            entry.metadata.albumArtist = item.getProperty("albumArtist", {}).toString();
            entry.metadata.genre       = item.getProperty("genre", {}).toString();
            entry.metadata.year        = (int) item.getProperty("year", 0);
            entry.metadata.trackNumber = (int) item.getProperty("trackNumber", 0);
            entry.metadata.scanned     = true;
            entry.fileSize   = (juce::int64) (double) item.getProperty("size", 0);
            entry.modifiedMs = (juce::int64) (double) item.getProperty("modified", 0);

            entries[path.toLowerCase()] = entry;
        }
    }
}

void TrackMetadataStore::save()
{
    if (storeFile == juce::File())
        return;

    juce::Array<juce::var> array;

    {
        const juce::ScopedLock scope(lock);

        for (const auto& pair : entries)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("path", pair.first);
            object->setProperty("title", pair.second.metadata.title);
            object->setProperty("artist", pair.second.metadata.artist);
            object->setProperty("album", pair.second.metadata.album);
            object->setProperty("albumArtist", pair.second.metadata.albumArtist);
            object->setProperty("genre", pair.second.metadata.genre);
            object->setProperty("year", pair.second.metadata.year);
            object->setProperty("trackNumber", pair.second.metadata.trackNumber);
            object->setProperty("size", (double) pair.second.fileSize);
            object->setProperty("modified", (double) pair.second.modifiedMs);
            array.add(juce::var(object));
        }
    }

    auto* root = new juce::DynamicObject();
    root->setProperty(kSchemaKey, kCurrentSchemaVersion);
    root->setProperty(kTracksKey, array);

    storeFile.getParentDirectory().createDirectory();

    // Atomic, like the other stores: a crash mid-write leaves the old
    // cache rather than a truncated one.
    juce::TemporaryFile temp(storeFile);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root), false)))
        temp.overwriteTargetFileWithTemporary();
}
