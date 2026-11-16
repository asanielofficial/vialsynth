#include "oscillator_popup.h"

#include "popup_browser.h"
#include "sound_engine.h"
#include "spectral_warp_editor.h"
#include "synth_base.h"
#include "synth_slider.h"
#include "table_settings_editor.h"
#include "fx_tables.h"

namespace {
  constexpr int kBottomStrip = 24;
}

OscillatorPopupInterface::OscillatorPopupInterface(SynthGuiInterface* main, vital::SoundEngine* engine,
                                                   int oscillator, int kind,
                                                   const vital::output_map& mono_modulations,
                                                   const vital::output_map& poly_modulations, const Skin& skin) :
    FullInterface(), SynthGuiInterface(main->getSynth(), false), main_(main), kind_(kind) {
  setOpaque(true);
  setSkinValues(skin, true);

  // Menus, bubbles and the shape browser of this window.
  popup_browser_ = std::make_unique<PopupBrowser>();
  addSubSection(popup_browser_.get());
  popup_browser_->setVisible(false);

  popup_selector_ = std::make_unique<SinglePopupSelector>();
  addSubSection(popup_selector_.get());
  popup_selector_->setVisible(false);
  popup_selector_->setAlwaysOnTop(true);
  popup_selector_->setWantsKeyboardFocus(true);

  dual_popup_selector_ = std::make_unique<DualPopupSelector>();
  addSubSection(dual_popup_selector_.get());
  dual_popup_selector_->setVisible(false);
  dual_popup_selector_->setAlwaysOnTop(true);
  dual_popup_selector_->setWantsKeyboardFocus(true);

  popup_display_1_ = std::make_unique<PopupDisplay>();
  addSubSection(popup_display_1_.get());
  popup_display_1_->setVisible(false);
  popup_display_1_->setAlwaysOnTop(true);

  popup_display_2_ = std::make_unique<PopupDisplay>();
  addSubSection(popup_display_2_.get());
  popup_display_2_->setVisible(false);
  popup_display_2_->setAlwaysOnTop(true);

  std::string prefix = "osc_" + std::to_string(oscillator + 1);
  std::string subtitle = "OSC " + std::to_string(oscillator + 1);
  auto close = [this] { if (close_callback_) close_callback_(); };

  if (kind < 3) {
    auto editor = std::make_unique<SpectralWarpEditor>(engine, vital::SoundEngine::oscLane(oscillator), prefix, true);
    editor->setVisibilityCallback([close](bool visible) { if (!visible) close(); });
    addSubSection(editor.get());
    editor->showCurve(0, kind);
    content_ = std::move(editor);
  }
  else {
    bool spectral = kind == 3;
    auto editor = std::make_unique<TableSettingsEditor>(
        spectral ? TableSettingsEditor::kSpectral : TableSettingsEditor::kDistort, prefix,
        vital::FxTables::oscSlot(oscillator, spectral), mono_modulations, poly_modulations, subtitle, !spectral);
    editor->setVisibilityCallback([close](bool visible) { if (!visible) close(); });

    // Dragging the display moves the mode's amount, which sits in a bar under it.
    amount_slider_ = std::make_unique<SynthSlider>(prefix + (spectral ? "_spectral_morph_amount" :
                                                                         "_distortion_amount"));
    addSlider(amount_slider_.get());
    amount_slider_->setSliderStyle(Slider::LinearBar);
    amount_slider_->setPopupPlacement(BubbleComponent::above);
    amount_slider_->setPopupPrefix(spectral ? "Spectral Morph: " : "Wave Morph: ");
    editor->setFrameSlider(amount_slider_.get());
    addSubSection(editor.get());
    editor->setVisible(true);
    content_ = std::move(editor);
  }

  // The sections added after the content draw over it.
  popup_browser_->toFront(true);
  popup_selector_->toFront(true);
  dual_popup_selector_->toFront(true);
  popup_display_1_->toFront(true);
  popup_display_2_->toFront(true);

  setSkinValues(skin, true);
  vital::control_map& controls = main->getSynth()->getControls();
  SynthSection::setAllValues(controls);
  for (auto& slider : getAllSliders()) {
    auto control = controls.find(slider.first);
    if (control != controls.end() && control->second)
      last_values_[slider.first] = control->second->value();
  }

  setSize(defaultSize(kind).getWidth(), defaultSize(kind).getHeight());
  startTimer(100);
}

OscillatorPopupInterface::~OscillatorPopupInterface() {
  stopTimer();
  // The render thread uses the editors below: stop it while this class (and so its overrides) is still alive.
  open_gl_context_.detach();
  open_gl_context_.setRenderer(nullptr);
}

// Text and images of the editors are drawn when they are laid out while showing, which a window that has not been
// shown yet is not: lay everything out once more after it is up.
void OscillatorPopupInterface::refreshLayout() {
  if (content_ == nullptr || getWidth() <= 0)
    return;

  {
    ScopedLock lock(open_gl_critical_section_);
    Rectangle<int> bounds = content_->getBounds();
    content_->setBounds(bounds.withWidth(bounds.getWidth() + 1));
    content_->setBounds(bounds);
  }
  redoPopupBackground();
}

String OscillatorPopupInterface::titleFor(int oscillator, int kind) {
  String name = kind < 3 ? String("Spectral ") + (kind == 0 ? "Filter" : kind == 1 ? "Phaser" : "Flanger") :
                kind == 3 ? "Spectral Table" : "Distort Table";
  return name + " - Oscillator " + String(oscillator + 1);
}

Rectangle<int> OscillatorPopupInterface::defaultSize(int kind) {
  return kind < 3 ? Rectangle<int>(0, 0, 720, 420) : Rectangle<int>(0, 0, 560, 520);
}

void OscillatorPopupInterface::paintBackground(Graphics& g) {
  g.fillAll(findColour(Skin::kBackground, true));
  paintChildrenBackgrounds(g);
}

void OscillatorPopupInterface::resized() {
  width_ = getWidth();
  resized_width_ = width_;
  display_scale_ = 1.0f;
  pixel_multiple_ = 1;
  if (getWidth() <= 0 || getHeight() <= 0)
    return;

  ScopedLock lock(open_gl_critical_section_);
  setSizeRatio(0.9f);

  Rectangle<int> area = getLocalBounds();
  Rectangle<int> content_area = area;
  if (amount_slider_)
    amount_slider_->setBounds(area.removeFromBottom(kBottomStrip).reduced(6, 3));
  content_area = amount_slider_ ? getLocalBounds().withTrimmedBottom(kBottomStrip) : getLocalBounds();
  if (content_)
    content_->setBounds(content_area);

  popup_browser_->setBounds(getLocalBounds());
  popup_selector_->setBounds(getLocalBounds());
  dual_popup_selector_->setBounds(getLocalBounds());
  redoPopupBackground();
}

void OscillatorPopupInterface::redoPopupBackground() {
  int width = getWidth();
  int height = getHeight();
  if (width <= 0 || height <= 0)
    return;

  ScopedLock lock(open_gl_critical_section_);
  background_.lock();
  background_image_ = Image(Image::RGB, width, height, true);
  Graphics g(background_image_);
  paintBackground(g);
  background_.updateBackgroundImage(background_image_);
  background_.unlock();
}

void OscillatorPopupInterface::newOpenGLContextCreated() {
  shaders_ = std::make_unique<Shaders>(open_gl_context_);
  open_gl_.shaders = shaders_.get();
  open_gl_.display_scale = 1.0f;
  last_render_scale_ = 1.0f;

  background_.init(open_gl_);
  initOpenGlComponents(open_gl_);
}

void OscillatorPopupInterface::renderOpenGL() {
  if (open_gl_.shaders == nullptr)
    return;

  ScopedLock lock(open_gl_critical_section_);
  open_gl_.display_scale = 1.0f;
  background_.render(open_gl_);
  renderOpenGlComponents(open_gl_, true);
}

void OscillatorPopupInterface::openGLContextClosing() {
  background_.destroy(open_gl_);
  destroyOpenGlComponents(open_gl_);
  open_gl_.shaders = nullptr;
  shaders_ = nullptr;
}

// Keeps this window and the main window in step: whatever changed a parameter (a drag here, automation, the main
// window), both show it.
void OscillatorPopupInterface::timerCallback() {
  syncValues();
}

void OscillatorPopupInterface::syncValues() {
  if (main_ == nullptr || main_->getSynth() == nullptr)
    return;

  vital::control_map& controls = main_->getSynth()->getControls();
  for (auto& entry : getAllSliders()) {
    SynthSlider* slider = entry.second;
    auto control = controls.find(entry.first);
    if (slider == nullptr || control == controls.end() || !control->second)
      continue;

    float value = control->second->value();
    auto last = last_values_.find(entry.first);
    if (last != last_values_.end() && last->second == value)
      continue;

    last_values_[entry.first] = value;
    if (!slider->isMouseButtonDown())
      slider->setValue(value, dontSendNotification);
    main_->updateGuiControl(entry.first, value);
  }

  for (auto& entry : getAllButtons()) {
    auto control = controls.find(entry.first);
    if (entry.second == nullptr || control == controls.end() || !control->second)
      continue;
    entry.second->setToggleState(control->second->value() > 0.5f, dontSendNotification);
  }
}

OscillatorPopupWindow::OscillatorPopupWindow(const String& title, OscillatorPopupInterface* content) :
    DocumentWindow(title, Colour(0xff2a2d30), DocumentWindow::closeButton) {
  setUsingNativeTitleBar(true);
  setAlwaysOnTop(true);
  setContentOwned(content, true);
  setResizable(true, false);
  setResizeLimits(420, 320, 2000, 1600);
}
