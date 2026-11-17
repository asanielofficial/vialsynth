/* VialSynth: the oscillators' mode popups (curve editors and table settings) live in a window of their own, always
 * on top, like the skin editor. The window holds a small interface of its own with its own OpenGL context, and
 * editors of its own, that read and write the same parameters as the main window. */

#pragma once

#include "JuceHeader.h"

#include "full_interface.h"
#include "synth_gui_interface.h"
#include "synth_slider.h"

class OscillatorPopupInterface : public FullInterface, public SynthGuiInterface, private Timer {
  public:
    // kinds as in FullInterface::showOscillatorWarpEditor: 0-2 curve editors, 3 spectral table, 4 distort table.
    OscillatorPopupInterface(SynthGuiInterface* main, vital::SoundEngine* engine, int oscillator, int kind,
                             const vital::output_map& mono_modulations, const vital::output_map& poly_modulations,
                             const Skin& skin);
    ~OscillatorPopupInterface() override;

    static String titleFor(int oscillator, int kind);

    // Modulation drags that end over a popup window: the slider of a popup under a screen point. over_popup says the
    // point is over one of these windows at all (even if no slider is there).
    static SynthSlider* sliderAtScreenPoint(Point<int> screen_point, bool& over_popup);
    static Rectangle<int> defaultSize(int kind);

    void setCloseCallback(std::function<void()> callback) { close_callback_ = callback; }

    void paintBackground(Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override { SynthSection::parentHierarchyChanged(); }
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void timerCallback() override;
    void refreshLayout();

  private:
    void redoPopupBackground();
    void syncValues();

    SynthGuiInterface* main_;
    std::function<void()> close_callback_;
    std::unique_ptr<SynthSection> content_;
    std::unique_ptr<SynthSlider> amount_slider_;
    int kind_;
    std::map<std::string, float> last_values_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorPopupInterface)
};

class OscillatorPopupWindow : public DocumentWindow {
  public:
    OscillatorPopupWindow(const String& title, OscillatorPopupInterface* content);
    ~OscillatorPopupWindow() override = default;

    void closeButtonPressed() override { delete this; }
};

// The sliders that only the popup windows have (the details of the oscillators' spectral modes and table settings),
// as hidden stand-ins in the main window. The modulation manager only knows the sliders of the main window; with
// these it can map a source to them when it is dropped on the real slider in a popup.
class ModulationProxySection : public SynthSection {
  public:
    ModulationProxySection(const vital::output_map& mono_modulations,
                           const std::map<std::string, SynthSlider*>& existing);

    void resized() override;

  private:
    std::vector<std::unique_ptr<SynthSlider>> proxies_;
};
