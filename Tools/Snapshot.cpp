// Snapshot.cpp — dev tool: run Burnish without a host and render its panel
// to a PNG, after playing a few seconds of a built-in phrase through a
// chain so the meters and spectra have something to show.
//   BurnishSnapshot out.png [preset-index] [module-id-to-show]
//   BurnishSnapshot out.png assist          the assistant setup page
//   BurnishSnapshot out.png dither          the dither panel
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <thread>

namespace
{
// A few seconds of plucked chords over a simple beat.
juce::AudioBuffer<float> testPhrase (double rate, double seconds)
{
    const int n = (int) (rate * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (7);
    const float chords[4][3] { { 220.0f, 261.6f, 329.6f }, { 174.6f, 220.0f, 261.6f }, { 196.0f, 246.9f, 293.7f }, { 164.8f, 196.0f, 246.9f } };
    const int beat = (int) (rate * 0.5);
    for (int i = 0; i < n; ++i)
    {
        const int bar = (i / (beat * 4)) % 4;
        const int inBar = i % (beat * 4);
        const float t = (float) (inBar % (beat * 2)) / (float) rate;
        const float env = std::exp (-t * 2.5f);
        float x = 0.0f;
        for (float f : chords[bar])
        {
            const float ph = (float) i * f / (float) rate;
            x += env * (std::sin (juce::MathConstants<float>::twoPi * ph) + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 2.0f * ph));
        }
        x *= 0.12f;
        const float tb = (float) (i % beat) / (float) rate;
        x += 0.4f * std::exp (-tb * 18.0f) * std::sin (juce::MathConstants<float>::twoPi * 55.0f * tb * (1.0f + 2.0f * std::exp (-tb * 30.0f)));
        const float th = (float) ((i + beat / 2) % beat) / (float) rate;
        const float hat = 0.06f * std::exp (-th * 60.0f) * (rng.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, x + hat);
        b.setSample (1, i, x - hat * 0.5f);
    }
    return b;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: BurnishSnapshot out.png [preset-index [module-id]] | assist | dither\n");
        return 1;
    }
    const double rate = 48000.0;
    const int block = 512;
    BurnishProcessor proc;
    proc.setPlayConfigDetails (2, 2, rate, block);
    proc.prepareToPlay (rate, block);

    const juce::String mode = argc > 2 ? argv[2] : "1";
    if (mode == "stress")
    {
        // What a host does: audio on its own thread the whole time, while the
        // editor is opened, used and closed again and again.
        proc.loadFactory (4);   // a chain with several modules
        auto phraseS = testPhrase (rate, 6.0);
        std::atomic<bool> running { true };
        std::thread audio ([&]
        {
            juce::MidiBuffer m;
            int pos = 0;
            juce::AudioBuffer<float> buf (2, block);
            while (running.load())
            {
                for (int c = 0; c < 2; ++c)
                    buf.copyFrom (c, 0, phraseS, c, pos, block);
                proc.processBlock (buf, m);
                pos = (pos + block) % (phraseS.getNumSamples() - block);
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
        const int rounds = argc > 3 ? juce::String (argv[3]).getIntValue() : 6;
        for (int round = 0; round < rounds; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            ed->setSize (BurnishPanel::designW, BurnishPanel::designH);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            // Visit every module panel, the assistant and the dither panel.
            for (int id = 0; id < burnish::NumModules; ++id)
            {
                proc.addModule (id);
                std::function<void (juce::Component&)> pressTile = [&] (juce::Component& c)
                {
                    if (auto* strip = dynamic_cast<ChainStrip*> (&c))
                        if (strip->onSelect) strip->onSelect (id);
                    for (auto* ch : c.getChildren()) pressTile (*ch);
                };
                pressTile (*ed);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            }
            std::function<void (juce::Component&, const juce::String&)> press = [&] (juce::Component& c, const juce::String& label)
            {
                if (auto* b = dynamic_cast<burnish::ui::PanelButton*> (&c))
                    if (b->getLabel() == label && b->onClick) { b->onClick ({}); return; }
                for (auto* ch : c.getChildren()) press (*ch, label);
            };
            press (*ed, "ASSIST");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            press (*ed, "ASSIST");
            press (*ed, "DITHER");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            if (round % 2 == 1)
                proc.loadFactory (round % (int) burnish::factoryPresets().size());
            ed.reset();   // closing the plugin window
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            std::printf ("round %d ok\n", round + 1);
        }
        running.store (false);
        audio.join();
        std::printf ("stress done\n");
        return 0;
    }
    if (mode != "assist" && mode != "dither")
    {
        proc.loadFactory (mode.getIntValue());
        if (argc > 3)
        {
            const int id = juce::String (argv[3]).getIntValue();
            proc.addModule (id, 0);
        }
    }
    auto phrase = testPhrase (rate, 6.0);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < phrase.getNumSamples(); pos += block)
    {
        const int len = juce::jmin (block, phrase.getNumSamples() - pos);
        juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, pos, len);
        proc.processBlock (view, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (BurnishPanel::designW, BurnishPanel::designH);
    if (auto* ed = dynamic_cast<BurnishEditor*> (editor.get()))
        juce::ignoreUnused (ed);
    // Let the timers run a few frames so meters and spectra fill.
    for (int i = 0; i < 20; ++i)
    {
        juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, (i * block) % (phrase.getNumSamples() - block), block);
        proc.processBlock (view, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }
    if (mode == "assistrun")
    {
        // A full assistant pass on the phrase, then the result page.
        proc.assistantSettings.analysisSeconds = 8.0f;
        proc.startAssistant();
        for (int i = 0; i < 1000 && proc.assistant.analysing(); ++i)
        {
            juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, (i * block) % (phrase.getNumSamples() - block), block);
            proc.processBlock (view, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        std::printf ("assistant built: %d, order size %d\n%s\n", (int) proc.assistant.built(), (int) proc.getOrder().size(), proc.assistant.summary().toRawUTF8());
        for (int i = 0; i < 30; ++i)
        {
            juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, (i * block) % (phrase.getNumSamples() - block), block);
            proc.processBlock (view, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
        // Save and reload the state as a host would, then check the order survived.
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        proc.setStateInformation (state.getData(), (int) state.getSize());
        std::printf ("after state round trip: order size %d, latency %d\n", (int) proc.getOrder().size(), proc.getLatencySamples());
        for (int i = 0; i < 10; ++i)
        {
            juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, (i * block) % (phrase.getNumSamples() - block), block);
            proc.processBlock (view, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
    }
    if (mode == "assist" || mode == "dither")
    {
        // Press the button through the panel: find the component by name.
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<burnish::ui::PanelButton*> (&c))
                if ((mode == "assist" && b->getLabel() == "ASSIST") || (mode == "dither" && b->getLabel() == "DITHER"))
                    if (b->onClick) b->onClick ({});
            for (auto* ch : c.getChildren()) visit (*ch);
        };
        visit (*editor);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
    juce::PNGImageFormat png;
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (! png.writeImageToStream (image, os))
    {
        std::printf ("couldn't write %s\n", argv[1]);
        return 1;
    }
    std::printf ("wrote %s (%d x %d), latency %d samples\n", argv[1], image.getWidth(), image.getHeight(), proc.getLatencySamples());
    return 0;
}
