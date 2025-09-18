#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>


bool isPythonOn = false;//Boolean used to handle assertions

//Maps each parameter name to its corresponding projection image filename for visual feedback
static const std::map<juce::String, juce::String> parameterToGlowImage = {
    { "GrainPos", "glow_grainPos.png" },
    { "GrainDur", "glow_grainDur.png" },
    { "GrainDensity", "glow_grainDensity.png" },
    { "GrainReverse", "glow_grainReverse.png" },
    { "GrainPitch", "glow_grainPitch.png" },
    { "GrainCutOff", "glow_grainCutOff.png" },
    { "lfoRate", "glow_lfo.png" }
};

//Resolves and returns the full path to a projection image file located in the "Assets" folder of the project directory
static juce::File getGlowFile(const juce::String& fileName)
{
    juce::File exeFolder = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory();

#if JUCE_MAC
    exeFolder = exeFolder.getParentDirectory()   // Contents
        .getParentDirectory()   // <Plugin>.vst3
        .getParentDirectory();  // build folder
#endif

    auto dir = exeFolder;
    while (dir.exists())
    {
        if (dir.getChildFile("CMProject.jucer").existsAsFile())
            break;

        dir = dir.getParentDirectory();
    }

    return dir.getChildFile("Assets").getChildFile(fileName);
}

//Loads, transforms (scale + rotate), and displays the appropriate projection image for a finger-assigned parameter.
void CMProjectAudioProcessorEditor::assignGlowToFinger(const juce::String& parameter,
    juce::ImageComponent& glowTarget,
    juce::Point<int> position,
    float rotationDeg,
    int targetWidth,
    int targetHeight)
{
    auto it = parameterToGlowImage.find(parameter);
    if (it == parameterToGlowImage.end())
        return;

    auto glowFile = getGlowFile(it->second);
    if (!glowFile.existsAsFile())
    {
        DBG("❌ Missing glow image: " << it->second);
        return;
    }

    juce::Image original = juce::ImageFileFormat::loadFrom(glowFile);
    if (original.isNull())
    {
        DBG("❌ Failed to load image: " << glowFile.getFullPathName());
        return;
    }

    //Create a new transparent image as canvas
    juce::Image canvas(juce::Image::ARGB, targetWidth, targetHeight, true);
    juce::Graphics g(canvas);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    //Prepare affine transform: center, scale, rotate
    float scaleX = (float)targetWidth / original.getWidth();
    float scaleY = (float)targetHeight / original.getHeight();

    juce::AffineTransform transform;
    transform = transform
        .translated(-original.getWidth() * 0.5f, -original.getHeight() * 0.5f) //center origin
        .scaled(scaleX, scaleY)
        .rotated(juce::degreesToRadians(rotationDeg))
        .translated(targetWidth * 0.5f, targetHeight * 0.5f); //move back to canvas center

    g.addTransform(transform);
    g.drawImage(original, 0, 0, original.getWidth(), original.getHeight(),
        0, 0, original.getWidth(), original.getHeight());

    glowTarget.setImage(canvas, juce::RectanglePlacement::centred);
    glowTarget.setBounds(position.getX(), position.getY(), targetWidth, targetHeight);
    glowTarget.setVisible(true);
    addAndMakeVisible(glowTarget);

    //Enforce strict Z-order: middle < ring < pinky
    middleGlow.toFront(false); //back
    ringGlow.toFront(false);  //middle
    pinkyGlow.toFront(false);  //top
}

//Static function used to find the agnostic correct path
static juce::File getHandTrackerScript()
{
    juce::File exeFolder = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile)
        .getParentDirectory();

#if JUCE_MAC
    
    exeFolder = exeFolder.getParentDirectory() //Contents
        .getParentDirectory()  //<Name>.vst3
        .getParentDirectory(); //build folder
#endif

    auto dir = exeFolder; //Walk up the tree until repo found
    while (dir.exists())
    {
        if (dir.getChildFile("CMProject.jucer").existsAsFile()) //Once found CMproject.jucer, search the python script
            break;
        dir = dir.getParentDirectory();
    }
    //append the relative bits
    return dir.getChildFile("python")
        .getChildFile("HandTracker")
        .getChildFile("main.py");
}

//For the hand neon-green image
static juce::File getHandImageFile()
{
    juce::File exeFolder = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile)
        .getParentDirectory();

#if JUCE_MAC
    exeFolder = exeFolder.getParentDirectory()
        .getParentDirectory()
        .getParentDirectory();
#endif

    auto dir = exeFolder;
    while (dir.exists())
    {
        if (dir.getChildFile("CMProject.jucer").existsAsFile()) //Same logic used for the python script
            break;
        dir = dir.getParentDirectory();
    }
    return dir.getChildFile("Assets").getChildFile("handimage.png"); //Append the image of hands
}

//borders of the plugin that light up periodically
class GridBackgroundComponent : public juce::Component, private juce::Timer
{
public:
    GridBackgroundComponent()
    {
        startTimerHz(30); //Smooth 30 FPS animation
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black); //Fill all black
        float glowValue = 0.5f * (1.0f + std::sin(phase));  //Sine wave from 0 to 1

        float hue = 0.3f; //green tone
        float saturation = juce::jmap(glowValue, 0.0f, 1.0f, 0.1f, 1.0f);  //soft gray → full color
        float brightness = juce::jmap(glowValue, 0.0f, 1.0f, 0.05f, 1.0f); //dim → full bright

        juce::Colour glow = juce::Colour::fromHSV(hue, saturation, brightness, 1.0f);
        g.setColour(glow); //Set the glow
        auto bounds = getLocalBounds().toFloat();
        float thickness = 4.0f; //thickness of the glow

        g.fillRect(bounds.removeFromTop(thickness));
        bounds = getLocalBounds().toFloat();
        g.fillRect(bounds.removeFromBottom(thickness));
        bounds = getLocalBounds().toFloat();
        g.fillRect(bounds.removeFromLeft(thickness));
        bounds = getLocalBounds().toFloat();
        g.fillRect(bounds.removeFromRight(thickness));
    }

    void timerCallback() override
    {
        phase += 0.02f; //controls glow speed (lower = slower)

        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
        repaint();
    }

private:
    float phase = 0.0f; //for sine wave cycling
};

//Struct for the knobs (from juce to images)
struct ImageKnobLookAndFeel : public juce::LookAndFeel_V4
{
    juce::Image knobImage; //image built
    ImageKnobLookAndFeel() {}
    void setKnobImage(const juce::Image& img) { knobImage = img; } //set the image

    // this is called whenever any rotary slider with this L&F needs painting
    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPosProportional,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& slider) override
    {
        if (knobImage.isNull()) //if it is null, just draw the default juce one
        {
            LookAndFeel_V4::drawRotarySlider(g, x, y, width, height,
                sliderPosProportional,
                rotaryStartAngle,
                rotaryEndAngle,
                slider);
            return;
        }
        //make it rotable
        const float angle = rotaryStartAngle
            + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const float cx = x + width * 0.5f;
        const float cy = y + height * 0.5f;
        const float radius = juce::jmin(width, height) * 0.5f;
        const juce::Rectangle<float> knobBounds(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

        //Ensure sharp rendering
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        //Apply rotation only to the image itself
        g.drawImageTransformed(knobImage,
            juce::AffineTransform::rotation(angle, knobImage.getWidth() * 0.5f, knobImage.getHeight() * 0.5f)
            .scaled(knobBounds.getWidth() / knobImage.getWidth(),
                knobBounds.getHeight() / knobImage.getHeight())
            .translated(knobBounds.getX(), knobBounds.getY()));
    }
};

class HDImageButton : public juce::ImageButton
{
public:
    using juce::ImageButton::ImageButton;
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        const auto image = getNormalImage();   //This is public
        auto bounds = getLocalBounds();
        g.drawImageWithin(image,
            bounds.getX(), bounds.getY(),
            bounds.getWidth(), bounds.getHeight(),
            juce::RectanglePlacement::centred,
            false);
    }
};

// ==================================================
// Synth Page Component (Granular Synth UI)
// ==================================================

class CMProjectAudioProcessorEditor::SynthPageComponent : public juce::Component, private juce::ChangeListener, private juce::Slider::Listener, public juce::FileDragAndDropTarget
{
public:
    juce::TextButton startButton, stopButton, startCamera, stopCamera, loadSampleButton; //StartSynth, StopSynth, StartCamera, StopCamera
    juce::TextButton recordMidiButton{ "Record MIDI" }, stopMidiButton{ "Stop Recording" }, saveMidiButton{ "Save MIDI" }; //Midi buttons
    HDImageButton grainPos, grainDur, grainDensity, grainReverse, grainCutOff, grainPitch, lfoRate; //Hd images for granulator parameters
    juce::Image startImg, stopImg; //Start and Stop Images
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider; //Adsr sliders
    juce::Label  attackLabel, decayLabel, sustainLabel, releaseLabel, granulatorTitle, extraTitleLabel; //Labels for the adsr and granulator
    juce::File currentSampleFile, originalSampleFile; //Reversed- not reversed file
    bool isReversed = false; //Boolean to handle when the sample is reversed or not
    juce::Rectangle<int> adsrBoxArea; //Adsr knobs area
    float currentGrainPos = 0.0f; //Current grain position value
    float sampleDuration = 1.0f; //default, will be updated
    bool isLfoActive = false;
    juce::TextButton resetButton{ "Reset" }; //Button useful to reset all the default values
    
    //Constructor
    SynthPageComponent()
    {
        formatManager.registerBasicFormats(); //wav,mp3
        thumbnail.addChangeListener(this);
        connectToSuperCollider(); //Function that connects juce to supercollider
        granulatorParametersTitle(); //Granulator parameters
        imagesSetup(); //Function that loads all the images
        setButtonsAndLookAndFeel();//Function that sets the buttons and the different look and feel
        addSynthPageComponents(); //Function that adds all the synthpage components and makes them visible
        onClickSynthFunction(); //Function that handles all one click functions for the synthpage
        adsrSetup();  //Function that handles adsr setup
        adsrTitleSet(); //Function that sets the style of the adsr title
        resetButton.setButtonText("Reset");
        resetButton.setTooltip("Reset all parameters");
        addAndMakeVisible(resetButton);
    }

    //Deconstructor
    ~SynthPageComponent() override
    {
        // Clear any LookAndFeel references FIRST
        setLookAndFeel(nullptr);
        
        // Clear LookAndFeel from all child components
        for (auto* child : getChildren())
        {
            if (child != nullptr)
                child->setLookAndFeel(nullptr);
        }
        
        oscSender.send("/stop", 60);
        thumbnail.removeChangeListener(this);
        oscSender.send("/disconnect");
        oscSender.disconnect();
    }
    void adsrTitleSet() {
        extraTitleLabel.setText("ADSR Envelope", juce::dontSendNotification);
        extraTitleLabel.setFont(juce::Font("Arial", 20.0f, juce::Font::bold));
        extraTitleLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen.withBrightness(1.2f));
        extraTitleLabel.setJustificationType(juce::Justification::centredLeft);
        //same glow effect
        auto* shadow2 = new juce::DropShadowEffect();
        shadow2->setShadowProperties(juce::DropShadow(juce::Colours::limegreen.withAlpha(0.4f), 4.0f, { 1, 1 }));
        extraTitleLabel.setComponentEffect(shadow2);
    }

    void addSynthPageComponents()
    {
        addAndMakeVisible(startButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(startCamera);
        addAndMakeVisible(stopCamera);
        addAndMakeVisible(loadSampleButton);
        addAndMakeVisible(recordMidiButton);
        addAndMakeVisible(stopMidiButton);
        addAndMakeVisible(saveMidiButton);
        addAndMakeVisible(grainPos);
        addAndMakeVisible(grainDur);
        addAndMakeVisible(grainDensity);
        addAndMakeVisible(grainCutOff);
        addAndMakeVisible(grainPitch);
        addAndMakeVisible(grainReverse);
        addAndMakeVisible(lfoRate);
        addAndMakeVisible(granulatorTitle);
        addAndMakeVisible(extraTitleLabel);
        addAndMakeVisible(lfoLinkLine);
        lfoLinkLine.setInterceptsMouseClicks(false, false);

    }
    void imagesSetup() {
        juce::File grainPosFile = getIconFile("grainPos.png");
        setupImageButton(grainPos, grainPosFile);
        juce::File grainDurFile = getIconFile("grainDur.png");
        setupImageButton(grainDur, grainDurFile);
        juce::File grainDensityFile = getIconFile("grainDensity.png");
        setupImageButton(grainDensity, grainDensityFile);
        juce::File grainCutOffFile = getIconFile("grainCutOff.png");
        setupImageButton(grainCutOff, grainCutOffFile);
        juce::File grainPitchFile = getIconFile("grainPitch.png");
        setupImageButton(grainPitch, grainPitchFile);
        juce::File grainReverseFile = getIconFile("grainReverse.png");
        setupImageButton(grainReverse, grainReverseFile);
        juce::File lfoRateFile = getIconFile("lfo.png");
        setupImageButton(lfoRate, lfoRateFile);


        //Load and apply the image for ADSR knobs
        juce::File knobFile = getIconFile("realknob.png"); //make sure the name matches the asset
        if (!knobFile.existsAsFile())
            DBG("❌ realknob.png not found at: " + knobFile.getFullPathName());
        else
        {
            juce::Image img = juce::ImageFileFormat::loadFrom(knobFile);
            if (img.isNull())
                DBG("❌ Failed to load realknob.png");
            else
                adsrKnobLookAndFeel.setKnobImage(img);
        }

    }

    void onClickSynthFunction() {
        int defaultNote = 60; //MIDI C4, used as default sound if you hit startSound
        float defaultVel = 1.0f;
        startButton.onClick = [this, defaultNote, defaultVel]() {
            oscSender.send("/start", defaultNote, defaultVel);
            stopButton.setEnabled(true);
            //Turn on glow
            startButton.setToggleState(true, juce::dontSendNotification);
            stopButton.setToggleState(false, juce::dontSendNotification);
            };

        stopButton.onClick = [this, defaultNote]() {
            oscSender.send("/stop", defaultNote);
            stopButton.setEnabled(false);
            //Turn off glow
            startButton.setToggleState(false, juce::dontSendNotification);
            stopButton.setToggleState(false, juce::dontSendNotification);

            };
        loadSampleButton.onClick = [this] { pickAndLoadSample();
            };
        grainReverse.onClick = [this]()
            {
                reverseSample(); //Reverse in buffer and in the aspect the audio track
                //update the button’s look:
                grainReverse.setToggleState(isReversed, juce::dontSendNotification);
            };
    }

    void setButtonsAndLookAndFeel() {
        startCamera.setLookAndFeel(&startCameraLookAndFeel);
        stopCamera.setLookAndFeel(&stopCameraLookAndFeel);
        loadSampleButton.setButtonText("Load Sample");
        loadSampleButton.setLookAndFeel(&loadButtonLookAndFeel);
        resetButton.setLookAndFeel(&loadButtonLookAndFeel);
        saveMidiButton.setLookAndFeel(&loadButtonLookAndFeel);
        stopCamera.setEnabled(false);  //only enable once camera is running
        stopButton.setEnabled(false);  //only enable once the sound is being played
        stopMidiButton.setEnabled(false);
        saveMidiButton.setEnabled(false);
        recordMidiButton.setLookAndFeel(&recordMidiLookAndFeel);
        stopMidiButton.setLookAndFeel(&stopMidiLookAndFeel);
        startButton.setButtonText("Start Drums");
        stopButton.setButtonText("Stop Drums");
        startButton.setLookAndFeel(&startButtonLookAndFeel);
        stopButton.setLookAndFeel(&stopButtonLookAndFeel);
        startButton.setClickingTogglesState(false);
        stopButton.setClickingTogglesState(false);
    }

    void connectToSuperCollider() {
        if (!oscSender.connect("127.0.0.1", 57121))
            DBG("❌ Could not connect to SuperCollider on port 57121");
        else
            DBG("✅ Connected to SuperCollider via OSC");

    }

    void granulatorParametersTitle() {
        granulatorTitle.setText("Granulator Parameters", juce::dontSendNotification);
        granulatorTitle.setFont(juce::Font("Arial", 20.0f, juce::Font::bold));
        granulatorTitle.setColour(juce::Label::textColourId, juce::Colours::limegreen.withBrightness(1.2f));
        granulatorTitle.setJustificationType(juce::Justification::centredLeft);
        auto* shadow = new juce::DropShadowEffect();
        shadow->setShadowProperties(juce::DropShadow(juce::Colours::limegreen.withAlpha(0.4f), 4.0f, { 1, 1 }));
        granulatorTitle.setComponentEffect(shadow);
    }

    void adsrSetup() {
        auto addADSR = [&](juce::Slider& s, juce::Label& l, const juce::String& text)
            {
                s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                s.setRange(0.0, 5.0, 0.001);
                s.setLookAndFeel(&adsrKnobLookAndFeel);
                s.addListener(this);
                l.setText(text, juce::dontSendNotification);
                l.attachToComponent(&s, true);
                l.setJustificationType(juce::Justification::centredRight);
                addAndMakeVisible(s);
                addAndMakeVisible(l);
            };

        addADSR(attackSlider, attackLabel, "A");
        addADSR(decaySlider, decayLabel, "D");
        sustainSlider.setRange(0.0, 1.0, 0.001);     // ← here!
        addADSR(sustainSlider, sustainLabel, "S");
        addADSR(releaseSlider, releaseLabel, "R");

        //Apply BPM-style font and color to ADSR labels
        auto styleADSRLabel = [](juce::Label& label)
            {
                label.setFont(juce::Font(15.5f, juce::Font::bold));
                label.setColour(juce::Label::textColourId, juce::Colours::lightgrey.withAlpha(0.85f));
                label.setJustificationType(juce::Justification::centredRight); // already used
            };

        //Call after addADSR
        styleADSRLabel(attackLabel);
        styleADSRLabel(decayLabel);
        styleADSRLabel(sustainLabel);
        styleADSRLabel(releaseLabel);

        //give defaults to match SC defaults:
        attackSlider.setValue(0.01);
        decaySlider.setValue(0.1);
        sustainSlider.setValue(0.85);
        releaseSlider.setValue(0.2);
    }

    void paint(juce::Graphics& g) override
    {
        // ==============================
        // ADSR BOX BACKGROUND VISUALS
        // ==============================
        {
            juce::Graphics::ScopedSaveState adsrClipState(g); //restores clipping at the end

            juce::Path adsrClip;
            adsrClip.addRoundedRectangle(adsrBoxArea.toFloat(), 8.0f);
            g.reduceClipRegion(adsrClip);

            juce::Colour adsrDark = juce::Colour::fromRGB(20, 20, 20);
            juce::Colour adsrLight = juce::Colour::fromRGB(40, 40, 40);
            juce::ColourGradient adsrBg(
                adsrDark, adsrBoxArea.getX(), adsrBoxArea.getBottom(),
                adsrLight, adsrBoxArea.getX(), adsrBoxArea.getY(), false
            );
            g.setGradientFill(adsrBg);
            g.fillRect(adsrBoxArea);

            auto adsrGlass = adsrBoxArea.withHeight(adsrBoxArea.getHeight() / 4);
            juce::ColourGradient glassGradientADSR(
                juce::Colours::white.withAlpha(0.2f),
                adsrGlass.getX(), adsrGlass.getY(),
                juce::Colours::transparentWhite,
                adsrGlass.getX(), adsrGlass.getBottom(),
                false
            );
            g.setGradientFill(glassGradientADSR);
            g.fillRect(adsrGlass);
        }


        // === ADSR SHAPE DRAWING ===
        juce::Path adsrPath;

        // Get current ADSR values from sliders
        float atk = (float)attackSlider.getValue();
        float dec = (float)decaySlider.getValue();
        float sus = (float)sustainSlider.getValue();
        float rel = (float)releaseSlider.getValue();

        const float susDisplayFrac = 0.20f;   // or make this dynamic as above
        const float sumADR = atk + dec + rel + 1e-6f;

        auto box = adsrBoxArea.toFloat().reduced(12.0f, 8.0f);
        float x0 = box.getX(), y0 = box.getY() + box.getHeight();
        float w = box.getWidth(), h = box.getHeight();

        // explicitly carve up the box
        float availW = w * (1.0f - susDisplayFrac);
        float aW = atk / sumADR * availW;
        float dW = dec / sumADR * availW;
        float rW = rel / sumADR * availW;

        float x1 = x0 + aW;
        float x2 = x1 + dW;
        float x3 = x2 + susDisplayFrac * w;
        float x4 = x3 + rW;

        // vertical positions
        float y1 = box.getY();
        float rawSus = (float)sustainSlider.getValue();    // 0…5
        sus = juce::jlimit(0.0f, 1.0f, rawSus / 5.0f);
        float y2 = box.getY() + (1.0f - sus) * h;
        //float y3 = y0;
        float sustainCurve = h * 0.05f;
        // build a smooth path
        adsrPath.startNewSubPath(x0, y0);
        adsrPath.lineTo(x1, y1);

        // 2) Decay lineare
        adsrPath.lineTo(x2, y2);
        //Sustain
        adsrPath.lineTo(x3, y2);

        // release curve:
        adsrPath.quadraticTo(
            x3 + 0.5f * (x4 - x3),
            y2 + 0.5f * (y0 - y2),
            x4, y0
        );

        // 4) Stampe come prima, con end-cap e joint arrotondati
        g.setColour(juce::Colours::limegreen.withBrightness(1.3f));
        g.strokePath(
            adsrPath,
            juce::PathStrokeType(
                2.0f,
                juce::PathStrokeType::JointStyle::curved,
                juce::PathStrokeType::EndCapStyle::rounded
            )
        );

        g.setColour(juce::Colours::yellow);
        float dotR = 3.0f;

        // five key points: (x0,y0), (x1,y1), (x2,y2), (x3,y2), (x4,y0)
        struct Pt { float x, y; };
        Pt pts[] = { {x0,y0}, {x1,y1}, {x2,y2}, {x3,y2}, {x4,y0} };

        for (auto& p : pts)
            g.fillEllipse(p.x - dotR, p.y - dotR,
                dotR * 2.0f, dotR * 2.0f);

        // ==============================
        // WAVEFORM BACKGROUND VISUALS
        // ==============================
        {
            juce::Graphics::ScopedSaveState waveformClipState(g);

            juce::Path clipPath;
            clipPath.addRoundedRectangle(waveformArea.toFloat(), 8.0f);
            g.reduceClipRegion(clipPath);

            juce::Colour dark = juce::Colour::fromRGB(20, 20, 20);
            juce::Colour light = juce::Colour::fromRGB(40, 40, 40);
            juce::ColourGradient bg(dark,
                waveformArea.getX(), waveformArea.getBottom(),
                light,
                waveformArea.getX(), waveformArea.getY(),
                false);
            g.setGradientFill(bg);
            g.fillRect(waveformArea);

            if (thumbnail.getTotalLength() > 0.0)
            {
                g.setColour(juce::Colours::limegreen.withBrightness(1.2f));
                thumbnail.drawChannel(g, waveformArea.translated(0.5f, 0.0f), 0.0, thumbnail.getTotalLength(), 0, 1.0f);
                thumbnail.drawChannel(g, waveformArea, 0.0, thumbnail.getTotalLength(), 0, 1.0f);
            }

                // ==== Draw GrainPos Indicator ====
                if (sampleDuration > 0.0f)
                {
                    float normPos = juce::jlimit(0.0f, 1.0f, currentGrainPos / sampleDuration);
                    int x = waveformArea.getX() + (int)(normPos * waveformArea.getWidth());

                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.drawLine((float)x, (float)waveformArea.getY(), (float)x, (float)waveformArea.getBottom(), 2.0f);
                }


            auto glassRect = waveformArea.withHeight(waveformArea.getHeight() / 4);
            juce::ColourGradient glassGradient(
                juce::Colours::white.withAlpha(0.2f),
                glassRect.getX(), glassRect.getY(),
                juce::Colours::transparentWhite,
                glassRect.getX(), glassRect.getBottom(),
                false
            );
            g.setGradientFill(glassGradient);
            g.fillRect(glassRect);
        }
    }

    //function that handles the change of the sliders value
    void sliderValueChanged(juce::Slider* s) override
    {
        //only ADSR sliders live here
        float atk = (float)attackSlider.getValue();
        float dec = (float)decaySlider.getValue();
        float sus = (float)sustainSlider.getValue();
        float rel = (float)releaseSlider.getValue();
        oscSender.send("/env", atk, dec, sus, rel); //send it to supercollider
        repaint(); //Paint again the wave
    }

    //function that reverses the sample
    void reverseSample()
    {
        if (!currentSampleFile.existsAsFile())
            return;

        if (!isReversed)
        {
            //Read the original file
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(originalSampleFile));
            if (reader == nullptr) return;

            auto numSamples = (int)reader->lengthInSamples; //check number of samples
            auto numChannels = (int)reader->numChannels;    //check number of channels
            juce::AudioBuffer<float> buffer(numChannels, numSamples);
            reader->read(&buffer, 0, numSamples, 0, true, true); //read the buffer

            //Reverse each channel in-place
            for (int ch = 0; ch < numChannels; ++ch)
                std::reverse(buffer.getWritePointer(ch),
                    buffer.getWritePointer(ch) + numSamples);

            //Write out a temp file
            auto temp = juce::File::createTempFile(".wav");
            if (auto* writer = formatManager
                .findFormatForFileExtension("wav")
                ->createWriterFor(new juce::FileOutputStream(temp),
                    reader->sampleRate,
                    (unsigned)numChannels,
                    reader->bitsPerSample,
                    {},
                    0))
            {
                writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
                delete writer;
            }
            else
            {
                jassertfalse;  //failed to create writer
                return;
            }

            //Point thumbnail & OSC at the *temp*
            thumbnail.setSource(new juce::FileInputSource(temp));
            repaint();
            oscSender.send("/loadSample", temp.getFullPathName()); //resend the correct sample to supercollider

            //Update state
            currentSampleFile = temp;
            isReversed = true;
        }
        else
        {
            //Simply restore the original
            thumbnail.setSource(new juce::FileInputSource(originalSampleFile));
            repaint();
            oscSender.send("/loadSample", originalSampleFile.getFullPathName());

            currentSampleFile = originalSampleFile;
            isReversed = false;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(20, 10);
        const int topY = area.getY() - 10;
        stopButton.setBounds(40-15, topY-1.3, 30, 30);
        startButton.setBounds(80-15, topY-1.3, 28, 30);
        startCamera.setBounds(120-16, topY-1, 46.5f, 30);
        stopCamera.setBounds(180-16, topY-1, 46.5f, 30);
        recordMidiButton.setBounds(240+416, topY+1, 40, 30);
        stopMidiButton.setBounds(290+416, topY+1, 40, 30);
        saveMidiButton.setBounds(340+413, topY+1, 100, 30);
        loadSampleButton.setBounds(40-16, topY+50, 100, 30);
        resetButton.setBounds(132, topY + 50, 80, 30);
        area.removeFromTop(30);

        //ADSR KNOBS POSITIONING - 2x2 layout
        int knobSize = 62;
        int spacingX = knobSize + 80; //horizontal space between columns
        int spacingY = knobSize + 20; //vertical space between rows

        int baseX = getWidth() - (2 * knobSize + 120);
        int baseY = getHeight() - (2 * spacingY + 156);
        int offsetTopRow = 12; //shift A and D knobs down of x pixels

        //First row (A and D)
        attackSlider.setBounds(baseX, baseY + offsetTopRow, knobSize, knobSize); // top-left
        decaySlider.setBounds(baseX + spacingX, baseY + offsetTopRow, knobSize, knobSize); // top-right

        //Second row (S and R)
        sustainSlider.setBounds(baseX, baseY + spacingY, knobSize, knobSize); // bottom-left
        releaseSlider.setBounds(baseX + spacingX, baseY + spacingY, knobSize, knobSize); // bottom-right
        const int sliderHeight = 30;
        const int gap = 20;
       
        //single row for all buttons
        auto buttonRow = area.removeFromTop(30);
        auto lowerRow = area.removeFromBottom(50);
        auto lowerRow2 = area.removeFromBottom(100);
        buttonRow.removeFromLeft(500); // add left margin to push right
       
        buttonRow.removeFromLeft(5);
        buttonRow.removeFromLeft(5);   
        grainDensity.setBounds(lowerRow.removeFromLeft(50));

        //move and size of grainCutOff 
        grainCutOff.setBounds(8, 395, 75, 75); 
        //move and size of grainPitch 
        grainPitch.setBounds(10, 470, 75, 75); 
        //move and size of grainReverse(done)  
        grainReverse.setBounds(80, 464.5, 86, 86); 
        //move and size of grainPos 
        grainPos.setBounds(158.5, 463.7, 86, 86);
        //move and size of grainDur 
        grainDur.setBounds(81.5, 391.7, 84, 84);
        //move and size of grainDensity 
        grainDensity.setBounds(168, 400, 65, 65);
        //move and size of lforate 
         lfoRate.setBounds(12.5, 550, 73, 73);

        //waveform
        area.removeFromTop(20);
        int waveformHeight = 140;
        waveformArea = area.removeFromTop(waveformHeight);
        granulatorTitle.setBounds(20, 350, 300, 30);  //Granulator title
        //envelope title
        extraTitleLabel.setBounds(687, 355, 200, 30); 
        //adsr box 
        adsrBoxArea = juce::Rectangle<int>(689, 551, 230, 130);  // x, y, width, height


        //lfo line position
        lfoLinkLine.setBounds(84.5, 584, 85, 4); // adjust x, y, width, height
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        auto bounds = lfoLinkLine.getBounds().toFloat();

        if (isLfoActive)
        {
            // Base line color
            juce::Colour base = juce::Colours::limegreen.withBrightness(1.25f).withAlpha(0.9f);

            // Glow color (less saturated, translucent)
            juce::Colour glow = juce::Colours::limegreen.withAlpha(0.25f);

            // Fill the base line
            g.setColour(base);
            g.fillRect(bounds);

            // Add glow shadow (soft spread)
            juce::DropShadow softShadow(glow, 12, {});
            softShadow.drawForRectangle(g, bounds.toNearestInt());

            // Add top translucent gloss (like the gloss area in your buttons)
            auto glossHeight = bounds.getHeight() * 0.5f;
            juce::Rectangle<float> gloss(bounds.getX(), bounds.getY(), bounds.getWidth(), glossHeight);

            juce::ColourGradient glossGrad(
                juce::Colours::white.withAlpha(0.05f),
                gloss.getCentreX(), gloss.getY(),
                juce::Colours::transparentBlack,
                gloss.getCentreX(), gloss.getBottom(),
                false
            );

            g.setGradientFill(glossGrad);
            g.fillRect(gloss);
        }
        else
        {
            g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
            g.fillRect(bounds);
        }
    }

    void setupImageButton(juce::ImageButton& button, const juce::File& imageFile)
    {
        if (!imageFile.existsAsFile())
        {
            DBG("❌ Could not find image: " + imageFile.getFullPathName());
            return;
        }
        juce::Image img = juce::ImageFileFormat::loadFrom(imageFile);

        button.setImages(false, true, true,
            img, 1.0f, {},   // normal
            img, 0.7f, {},   // over
            img, 0.5f, {});  // down
        
       // button.setSize(img.getWidth(), img.getHeight()); // <-- important
        //This ensures that only the visible parts of the image respond to mouse events
        button.setClickingTogglesState(false);
    }

    static juce::File getIconFile(const juce::String& fileName)
    {
        juce::File exeFolder = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();

#if JUCE_MAC
        exeFolder = exeFolder.getParentDirectory()   // Contents
            .getParentDirectory()   // <Plugin>.vst3
            .getParentDirectory();  // build folder
#endif

        auto dir = exeFolder;
        while (dir.exists())
        {
            if (dir.getChildFile("CMProject.jucer").existsAsFile())
                break;

            dir = dir.getParentDirectory();
        }

        return dir.getChildFile("Assets").getChildFile(fileName); // returns e.g., "Start Icon.png"
    }

    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (source == &thumbnail)
            repaint();
    }
   
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& file : files)
            if (file.endsWith(".wav") || file.endsWith(".aiff") || file.endsWith(".flac") || file.endsWith(".mp3"))
                return true;
        return false;
    }

    static juce::String truncateWithEllipsis(const juce::String& s, int maxChars)
    {
        if (s.length() <= maxChars)
            return s;
        return s.substring(0, maxChars) + "...";
    }

    //Function used when a sample is draggedAndDropped inside the load rectangle
    void filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) override
    {
        if (files.isEmpty())
            return;

        juce::File droppedFile(files[0]);

        if (droppedFile.existsAsFile())
        {
            DBG("→ Dropped file: " << droppedFile.getFullPathName());

            oscSender.send("/loadSample", droppedFile.getFullPathName());

            thumbnail.setSource(new juce::FileInputSource(droppedFile));
            repaint();
            startButton.setEnabled(true);

            //Setting the name of the sample 
            auto fullName = droppedFile.getFileName();
            auto displayName = truncateWithEllipsis(fullName, 14);
            loadSampleButton.setButtonText(displayName);
            loadSampleButton.setTooltip(fullName);
            currentSampleFile = droppedFile;
            isReversed = false;  //< reset reverse-state whenever a fresh file is loaded
            originalSampleFile = droppedFile;
        }
    }

    void syncBpm(float newBpm)
    {
        BPMSlider.setValue(newBpm, juce::dontSendNotification);
        sendOSC();
    }

private:
    juce::Slider BPMSlider;
    juce::OSCSender oscSender;
    juce::Label grainDurLabel, grainPosLabel, cutoffLabel, bpmLabel;
    juce::Rectangle<int> waveformArea;
    juce::AudioFormatManager formatManager;   // Used to recognize audio formats (.wav, .mp3, etc.)
    juce::AudioThumbnailCache thumbnailCache{ 5 }; // Caches thumbnails for efficiency (5 = number of items)
    juce::AudioThumbnail thumbnail{ 2048, formatManager, thumbnailCache }; // Main object to draw the waveform
    StartCameraButtonLookAndFeel startCameraLookAndFeel;
    StopCameraButtonLookAndFeel stopCameraLookAndFeel;
    LoadButtonLookAndFeel loadButtonLookAndFeel;
    RecordMidiButtonLookAndFeel recordMidiLookAndFeel;
    StopMidiButtonLookAndFeel stopMidiLookAndFeel;
    StartButtonLookAndFeel  startButtonLookAndFeel;
    StopButtonLookAndFeel   stopButtonLookAndFeel;
    ImageKnobLookAndFeel adsrKnobLookAndFeel;
    juce::Component lfoLinkLine;



    //Function that allows to select a sample to play
    void pickAndLoadSample()
    {
        auto chooser = std::make_unique<juce::FileChooser>(
            "Select a sample to load ",
            juce::File{},                     // start directory
            "*.wav;*.aiff;*.flac;*.mp3");     // filter

        chooser->launchAsync(juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
            [this, fc = chooser.get()](const juce::FileChooser&)
            {
                auto fileToLoad = fc->getResult();
                if (fileToLoad.existsAsFile())
                {
                    DBG("→ Loading sample: " << fileToLoad.getFullPathName());
                    oscSender.send("/loadSample", fileToLoad.getFullPathName());

                    // Send sample duration [s]
                    double duration = 0.0;
                    if (auto* reader = formatManager.createReaderFor(fileToLoad))
                    {
                        duration = reader->lengthInSamples / reader->sampleRate;
                        oscSender.send("/sampleDuration", (float)duration);
                        DBG("Sample duration: " << duration << " seconds");
                        delete reader;
                    }

                    //Load a selected audio file into the thumbnail (for drawing the waveform)
                    thumbnail.setSource(new juce::FileInputSource(fileToLoad));
                    //Repaint the component to update the waveform after loading
                    repaint();
                    startButton.setEnabled(true); //Let the user play the sound

                    //Set the name of the sample
                    auto fullName = fileToLoad.getFileName();
                    auto displayName = truncateWithEllipsis(fullName, 14);
                    loadSampleButton.setButtonText(displayName);
                    loadSampleButton.setTooltip(fullName);
                    currentSampleFile = fileToLoad;
                    originalSampleFile = fileToLoad;
                    isReversed = false;
                }
            });
        // keep the chooser alive until the lambda ends
        chooser.release();
    }

    //Function that sends the new values to the supercollider granulator
    void sendOSC()
    {
        auto* editor = dynamic_cast<CMProjectAudioProcessorEditor*>(getParentComponent());
        float bpm = 120.0f;
        if (editor != nullptr)
        {
            auto text = editor->bpmLabel.getText();
            bpm = text.getFloatValue();
            oscSender.send("/bpm", bpm);
   
        }

    }
};

class ParameterIconButton : public juce::Button
{
public:
    ParameterIconButton(const juce::String& paramId, const juce::Image& img)
        : juce::Button(paramId), parameterId(paramId), icon(img) {}

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(isMouseOver ? juce::Colours::white.withAlpha(0.8f)
            : juce::Colours::white.withAlpha(0.6f));
        g.fillEllipse(r);
        g.drawImageWithin(icon, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }

    juce::String parameterId;

private:
    juce::Image icon;
    
    
};

// ==================================================
// Drum Page Component
// ==================================================

class CMProjectAudioProcessorEditor::DrumPageComponent : public juce::Component, public juce::Timer
{
public:
    juce::TextButton startCamera, stopCamera;
    juce::TextButton startDrumsButton, stopDrumsButton; //Start and Stop Drum Buttons
    juce::OwnedArray<HDImageButton> rowButtons;
    DrumPageComponent(CMProjectAudioProcessor& p) : processor(p)
    {
        startConfiguration(); //function that handles the first drumPage configuration
        addAndMakeVisibleFunction(); //Function that makes visible all the buttons
        searchTheKnobImage(); //Function that sets the knob images
        createTheLoadSamples(); //Function that handles the creation of the grid and the 4 different load sample
        onClickDrumPage(); //Function that handles the on click functions in the drumPage
    }

    void addAndMakeVisibleFunction() {
        addAndMakeVisible(startDrumsButton);
        addAndMakeVisible(stopDrumsButton);
        addAndMakeVisible(startCamera);
        addAndMakeVisible(stopCamera);
    }
    void startConfiguration() {
        startDrumsButton.setButtonText("Start Drums");
        stopDrumsButton.setButtonText("Stop Drums");
        startDrumsButton.setLookAndFeel(&startButtonLookAndFeel);
        stopDrumsButton.setLookAndFeel(&stopButtonLookAndFeel);
        startDrumsButton.setClickingTogglesState(false);
        stopDrumsButton.setClickingTogglesState(false);
        startCamera.setButtonText("Start Camera");
        stopCamera.setButtonText("Stop Camera");
        startCamera.setLookAndFeel(&startCameraLookAndFeel);
        stopCamera.setLookAndFeel(&stopCameraLookAndFeel);
        stopCamera.setEnabled(false);
        stopDrumsButton.setEnabled(false);
    }
    void searchTheKnobImage() {
        auto knobFile = SynthPageComponent::getIconFile("realknob.png");
        if (!knobFile.existsAsFile())
            DBG("volumeKnob.png not found at: " + knobFile.getFullPathName());
        else
        {
            auto img = juce::ImageFileFormat::loadFrom(knobFile);
            if (img.isNull())
                DBG("failed to load volumeKnob.png");
            else
                imageKnobLookAndFeel.setKnobImage(img);
        }
    }
    void timerCallback() override
    {
        if (!isPlaying)
            return;

        //Evaluate current step BEFORE advancing
        for (int track = 0; track < 4; ++track)
        {
            if (stepButtons[track]->getUnchecked(currentStep)->getToggleState())
                processor.triggerSamplePlayback(track);
        }
        //Then move to the next step
        currentStep = (currentStep + 1) % 16;
    }
    void createTheLoadSamples() {
        //Creating the 4 load samples textButtons
        for (int i = 0; i < 4; ++i)
        {

            // Add mute button
            auto* mute = new juce::TextButton("M");
            mute->setClickingTogglesState(true);
            mute->setLookAndFeel(&muteLookAndFeel);
            muteButtons.add(mute);
            addAndMakeVisible(mute);
            // Store previous volume value
            previousVolumes.add(1.0f);

            mute->onClick = [this, i, mute]()
                {
                    bool isMuted = mute->getToggleState();

                    if (isMuted)
                    {
                        previousVolumes.set(i, volumeSliders[i]->getValue());  // Save current knob
                        processor.trackVolumes[i] = 0.0f;
                    }
                    else
                    {
                        processor.trackVolumes[i] = previousVolumes[i];
                    }

                };

            lights.add(new juce::Component());
            addAndMakeVisible(lights.getLast());

            auto* button = new juce::TextButton("Load Sample");
            button->setLookAndFeel(&loadButtonLookAndFeel);
            loadSampleButtons.add(button);
            addAndMakeVisible(button);

            loadedSamples.add(juce::File());

            //Make it loaddable
            loadSampleButtons[i]->onClick = [this, i]() { openFileChooserForTrack(i); };

            //Volume knobs sliders
            auto* slider = new juce::Slider();
            slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider->setRange(0.0, 1.0, 0.01);
            slider->setValue(1.0);

            // *** apply your image knob look-and-feel ***
            slider->setLookAndFeel(&imageKnobLookAndFeel);

            volumeSliders.add(slider);
            addAndMakeVisible(slider);

            // Connect to processor
            slider->onValueChange = [this, slider, i]()
                {
                    float newVal = (float)slider->getValue(); //get the current value
                    previousVolumes.set(i, newVal); //remember the last value
                    if (!muteButtons[i]->getToggleState())
                        processor.trackVolumes[i] = newVal;
                };
        }

        //Create 4 × 4 grid of step buttons
        for (int track = 0; track < 4; ++track)
        {
            auto* row = new juce::OwnedArray<juce::TextButton>();
            for (int step = 0; step < 16; ++step)
            {
                auto* stepBtn = new juce::TextButton();
                stepBtn->setClickingTogglesState(true);
                stepBtn->setColour(juce::TextButton::buttonOnColourId, juce::Colours::limegreen.brighter(0.2f)); // toggled-on color

                stepBtn->setLookAndFeel(&roundedLookAndFeel); // <-- this line adds the custom corners

                row->add(stepBtn);
                addAndMakeVisible(stepBtn);
            }
            stepButtons.add(row);
        }

        for (int r = 0; r < 4; ++r)
        {
            auto file = SynthPageComponent::getIconFile("row" + juce::String(r + 1) + ".png");
            if (!file.existsAsFile()) { DBG("❌ row" << (r + 1) << ".png not found"); continue; }

            auto img = juce::ImageFileFormat::loadFrom(file);
            auto* btn = new HDImageButton("row" + juce::String(r + 1));
            btn->setImages(false, true, true,
                img, 1.0f, {},   // normal
                img, 0.7f, {},   // over
                img, 0.5f, {});  // down
            btn->setClickingTogglesState(true);
            addAndMakeVisible(btn);
            rowButtons.add(btn);
        }

    }
    void onClickDrumPage() {
        startDrumsButton.onClick = [this]()
            {
                if (!isPlaying)
                {
                    stopDrumsButton.setEnabled(true);
                    startDrumsButton.setEnabled(false);
                    currentStep = 0;
                    isPlaying = true;
                    timerCallback();
                    auto hz = int((bpm / 60.0f) * 4.0f);
                    startTimerHz(hz);

                    // 🔁 Set glow ON
                    startDrumsButton.setToggleState(true, juce::dontSendNotification);
                    stopDrumsButton.setToggleState(false, juce::dontSendNotification);
                }
            };

        stopDrumsButton.onClick = [this]()
            {
                if (isPlaying)
                {
                    startDrumsButton.setEnabled(true);
                    stopDrumsButton.setEnabled(false);
                    isPlaying = false;
                    stopTimer();

                    // 🔁 Set glow OFF
                    startDrumsButton.setToggleState(false, juce::dontSendNotification);
                    stopDrumsButton.setToggleState(false, juce::dontSendNotification);
                }
            };
    }

    void paint(juce::Graphics& g) override
    {
 
        for (int i = 0; i < lights.size(); ++i)
        {
            auto* light = lights[i];
            if (light != nullptr)
            {
                auto bounds = light->getBounds().toFloat();
                auto center = bounds.getCentre();
                float radius = bounds.getWidth() * 0.5f;

                bool isLoaded = (i < loadedSamples.size() && loadedSamples[i].existsAsFile());

                // Slightly smaller ellipse
                auto lightBounds = bounds.reduced(3.0f);

                // Light body with soft inner gradient
                juce::Colour baseColor = isLoaded
                    ? juce::Colours::limegreen.withBrightness(1.15f)
                    : juce::Colours::darkgrey.darker(0.7f);

                juce::ColourGradient ballGradient(
                    baseColor.brighter(0.2f), center.getX(), center.getY() - radius * 0.3f,
                    baseColor.darker(0.3f), center.getX(), center.getY() + radius * 0.3f,
                    false
                );

                g.setGradientFill(ballGradient);
                g.fillEllipse(lightBounds);

                // Subtle inner glow overlay (no external ring)
                if (isLoaded)
                {
                    g.setColour(juce::Colours::limegreen.withAlpha(0.15f));
                    g.fillEllipse(lightBounds); // very soft inner aura
                }

                // Reflection for glassiness
                auto reflection = lightBounds.reduced(lightBounds.getWidth() * 0.3f, lightBounds.getHeight() * 0.6f)
                    .withY(lightBounds.getY() + lightBounds.getHeight() * 0.15f);
                g.setColour(juce::Colours::white.withAlpha(0.15f));
                g.fillEllipse(reflection);
            }
        }

    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(20, 10); // 20px left/right, 10px top/bottom

        // Manually place top buttons
        const int topY = area.getY() - 10;

        int horizontalOffset = -17.2;  // change this to move the whole drum machine rows right or left
        int verticalOffset = -5.5;  // change this to move the whole drum machine rows up or down


        stopDrumsButton.setBounds(40-15, topY-1.3, 30, 30);
        startDrumsButton.setBounds(80-15, topY-1.3, 28, 30);
        startCamera.setBounds(120 - 16, topY - 1, 46.5f, 30);
        stopCamera.setBounds(180 - 16, topY - 1, 46.5f, 30);

        
        // Push layout area down
        area.removeFromTop(50);

        const int lightSize = 20;
        const int lightSpacing = 55;

        // Positioning references
        const int muteX = 42;
        const int lightsX = muteX + 55;
        const int btnX = lightsX + 60;
        const int btnW = 120, btnH = 30;
        const int sliderX = btnX + btnW + 30;
        const int sliderSize = 57;
        const int sliderYOffset = -19;
        const int muteYOffset = -3; // mute buttons vertical movement

        for (int i = 0; i < 4; ++i)
        {
            int rowY = area.getY() + i * (lightSize + lightSpacing);

            muteButtons[i]->setBounds(muteX + horizontalOffset, rowY + muteYOffset+ verticalOffset, 26, 26);

            lights[i]->setBounds(lightsX + horizontalOffset, rowY+ verticalOffset, lightSize, lightSize);

            loadSampleButtons[i]->setBounds(
                btnX+ horizontalOffset, rowY - 5 + verticalOffset,
                btnW, btnH
            );

            volumeSliders[i]->setBounds(
                sliderX + horizontalOffset,
                rowY + sliderYOffset + verticalOffset,
                sliderSize, sliderSize
            );
        }

        //Step sequencer buttons (aligned per row)
        const int stepWidth = 15; //width of the step
        const int stepHeight = 24.5; //height of the step
        const int stepSpacing = 7; //spacing between steps
        const int stepLeftPadding = 32;
        const int startX = volumeSliders[0]->getRight() + stepLeftPadding;

        for (int track = 0; track < 4; ++track)
        {
            int y = lights[track]->getY() + (lightSize / 2) - (stepHeight / 2);

            for (int step = 0; step < 16; ++step)
            {
                int x = startX + step * (stepWidth + stepSpacing);
                stepButtons[track]->getUnchecked(step)->setBounds(x, y, stepWidth, stepHeight);
            }
        }
        const int numberSize = 50;//number images size

              // compute the total width of our 16-step grid:
            const int gridWidth = (stepWidth + stepSpacing) * 16 - stepSpacing;
               // place the row icons just to the right of that:
            const int numberX = startX + gridWidth + 8;    // 8px padding past grid

        for (int r = 0; r < 4; ++r)
        {
            int y = lights[r]->getY() + (lightSize - numberSize) / 2;
            rowButtons[r]->setBounds(numberX+17.5, y-1.8, numberSize, numberSize);
        }
    }

    void syncBpm(float newBpm)
    {
        bpm = newBpm;  //store it
        if (isPlaying)
        {
            //for 16th-note grid at N BPM: callbacks per second = (bpm/60)*4
            auto hz = int((bpm / 60.0f) * 4.0f);
            startTimerHz(hz);
        }
    }
private:

   
    juce::OwnedArray<juce::Component> lights; //Lights of the drummachine
    juce::OwnedArray<juce::TextButton> loadSampleButtons; //The load buttons for the drumMachine
    juce::Array<juce::File> loadedSamples; //Load arrays for the drum machine
    juce::OwnedArray<juce::Slider> volumeSliders; //VolumeSliders for the drum machine
    juce::OwnedArray<juce::TextButton> muteButtons; //Mute BUttons for the drum Machine 
    juce::Array<float> previousVolumes;  // to remember last knob value
    RoundedStepLookAndFeel roundedLookAndFeel; //Rounded steps lookandfeel
    MuteButtonLookAndFeel muteLookAndFeel; //Mute button styled
    LoadButtonLookAndFeel loadButtonLookAndFeel; //Load Button styled
    ImageKnobLookAndFeel imageKnobLookAndFeel; //General knob styled
    StartButtonLookAndFeel startButtonLookAndFeel; //Start Button styled
    StopButtonLookAndFeel stopButtonLookAndFeel; //Stop Button Styled
    StartCameraButtonLookAndFeel startCameraLookAndFeel; //Start Camera Styled
    StopCameraButtonLookAndFeel stopCameraLookAndFeel; //Stop Camera styled

    //2D structure: stepButtons[track][step]
    juce::OwnedArray<juce::OwnedArray<juce::TextButton>> stepButtons;
    float bpm = 120.0f;
    int currentStep = 0;
    bool isPlaying = false;
    CMProjectAudioProcessor& processor;

    //Function that truncates the sample name
    static juce::String truncateWithEllipsis(const juce::String& s, int maxChars)
    {
        if (s.length() <= maxChars)
            return s;
        //take the first maxChars characters and add “…”
        return s.substring(0, maxChars) + "...";
    }

    void openFileChooserForTrack(int trackIndex)
    {
        auto* chooser = new juce::FileChooser(
            "Select a sample",
            juce::File{},
            "*.wav;*.aiff;*.flac;*.mp3"
        );

        chooser->launchAsync(juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
            [this, trackIndex, chooser](const juce::FileChooser& fc)
            {
                auto selectedFile = fc.getResult();
                if (selectedFile.existsAsFile())
                {
                    loadedSamples.set(trackIndex, selectedFile);
                    lights[trackIndex]->repaint();
                    processor.loadSampleForTrack(trackIndex, selectedFile);

                    //get the filename
                    auto fullName = selectedFile.getFileName();
                    //truncate to first 12 chars 
                    auto displayName = truncateWithEllipsis(fullName, 14);

                    //set the name on the button
                    loadSampleButtons[trackIndex]->setButtonText(displayName);

                    //keep the full name as a tooltip on hover
                    loadSampleButtons[trackIndex]->setTooltip(fullName);
                }
                delete chooser;
            });
    }

};

//==============================================================================
// Main GUI container: hosts and switches between synth and drum pages, common both for synth and drums
//==============================================================================

CMProjectAudioProcessorEditor::CMProjectAudioProcessorEditor(CMProjectAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), tooltipWindow(this, 300)
{
    startingConfigurationGlobal(); //Function that handles the starting configuration
    loadHandiImageFromPath(); //function that handles the upload of the hand image
    clearFingersStartAllSetUp(); //Function that handles ClearFingers and StartAll
    setToolTipFunction(); //Function that handles all the toolTip functions for both Synth and Drum page
    midiOnClickSetUpFunction(); //Function that handles all the oneclick setup functions
    pluginTitle(); //function that sets the plugin title
    setToolTipFunction(); //Function that handles all the toolTip functions for both Synth and Drum page
    addListenerToGLobal(); //function that sets all the addListeners
    globalBpmSetUp(); //Function that handles the global bpm setup
    fingersSetUp(); //Function that setUps the fingers
    setSize(950, 750); //Total size of the plugin
    startTimerHz(60); //starting camerapython timer
}

//When the plugin is closed, all the components are closed/deleted
CMProjectAudioProcessorEditor::~CMProjectAudioProcessorEditor()
{
    stopPythonHandTracker();
        
        // Remove listeners first
        synthPage->startCamera.removeListener(this);
        synthPage->stopCamera.removeListener(this);
        synthPage->startButton.removeListener(this);
        synthPage->stopButton.removeListener(this);
        drumPage->startCamera.removeListener(this);
        drumPage->stopCamera.removeListener(this);
        synthPage->resetButton.removeListener(this);
        switchButton.removeListener(this);
        indexButton.removeListener(this);
        middleButton.removeListener(this);
        ringButton.removeListener(this);
        pinkyButton.removeListener(this);
        lfoParamButton.removeListener(this);
        indexLeftButton.removeListener(this);
        middleLeftButton.removeListener(this);
        clearLookAndFeelRecursively (this);
        
        // Now safe to delete
        delete drumPage;
        delete synthPage;
}

void CMProjectAudioProcessorEditor::startingConfigurationGlobal() {
    synthPage = new SynthPageComponent();
    drumPage = new DrumPageComponent(audioProcessor);
    background = std::make_unique<GridBackgroundComponent>();
    addAndMakeVisible(background.get()); //comes before anything else

    addAndMakeVisible(synthPage);
    addAndMakeVisible(drumPage);
    drumPage->setVisible(false);
    addAndMakeVisible(switchButton);
    switchButton.setButtonText("Switch Page");
    switchButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromFloatRGBA(0.22f, 0.22f, 0.22f, 0.75f));
    switchButton.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey.withAlpha(0.8f));
}
void CMProjectAudioProcessorEditor::setToolTipFunction() {

    synthPage->grainPos.setTooltip("Grain Position\nUse this to shift the grain window around within the sample.");
    synthPage->grainDur.setTooltip("Grain Duration\nControls how long each grain plays before the next one starts.");
    synthPage->grainDensity.setTooltip("Grain Density\nMore density means more overlapping grains thicker sound.");
    synthPage->grainReverse.setTooltip("Grain Reverse\nToggle to play each grain backwards.");
    synthPage->grainPitch.setTooltip("Grain Pitch\nTransposes the pitch of each grain.");
    synthPage->grainCutOff.setTooltip("Filter Cut-off\nA low-pass cutoff on the granular output.");
    synthPage->lfoRate.setTooltip("LFO Rate");
    synthPage->startButton.setTooltip("Start Button");
    synthPage->stopButton.setTooltip("Stop Button");
    synthPage->startCamera.setTooltip("Start Camera");
    synthPage->recordMidiButton.setTooltip("Start Midi recording");
    synthPage->stopMidiButton.setTooltip("Stop Midi recording");
    synthPage->saveMidiButton.setTooltip("Save Midi recording");
    synthPage->loadSampleButton.setTooltip("Load your sample");
    synthPage->stopCamera.setTooltip("Stop Camera");
    synthPage->attackSlider.setTooltip("Attack\n Time to reach peak");
    synthPage->decaySlider.setTooltip("Decay\n Time to fall to sustain level");
    synthPage->sustainSlider.setTooltip("Sustain\n Level held until release");
    synthPage->releaseSlider.setTooltip("Release\n Time to fade out");
    drumPage->startCamera.setTooltip("Start Camera");
    drumPage->stopCamera.setTooltip("Stop Camera");

}
void CMProjectAudioProcessorEditor::pluginTitle() {
    //Static PLugin Title
    pageTitleLabel.setText("HAND GRANULATOR", juce::dontSendNotification);
    pageTitleLabel.setFont(juce::Font("Verdana", 30.0f, juce::Font::bold));
    pageTitleLabel.setJustificationType(juce::Justification::centred);
    pageTitleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey.withAlpha(0.9f));
    addAndMakeVisible(pageTitleLabel);

}
void CMProjectAudioProcessorEditor::fingersSetUp() {
    addAndMakeVisible(indexButton); //Index Finger
    indexButton.addListener(this);
    indexButton.setZoomFactor(2.5f);
    addAndMakeVisible(middleButton); //Middle Finger
    middleButton.addListener(this);
    middleButton.setZoomFactor(2.5f);
    addAndMakeVisible(ringButton); //Ring Finger
    ringButton.addListener(this);
    ringButton.setZoomFactor(2.5f);
    addAndMakeVisible(pinkyButton); //Pinky Finger
    pinkyButton.addListener(this);
    pinkyButton.setZoomFactor(2.5f);
    lfoParamButton.setSquareStyle(true); // this sets the visual style to square
    addAndMakeVisible(lfoParamButton); //Parameter modulated by LFO
    lfoParamButton.addListener(this);
    lfoParamButton.setZoomFactor(2.5f);
    lfoParamButton.setTooltip("Parameter Modulated by LFO");
    addAndMakeVisible(statusDisplay); //Status Display
    addAndMakeVisible(indexLeftButton); //IndexLeftFinger
    indexLeftButton.addListener(this);
    indexLeftButton.setZoomFactor(2.5f);
    indexLeftButton.setVisible(false);     
    addAndMakeVisible(middleLeftButton); //MIddleLeftFinger
    middleLeftButton.addListener(this);
    middleLeftButton.setZoomFactor(2.5f);
    middleLeftButton.setVisible(false);    
    addAndMakeVisible(indexRightButton); //IndexRightFinger
    indexRightButton.addListener(this);
    indexRightButton.setZoomFactor(2.5f);
    indexRightButton.setVisible(false);
    addAndMakeVisible(middleRightButton); //IndexRightFinger
    middleRightButton.addListener(this);
    middleRightButton.setZoomFactor(2.5f);
    middleRightButton.setVisible(false);


}
void CMProjectAudioProcessorEditor::clearFingersStartAllSetUp() {
    addAndMakeVisible(clearFingersButton);
    clearFingersButton.addListener(this);
    clearFingersButton.setLookAndFeel(&clearFingerButtonLookAndFeel);
    startAllButton.setLookAndFeel(&startAllButtonLookAndFeel);
    startAllButton.setClickingTogglesState(true);
    startAllButton.addListener(this);
    addAndMakeVisible(startAllButton);
}
void CMProjectAudioProcessorEditor::midiOnClickSetUpFunction() {
    //Midi on.click setup
    synthPage->recordMidiButton.onClick = [this]()
        {
            audioProcessor.startMidiRecording();
            synthPage->recordMidiButton.setEnabled(false);
            synthPage->stopMidiButton.setEnabled(true);
        };

    synthPage->stopMidiButton.onClick = [this]()
        {
            audioProcessor.stopMidiRecording();
            synthPage->stopMidiButton.setEnabled(false);
            synthPage->saveMidiButton.setEnabled(true);
        };

    synthPage->saveMidiButton.onClick = [this]()
        {
            auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
            auto file = desktop.getChildFile("melody.midi");

            if (audioProcessor.saveMidiRecording(file))
                DBG(" MIDI saved to " << file.getFullPathName());
            else
                DBG(" Failed to save MIDI");

            synthPage->saveMidiButton.setEnabled(false);
            synthPage->recordMidiButton.setEnabled(true);
        };
}
void CMProjectAudioProcessorEditor::loadHandiImageFromPath() {
    //Load hand image from path
    juce::File imageFile = getHandImageFile();
    if (imageFile.existsAsFile())
    {
        juce::Image handImage = juce::ImageFileFormat::loadFrom(imageFile);
        handOverlay.setImage(handImage.rescaled(handImage.getWidth() * 2,
            handImage.getHeight() * 2,
            juce::Graphics::highResamplingQuality),
            juce::RectanglePlacement::centred);
        addAndMakeVisible(handOverlay);
        handOverlay.setInterceptsMouseClicks(false, false);
    }
    else
    {
        DBG("handimage.png not found at the specified path.");
    }

}
void CMProjectAudioProcessorEditor::globalBpmSetUp() {
    //Setting values for the global bpm
    bpmLabel.setEditable(true, true, false);
    bpmLabel.setText("120", juce::dontSendNotification);
    bpmLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setWantsKeyboardFocus(true); // Ensure it can receive focus
    //bpmLabel.addMouseListener(this, true); // Make this component listen to mouse events

    //syncronyze bpm
    bpmLabel.onTextChange = [this]()
        {
            auto text = bpmLabel.getText();
            if (!text.containsOnly("0123456789.")) //allow only digits and decimal point
            {
                statusDisplay.showMessage("Invalid BPM!");
                bpmLabel.setText("120", juce::dontSendNotification); //reset to default or previous value
                return;
            }
            auto bpm = text.getFloatValue();

            if (bpm >= 1.0f && bpm <= 300.0f)
            {
                synthPage->syncBpm(bpm);
                drumPage->syncBpm(bpm);
                statusDisplay.showMessage("BPM updated!");
            }
            else {
                statusDisplay.showMessage("Invalid BPM!");
                bpmLabel.setText("120", juce::dontSendNotification); //reset to default or previous value
                return;
            }
        };

    addAndMakeVisible(bpmLabel);
}
void CMProjectAudioProcessorEditor::addListenerToGLobal() {
    switchButton.addListener(this);
    synthPage->startCamera.addListener(this);
    synthPage->stopCamera.addListener(this);
    synthPage->startButton.addListener(this);
    synthPage->stopButton.addListener(this);
    synthPage->resetButton.addListener(this);

    drumPage->startDrumsButton.addListener(this);
    drumPage->stopDrumsButton.addListener(this);
    drumPage->startCamera.addListener(this);
    drumPage->stopCamera.addListener(this);

    synthPage->grainPos.addListener(this);
    synthPage->grainDur.addListener(this);
    synthPage->grainDensity.addListener(this);
    synthPage->grainReverse.addListener(this);
    synthPage->grainPitch.addListener(this);
    synthPage->grainCutOff.addListener(this);
    synthPage->lfoRate.addListener(this);

    for (auto* btn : drumPage->rowButtons)
        btn->addListener(this);

}
void CMProjectAudioProcessorEditor::paint(juce::Graphics&) {}
void CMProjectAudioProcessorEditor::resized()
{
    auto forstartsbound = getLocalBounds().reduced(20, 10);
    const int topY = forstartsbound.getY() - 10;
    startAllButton.setBounds(180 + 45, topY + 39, 80, 30);
   

    if (background)
        background->setBounds(getLocalBounds());

    auto fullArea = getLocalBounds();

    auto areas = getLocalBounds(); //the whole plugin window
    const int btnW = 100; // same width
    const int btnH = 50; //same height
    const int margin = 10; //keep it off the very edge
    int x = areas.getRight() - btnW - margin; //right side
    int y = (areas.getHeight() - btnH) / 2; //vertical center
    switchButton.setBounds(x-9, y-260, 100, 30);

    auto bpmBar = fullArea.removeFromTop(40); //reduce vertical space
    bpmLabel.setBounds(getWidth() - 200, 25, 70, 24);

    //displace the label and the BPM write
    bpmTitleLabel.setBounds(getWidth() - 124, 24, 40, 26);
    bpmLabel.setBounds(getWidth() - 90, 43, 70, 27.5);
    synthPage->setBounds(fullArea);
    drumPage->setBounds(fullArea);

    handOverlay.setBounds(65, 380, 800, 350); //Hands dimension and displacement

    const auto imageX = 50;
    const auto imageY = 395;
    const auto circleDiameter = 20;

    //Values of the dot to perfectly sit on the index finger
    const int dotX = imageX + 560 - circleDiameter / 2;
    const int dotY = imageY + 15 - circleDiameter / 2;
    const int dotLeftX = imageX + 272 - circleDiameter / 2;

    indexButton.setBounds(dotX, dotY, circleDiameter, circleDiameter);
    indexRightButton.setBounds(dotX, dotY, circleDiameter, circleDiameter);
    indexLeftButton.setBounds(dotLeftX, dotY, circleDiameter, circleDiameter);
    //Values of the dot to perfectly sit on the middle finger
    const int midOffsetX = 511.8;
    const int midLeftOff = 317;
    const int midOffsetY = 3.9;
    const int midX = imageX + midOffsetX - circleDiameter / 2;
    const int midLx= imageX + midLeftOff - circleDiameter / 2;
    const int midY = imageY + midOffsetY - circleDiameter / 2;
    middleButton.setBounds(midX, midY, circleDiameter, circleDiameter);
    middleRightButton.setBounds(midX, midY, circleDiameter, circleDiameter);
    middleLeftButton.setBounds(midLx, midY, circleDiameter, circleDiameter);
    //Values of the dot to perfectly sit on the ring finger
    const int ringOffx = 468;
    const int ringOffy = 17;
    const int ringX = imageX + ringOffx - circleDiameter / 2;
    const int ringY = imageY + ringOffy - circleDiameter / 2;
    ringButton.setBounds(ringX, ringY, circleDiameter, circleDiameter);

    //Values of the dot to perfectly sit on the pinky finger
    const int pinkyOffx = 438;
    const int pinkyOffy = 62;
    const int pinkyX = imageX + pinkyOffx - circleDiameter / 2;
    const int pinkyY = imageY + pinkyOffy - circleDiameter / 2;
    pinkyButton.setBounds(pinkyX, pinkyY, circleDiameter, circleDiameter);
    
    const int lfoOffx = 139;
    const int lfoOffy = 221;
    const int lfoX = imageX + lfoOffx - circleDiameter / 2;
    const int lfoY = imageY + lfoOffy - circleDiameter / 2;
    lfoParamButton.setBounds(lfoX, lfoY, 40, 40);
    
    auto area = getLocalBounds();
    auto boxW = 180;
    auto boxH = 50;
    int statusX = 2;     // horizontal offset from the left
    int statusY = getHeight() - boxH - 2; // vertical offset from the bottom
    statusDisplay.setBounds(statusX, statusY, boxW, boxH);
    clearFingersButton.setBounds(statusX + 190, statusY + 10, 100, 30);
    //plugin title
    auto textWidth = pageTitleLabel.getFont().getStringWidth("HAND GRANULATOR");
    int padding = 20;
    int totalWidth = textWidth + padding;

    pageTitleLabel.setBounds(getWidth() / 2 - totalWidth / 2 + 5 , 5, totalWidth, 40);
  

}
void CMProjectAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& e,
    const juce::MouseWheelDetails& wheel)
{
    if (bpmLabel.getBounds().contains(e.getPosition()))
    {
        float bpm = bpmLabel.getText().getFloatValue();

        // use deltaY (positive = wheel up, negative = down)
        bpm += wheel.deltaY;

        bpm = juce::jlimit(1.0f, 999.0f, bpm);
        bpmLabel.setText(juce::String(bpm, 1), juce::dontSendNotification);

        synthPage->syncBpm(bpm);
        drumPage->syncBpm(bpm);
        statusDisplay.showMessage("BPM set to " + juce::String(bpm, 1));
    }
    else
    {
        Component::mouseWheelMove(e, wheel);
    }
}



void CMProjectAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    for (int r = 0; r < drumPage->rowButtons.size(); ++r)
    {
        if (button == drumPage->rowButtons[r])
        {
            setCurrentParameter(juce::String(r));
            currentParameterIcon = drumPage->rowButtons[r]->getNormalImage();
            statusDisplay.showMessage(currentParameter);
            return;
        }
    }
    // Handle page switching
    if (button == &switchButton)
    {
        showingSynth = !showingSynth;
        synthPage->setVisible(showingSynth);
        drumPage->setVisible(!showingSynth);
        
        //Set the correct page for Python
        currentPage = showingSynth ? "synth" : "drum";
        
        if(isPythonOn)
        {
            audioProcessor.senderToPython.send("/activePage", currentPage);
        }
        
        currentParameter.clear();
        
        //Hide glow images when Drum page is shown
        indexGlow.setVisible(showingSynth);
        middleGlow.setVisible(showingSynth);
        ringGlow.setVisible(showingSynth);
        pinkyGlow.setVisible(showingSynth);

        //hide the correct fingers for drumpage and for synthpage
        indexButton.setVisible(showingSynth);
        middleButton.setVisible(showingSynth);
        ringButton.setVisible(showingSynth);
        pinkyButton.setVisible(showingSynth);
        lfoParamButton.setVisible(showingSynth);
        indexLeftButton.setVisible(!showingSynth);
        middleLeftButton.setVisible(!showingSynth);
        indexRightButton.setVisible(!showingSynth);
        middleRightButton.setVisible(!showingSynth);
        audioProcessor.processingSender.send("/activePage", currentPage);
    }

    if (button == &synthPage->resetButton)
    {
        if (!isPythonOn)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }

        // Send a reset request to Python
        audioProcessor.senderToPython.send("/resetParameters");
        statusDisplay.showMessage("Resetting parameters!");
        return;
    }

    if (button == &startAllButton)
    {
        if (startAllButton.getToggleState())
        {
            
            //start synth & drums
            if (synthPage->startButton.isEnabled() && drumPage->startDrumsButton.isEnabled()) {
                synthPage->startButton.triggerClick();
                drumPage->startDrumsButton.triggerClick();
                synthPage->stopButton.setEnabled(false);
                drumPage->stopDrumsButton.setEnabled(false);
                statusDisplay.showMessage("Synth + Drums Started");
            }
            else
                statusDisplay.showMessage("Something already playing!");
        }
        else
        {
            //stop synth & drums
            if (synthPage->stopButton.isEnabled() && drumPage->stopDrumsButton.isEnabled()) {
                synthPage->stopButton.triggerClick();
                drumPage->stopDrumsButton.triggerClick();
                synthPage->startButton.setEnabled(true);
                drumPage->startDrumsButton.setEnabled(true);
                statusDisplay.showMessage("Synth + Drums Stopped");
            }
            else
                statusDisplay.showMessage("can't do this!");
        }
        return;
    }
    if (startAllButton.getToggleState())
         {
        if (button == &synthPage->stopButton
             || button == &drumPage->stopDrumsButton)
             {  
                return; //“all or nothing” mode
            }
        }
    //Handle camera controls
    else if (button == &synthPage->startCamera || button == &drumPage->startCamera)
    {
        launchPythonHandTracker();
        synthPage->startCamera.setEnabled(false);
        synthPage->stopCamera.setEnabled(true);
        drumPage->startCamera.setEnabled(false);
        drumPage->stopCamera.setEnabled(true);
        statusDisplay.showMessage("Camera Started");
        isPythonOn = true;
    }
    else if (button == &synthPage->stopCamera || button == &drumPage->stopCamera)
    {
        isPythonOn = false;
        stopPythonHandTracker();
        synthPage->startCamera.setEnabled(true);
        synthPage->stopCamera.setEnabled(false);
        drumPage->startCamera.setEnabled(true);
        drumPage->stopCamera.setEnabled(false);
        statusDisplay.showMessage("Camera Stopped");
    }
    //Handle start/stop sound
    else if (button == &synthPage->startButton)
    {
        synthPage->startButton.setEnabled(false);
        synthPage->stopButton.setEnabled(true);
    }
    else if (button == &synthPage->stopButton)
    {
        synthPage->startButton.setEnabled(true);
        synthPage->stopButton.setEnabled(false);
    }

    //Handle parameter selection
    if (button == &synthPage->grainPos)
    {
        setCurrentParameter("GrainPos");
        currentParameterIcon = synthPage->grainPos.getNormalImage();
        statusDisplay.showMessage("grainPosition selected");
    }
    else if (button == &synthPage->grainDur)
    {
        setCurrentParameter("GrainDur");
        currentParameterIcon = synthPage->grainDur.getNormalImage();
        statusDisplay.showMessage("grainDuration selected");
    }
    else if (button == &synthPage->grainDensity)
    {
        setCurrentParameter("GrainDensity");
        currentParameterIcon = synthPage->grainDensity.getNormalImage();
        statusDisplay.showMessage("grainDensity selected");
    }
    else if (button == &synthPage->grainPitch)
    {
        setCurrentParameter("GrainPitch");
        currentParameterIcon = synthPage->grainPitch.getNormalImage();
        statusDisplay.showMessage("GrainPitch selected");
    }
    else if (button == &synthPage->grainCutOff)
    {
        setCurrentParameter("GrainCutOff");
        currentParameterIcon = synthPage->grainCutOff.getNormalImage();
        statusDisplay.showMessage("CutOff selected");
    }
    else if (button == &synthPage->lfoRate)
    {
        setCurrentParameter("lfoRate");
        currentParameterIcon = synthPage->lfoRate.getNormalImage();
        statusDisplay.showMessage("lfoRate selected");
    }

    auto isAlreadyAssigned = [&](const juce::String& param) -> bool
        {
            for (int i = 0; i < 4; ++i)
                if (audioProcessor.fingerControls[i] == param)
                    return true;
            return false;
        };

    auto isDrumAlreadyAssigned = [&](const juce::String& param) -> bool
        {
            for (int i = 0; i < 4; ++i)
                if (audioProcessor.fingerDrumMapping[i] == param)
                    return true;
            return false;
        };
    //Ensure only one finger is assigned to each parameter
    auto ensureUniqueAssignment = [this](int fingerIndex)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (i != fingerIndex && audioProcessor.fingerControls[i] == currentParameter)
                    audioProcessor.fingerControls[i] = "";
            }
            audioProcessor.senderToPython.send("/activePage", currentPage);
            audioProcessor.fingerControls[fingerIndex] = currentParameter;
            audioProcessor.sendFingerAssignementsOSC();
        };

    auto ensureDrumUniqueAssignment = [this](int fingerIndex)
        {
            //  duplicati
            for (int i = 0; i < 4; ++i) {

                if (i != fingerIndex && audioProcessor.fingerDrumMapping[i] == currentParameter)
                    audioProcessor.fingerDrumMapping[i] = "";

            }
            //  salva l’indice di sample 
            juce::String( sampleIndex) = currentParameter;
            audioProcessor.senderToPython.send("/activePage", currentPage);
            audioProcessor.fingerDrumMapping[fingerIndex] = sampleIndex;

            // Python
            audioProcessor.sendFingerDrumMappingOSC();
        };

    //Handle fingers assignment
    if (button == &indexButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }
            ensureUniqueAssignment(0);
            indexButton.setIconImage(currentParameterIcon);
            indexButton.setTooltip(currentParameter);
            statusDisplay.showMessage("Index->" + currentParameter);
            assignGlowToFinger(currentParameter, indexGlow, { 591, 330 }, 20.0f, 73, 73); //+x to the righ, +y to go down
            audioProcessor.processingSender.send("/fingers_proc", 1);
    }

    if (button == &indexRightButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isDrumAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }

        ensureDrumUniqueAssignment(0);
        indexRightButton.setIconImage(currentParameterIcon);
        indexRightButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Index->" + currentParameter);
        assignGlowToFinger(currentParameter, indexGlow, { 591, 330 }, 20.0f, 73, 73); //+x to the righ, +y to go down
        audioProcessor.processingSender.send("/fingers_proc", 7);
    }

    else if (button == &middleButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }
        ensureUniqueAssignment(1);
        middleButton.setIconImage(currentParameterIcon);
        middleButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Middle->" + currentParameter);
        assignGlowToFinger(currentParameter, middleGlow, { 532, 316 }, 6.0f, 72, 72); //+x to the righ, +y to go down
        audioProcessor.processingSender.send("/fingers_proc", 2);
    }

    else if (button == &middleRightButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isDrumAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }
        ensureDrumUniqueAssignment(1);
        middleRightButton.setIconImage(currentParameterIcon);
        middleRightButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Middle->" + currentParameter);
        assignGlowToFinger(currentParameter, middleGlow, { 532, 316 }, 6.0f, 72, 72); //+x to the righ, +y to go down
        audioProcessor.processingSender.send("/fingers_proc", 8);
    }
    else if (button == &ringButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }

        ensureUniqueAssignment(2);
        ringButton.setIconImage(currentParameterIcon);
        ringButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Ring->" + currentParameter);
        assignGlowToFinger(currentParameter, ringGlow, { 474, 331 }, -10.0f, 72, 72); //+x to the righ, +y to go down
        audioProcessor.processingSender.send("/fingers_proc", 3);

    }
    else if (button == &pinkyButton)
    {
        if (isPythonOn == false)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isAlreadyAssigned(currentParameter))
        {
            statusDisplay.showMessage(currentParameter + " is already mapped!");
            return;
        }
        ensureUniqueAssignment(3);
        pinkyButton.setIconImage(currentParameterIcon);
        pinkyButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Pinky->" + currentParameter);
        assignGlowToFinger(currentParameter, pinkyGlow, { 437, 382 }, -25.0f, 68, 68); //+x to the righ, +y to go down
        audioProcessor.processingSender.send("/fingers_proc", 4);

    }
    else if (button == &indexLeftButton)
    {
        if (!isPythonOn) { statusDisplay.showMessage("Open Camera first"); return; }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isDrumAlreadyAssigned(currentParameter)) { statusDisplay.showMessage(currentParameter + " is already mapped!"); return; }

        ensureDrumUniqueAssignment(2);
        indexLeftButton.setIconImage(currentParameterIcon);
        indexLeftButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Left-Index" + currentParameter);
        audioProcessor.processingSender.send("/fingers_proc", 5);
    }
    else if (button == &middleLeftButton)
    {
        if (!isPythonOn) { statusDisplay.showMessage("Open Camera first"); return; }
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if (isDrumAlreadyAssigned(currentParameter)) { statusDisplay.showMessage(currentParameter + " is already mapped!"); return; }
        ensureDrumUniqueAssignment(3);
        middleLeftButton.setIconImage(currentParameterIcon);
        middleLeftButton.setTooltip(currentParameter);
        statusDisplay.showMessage("Left-Middle " + currentParameter);
        audioProcessor.processingSender.send("/fingers_proc", 6);
    }
    else if(button == &lfoParamButton)
    {
        
        if (currentParameter.isEmpty())
            {
                statusDisplay.showMessage("Select a parameter");
                return;
            }
        
        if(currentParameter == "lfoRate")
        {
            statusDisplay.showMessage("Can't assign LFO Rate");
            return;
        }
        lfoParamButton.setIconImage(currentParameterIcon);
        lfoParamButton.setTooltip(currentParameter);
        statusDisplay.showMessage("LFO->" + currentParameter);
        synthPage->isLfoActive = true;
        synthPage->repaint();

        
        if (! audioProcessor.sendLfoTargetOSC (currentParameter))
                DBG ("Could not send /lfoTarget");
    }
    else if (button == &clearFingersButton)
    {
        if (isPythonOn == false) //Avoid to send messages if camera is off
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }

        //clear each CircleButton visually
        auto clearCircle = [](CircleButton& btn)
            {
                btn.setIconImage(juce::Image());  //remove icon
                btn.repaint(); //repaint
                btn.setTooltip({});//clear tooltip
            };

        if (showingSynth)
        {
            //clear in the audioProcessor state
            for (int i = 0; i < 4; ++i)
                audioProcessor.fingerControls[i].clear();
            audioProcessor.sendFingerAssignementsOSC();  //push empty assignments

            clearCircle(indexButton);
            clearCircle(middleButton);
            clearCircle(ringButton);
            clearCircle(pinkyButton);
            clearCircle(lfoParamButton);
            audioProcessor.processingSender.send("/clearSynth");
            //clear also the glow effect
            auto clearGlow = [this](juce::ImageComponent& glow)
                {
                    glow.setVisible(false);
                    glow.setImage(juce::Image(), juce::RectanglePlacement::centred);

                };

            clearGlow(indexGlow);
            clearGlow(middleGlow);
            clearGlow(ringGlow);
            clearGlow(pinkyGlow);
            synthPage->isLfoActive = false;
            synthPage->repaint();


        }
        else  //drum page
        {
            for (int i = 0; i < 4; ++i)
                audioProcessor.fingerDrumMapping[i].clear(); //becomes ""
            audioProcessor.sendFingerDrumMappingOSC();  //sends fingers updated
            audioProcessor.processingSender.send("/clearDrum");
            clearCircle(indexRightButton);
            clearCircle(middleRightButton);
            clearCircle(indexLeftButton);
            clearCircle(middleLeftButton);
        }
        statusDisplay.showMessage("Finger mappings cleared");
    }

}


//==============================================================================
//PYTHON PROCESS//
//==============================================================================

//This function starts the python process
void CMProjectAudioProcessorEditor::launchPythonHandTracker()
{
    if (cameraRunning)
        return;

    const juce::File script = getHandTrackerScript(); //Find the python script 

    if (! script.existsAsFile())
    {
        return;
    }

#if JUCE_WINDOWS
    //Point directly at the python.exe in your conda env
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::String pythonExe = home.getChildFile("anaconda3")
                                .getChildFile("envs")
                                .getChildFile("handtracker-env")
                                .getChildFile("python.exe")
                                .getFullPathName();
    if (!juce::File(pythonExe).existsAsFile())
    {
        return;
    }
    if (pythonProcess.isRunning())
        return;

    juce::StringArray cmd{ pythonExe, script.getFullPathName() };
 
    if (!pythonProcess.start(cmd))
    {
        DBG("❌ Failed to launch Python tracker");
        
    }
    else
    {
        cameraRunning = true;
        juce::Thread::sleep(500); //wait 0.5 seconds
        
        if (!audioProcessor.senderToPython.connect("127.0.0.1", 9002))
        {
            DBG("❌ Could not connect to Python OSC server on port 9002");
        }
        else
        {
            isPythonOn = true; //Change the boolean 
            audioProcessor.senderToPython.send("/activePage", currentPage); //inform the python of the current position in the plugin
        }       
    }

#else
    // macOS: invoke a login zsh so that `conda activate` is available
    juce::String quotedScriptPath = script.getFullPathName().quoted();
    juce::String zshCommand =
        "conda activate handtracker-env && "
        "python3 " + quotedScriptPath;

    juce::StringArray cmd { "/bin/zsh", "-ic", zshCommand };
    
    if (pythonProcess.isRunning())
        return;
        
        DBG("Launching: " << cmd.joinIntoString(" "));
        if (! pythonProcess.start(cmd))
        {
            DBG(" couldn’t launch Python hand-tracker");
            return;
        }
        else
        {
            DBG("Python process started");
            cameraRunning = true;
            isPythonOn = true;
            juce::Thread::sleep(500); // wait 0.5 seconds
            
            if (!audioProcessor.senderToPython.connect("127.0.0.1", 9002))
            {
                DBG("❌ Could not connect to Python OSC server on port 9002");
            }
            else
            {
                DBG("✅ Connected to Python OSC server on port 9002");
                audioProcessor.senderToPython.send("/activePage", currentPage);

            }
           
            
        }
#endif
    
}

//This function stops the python process
void CMProjectAudioProcessorEditor::stopPythonHandTracker()
{
    if (! cameraRunning)
        return;

    if (pythonProcess.isRunning())
    {
        pythonProcess.kill();
        pythonProcess.waitForProcessToFinish(2000);
    }
    cameraRunning = false;
}

//This function controls if the camera has been closed from the X
void CMProjectAudioProcessorEditor::timerCallback()
{

    auto& p = audioProcessor;
    float dur = p.getGrainDur();
    float pos = p.getGrainPos();
    float cut = p.getCutoff();
    float den = p.getDensity();
    float pit = p.getPitch();
    float rev = p.getReverse();

    if (synthPage)
    {
        synthPage->currentGrainPos = audioProcessor.getGrainPos();
    }

    synthPage->repaint(); //force waveform + bar to redraw

    //Closed the camera from an external X
    if (cameraRunning && !pythonProcess.isRunning())
    {
        cameraRunning = false;
        synthPage->startCamera.setEnabled(true);
        synthPage->stopCamera.setEnabled(false);
        drumPage->startCamera.setEnabled(true);
        drumPage->stopCamera.setEnabled(false);
        isPythonOn = false;
    }
}
