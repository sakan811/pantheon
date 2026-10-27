// Renders the plugin editor to a PNG while feeding it programme material that has a missing
// fundamental (see TestSignals.h), so the spectrum display shows real activity.
//
//   HephaestusSnapshot <out.png> [preset name]

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestSignals.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: HephaestusSnapshot <out.png> [preset name]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    HephaestusProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (argc > 2)
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            if (proc.getProgramName (i) == argv[2])
                proc.setCurrentProgram (i);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (true);

    test::MissingFundamentalProgramme programme;
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 200; ++block)
    {
        programme.fill (buffer);
        proc.processBlock (buffer, midi);
        if (block % 4 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
    juce::File out (argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
