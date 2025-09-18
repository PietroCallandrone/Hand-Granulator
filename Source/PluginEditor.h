#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class GridBackgroundComponent;




//to style the hand granulator title
class GlossyTitleLabel : public juce::Label
{
public:
    GlossyTitleLabel() = default;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto text = getText();

        // Shadow or "depth"
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.setFont(getFont());
        g.drawText(text, bounds.translated(1, 1), juce::Justification::centred);

        // Main text
        juce::ColourGradient gradient(
            juce::Colours::lightgrey.brighter(0.4f), bounds.getTopLeft(),
            juce::Colours::lightgrey.darker(0.4f), bounds.getBottomLeft(), false
        );
        g.setGradientFill(gradient);
        g.drawFittedText(text, bounds.toNearestInt(), juce::Justification::centred, 1);
    }
};

struct LoadButtonLookAndFeel : public juce::LookAndFeel_V4
{
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour&, bool isMouseOver, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

        // Translucent dark grey base
        juce::Colour base = juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f);  //softer dark grey
        if (isMouseOver) base = base.brighter(0.1f);
        if (isButtonDown) base = base.darker(0.1f);

        g.setColour(base);
        g.fillRoundedRectangle(bounds, 6.0f);

        // Gloss on top third
        juce::Rectangle<float> glossArea(bounds.withHeight(bounds.getHeight() * 0.35f));
        juce::ColourGradient gloss(
            juce::Colours::white.withAlpha(0.05f),
            glossArea.getCentreX(), glossArea.getY(),
            juce::Colours::transparentBlack,
            glossArea.getCentreX(), glossArea.getBottom(),
            false
        );
        g.setGradientFill(gloss);
        g.fillRoundedRectangle(glossArea, 6.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
        bool, bool) override
    {
        auto bounds = button.getLocalBounds();
        juce::Font font = getTextButtonFont(button, button.getHeight()).withHeight(14.0f).boldened();

        g.setFont(font);

        //Subtle soft-shadow glow behind text
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawFittedText(button.getButtonText(), bounds.translated(1, 1), juce::Justification::centred, 1);

        //Slightly dimmer neon green text
        g.setColour(juce::Colours::lightgrey.withAlpha(0.8f));
        g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
    }
};

// Custom styled label used for BPM
class CustomBpmLabel : public juce::Label
{
public:
    CustomBpmLabel() = default;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f)); // same as your button color
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(juce::Colours::lightgrey.withAlpha(0.85f));
        g.setFont(juce::Font(14.5f, juce::Font::bold));
        g.drawFittedText(getText(), getLocalBounds(), juce::Justification::centred, 1);
    }

    juce::TextEditor* createEditorComponent() override
    {
        auto* editor = new juce::TextEditor();

        editor->setJustification(juce::Justification::centred);
        editor->setFont(juce::Font(14.5f, juce::Font::bold));

        // Match the static look
        editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f));
        editor->setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey.withAlpha(0.85f));
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);

        editor->setBorder(juce::BorderSize<int>(0));
        editor->setScrollbarsShown(false);
        editor->setIndents(0, 0);

        editor->setSize(getWidth(), getHeight()); // force matching size

        return editor;
    }

};

class CMProjectAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Button::Listener, private juce::Timer, public juce::MouseListener
{
public:
   // juce::String fingerControls[3] = { {}, {}, {} };
    CMProjectAudioProcessorEditor(CMProjectAudioProcessor&);
    ~CMProjectAudioProcessorEditor() override;

    //void buttonClicked(juce::Button*) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e,
        const juce::MouseWheelDetails& wheel) override;
    CustomBpmLabel bpmLabel;
    juce::Label bpmTitleLabel;
    GlossyTitleLabel pageTitleLabel;


    void setCurrentParameter(const juce::String& p) noexcept { currentParameter = p; }
    juce::String currentParameter;
    void setToolTipFunction();
    void fingersSetUp();
    void pluginTitle();
    void loadHandiImageFromPath();
    void midiOnClickSetUpFunction();
    void clearFingersStartAllSetUp();
    void startingConfigurationGlobal();
    void globalBpmSetUp();
    void addListenerToGLobal();
    juce::TextButton clearFingersButton{ "Clear Fingers" };
    //HDImageButton rowIcon[4];
   // void sliderValueChanged(juce::Slider* s) override;
  
private:

    class SynthPageComponent;
    class DrumPageComponent;
    LoadButtonLookAndFeel startAllButtonLookAndFeel;
    LoadButtonLookAndFeel clearFingerButtonLookAndFeel;
    std::unique_ptr<GridBackgroundComponent> background;
    CMProjectAudioProcessor& audioProcessor;
    juce::ImageComponent handOverlay;
    SynthPageComponent* synthPage = nullptr;
    DrumPageComponent* drumPage = nullptr;
    juce::TooltipWindow tooltipWindow{ this, 300 /* delay in ms */ }; //OnMousePointed
    juce::TextButton startAllButton{ "Start All" };
    
    void clearLookAndFeelRecursively(Component* component)
    {
        if (component == nullptr) return;
        
        // Clear LookAndFeel from this component
        component->setLookAndFeel(nullptr);
        
        // Recursively clear from all children
        for (auto* child : component->getChildren())
        {
            clearLookAndFeelRecursively(child);
        }
    }


    //full function to style the switch page
    class ShadowedTextButton : public juce::TextButton
    {
    public:
        ShadowedTextButton(const juce::String& name) : juce::TextButton(name) {}

        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
        {
            auto bounds = getLocalBounds().toFloat().reduced(1.0f);
            juce::Colour base = juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f);
            if (isMouseOver) base = base.brighter(0.1f);
            if (isButtonDown) base = base.darker(0.1f);
            g.setColour(base);
            g.fillRoundedRectangle(bounds, 6.0f);
            juce::Rectangle<float> gloss(bounds.withHeight(bounds.getHeight() * 0.35f));
            juce::ColourGradient glossGradient(
                juce::Colours::white.withAlpha(0.05f),
                gloss.getCentreX(), gloss.getY(),
                juce::Colours::transparentBlack,
                gloss.getCentreX(), gloss.getBottom(),
                false
            );
            g.setGradientFill(glossGradient);
            g.fillRoundedRectangle(gloss, 6.0f);

            juce::Font font(14.0f, juce::Font::bold);
            g.setFont(font);
            auto text = getButtonText();
            auto textBounds = getLocalBounds();

            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.drawFittedText(text, textBounds.translated(1, 1), juce::Justification::centred, 1);

            g.setColour(juce::Colours::lightgrey.withAlpha(0.8f));
            g.drawFittedText(text, textBounds, juce::Justification::centred, 1);
        }
    };

    ShadowedTextButton switchButton{ "Switch Page" };

    bool showingSynth = true;
    juce::String currentPage = "synth";
    bool cameraRunning = false;

    //Button::Listener
    void buttonClicked(juce::Button* button) override;
    juce::Image currentParameterIcon;
    //python
    void launchPythonHandTracker();
    void stopPythonHandTracker();
    void timerCallback() override;
    juce::ChildProcess pythonProcess;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CMProjectAudioProcessorEditor)
    
    //Class used to create the white dots that sit on the fingers
    class CircleButton : public juce::Button
    {
    public:
        CircleButton() : Button("indexHotspot") {}
        void setIconImage(const juce::Image& img)
        {
            icon = img;
            hasIcon = true;
            repaint();
        }
        void setZoomFactor(float newZoom) noexcept
        {
            zoomFactor = juce::jmax(1.0f, newZoom);
        }


        void setSquareStyle(bool shouldUseSquare) noexcept
        {
            useSquareStyle = shouldUseSquare;
            repaint();
        }

        //drawing a solid white circle filling our bounds
        void paintButton(juce::Graphics& g, bool /*isOver*/, bool /*isDown*/) override
        {
            constexpr float outlineWidth = 2.0f;
            auto bounds = getLocalBounds().toFloat().reduced(outlineWidth * 0.5f);

            if (useSquareStyle)
            {
                g.setColour(juce::Colours::transparentBlack); // fully transparent background
                g.fillRect(bounds); // ensure background is cleared

                // Use same limegreen as title
                float cornerRadius = 5.0f; // or any radius you like
                juce::Colour titleColor = juce::Colours::limegreen.withBrightness(1.2f);
                g.setColour(titleColor);
                g.drawRoundedRectangle(bounds, cornerRadius, 1.0f); // Adjust thickness as needed
            }

            else
            {
                juce::ColourGradient glow(
                    juce::Colour::fromFloatRGBA(0.f, 1.f, 1.f, 0.5f),
                    bounds.getCentreX(), bounds.getCentreY(),
                    juce::Colours::transparentWhite,
                    bounds.getRight(), bounds.getBottom(),
                    true
                );
                g.setGradientFill(glow);
                g.fillEllipse(bounds);

                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.drawEllipse(bounds, outlineWidth);
            }

            if (hasIcon && icon.isValid())
            {
                float iw = (float)icon.getWidth(), ih = (float)icon.getHeight();
                float srcW = juce::jmax(1.0f, iw / zoomFactor);
                float srcH = juce::jmax(1.0f, ih / zoomFactor);
                float srcX = (iw - srcW) * 0.5f, srcY = (ih - srcH) * 0.5f;

                // Create circular clip path inside the bounds
                juce::Path clip;
                clip.addEllipse(bounds);
                g.reduceClipRegion(clip);

                // Draw the image scaled to the whole bounds — now safely clipped
                g.drawImage(icon,
                    bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight(),
                    srcX, srcY,
                    srcW, srcH);
            }

        }
        void clearIcon() noexcept
        {
            hasIcon = false;
            repaint();
        }

        void restoreIcon() noexcept
        {
            if (icon.isValid())
                hasIcon = true;
            repaint();
        }
        //pointer cursor
        juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }
    private:
        juce::Image icon;
        bool hasIcon = false;
        float       zoomFactor = 5.0f;  // try 1.5–3.0 for a tighter crop
        bool useSquareStyle = false;

    };

    CircleButton indexButton;
    CircleButton middleButton;
    CircleButton ringButton;
    CircleButton pinkyButton;
    CircleButton indexLeftButton;
    CircleButton middleLeftButton;
    CircleButton indexRightButton;
    CircleButton middleRightButton;
    CircleButton lfoParamButton;
    juce::ImageComponent indexGlow, middleGlow, ringGlow, pinkyGlow;

    // Draws and positions a projecting image for the selected parameter at a given location with optional rotation and size
    void assignGlowToFinger(const juce::String& parameter,
        juce::ImageComponent& glowTarget,
        juce::Point<int> position,
        float rotationDeg = 0.0f,
        int targetWidth = 120,
        int targetHeight = 120);

    //Class for the status display
    class StatusDisplay : public juce::Component,
        private juce::Timer
    {
    public:
        StatusDisplay()
        {
            //tick every 3 seconds to auto-clear
            startTimerHz(1);  //trigger clear manually after message is set
            stopTimer();       //run when we call showMessage()
        }

        void paint(juce::Graphics& g) override
        {
            //draw the screen rectangule
            auto displayArea = getLocalBounds().toFloat().reduced(4.0f);

            //Background
            juce::Colour dark = juce::Colour::fromRGB(20, 20, 20);
            juce::Colour light = juce::Colour::fromRGB(40, 40, 40);
            juce::ColourGradient backgroundGradient(
                dark,
                displayArea.getX(), displayArea.getBottom(), //start at bottom-left
                light,
                displayArea.getX(), displayArea.getY(),      //end at top-left
                false//linear
            );
            g.setGradientFill(backgroundGradient);
            g.fillRoundedRectangle(displayArea, 8.0f);
            //Soft glass reflection at the top quarter of the display
            auto glassRect = displayArea.withHeight(displayArea.getHeight() * 0.25f);

            juce::ColourGradient glassGradient(
                juce::Colours::white.withAlpha(0.2f),
                glassRect.getX(), glassRect.getY(),
                juce::Colours::transparentWhite,
                glassRect.getX(), glassRect.getBottom(),
                false
            );
            g.setGradientFill(glassGradient);
            g.fillRect(glassRect);


            //Light-grey rounded border around the display
            g.setColour(juce::Colours::lightgrey);
            //g.drawRoundedRectangle(displayArea, 8.0f, 1.0f); //1.0f-> border

            //only draw the text if there’s a message
            if (!message.isEmpty())
            {
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(14.0f, juce::Font::bold));
                g.drawFittedText(message,
                    getLocalBounds().reduced(8, 6),
                    juce::Justification::centred,
                    1);
            }
        }


        /** Call this to show a new status string.  It will
            repaint immediately, then clear itself after 3s. */
        void showMessage(const juce::String& m)
        {
            message = m;
            toFront(false);
            repaint();
            stopTimer();
            startTimer(3000);// one-shot 3 seconds
        }

    private:
        void timerCallback() override
        {
            message.clear();
            stopTimer();
            repaint();
        }

        juce::String message;
    };
    StatusDisplay statusDisplay;

 // ==================================================
 // look and feel functions for buttons visual effects!
 // ==================================================

 //Steps styling function
    struct RoundedStepLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
            const juce::Colour&, bool isMouseOverButton, bool isButtonDown) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
            bool isToggled = button.getToggleState();

            float cornerRadius = 3.0f;

            // === SHADOW BELOW BUTTON ===
            {
                juce::DropShadow ds(juce::Colours::black.withAlpha(0.4f), 6, { 0, 3 });
                ds.drawForRectangle(g, bounds.getSmallestIntegerContainer());
            }

            // === BASE COLOR ===
            juce::Colour baseColour = isToggled ? juce::Colours::limegreen
                : juce::Colour::fromFloatRGBA(0.27f, 0.27f, 0.27f, 1.0f);

            // === BASE BODY ===
            g.setColour(baseColour);
            g.fillRoundedRectangle(bounds, cornerRadius);

            // === INNER LIGHT + DEPTH ===
            {
                juce::ColourGradient innerGlow(
                    juce::Colours::black.withAlpha(0.15f),
                    bounds.getCentreX(), bounds.getY(),
                    juce::Colours::transparentBlack,
                    bounds.getCentreX(), bounds.getBottom(),
                    false);
                g.setGradientFill(innerGlow);
                g.fillRoundedRectangle(bounds, cornerRadius);
            }

            // === GREEN GLOW WHEN TOGGLED ===
            if (isToggled)
            {
                juce::Rectangle<float> glowArea = bounds.expanded(3.0f);
                juce::ColourGradient glow(
                    juce::Colours::limegreen.withAlpha(0.3f),
                    glowArea.getCentreX(), glowArea.getCentreY(),
                    juce::Colours::transparentBlack,
                    glowArea.getCentreX(), glowArea.getBottom(),
                    true);
                g.setGradientFill(glow);
                g.fillRoundedRectangle(glowArea, cornerRadius + 1.0f);
            }

            // === TOP EDGE LIGHT REFLECTION ===
            if (!isToggled)
            {
                juce::Rectangle<float> gloss = bounds.withHeight(bounds.getHeight() * 0.35f);
                juce::ColourGradient grad(
                    juce::Colours::white.withAlpha(0.05f),
                    gloss.getCentreX(), gloss.getY(),
                    juce::Colours::transparentBlack,
                    gloss.getCentreX(), gloss.getBottom(),
                    false);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(gloss, cornerRadius);
            }

            // === BORDER ===
            g.setColour(baseColour.darker(1.7f));
            g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
        }

        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override {}
    };

    //Mute button for the drumPage
    struct MuteButtonLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
            const juce::Colour&, bool isMouseOver, bool isButtonDown) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

            juce::Colour base = juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f); //same translucent grey
            if (isMouseOver) base = base.brighter(0.1f);
            if (isButtonDown) base = base.darker(0.1f);
            g.setColour(base);
            g.fillRoundedRectangle(bounds, 6.0f);
            //subtle top gloss
            juce::Rectangle<float> glossArea(bounds.withHeight(bounds.getHeight() * 0.35f));
            juce::ColourGradient gloss(
                juce::Colours::white.withAlpha(0.05f),
                glossArea.getCentreX(), glossArea.getY(),
                juce::Colours::transparentBlack,
                glossArea.getCentreX(), glossArea.getBottom(),
                false
            );
            g.setGradientFill(gloss);
            g.fillRoundedRectangle(glossArea, 6.0f);
        }

        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds();
            juce::Font font = getTextButtonFont(button, button.getHeight()).withHeight(14.0f).boldened();
            g.setFont(font);

            if (button.getToggleState())
            {
                auto glowText = button.getButtonText();

                //Glow behind the text
                g.setFont(font);
                g.setColour(juce::Colours::limegreen.withAlpha(0.3f));

                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.drawFittedText(glowText, bounds.translated(dx, dy), juce::Justification::centred, 1);

                //Solid text
                g.setColour(juce::Colours::limegreen.withBrightness(1.15f));
                g.drawFittedText(glowText, bounds, juce::Justification::centred, 1);
            }
            else
            {
                g.setColour(juce::Colours::lightgrey.withAlpha(0.8f));
                g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
            }
        }
    };
    //start and stop buttons functions visual
    struct StartButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);

            // Shrink triangle height to 60% and center it vertically
            float triangleHeight = bounds.getHeight() * 0.85f;
            float yOffset = (bounds.getHeight() - triangleHeight) / 2.0f;

            juce::Path triangle;
            triangle.addTriangle(
                bounds.getX(), bounds.getY() + yOffset,
                bounds.getRight(), bounds.getCentreY(),
                bounds.getX(), bounds.getBottom() - yOffset);

            if (button.getToggleState())
            {
                // Glow behind triangle
                g.setColour(juce::Colours::limegreen.withAlpha(0.3f));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.fillPath(triangle, juce::AffineTransform::translation((float)dx, (float)dy));

                // Solid limegreen triangle
                g.setColour(juce::Colours::limegreen.withBrightness(1.15f));
            }
            else
            {
                // Default solid white
                g.setColour(juce::Colours::white.withAlpha(0.85f));
            }

            g.fillPath(triangle);
        }
    };
    struct StopButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.85f));

            float barWidth = bounds.getWidth() * 0.2f;
            float gap = bounds.getWidth() * 0.15f;

            float barHeight = bounds.getHeight() * 0.8f;
            float yOffset = (bounds.getHeight() - barHeight) / 2.0f;

            juce::Rectangle<float> leftBar(
                bounds.getX() + (bounds.getWidth() - 2 * barWidth - gap) * 0.5f,
                bounds.getY() + yOffset,
                barWidth,
                barHeight);

            juce::Rectangle<float> rightBar = leftBar.translated(barWidth + gap, 0);
            g.fillRect(leftBar);
            g.fillRect(rightBar);
        }
    };
    //start camera buttons
    struct StartCameraButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.85f));

            //Camera body on the left
            juce::Rectangle<float> body(bounds.getX(), bounds.getCentreY() - 8.0f, 22.0f, 16.0f);
            g.fillRoundedRectangle(body, 4.0f);

            //Corrected trapezoid: small side near rectangle, large side outside
            juce::Path lens;
            float lensHeight = 12.0f;
            float taper = 4.0f;
            float baseX = body.getRight();      //right of the camera body
            float centerY = body.getCentreY();

            lens.startNewSubPath(baseX, centerY - lensHeight / 2 + taper);     // top-left (narrow base)
            lens.lineTo(baseX + 8.0f, centerY - lensHeight / 2);      // top-right
            lens.lineTo(baseX + 8.0f, centerY + lensHeight / 2);     // bottom-right
            lens.lineTo(baseX, centerY + lensHeight / 2 - taper);    // bottom-left
            lens.closeSubPath();
            g.fillPath(lens);
        }
    };
    //stop camera buttons
    struct StopCameraButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.85f));

            // Camera body
            juce::Rectangle<float> body(bounds.getX(), bounds.getCentreY() - 8.0f, 22.0f, 16.0f);
            g.fillRoundedRectangle(body, 4.0f);

            // Lens (trapezoid, flipped horizontally)
            juce::Path lens;
            float lensHeight = 12.0f;
            float taper = 4.0f;
            float baseX = body.getRight();
            float centerY = body.getCentreY();

            lens.startNewSubPath(baseX, centerY - lensHeight / 2 + taper);       // top-left (narrow base)
            lens.lineTo(baseX + 8.0f, centerY - lensHeight / 2);                 // top-right
            lens.lineTo(baseX + 8.0f, centerY + lensHeight / 2);                 // bottom-right
            lens.lineTo(baseX, centerY + lensHeight / 2 - taper);               // bottom-left
            lens.closeSubPath();
            g.fillPath(lens);

            // Oblique "disabled" line (white core + blackish border)
            juce::Path slash;
            float lineWidth = 2.5f;
            float borderWidth = 6.5f; // ⬅️ thicker border

            slash.startNewSubPath(bounds.getX() + 2.0f, bounds.getBottom() - 2.0f);
            slash.lineTo(bounds.getRight() - 2.0f, bounds.getY() + 2.0f);

            //Border first (button base color)
            g.setColour(juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f));
            g.strokePath(slash, juce::PathStrokeType(borderWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            //Inner white line
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.strokePath(slash, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };
    //record midi buttons
    struct RecordMidiButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);
            float radius = 6.5f;
            auto center = bounds.getCentre();

            juce::Path circlePath;
            circlePath.addEllipse(center.x - radius, center.y - radius, radius * 2, radius * 2);

            if (button.getToggleState())
            {
                g.setColour(juce::Colours::red.withAlpha(0.3f));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.fillEllipse(center.x + dx - radius, center.y + dy - radius, radius * 2, radius * 2);

                g.setColour(juce::Colours::red.brighter(0.2f));
            }
            else
            {
                g.setColour(juce::Colours::red.withAlpha(0.8f));
            }

            //Fill the main red circle
            g.fillPath(circlePath);
            //Draw thin black border
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.strokePath(circlePath, juce::PathStrokeType(1.0f));
        }
    };
    //stop midi button
    struct StopMidiButtonLookAndFeel : public MuteButtonLookAndFeel
    {
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(8.0f);
            float size = 11.30f;
            auto center = bounds.getCentre();
            juce::Rectangle<float> square(center.x - size / 2, center.y - size / 2, size, size);

            if (button.getToggleState())
            {
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.fillRoundedRectangle(square.translated(dx, dy), 3.0f);

                g.setColour(juce::Colours::white.brighter(0.15f));
            }
            else
            {
                g.setColour(juce::Colours::white.withAlpha(0.8f));
            }

            g.fillRoundedRectangle(square, 3.0f);
        }
    };

 
};

