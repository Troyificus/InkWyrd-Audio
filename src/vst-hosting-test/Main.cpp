#include <iostream>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_events/juce_events.h>

#include "PluginChain.h"
#include "PluginScanner.h"

namespace
{
    void printPluginList(const juce::Array<juce::PluginDescription>& list)
    {
        for (int i = 0; i < list.size(); ++i)
            std::cout << "  " << i << ": " << list[i].name.toStdString()
                       << " (" << list[i].manufacturerName.toStdString() << ")" << std::endl;
    }

    void printChain(PluginChain& chain)
    {
        auto n = chain.getNumPlugins();
        if (n == 0)
        {
            std::cout << "Chain is empty (mic passes through unprocessed)." << std::endl;
            return;
        }
        std::cout << "Chain (" << n << "):" << std::endl;
        for (int i = 0; i < n; ++i)
            std::cout << "  " << i << ": " << chain.getPluginName(i).toStdString() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    // INKWYRD_PICKTEST=<path.vst3>: the "user picks a plugin" path that
    // replaced the folder-wide scan. Checks the thing that actually
    // matters - that ONE chosen file yields a usable plugin description,
    // survives a save/restore, and can be taken off the list again.
    {
        auto pickPath = juce::SystemStats::getEnvironmentVariable("INKWYRD_PICKTEST", "");
        if (pickPath.isNotEmpty())
        {
            juce::ScopedJuceInitialiser_GUI juceInitialiser;

            int failures = 0;
            auto check = [&failures](bool condition, const char* what)
            {
                std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;
                if (!condition)
                    ++failures;
            };

            juce::File chosen(pickPath);
            std::cout << "picking: " << chosen.getFileName().toStdString() << std::endl;

            auto listFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("inkwyrd-picktest.xml");
            listFile.deleteFile();

            PluginScanner picker;
            check(picker.getNumKnownPlugins() == 0, "a fresh list starts empty, not full of everything installed");

            juce::String error;
            auto added = picker.addPluginsFromFile(chosen, error);
            check(added > 0, "picking one .vst3 adds it");
            if (added == 0)
                std::cout << "     error was: " << error.toStdString() << std::endl;

            auto types = picker.getKnownPlugins();
            check(types.size() == added, "and the list holds exactly what was added");
            if (!types.isEmpty())
                std::cout << "     got: " << types[0].name.toStdString()
                           << " by " << types[0].manufacturerName.toStdString() << std::endl;

            juce::String secondError;
            check(picker.addPluginsFromFile(chosen, secondError) == 0,
                   "adding the same plugin twice adds nothing");
            check(secondError.isNotEmpty(), "and says why rather than failing silently");
            check(picker.getNumKnownPlugins() == added, "the list didn't grow on the duplicate");

            picker.saveToCache(listFile);
            check(listFile.existsAsFile(), "the chosen list is written to disk");

            PluginScanner reloaded;
            reloaded.restoreFromCache(listFile);
            check(reloaded.getNumKnownPlugins() == added, "and comes back on the next launch");
            check(reloaded.getKnownPlugins()[0].name == types[0].name,
                   "with the same plugin in it");

            juce::String badError;
            check(picker.addPluginsFromFile(chosen.getSiblingFile("definitely-not-real.vst3"), badError) == 0,
                   "a file that isn't a plugin is refused");
            check(badError.isNotEmpty(), "and explains itself");

            // The other half of the rebuild: a chosen plugin has to
            // instantiate AND produce an editor that can be opened and
            // closed again. Adding a plugin at its defaults with no way
            // to touch it is what this replaced.
            {
                juce::String instanceError;
                auto instance = reloaded.createInstance(types[0], 44100.0, 512, instanceError);
                check(instance != nullptr, "the chosen plugin actually instantiates");

                if (instance == nullptr)
                {
                    std::cout << "     error was: " << instanceError.toStdString() << std::endl;
                }
                else
                {
                    auto ownEditor = instance->hasEditor();
                    std::unique_ptr<juce::AudioProcessorEditor> editor(
                        ownEditor ? instance->createEditorIfNeeded()
                                  : new juce::GenericAudioProcessorEditor(*instance));

                    check(editor != nullptr, "and gives us an editor to show");

                    if (editor != nullptr)
                    {
                        check(editor->getWidth() > 0 && editor->getHeight() > 0,
                               "with a real size rather than a zero-sized window");
                        std::cout << "     editor " << editor->getWidth() << "x" << editor->getHeight()
                                   << (ownEditor ? " (the plugin's own)" : " (generic fallback)")
                                   << std::endl;
                    }

                    // Closing it must hand the editor back to the plugin
                    // cleanly - this is where a lifetime mistake shows up.
                    editor.reset();
                    check(true, "and closing the editor doesn't crash");

                    instance->releaseResources();
                }
            }

            picker.removePlugin(types[0]);
            check(picker.getNumKnownPlugins() == added - 1, "a plugin can be taken off the list");

            listFile.deleteFile();

            std::cout << (failures == 0 ? "PICK TEST PASSED" : "PICK TEST FAILED")
                       << " (" << failures << " failure(s))" << std::endl;
            return failures == 0 ? 0 : 1;
        }
    }

    // INKWYRD_CACHETEST=1: proves the plugin cache is worth having, and
    // that restoring it produces the same list a real scan does.
    //
    // The scan loads every plugin binary on the machine, which is why it
    // used to freeze the app's UI for ~18 seconds at every launch (see
    // MessageThreadWatchdog). The app now restores this cache instead
    // and only ever scans on a background thread.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_CACHETEST", "").isNotEmpty())
    {
        auto cacheFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("inkwyrd-plugin-cache-test.xml");
        cacheFile.deleteFile();

        PluginScanner fresh;
        auto scanStart = juce::Time::getMillisecondCounterHiRes();
        auto scanned = fresh.scan();
        auto scanMs = juce::Time::getMillisecondCounterHiRes() - scanStart;

        std::cout << "real scan: " << scanned.size() << " plugin(s) in "
                   << juce::String(scanMs, 0).toStdString() << " ms" << std::endl;

        fresh.saveToCache(cacheFile);
        std::cout << "cache written: " << (cacheFile.getSize() / 1024) << " KB" << std::endl;

        PluginScanner cached;
        auto restoreStart = juce::Time::getMillisecondCounterHiRes();
        cached.restoreFromCache(cacheFile);
        auto restoreMs = juce::Time::getMillisecondCounterHiRes() - restoreStart;

        auto restored = cached.getKnownPlugins();
        std::cout << "cache restore: " << restored.size() << " plugin(s) in "
                   << juce::String(restoreMs, 1).toStdString() << " ms" << std::endl;

        bool sameCount = restored.size() == scanned.size();

        // Compare as SETS first: an order difference is cosmetic (it only
        // changes the row order in the Voice FX list), whereas a missing
        // plugin would mean the cache silently loses one.
        juce::StringArray scannedIds, restoredIds;
        for (const auto& d : scanned)  scannedIds.add(d.fileOrIdentifier + "|" + d.name);
        for (const auto& d : restored) restoredIds.add(d.fileOrIdentifier + "|" + d.name);

        bool sameOrder = (scannedIds == restoredIds);
        auto sortedScanned = scannedIds;  sortedScanned.sort(true);
        auto sortedRestored = restoredIds; sortedRestored.sort(true);
        bool sameSet = (sortedScanned == sortedRestored);

        std::cout << (sameCount ? "PASS" : "FAIL")
                   << "  the cache restores the same number of plugins" << std::endl;
        std::cout << (sameSet ? "PASS" : "FAIL")
                   << "  the cache restores exactly the same plugins (as a set)" << std::endl;
        std::cout << (sameOrder ? "same order" : "DIFFERENT ORDER (cosmetic)")
                   << "  - order of the Voice FX list" << std::endl;

        if (!sameSet)
        {
            for (const auto& id : sortedScanned)
                if (!sortedRestored.contains(id))
                    std::cout << "  MISSING from cache: " << id.toStdString() << std::endl;
            for (const auto& id : sortedRestored)
                if (!sortedScanned.contains(id))
                    std::cout << "  EXTRA in cache: " << id.toStdString() << std::endl;
        }

        bool sameNames = sameSet;

        // A missing or corrupt cache must degrade to "no plugins yet",
        // not crash - the app treats that as "scan in the background".
        PluginScanner missing;
        missing.restoreFromCache(cacheFile.getSiblingFile("does-not-exist.xml"));
        std::cout << (missing.getNumKnownPlugins() == 0 ? "PASS" : "FAIL")
                   << "  a missing cache file is survivable" << std::endl;

        auto corrupt = cacheFile.getSiblingFile("inkwyrd-plugin-cache-corrupt.xml");
        corrupt.replaceWithText("<this is not valid xml");
        PluginScanner broken;
        broken.restoreFromCache(corrupt);
        std::cout << (broken.getNumKnownPlugins() == 0 ? "PASS" : "FAIL")
                   << "  a corrupt cache file is survivable" << std::endl;

        corrupt.deleteFile();
        cacheFile.deleteFile();
        return (sameCount && sameNames) ? 0 : 1;
    }

    PluginScanner scanner;
    std::cout << "Scanning for VST3 plugins..." << std::endl;
    auto found = scanner.scan();
    std::cout << "Found " << found.size() << " plugin(s):" << std::endl;
    printPluginList(found);

    PluginChain chain;

    juce::AudioProcessorPlayer processorPlayer;
    processorPlayer.setProcessor(&chain);

    juce::AudioDeviceManager deviceManager;
    auto openError = deviceManager.initialiseWithDefaultDevices(1, 2); // 1 input (mic), stereo out
    if (openError.isNotEmpty())
    {
        std::cout << "Failed to open audio device: " << openError.toStdString() << std::endl;
        return 1;
    }
    deviceManager.addAudioCallback(&processorPlayer);

    std::cout << "Live: mic -> plugin chain -> speakers. Wear headphones to avoid feedback." << std::endl;
    std::cout << "Commands: a <index> = add plugin from the scan list, r <index> = remove from chain, "
                 "l = list chain, q = quit" << std::endl;

    std::thread inputThread([&]
    {
        std::string line;
        while (true)
        {
            std::cout << "> ";
            if (!std::getline(std::cin, line) || line == "q")
            {
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            }

            if (line.empty())
                continue;

            if (line == "l")
            {
                juce::MessageManager::callAsync([&chain] { printChain(chain); });
            }
            else if (line.size() > 2 && (line[0] == 'a' || line[0] == 'r') && line[1] == ' ')
            {
                auto index = std::atoi(line.c_str() + 2);
                bool isAdd = (line[0] == 'a');

                juce::MessageManager::callAsync([&chain, &scanner, &found, index, isAdd]
                {
                    if (isAdd)
                    {
                        if (index < 0 || index >= found.size())
                        {
                            std::cout << "No such plugin index." << std::endl;
                            return;
                        }
                        juce::String error;
                        if (chain.addPlugin(scanner, found[index], error))
                            std::cout << "Added: " << found[index].name.toStdString() << std::endl;
                        else
                            std::cout << "Failed to load " << found[index].name.toStdString()
                                       << ": " << error.toStdString() << std::endl;
                    }
                    else
                    {
                        if (index < 0 || index >= chain.getNumPlugins())
                        {
                            std::cout << "No such chain index." << std::endl;
                            return;
                        }
                        auto name = chain.getPluginName(index);
                        chain.removePlugin(index);
                        std::cout << "Removed: " << name.toStdString() << std::endl;
                    }
                });
            }
            else
            {
                std::cout << "Unknown command." << std::endl;
            }
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoop();

    inputThread.join();

    deviceManager.removeAudioCallback(&processorPlayer);
    processorPlayer.setProcessor(nullptr);

    return 0;
}
