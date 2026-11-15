/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

#include "paths.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "synth_slider.h"

#include <functional>

// The popup of an oscillator's Random Amplitudes mode: the shuffle slider walks through a chain of random patterns,
// the dice button rolls a new chain.
class RandomAmpEditor : public SynthSection {
  public:
    static constexpr int kPadding = 8;
    static constexpr int kTopHeight = 28;
    static constexpr int kSliderHeight = 20;

    RandomAmpEditor(int oscillator) : SynthSection("random_amp_editor_" + String(oscillator)),
                                      seed_name_("osc_" + std::to_string(oscillator + 1) + "_random_seed") {
      title_ = std::make_unique<PlainTextComponent>("Title", "RANDOM AMPLITUDES  -  OSC " + String(oscillator + 1));
      addOpenGlComponent(title_.get());
      title_->setFontType(PlainTextComponent::kLight);
      title_->setJustification(Justification::centredLeft);

      shuffle_ = std::make_unique<SynthSlider>("osc_" + std::to_string(oscillator + 1) + "_random_shuffle");
      addSlider(shuffle_.get());
      shuffle_->setSliderStyle(Slider::LinearBar);
      shuffle_->setPopupPlacement(BubbleComponent::above);
      shuffle_->setModulationPlacement(BubbleComponent::above);
      shuffle_->setPopupPrefix("Shuffle: ");
      shuffle_->setDoubleClickReturnValue(true, 0.0);

      dice_button_ = std::make_unique<OpenGlToggleButton>(TRANS("Dice"));
      dice_button_->setText("Roll Dice");
      dice_button_->setUiButton(false);
      dice_button_->addListener(this);
      addAndMakeVisible(dice_button_.get());
      addOpenGlComponent(dice_button_->getGlComponent());

      close_button_ = std::make_unique<OpenGlShapeButton>("close");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(Paths::exitX());

      setSkinOverride(Skin::kLfo);
    }

    virtual ~RandomAmpEditor() = default;

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);
      float rounding = findValue(Skin::kLabelBackgroundRounding);
      g.setColour(findColour(Skin::kTextComponentBackground, true));
      g.fillRoundedRectangle(dice_button_->getBounds().toFloat(), rounding);
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      int margin = size_ratio_ * kPadding;
      int top_height = size_ratio_ * kTopHeight;
      title_->setTextSize(size_ratio_ * 14);
      title_->setColor(findColour(Skin::kBodyText, true));
      close_button_->setBounds(getWidth() - margin - top_height, margin, top_height, top_height);
      title_->setBounds(margin, margin, close_button_->getX() - 2 * margin, top_height);
      int y = margin * 2 + top_height;
      int dice_width = size_ratio_ * 90;
      int slider_height = size_ratio_ * kSliderHeight;
      dice_button_->setBounds(getWidth() - margin - dice_width, y, dice_width, slider_height);
      shuffle_->setBounds(margin, y, dice_button_->getX() - 2 * margin, slider_height);
      SynthSection::resized();
    }

    void setVisible(bool should_be_visible) override {
      bool changed = should_be_visible != isVisible();
      SynthSection::setVisible(should_be_visible);
      if (changed)
        repaintBackground();
      if (should_be_visible) {
        Image image(Image::ARGB, 1, 1, false);
        Graphics g(image);
        paintOpenGlChildrenBackgrounds(g);
      }
      if (changed && visibility_callback_)
        visibility_callback_(should_be_visible);
    }

    void setVisibilityCallback(std::function<void(bool)> callback) { visibility_callback_ = callback; }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else if (clicked_button == dice_button_.get()) {
        SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
        if (parent)
          parent->getSynth()->valueChangedInternal(seed_name_, 1.0f + (Random::getSystemRandom().nextInt(99998)));
      }
      else
        SynthSection::buttonClicked(clicked_button);
    }

  private:
    std::string seed_name_;
    std::function<void(bool)> visibility_callback_;
    std::unique_ptr<PlainTextComponent> title_;
    std::unique_ptr<SynthSlider> shuffle_;
    std::unique_ptr<OpenGlToggleButton> dice_button_;
    std::unique_ptr<OpenGlShapeButton> close_button_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RandomAmpEditor)
};
