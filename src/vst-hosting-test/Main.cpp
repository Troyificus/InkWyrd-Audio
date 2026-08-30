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
