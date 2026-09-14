#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TagEditor.h"

// The tag editor: right-click a track in the Library or Playlist window
// and change what the file itself says it is.
//
// A window of its own rather than a sixth window in the snap layout -
// it's opened for a job and closed again, not arranged on screen next to
// the others. Native title bar for the same reason PluginEditorWindow
// uses one: it isn't part of the magnetic group.
//
// SEVERAL TRACKS AT ONCE is the case that shapes the whole thing. A
// field whose value differs across the selection shows <keep> and is
// left alone unless the user types in it, which is what lets you fix an
// album's artist without flattening thirteen different titles.
class TagEditorWindow final : public juce::DocumentWindow
{
public:
    // onPrepareForWrite runs immediately before any file is touched: the
    // app stops a preview of these files and reports any that are
    // playing. A non-empty return means don't write, and is shown to the
    // user.
    //
    // onSaved carries the files that changed, so the app can re-read
    // their tags and repaint the lists.
    TagEditorWindow(const juce::Array<juce::File>& files,
                     std::function<juce::String(const juce::Array<juce::File>&)> onPrepareForWrite,
                     std::function<void(const juce::Array<juce::File>&)> onSaved,
                     std::function<void(TagEditorWindow*)> onClose);

    void closeButtonPressed() override;

private:
    class Content;

    std::function<void(TagEditorWindow*)> onClose;
};
