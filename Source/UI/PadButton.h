//
//  PadButton.h
//  Kitbox
//
//  One of the sixteen pads.
//
//  Click: select the pad and play it, louder the higher up it is hit - the top
//  edge is full velocity, the bottom edge about a third. Right-click: load,
//  rename or clear.
//
//  Drag and drop, all three ways:
//
//  - files from the Finder onto a pad load it; several files fill that pad and
//    the ones after it, in name order;
//  - a pad dragged onto another pad swaps the two, sample and knobs;
//  - a pad dragged out of the window hands the host or the Finder the pad's
//    original file (the editor, as the drag container, supplies it).
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class KitboxProcessor;

class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget
{
public:
    PadButton (KitboxProcessor& processorToUse, int padIndex);

    std::function<void (int)> onSelect;

    void setSelected (bool shouldBeSelected);

    /** 30 Hz, from the editor: picks up hits from MIDI and fades the flash. */
    void updateFlash();

    static bool parseDragDescription (const juce::var& description, int& pad);
    static juce::String dragDescription (int pad);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override            { setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { setDropHover (true); }
    void itemDragExit (const SourceDetails&) override  { setDropHover (false); }
    void itemDropped (const SourceDetails&) override;

private:
    void setDropHover (bool hover);
    void showMenu();
    void showRename();

    KitboxProcessor& processor;
    const int index;
    bool selected = false, dropHover = false, dragStarted = false;
    int lastHitCount = 0;
    int shownNote = -1, shownChoke = -1;
    float flash = 0.0f;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> renameWindow;
};
