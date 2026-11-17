#include "oscillator_popup.h"

#include "popup_browser.h"
#include "sound_engine.h"
#include "spectral_warp_editor.h"
#include "synth_base.h"
#include "synth_slider.h"
#include "table_settings_editor.h"
#include "fx_tables.h"
#include "modulation_meter.h"
#include "open_gl_multi_quad.h"
#include "shaders.h"

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

  // Modulation cues: the meters of modulated sliders, and the sliders a dragged source can go to.
  {
    std::map<std::string, SynthSlider*> sliders = getAllSliders();
    modulation_overlay_ = std::make_unique<PopupModulationOverlay>(sliders, mono_modulations, poly_modulations);
    addSubSection(modulation_overlay_.get());
    modulation_overlay_->setInterceptsMouseClicks(false, false);
  }

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

std::atomic<bool> OscillatorPopupInterface::modulation_drag_active_ { false };

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

  if (modulation_overlay_)
    modulation_overlay_->setBounds(getLocalBounds());
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
  float scale = forced_render_scale_ > 0.0f ? forced_render_scale_ : 1.0f;
  background_image_ = Image(Image::RGB, std::ceil(width * scale), std::ceil(height * scale), true);
  Graphics g(background_image_);
  g.addTransform(AffineTransform::scale(scale));
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

  // How many pixels this window really draws per unit: the viewport JUCE has just set covers the whole window.
  GLint viewport[4] = { 0, 0, 0, 0 };
  glGetIntegerv(GL_VIEWPORT, viewport);
  if (viewport[2] > 0 && getWidth() > 0) {
    float scale = viewport[2] * 1.0f / getWidth();
    if (std::abs(scale - forced_render_scale_) > 0.001f) {
      forced_render_scale_ = scale;
      // The cached background is drawn at this scale too.
      Component::SafePointer<Component> safe(this);
      MessageManager::callAsync([safe]() mutable {
        if (auto* popup = dynamic_cast<OscillatorPopupInterface*>(safe.getComponent()))
          popup->refreshLayout();
      });
    }
  }

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

SynthSlider* OscillatorPopupInterface::sliderAtScreenPoint(Point<int> screen_point, bool& over_popup) {
  over_popup = false;
  Component* component = Desktop::getInstance().findComponentAt(screen_point);
  if (component == nullptr)
    return nullptr;

  if (dynamic_cast<OscillatorPopupInterface*>(component) == nullptr &&
      component->findParentComponentOfClass<OscillatorPopupInterface>() == nullptr)
    return nullptr;

  over_popup = true;
  for (Component* parent = component; parent != nullptr; parent = parent->getParentComponent()) {
    if (auto* slider = dynamic_cast<SynthSlider*>(parent))
      return slider;
  }
  return nullptr;
}

ModulationProxySection::ModulationProxySection(const vital::output_map& mono_modulations,
                                               const std::map<std::string, SynthSlider*>& existing) :
    SynthSection("Modulation proxies") {
  static const char* kNames[] = { "spectral_position", "spectral_gain", "spectral_density", "spectral_resonance",
                                  "spectral_depth", "spectral_phase", "table_position", "table_gain",
                                  "table_resonance", "spectral_table_mix", "distortion_bias", "distortion_smooth",
                                  "distortion_drive", "distortion_mix" };
  for (int osc = 1; osc <= vital::kNumOscillators; ++osc) {
    for (const char* name : kNames) {
      std::string full_name = "osc_" + std::to_string(osc) + "_" + name;
      if (existing.count(full_name) || !mono_modulations.count(full_name))
        continue;

      auto slider = std::make_unique<SynthSlider>(full_name);
      addSlider(slider.get());
      slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      slider->setVisible(false);
      proxies_.push_back(std::move(slider));
    }
  }
}

void ModulationProxySection::resized() {
  for (auto& slider : proxies_)
    slider->setBounds(0, 0, getWidth(), getHeight());
}

PopupModulationOverlay::PopupModulationOverlay(const std::map<std::string, SynthSlider*>& sliders,
                                               const vital::output_map& mono_modulations,
                                               const vital::output_map& poly_modulations) :
    SynthSection("Popup modulation") {
  int num_rotary = 0, num_linear = 0;
  for (auto& slider : sliders) {
    auto mono = mono_modulations.find(slider.first);
    if (mono == mono_modulations.end() || mono->second == nullptr || slider.second == nullptr)
      continue;
    if (slider.second->isRotary() && !slider.second->isTextOrCurve())
      ++num_rotary;
    else
      ++num_linear;
  }

  rotary_meters_ = std::make_unique<OpenGlMultiQuad>(std::max(1, num_rotary), Shaders::kRotaryModulationFragment);
  linear_meters_ = std::make_unique<OpenGlMultiQuad>(std::max(1, num_linear), Shaders::kLinearModulationFragment);
  rotary_destinations_ = std::make_unique<OpenGlMultiQuad>(std::max(1, num_rotary), Shaders::kCircleFragment);
  linear_destinations_ = std::make_unique<OpenGlMultiQuad>(std::max(1, num_linear),
                                                           Shaders::kRoundedRectangleFragment);
  for (OpenGlMultiQuad* quads : { rotary_meters_.get(), linear_meters_.get(), rotary_destinations_.get(),
                                  linear_destinations_.get() }) {
    quads->setTargetComponent(this);
    quads->setScissorComponent(nullptr);
  }
  rotary_destinations_->setAlpha(0.0f, true);
  linear_destinations_->setAlpha(0.0f, true);

  int rotary_index = 0, linear_index = 0;
  for (auto& slider : sliders) {
    auto mono = mono_modulations.find(slider.first);
    if (mono == mono_modulations.end() || mono->second == nullptr || slider.second == nullptr)
      continue;

    auto poly = poly_modulations.find(slider.first);
    const vital::Output* poly_total = poly == poly_modulations.end() ? nullptr : poly->second;
    bool rotary = slider.second->isRotary() && !slider.second->isTextOrCurve();
    int index = rotary ? rotary_index++ : linear_index++;
    Entry entry;
    entry.slider = slider.second;
    entry.rotary = rotary;
    entry.index = index;
    entry.meter = std::make_unique<ModulationMeter>(mono->second, poly_total, slider.second,
                                                    rotary ? rotary_meters_.get() : linear_meters_.get(), index);
    entry.meter->setName(slider.first);
    addChildComponent(entry.meter.get());
    entries_.push_back(std::move(entry));
  }
  startTimerHz(30);
}

PopupModulationOverlay::~PopupModulationOverlay() {
  stopTimer();
}

void PopupModulationOverlay::resized() {
  float meter_thickness = findValue(Skin::kKnobModMeterArcThickness);
  Colour center = findColour(Skin::kModulationMeter, true);
  Colour left = findColour(Skin::kModulationMeterLeft, true);
  Colour right = findColour(Skin::kModulationMeterRight, true);
  for (OpenGlMultiQuad* meters : { rotary_meters_.get(), linear_meters_.get() }) {
    meters->setModColor(center);
    meters->setColor(left);
    meters->setAltColor(right);
  }
  rotary_meters_->setThickness(meter_thickness);

  Colour lighten = findColour(Skin::kLightenScreen, true);
  rotary_destinations_->setColor(lighten);
  linear_destinations_->setColor(lighten);
  linear_destinations_->setRounding(findValue(Skin::kLabelBackgroundRounding));
  SynthSection::resized();
  timerCallback();
}

void PopupModulationOverlay::initOpenGlComponents(OpenGlWrapper& open_gl) {
  rotary_meters_->init(open_gl);
  linear_meters_->init(open_gl);
  rotary_destinations_->init(open_gl);
  linear_destinations_->init(open_gl);
  SynthSection::initOpenGlComponents(open_gl);
}

void PopupModulationOverlay::destroyOpenGlComponents(OpenGlWrapper& open_gl) {
  rotary_meters_->destroy(open_gl);
  linear_meters_->destroy(open_gl);
  rotary_destinations_->destroy(open_gl);
  linear_destinations_->destroy(open_gl);
  SynthSection::destroyOpenGlComponents(open_gl);
}

// On the message thread: where each meter sits, and the highlights while a source is being dragged.
void PopupModulationOverlay::timerCallback() {
  SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
  if (gui == nullptr || gui->getSynth() == nullptr || getWidth() <= 0 || getHeight() <= 0)
    return;

  bool dragging = OscillatorPopupInterface::isModulationDragActive();
  float width = getWidth(), height = getHeight();
  int num_rotary = 0, num_linear = 0;
  for (Entry& entry : entries_) {
    int modulations = gui->getSynth()->getNumModulations(entry.slider->getName().toStdString());
    bool showing = entry.slider->isShowing();
    entry.meter->setModulated(modulations > 0);
    entry.meter->setVisible(modulations > 0 && showing);
    if (showing)
      entry.meter->setBounds(getLocalArea(entry.slider, entry.slider->getLocalBounds()));

    if (dragging && showing && entry.slider->isEnabled()) {
      Rectangle<float> bounds = getLocalArea(entry.slider, entry.slider->getLocalBounds()).toFloat();
      if (entry.rotary) {
        float side = std::min(bounds.getWidth(), bounds.getHeight()) * 0.9f;
        bounds = Rectangle<float>(side, side).withCentre(bounds.getCentre());
      }
      float x = 2.0f * bounds.getX() / width - 1.0f;
      float y = 1.0f - 2.0f * bounds.getBottom() / height;
      float w = 2.0f * bounds.getWidth() / width;
      float h = 2.0f * bounds.getHeight() / height;
      if (entry.rotary)
        rotary_destinations_->setQuad(num_rotary++, x, y, w, h);
      else
        linear_destinations_->setQuad(num_linear++, x, y, w, h);
    }
  }

  rotary_destinations_->setNumQuads(num_rotary);
  linear_destinations_->setNumQuads(num_linear);
  rotary_destinations_->setAlpha(num_rotary > 0 ? 1.0f : 0.0f);
  linear_destinations_->setAlpha(num_linear > 0 ? 1.0f : 0.0f);
}

void PopupModulationOverlay::renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) {
  if (!animate)
    return;

  int num_voices = 1;
  SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
  if (gui && gui->getSynth()) {
    const vital::Output* readout = gui->getSynth()->getStatusOutput("num_voices");
    if (readout)
      num_voices = std::max<float>(0.0f, readout->value()[0]);
  }

  for (Entry& entry : entries_) {
    bool show = entry.meter->isModulated() && entry.slider->isShowing();
    entry.meter->setActive(show);
    if (show)
      entry.meter->updateDrawing(num_voices);
  }

  OpenGlComponent::setViewPort(this, open_gl);
  rotary_destinations_->render(open_gl, true);
  linear_destinations_->render(open_gl, true);
  rotary_meters_->render(open_gl, true);
  linear_meters_->render(open_gl, true);
  SynthSection::renderOpenGlComponents(open_gl, animate);
}
