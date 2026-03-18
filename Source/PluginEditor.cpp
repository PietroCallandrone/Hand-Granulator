#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

#if JUCE_MAC
 #include <dlfcn.h>
#endif

bool isPythonOn = false;//Boolean used to handle assertions

//Maps each parameter name to its corresponding projection image filename for visual feedback
static const std::map<juce::String, juce::String> parameterToGlowImage = {
    { "GrainPos", "glow_grainPos.png" },
    { "GrainDur", "glow_grainDur.png" },
    { "GrainDensity", "glow_grainDensity.png" },
    { "GrainReverse", "glow_grainReverse.png" },
    { "GrainPitch", "glow_grainPitch.png" },
    { "GrainCutOff", "glow_grainCutOff.png" }
};

static juce::File getMacModuleBundleBuildDirectory()
{
#if JUCE_MAC
    Dl_info info {};

    if (dladdr((const void*) &getMacModuleBundleBuildDirectory, &info) != 0
        && info.dli_fname != nullptr)
    {
        juce::File moduleFile { juce::String::fromUTF8(info.dli_fname) };

        if (moduleFile.existsAsFile())
            return moduleFile.getParentDirectory()   // MacOS
                             .getParentDirectory()   // Contents
                             .getParentDirectory();  // Bundle root
    }
#endif

    return {};
}

static juce::File findProjectRoot()
{
    auto dir = getMacModuleBundleBuildDirectory();

    if (! dir.isDirectory())
        dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                  .getParentDirectory();

    auto fallback = juce::File::getCurrentWorkingDirectory();

    while (dir.exists())
    {
        if (dir.getChildFile("CMProject.jucer").existsAsFile())
            return dir;

        dir = dir.getParentDirectory();
    }

    while (fallback.exists())
    {
        if (fallback.getChildFile("CMProject.jucer").existsAsFile())
            return fallback;

        fallback = fallback.getParentDirectory();
    }

    return {};
}

static juce::File getHandTrackerPythonExecutable()
{
    auto overridePath = juce::SystemStats::getEnvironmentVariable("HANDTRACKER_PYTHON", {});

    if (overridePath.isNotEmpty())
    {
        juce::File overridePython { overridePath };

        if (overridePython.existsAsFile())
            return overridePython;
    }

    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::StringArray candidates;

#if JUCE_WINDOWS
    candidates.add(home.getChildFile("miniconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("python.exe").getFullPathName());
    candidates.add(home.getChildFile("Miniconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("python.exe").getFullPathName());
    candidates.add(home.getChildFile("anaconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("python.exe").getFullPathName());
    candidates.add(home.getChildFile("Anaconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("python.exe").getFullPathName());
#else
    candidates.add(home.getChildFile("opt").getChildFile("miniconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("bin").getChildFile("python3").getFullPathName());
    candidates.add(home.getChildFile("opt").getChildFile("miniconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("bin").getChildFile("python").getFullPathName());
    candidates.add(home.getChildFile("anaconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("bin").getChildFile("python3").getFullPathName());
    candidates.add(home.getChildFile("anaconda3").getChildFile("envs").getChildFile("handtracker-env").getChildFile("bin").getChildFile("python").getFullPathName());
#endif

    for (const auto& candidate : candidates)
    {
        juce::File pythonExe { candidate };

        if (pythonExe.existsAsFile())
            return pythonExe;
    }

    return {};
}

//Resolves and returns the full path to a projection image file located in the "Assets" folder of the project directory
static juce::File getGlowFile(const juce::String& fileName)
{
    auto projectRoot = findProjectRoot();
    return projectRoot.getChildFile("Assets").getChildFile(fileName);
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
    const auto scale = (float) getWidth() / 800.0f;
    glowTarget.setBounds(juce::Rectangle<int>(
        juce::roundToInt(position.getX() * scale),
        juce::roundToInt(position.getY() * scale),
        juce::roundToInt(targetWidth * scale),
        juce::roundToInt(targetHeight * scale)));
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
    auto projectRoot = findProjectRoot();
    return projectRoot.getChildFile("python")
        .getChildFile("HandTracker")
        .getChildFile("main.py");
}

//For the hand neon-green image
static juce::File getHandImageFile()
{
    auto projectRoot = findProjectRoot();
    return projectRoot.getChildFile("Assets").getChildFile("handimage.png"); //Append the image of hands
}

class CMProjectAudioProcessorEditor::HandVisualizerComponent : public juce::Component
{
public:
    explicit HandVisualizerComponent(CMProjectAudioProcessor& processorToUse)
        : processor(processorToUse)
    {
        setInterceptsMouseClicks(false, false);
    }

    void tick(double newTimeSeconds)
    {
        timeSeconds = newTimeSeconds;
        modulationEnergy = juce::jlimit(0.0f, 1.0f,
            (processor.getDensity() / 5.0f) * 0.35f
            + (std::abs(processor.getPitch()) / 12.0f) * 0.2f
            + (processor.getReverse() * 0.2f)
            + (processor.getGrainDur() / 0.5f) * 0.25f);

        reverseAmount = juce::jlimit(0.0f, 1.0f, processor.getReverse());

        const auto snapshot = processor.getTrackedHands();

        for (size_t handIndex = 0; handIndex < hands.size(); ++handIndex)
        {
            auto& displayState = hands[handIndex];
            const auto& trackedHand = snapshot[handIndex];

            if (trackedHand.visible)
            {
                if (! displayState.seeded)
                {
                    displayState.smoothed = trackedHand.landmarks;
                    displayState.seeded = true;
                }
                else
                {
                    for (size_t pointIndex = 0; pointIndex < displayState.smoothed.size(); ++pointIndex)
                    {
                        auto current = displayState.smoothed[pointIndex];
                        const auto target = trackedHand.landmarks[pointIndex];
                        displayState.smoothed[pointIndex] = {
                            juce::jmap(0.28f, current.x, target.x),
                            juce::jmap(0.28f, current.y, target.y)
                        };
                    }
                }
            }

            displayState.visible = trackedHand.visible;
            displayState.visibility = juce::jlimit(0.0f, 1.0f,
                displayState.visibility + (trackedHand.visible ? 0.12f : -0.08f));

            if (! trackedHand.visible && displayState.visibility <= 0.01f)
                displayState.seeded = false;
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        juce::ColourGradient background(
            juce::Colour::fromRGB(8, 10, 12),
            bounds.getCentreX(), bounds.getY(),
            juce::Colour::fromRGB(2, 5, 7),
            bounds.getCentreX(), bounds.getBottom(),
            false);

        g.setGradientFill(background);
        g.fillRoundedRectangle(bounds, 28.0f);

        const auto scene = getSceneBounds();

        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(scene, 24.0f, 1.0f);

        bool hasVisibleHands = false;

        for (size_t handIndex = 0; handIndex < hands.size(); ++handIndex)
        {
            if (hands[handIndex].visibility <= 0.01f)
                continue;

            hasVisibleHands = true;
            drawHand(g, scene, (int) handIndex, hands[handIndex]);
        }

        if (! hasVisibleHands)
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.setFont(juce::Font { juce::FontOptions(16.0f) });
            g.drawFittedText("Start camera to render live hand visuals inside the plugin",
                scene.reduced(24.0f).toNearestInt(),
                juce::Justification::centredBottom,
                2);
        }
    }

private:
    struct VisualHand
    {
        bool visible = false;
        bool seeded = false;
        float visibility = 0.0f;
        std::array<juce::Point<float>, 21> smoothed {};
    };

    juce::Rectangle<float> getSceneBounds() const
    {
        auto available = getLocalBounds().toFloat().reduced(18.0f, 10.0f);
        constexpr float aspect = 16.0f / 9.0f;

        auto width = available.getWidth();
        auto height = width / aspect;

        if (height > available.getHeight())
        {
            height = available.getHeight();
            width = height * aspect;
        }

        return {
            available.getCentreX() - width * 0.5f,
            available.getCentreY() - height * 0.5f,
            width,
            height
        };
    }

    juce::Point<float> mapPoint(const juce::Rectangle<float>& scene, juce::Point<float> point) const
    {
        return {
            scene.getX() + point.x * scene.getWidth(),
            scene.getY() + point.y * scene.getHeight()
        };
    }

    juce::Colour getHandColour(int handIndex, float alpha) const
    {
        const auto tint = juce::jlimit(0.0f, 1.0f, modulationEnergy * 0.35f + reverseAmount * 0.15f);
        const auto base = handIndex == 0
            ? juce::Colour::fromRGB(24, 132, 46)
            : juce::Colour::fromRGB(18, 120, 40);

        return base.interpolatedWith(juce::Colour::fromRGB(72, 230, 88), 0.08f + tint * 0.12f)
                   .withAlpha(alpha);
    }

    juce::Colour getParameterColour(const juce::String& parameter) const
    {
        if (parameter == "GrainPos")     return juce::Colour::fromRGB(84, 240, 255);
        if (parameter == "GrainDur")     return juce::Colour::fromRGB(255, 192, 92);
        if (parameter == "GrainDensity") return juce::Colour::fromRGB(108, 255, 140);
        if (parameter == "GrainPitch")   return juce::Colour::fromRGB(255, 122, 214);
        if (parameter == "GrainCutOff")  return juce::Colour::fromRGB(120, 168, 255);
        if (parameter == "GrainReverse") return juce::Colour::fromRGB(244, 250, 255);
        if (parameter == "lfoRate")      return juce::Colour::fromRGB(196, 134, 255);
        return juce::Colours::white;
    }

    void drawHand(juce::Graphics& g,
                  const juce::Rectangle<float>& scene,
                  int handIndex,
                  const VisualHand& hand) const
    {
        auto mapped = mapHand(scene, hand);
        shortenFinger(mapped, std::array<int, 4>{ 1, 2, 3, 4 }, 0.900f);
        shortenFinger(mapped, std::array<int, 4>{ 5, 6, 7, 8 }, 0.900f);
        shortenFinger(mapped, std::array<int, 4>{ 9, 10, 11, 12 }, 0.905f);
        shortenFinger(mapped, std::array<int, 4>{ 13, 14, 15, 16 }, 0.900f);
        shortenFinger(mapped, std::array<int, 4>{ 17, 18, 19, 20 }, 0.885f);
        applyPinchClosure(mapped);

        const auto baseColour = getHandColour(handIndex, 0.98f * hand.visibility);
        const auto shadowColour = juce::Colour::fromRGB(0, 0, 0).withAlpha(0.44f * hand.visibility);
        const auto outlineColour = juce::Colour::fromRGB(108, 255, 72).withAlpha(0.95f * hand.visibility);
        const auto highlightColour = juce::Colour::fromRGB(220, 255, 176).withAlpha(0.30f * hand.visibility);

        drawPalmVolume(g, mapped, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);

        drawFingerVolume(g, mapped, std::array<int, 4>{ 1, 2, 3, 4 }, handIndex == 0, 20.5f, 16.8f, 13.0f, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);
        drawFingerVolume(g, mapped, std::array<int, 4>{ 5, 6, 7, 8 }, false,         22.2f, 18.4f, 13.8f, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);
        drawFingerVolume(g, mapped, std::array<int, 4>{ 9, 10, 11, 12 }, false,      24.0f, 19.4f, 14.6f, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);
        drawFingerVolume(g, mapped, std::array<int, 4>{ 13, 14, 15, 16 }, false,     22.0f, 17.6f, 13.2f, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);
        drawFingerVolume(g, mapped, std::array<int, 4>{ 17, 18, 19, 20 }, false,     18.4f, 14.8f, 11.0f, baseColour, shadowColour, outlineColour, highlightColour, hand.visibility);

        drawTextureLines(g, mapped, outlineColour, highlightColour, hand.visibility);

        if (handIndex == 1)
            drawAssignedParameterLabels(g, scene, hand);
    }

    std::array<juce::Point<float>, 21> mapHand(const juce::Rectangle<float>& scene,
                                               const VisualHand& hand) const
    {
        std::array<juce::Point<float>, 21> mapped {};

        for (size_t i = 0; i < mapped.size(); ++i)
            mapped[i] = mapPoint(scene, hand.smoothed[i]);

        return mapped;
    }

    juce::Point<float> pointAlong(juce::Point<float> a, juce::Point<float> b, float amount) const
    {
        return { juce::jmap(amount, a.x, b.x), juce::jmap(amount, a.y, b.y) };
    }

    void shortenFinger(std::array<juce::Point<float>, 21>& points,
                       std::array<int, 4> indices,
                       float scale) const
    {
        const auto base = points[(size_t) indices[0]];

        for (size_t i = 1; i < indices.size(); ++i)
            points[(size_t) indices[i]] = pointAlong(base, points[(size_t) indices[i]], scale);
    }

    void applyPinchClosure(std::array<juce::Point<float>, 21>& points) const
    {
        const std::array<int, 4> otherTips { 8, 12, 16, 20 };
        const std::array<int, 4> otherNearTips { 7, 11, 15, 19 };
        const std::array<int, 4> otherBaseJoints { 6, 10, 14, 18 };
        constexpr float threshold = 54.0f;

        for (size_t i = 0; i < otherTips.size(); ++i)
        {
            const auto thumbTip = points[4];
            const auto otherTip = points[(size_t) otherTips[i]];
            const auto distance = thumbTip.getDistanceFrom(otherTip);

            if (distance >= threshold)
                continue;

            const auto t = juce::jlimit(0.0f, 1.0f, 1.0f - (distance / threshold));
            const bool isPinky = i == 3;

            const auto pinchMid = isPinky
                ? pointAlong(otherTip, thumbTip, 0.78f)
                : pointAlong(thumbTip, otherTip, 0.5f);
            const auto thumbTarget = isPinky
                ? pointAlong(thumbTip, otherTip, 0.28f)
                : pinchMid;
            const auto otherTarget = isPinky
                ? pointAlong(otherTip, thumbTip, 0.82f)
                : pinchMid;

            const float thumbTipPull = isPinky ? 0.20f : 0.34f;
            const float otherTipPull = isPinky ? 0.58f : 0.34f;
            const float thumbJointPull = isPinky ? 0.05f : 0.12f;
            const float otherNearPull = isPinky ? 0.02f : 0.10f;
            const float otherBasePull = 0.0f;

            points[4] = pointAlong(thumbTip, thumbTarget, thumbTipPull * t);
            points[(size_t) otherTips[i]] = pointAlong(otherTip, otherTarget, otherTipPull * t);

            points[3] = pointAlong(points[3], pinchMid, thumbJointPull * t);
            points[(size_t) otherNearTips[i]] = pointAlong(points[(size_t) otherNearTips[i]], pinchMid, otherNearPull * t);
            points[(size_t) otherBaseJoints[i]] = pointAlong(points[(size_t) otherBaseJoints[i]], pinchMid, otherBasePull * t);
        }
    }

    juce::Point<float> perpendicular(juce::Point<float> from, juce::Point<float> to) const
    {
        auto delta = to - from;
        const auto length = juce::jmax(1.0f, std::sqrt(delta.x * delta.x + delta.y * delta.y));
        delta /= length;
        return { -delta.y, delta.x };
    }

    juce::Path makeSmoothPath(const std::vector<juce::Point<float>>& points, bool closed) const
    {
        juce::Path path;

        if (points.empty())
            return path;

        path.startNewSubPath(points.front());

        if (points.size() == 1)
            return path;

        for (size_t i = 1; i < points.size() - 1; ++i)
        {
            const auto mid = pointAlong(points[i], points[i + 1], 0.5f);
            path.quadraticTo(points[i], mid);
        }

        path.lineTo(points.back());

        if (closed)
            path.closeSubPath();

        return path;
    }

    juce::Path createPalmFillPath(const std::array<juce::Point<float>, 21>& points) const
    {
        const auto wristLeft = points[0] + perpendicular(points[0], points[17]) * 20.0f;
        const auto wristRight = points[0] - perpendicular(points[0], points[5]) * 20.0f;
        const auto wristBottom = pointAlong(wristLeft, wristRight, 0.5f) + juce::Point<float>(0.0f, 1.5f);

        return makeSmoothPath({
            wristLeft,
            points[17], pointAlong(points[17], points[13], 0.42f), points[13], points[9], points[5],
            wristRight,
            wristBottom
        }, true);
    }

    juce::Path createHandOutlinePath(const std::array<juce::Point<float>, 21>& points) const
    {
        const auto wristLeft = points[0] + perpendicular(points[0], points[17]) * 22.0f;
        const auto wristRight = points[0] - perpendicular(points[0], points[5]) * 22.0f;
        const auto wristBottom = pointAlong(wristLeft, wristRight, 0.5f) + juce::Point<float>(0.0f, 1.0f);

        return makeSmoothPath({
            wristLeft,
            points[17], pointAlong(points[17], points[18], 0.40f), points[20],
            pointAlong(points[19], points[18], 0.45f), points[17],
            pointAlong(points[13], points[14], 0.35f), points[16],
            pointAlong(points[15], points[14], 0.45f), points[13],
            pointAlong(points[9], points[10], 0.35f), points[12],
            pointAlong(points[11], points[10], 0.45f), points[9],
            pointAlong(points[5], points[6], 0.35f), points[8],
            pointAlong(points[7], points[6], 0.45f), points[5],
            pointAlong(points[2], points[3], 0.35f), points[4],
            pointAlong(points[3], points[2], 0.45f), points[1],
            wristRight,
            wristBottom
        }, true);
    }

    void fillSoftStroke(juce::Graphics& g,
                        juce::Point<float> start,
                        juce::Point<float> end,
                        float width,
                        juce::Colour fill,
                        juce::Colour shadow,
                        juce::Colour outline,
                        juce::Colour highlight) const
    {
        g.setColour(shadow);
        g.drawLine({ start.translated(width * 0.06f, width * 0.10f), end.translated(width * 0.06f, width * 0.10f) }, width + 2.2f);

        g.setColour(fill);
        g.drawLine({ start, end }, width);

        g.setColour(outline);
        g.drawLine({ start, end }, juce::jmax(1.0f, width * 0.10f));

        const auto normal = perpendicular(start, end);
        g.setColour(highlight);
        g.drawLine({ start - normal * (width * 0.14f), end - normal * (width * 0.14f) }, juce::jmax(1.0f, width * 0.16f));
    }

    juce::Path createFingerShape(const std::array<juce::Point<float>, 21>& points,
                                 std::array<int, 4> indices,
                                 float width0,
                                 float width1,
                                 float width2) const
    {
        const std::array<float, 4> radii {
            width0 * 0.50f,
            width1 * 0.47f,
            width2 * 0.43f,
            width2 * 0.30f
        };

        std::vector<juce::Point<float>> leftSide;
        std::vector<juce::Point<float>> rightSide;

        for (size_t i = 0; i < indices.size(); ++i)
        {
            const auto current = points[(size_t) indices[i]];
            const auto prevIndex = (i == 0) ? indices[0] : indices[i - 1];
            const auto nextIndex = (i == indices.size() - 1) ? indices[i] : indices[i + 1];
            const auto prev = points[(size_t) prevIndex];
            const auto next = points[(size_t) nextIndex];
            const auto normal = perpendicular(prev, next);

            leftSide.push_back(current + normal * radii[i]);
            rightSide.push_back(current - normal * radii[i]);
        }

        std::reverse(rightSide.begin(), rightSide.end());
        leftSide.insert(leftSide.end(), rightSide.begin(), rightSide.end());

        return makeSmoothPath(leftSide, true);
    }

    void fillFingerShape(juce::Graphics& g,
                         const juce::Path& shape,
                         juce::Colour fill,
                         juce::Colour shadow,
                         juce::Colour outline,
                         juce::Colour highlight) const
    {
        const auto bounds = shape.getBounds();

        juce::ColourGradient fingerGradient(
            highlight.interpolatedWith(fill, 0.42f),
            bounds.getX() + bounds.getWidth() * 0.20f, bounds.getY() + bounds.getHeight() * 0.08f,
            shadow.interpolatedWith(fill, 0.14f),
            bounds.getRight(), bounds.getBottom(),
            false);

        g.setColour(shadow.withAlpha(shadow.getFloatAlpha() * 0.42f));
        g.fillPath(shape, juce::AffineTransform::translation(bounds.getWidth() * 0.025f, bounds.getHeight() * 0.035f));

        g.setGradientFill(fingerGradient);
        g.fillPath(shape);

        g.setColour(outline.withAlpha(0.78f));
        g.strokePath(shape, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawPalmVolume(juce::Graphics& g,
                        const std::array<juce::Point<float>, 21>& points,
                        juce::Colour fill,
                        juce::Colour shadow,
                        juce::Colour outline,
                        juce::Colour highlight,
                        float visibility) const
    {
        juce::ignoreUnused(visibility);

        const auto palmPath = createPalmFillPath(points);
        const auto outlinePath = createHandOutlinePath(points);
        const auto bounds = palmPath.getBounds();

        juce::ColourGradient palmGradient(
            highlight.interpolatedWith(fill, 0.38f),
            bounds.getX() + bounds.getWidth() * 0.18f, bounds.getY() + bounds.getHeight() * 0.08f,
            shadow.interpolatedWith(fill, 0.12f),
            bounds.getRight(), bounds.getBottom(),
            false);

        g.setGradientFill(palmGradient);
        g.fillPath(palmPath);

        g.setColour(outline.withAlpha(0.82f));
        g.strokePath(outlinePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(highlight.withAlpha(0.16f));
        g.strokePath(makeSmoothPath({
            pointAlong(points[5], points[9], 0.35f),
            pointAlong(points[5], points[9], 0.85f),
            pointAlong(points[9], points[13], 0.55f)
        }, false), juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawFingerVolume(juce::Graphics& g,
                          const std::array<juce::Point<float>, 21>& points,
                          std::array<int, 4> indices,
                          bool thumb,
                          float width0,
                          float width1,
                          float width2,
                          juce::Colour fill,
                          juce::Colour shadow,
                          juce::Colour outline,
                          juce::Colour highlight,
                          float visibility) const
    {
        juce::ignoreUnused(visibility);
        const auto segmentFill = thumb
            ? fill.interpolatedWith(juce::Colour::fromRGB(138, 245, 168), 0.07f)
            : fill.interpolatedWith(juce::Colour::fromRGB(148, 255, 176), 0.10f);

        const auto fingerShape = createFingerShape(points, indices, width0, width1, width2);
        fillFingerShape(g, fingerShape, segmentFill, shadow, outline, highlight);

        const std::array<float, 3> creaseWidths {
            juce::jmax(1.0f, width0 * 0.07f),
            juce::jmax(1.0f, width1 * 0.07f),
            juce::jmax(1.0f, width2 * 0.07f)
        };

        for (size_t segment = 0; segment < 3; ++segment)
        {
            const auto start = points[(size_t) indices[segment]];
            const auto end = points[(size_t) indices[segment + 1]];
            const auto mid = pointAlong(start, end, 0.52f);
            const auto normal = perpendicular(start, end);
            const auto halfWidth = (segment == 0 ? width0 : (segment == 1 ? width1 : width2)) * 0.28f;

            g.setColour(outline.withAlpha(0.32f));
            g.drawLine({ mid + normal * halfWidth, mid - normal * halfWidth }, creaseWidths[segment]);

            g.setColour(highlight.withAlpha(0.10f));
            g.drawLine({ mid + normal * (halfWidth * 0.55f), mid - normal * (halfWidth * 0.55f) }, 1.0f);
        }
    }

    void drawTextureLines(juce::Graphics& g,
                          const std::array<juce::Point<float>, 21>& points,
                          juce::Colour lineColour,
                          juce::Colour glowColour,
                          float visibility) const
    {
        const auto drawLineSet = [&](const juce::Path& path, float lineWidth, float glowWidth)
        {
            g.setColour(glowColour.withAlpha(0.10f * visibility));
            g.strokePath(path, juce::PathStrokeType(glowWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(lineColour.withAlpha(0.76f * visibility));
            g.strokePath(path, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        const std::array<std::array<int, 4>, 5> fingers {{
            {{ 1, 2, 3, 4 }},
            {{ 5, 6, 7, 8 }},
            {{ 9, 10, 11, 12 }},
            {{ 13, 14, 15, 16 }},
            {{ 17, 18, 19, 20 }}
        }};

        for (const auto& finger : fingers)
        {
            drawLineSet(makeSmoothPath({
                points[(size_t) finger[0]],
                points[(size_t) finger[1]],
                points[(size_t) finger[2]],
                points[(size_t) finger[3]]
            }, false), 0.95f, 3.0f);

            for (size_t segment = 0; segment < 3; ++segment)
            {
                const auto start = points[(size_t) finger[segment]];
                const auto end = points[(size_t) finger[segment + 1]];
                const auto mid = pointAlong(start, end, 0.5f);
                const auto normal = perpendicular(start, end);
                const auto halfWidth = start.getDistanceFrom(end) * 0.17f;

                drawLineSet(makeSmoothPath({
                    mid + normal * halfWidth,
                    mid - normal * halfWidth
                }, false), 0.85f, 2.6f);
            }
        }

    }

    void drawAssignedParameterLabels(juce::Graphics& g,
                                     const juce::Rectangle<float>& scene,
                                     const VisualHand& hand) const
    {
        const std::array<int, 4> tipIndices { 8, 12, 16, 20 };

        for (size_t i = 0; i < tipIndices.size(); ++i)
        {
            const auto& parameter = processor.fingerControls[i];

            if (parameter.isEmpty())
                continue;

            const auto tip = mapPoint(scene, hand.smoothed[(size_t) tipIndices[i]]);
            const auto labelArea = juce::Rectangle<float>(0.0f, 0.0f, 112.0f, 24.0f)
                .withCentre({ tip.x + 64.0f, tip.y - 16.0f });

            const auto labelColour = getParameterColour(parameter);

            g.setColour(juce::Colours::black.withAlpha(0.35f * hand.visibility));
            g.fillRoundedRectangle(labelArea.translated(0.0f, 2.0f), 12.0f);

            g.setColour(labelColour.withAlpha(0.22f * hand.visibility));
            g.fillRoundedRectangle(labelArea, 12.0f);

            g.setColour(labelColour.withAlpha(0.75f * hand.visibility));
            g.drawRoundedRectangle(labelArea, 12.0f, 1.0f);

            g.setColour(juce::Colours::white.withAlpha(0.94f * hand.visibility));
            g.setFont(juce::Font { juce::FontOptions(12.0f) }.boldened());
            g.drawFittedText(parameter, labelArea.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    CMProjectAudioProcessor& processor;
    std::array<VisualHand, 2> hands;
    double timeSeconds = 0.0;
    float modulationEnergy = 0.0f;
    float reverseAmount = 0.0f;
};

//borders of the plugin that light up periodically
class GridBackgroundComponent : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black); //Fill all black
        float glowValue = 0.5f * (1.0f + std::sin(phase));  //Sine wave from 0 to 1

        float hue = 0.3f; //green tone
        float saturation = juce::jmap(glowValue, 0.0f, 1.0f, 0.1f, 1.0f);  //soft gray → full color
        float brightness = juce::jmap(glowValue, 0.0f, 1.0f, 0.05f, 1.0f); //dim → full bright

        juce::Colour glow = juce::Colours::transparentBlack;
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

    void timerCallback()
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

    static juce::Rectangle<int> getVisibleBounds(const juce::Image& image)
    {
        if (! image.isValid())
            return {};

        const auto width = image.getWidth();
        const auto height = image.getHeight();
        int minX = width;
        int minY = height;
        int maxX = -1;
        int maxY = -1;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto pixel = image.getPixelAt(x, y);

                if (pixel.getAlpha() > 10)
                {
                    minX = juce::jmin(minX, x);
                    minY = juce::jmin(minY, y);
                    maxX = juce::jmax(maxX, x);
                    maxY = juce::jmax(maxY, y);
                }
            }
        }

        if (maxX < minX || maxY < minY)
            return { 0, 0, width, height };

        return { minX, minY, maxX - minX + 1, maxY - minY + 1 };
    }

    std::function<void(HDImageButton&, const juce::MouseEvent&)> onMouseDownCallback;
    std::function<void(HDImageButton&, const juce::MouseEvent&)> onMouseDragCallback;
    std::function<void(HDImageButton&, const juce::MouseEvent&)> onMouseUpCallback;

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        const auto image = getNormalImage();   //This is public
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        auto sourceBounds = getVisibleBounds(image);

        if (! image.isValid() || sourceBounds.isEmpty() || bounds.isEmpty())
            return;

        auto fittedBounds = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                .appliedTo(sourceBounds.toFloat(), bounds);

        g.drawImage(image,
                    fittedBounds.getX(), fittedBounds.getY(),
                    fittedBounds.getWidth(), fittedBounds.getHeight(),
                    (float) sourceBounds.getX(), (float) sourceBounds.getY(),
                    (float) sourceBounds.getWidth(), (float) sourceBounds.getHeight());
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::ImageButton::mouseDown(event);

        if (onMouseDownCallback)
            onMouseDownCallback(*this, event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        juce::ImageButton::mouseDrag(event);

        if (onMouseDragCallback)
            onMouseDragCallback(*this, event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::ImageButton::mouseUp(event);

        if (onMouseUpCallback)
            onMouseUpCallback(*this, event);
    }
};

// ==================================================
// Synth Page Component (Granular Synth UI)
// ==================================================

class CMProjectAudioProcessorEditor::SynthPageComponent : public juce::Component, private juce::ChangeListener, public juce::FileDragAndDropTarget
{
public:
    juce::TextButton startButton, stopButton, startCamera, stopCamera, loadSampleButton; //StartSynth, StopSynth, StartCamera, StopCamera
    juce::TextButton recordMidiButton{ "Record MIDI" }, stopMidiButton{ "Stop Recording" }, saveMidiButton{ "Save MIDI" }; //Midi buttons
    HDImageButton grainPos, grainDur, grainDensity, grainReverse, grainCutOff, grainPitch; //Hd images for granulator parameters
    juce::Image startImg, stopImg; //Start and Stop Images
    juce::Label granulatorTitle;
    juce::File currentSampleFile, originalSampleFile; //Reversed- not reversed file
    bool isReversed = false; //Boolean to handle when the sample is reversed or not
    float currentGrainPos = 0.0f; //Current grain position value
    float sampleDuration = 1.0f; //default, will be updated
    juce::TextButton resetButton{ "Reset" }; //Button useful to reset all the default values
    
    //Constructor
    SynthPageComponent(CMProjectAudioProcessor& p) : processor(p)
    {
        formatManager.registerBasicFormats(); //wav,mp3
        thumbnail.addChangeListener(this);
        granulatorParametersTitle(); //Granulator parameters
        imagesSetup(); //Function that loads all the images
        setButtonsAndLookAndFeel();//Function that sets the buttons and the different look and feel
        addSynthPageComponents(); //Function that adds all the synthpage components and makes them visible
        onClickSynthFunction(); //Function that handles all one click functions for the synthpage
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
        
        thumbnail.removeChangeListener(this);
    }
    void addSynthPageComponents()
    {
        addAndMakeVisible(startButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(startCamera);
        addAndMakeVisible(stopCamera);
        addAndMakeVisible(loadSampleButton);
        addAndMakeVisible(grainPos);
        addAndMakeVisible(grainDur);
        addAndMakeVisible(grainDensity);
        addAndMakeVisible(grainCutOff);
        addAndMakeVisible(grainPitch);
        addAndMakeVisible(grainReverse);
        addAndMakeVisible(granulatorTitle);

    }
    void imagesSetup() {
        juce::File grainPosFile = getIconFile("grainPosButton.png");
        setupImageButton(grainPos, grainPosFile);
        juce::File grainDurFile = getIconFile("grainDurButton.png");
        setupImageButton(grainDur, grainDurFile);
        juce::File grainDensityFile = getIconFile("grainDensityButton.png");
        setupImageButton(grainDensity, grainDensityFile);
        juce::File grainCutOffFile = getIconFile("grainCutOffButton.png");
        setupImageButton(grainCutOff, grainCutOffFile);
        juce::File grainPitchFile = getIconFile("grainPitchButton.png");
        setupImageButton(grainPitch, grainPitchFile);
        juce::File grainReverseFile = getIconFile("grainReverseButton.png");
        setupImageButton(grainReverse, grainReverseFile);
    }

    void onClickSynthFunction() {
        int defaultNote = 60; //MIDI C4, used as default sound if you hit startSound
        float defaultVel = 1.0f;
        startButton.onClick = [this, defaultNote, defaultVel]() {
            processor.startManualSynthNote(defaultNote, defaultVel);
            stopButton.setEnabled(true);
            //Turn on glow
            startButton.setToggleState(true, juce::dontSendNotification);
            stopButton.setToggleState(false, juce::dontSendNotification);
            };

        stopButton.onClick = [this, defaultNote]() {
            processor.stopManualSynthNote(defaultNote);
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
        startButton.setButtonText("Start Synth");
        stopButton.setButtonText("Stop Synth");
        startButton.setLookAndFeel(&startButtonLookAndFeel);
        stopButton.setLookAndFeel(&stopButtonLookAndFeel);
        startButton.setClickingTogglesState(false);
        stopButton.setClickingTogglesState(false);
    }

    void granulatorParametersTitle() {
        granulatorTitle.setText({}, juce::dontSendNotification);
        granulatorTitle.setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        // ==============================
        // WAVEFORM BACKGROUND VISUALS
        // ==============================
        {
            juce::Graphics::ScopedSaveState waveformClipState(g);

            constexpr float waveformCornerRadius = 8.0f;

            juce::Path clipPath;
            clipPath.addRoundedRectangle(waveformArea.toFloat(), waveformCornerRadius);
            g.reduceClipRegion(clipPath);

            juce::Colour bgBottom = juce::Colour::fromRGBA(12, 18, 16, 230);
            juce::Colour bgTop = juce::Colour::fromRGBA(62, 78, 74, 170);
            juce::ColourGradient bg(bgBottom,
                waveformArea.getX(), waveformArea.getBottom(),
                bgTop,
                waveformArea.getRight(), waveformArea.getY(),
                false);
            bg.addColour(0.45, juce::Colour::fromRGBA(150, 190, 178, 58));
            g.setGradientFill(bg);
            g.fillRect(waveformArea);

            juce::ColourGradient innerTint(
                juce::Colour::fromRGBA(255, 255, 255, 42),
                waveformArea.getCentreX(), waveformArea.getY(),
                juce::Colour::fromRGBA(170, 255, 220, 16),
                waveformArea.getCentreX(), waveformArea.getBottom(),
                false);
            innerTint.addColour(0.5, juce::Colour::fromRGBA(255, 255, 255, 12));
            g.setGradientFill(innerTint);
            g.fillRect(waveformArea.reduced(1));

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


            auto glassRect = waveformArea.withHeight(waveformArea.getHeight() / 3);
            juce::ColourGradient glassGradient(
                juce::Colour::fromRGBA(255, 255, 255, 88),
                glassRect.getX(), glassRect.getY(),
                juce::Colour::fromRGBA(255, 255, 255, 0),
                glassRect.getX(), glassRect.getBottom(),
                false);
            glassGradient.addColour(0.35, juce::Colour::fromRGBA(255, 255, 255, 32));
            g.setGradientFill(glassGradient);
            g.fillRect(glassRect.reduced(1, 0));

            juce::Path sheenPath;
            auto sheenArea = waveformArea.toFloat().reduced(4.0f, 4.0f);
            sheenPath.startNewSubPath(sheenArea.getX(), sheenArea.getY() + sheenArea.getHeight() * 0.18f);
            sheenPath.quadraticTo(sheenArea.getCentreX(), sheenArea.getY() - 6.0f,
                                  sheenArea.getRight(), sheenArea.getY() + sheenArea.getHeight() * 0.12f);
            sheenPath.lineTo(sheenArea.getRight(), sheenArea.getY());
            sheenPath.lineTo(sheenArea.getX(), sheenArea.getY());
            sheenPath.closeSubPath();
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 26));
            g.fillPath(sheenPath);

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 110));
            g.drawRoundedRectangle(waveformArea.toFloat().reduced(0.75f), waveformCornerRadius, 1.0f);

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 34));
            g.drawRoundedRectangle(waveformArea.toFloat().reduced(2.0f), waveformCornerRadius - 1.25f, 1.0f);
        }
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
            processor.loadSynthSample(temp);

            //Update state
            currentSampleFile = temp;
            isReversed = true;
        }
        else
        {
            //Simply restore the original
            thumbnail.setSource(new juce::FileInputSource(originalSampleFile));
            repaint();
            processor.loadSynthSample(originalSampleFile);

            currentSampleFile = originalSampleFile;
            isReversed = false;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto scale = (float) getWidth() / 800.0f;
        const auto scaled = [scale](int value) { return juce::roundToInt(value * scale); };

        auto topRow = area.removeFromTop(scaled(80))
                          .withTrimmedTop(scaled(40))
                          .withTrimmedLeft(scaled(40))
                          .withTrimmedRight(scaled(40));

        auto leftControls = topRow.removeFromLeft(scaled(250));
        stopButton.setBounds(leftControls.removeFromLeft(scaled(35)).withSizeKeepingCentre(scaled(30), scaled(30)));
        leftControls.removeFromLeft(scaled(5));
        startButton.setBounds(leftControls.removeFromLeft(scaled(35)).withSizeKeepingCentre(scaled(28), scaled(30)));
        leftControls.removeFromLeft(scaled(15));
        startCamera.setBounds(leftControls.removeFromLeft(scaled(50)).withSizeKeepingCentre(scaled(46), scaled(30)));
        leftControls.removeFromLeft(scaled(5));
        stopCamera.setBounds(leftControls.removeFromLeft(scaled(50)).withSizeKeepingCentre(scaled(46), scaled(30)));

        auto rightControls = topRow.removeFromRight(scaled(200));
        loadSampleButton.setBounds(rightControls.removeFromLeft(scaled(100)).withSizeKeepingCentre(scaled(100), scaled(30)));
        rightControls.removeFromLeft(scaled(10));
        resetButton.setBounds(rightControls.removeFromLeft(scaled(80)).withSizeKeepingCentre(scaled(80), scaled(30)));

        area.removeFromTop(scaled(10));
        waveformArea = area.removeFromTop(scaled(110)).withTrimmedLeft(scaled(40)).withTrimmedRight(scaled(40));

        area.removeFromTop(scaled(10));
        granulatorTitle.setFont(juce::Font("Arial", 20.0f * scale, juce::Font::bold));
        granulatorTitle.setBounds(area.removeFromTop(scaled(30)).withTrimmedLeft(scaled(40)).withTrimmedRight(scaled(40)));

        auto gridArea = area.removeFromTop(scaled(120)).withTrimmedLeft(scaled(40)).withTrimmedRight(scaled(40));
        auto row1 = gridArea.removeFromTop(scaled(55));
        gridArea.removeFromTop(scaled(10));
        auto row2 = gridArea.removeFromTop(scaled(55));

        const int gap = scaled(15);
        const int boxWidth = (row1.getWidth() - (gap * 2)) / 3;
        
        grainPos.setBounds(row1.removeFromLeft(boxWidth));
        row1.removeFromLeft(gap);
        grainDur.setBounds(row1.removeFromLeft(boxWidth));
        row1.removeFromLeft(gap);
        grainDensity.setBounds(row1.removeFromLeft(boxWidth));

        grainReverse.setBounds(row2.removeFromLeft(boxWidth));
        row2.removeFromLeft(gap);
        grainPitch.setBounds(row2.removeFromLeft(boxWidth));
        row2.removeFromLeft(gap);
        grainCutOff.setBounds(row2.removeFromLeft(boxWidth));
    }

    static juce::Image loadButtonImage(const juce::File& imageFile)
    {
        if (imageFile.hasFileExtension(".svg"))
        {
            auto svg = juce::XmlDocument::parse(imageFile);

            if (svg == nullptr)
                return {};

            auto drawable = juce::Drawable::createFromSVG(*svg);

            if (drawable == nullptr)
                return {};

            auto bounds = drawable->getDrawableBounds();
            auto width = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
            auto height = juce::jmax(1, juce::roundToInt(bounds.getHeight()));
            juce::Image image(juce::Image::ARGB, width, height, true);
            juce::Graphics g(image);

            drawable->drawWithin(g,
                                 juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height),
                                 juce::RectanglePlacement::centred,
                                 1.0f);
            return image;
        }

        return juce::ImageFileFormat::loadFrom(imageFile);
    }

    void setupImageButton(juce::ImageButton& button, const juce::File& imageFile)
    {
        if (!imageFile.existsAsFile())
        {
            DBG("❌ Could not find image: " + imageFile.getFullPathName());
            return;
        }
        juce::Image img = loadButtonImage(imageFile);

        if (!img.isValid())
        {
            DBG("Could not load image: " + imageFile.getFullPathName());
            return;
        }

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

            processor.loadSynthSample(droppedFile);

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

private:
    CMProjectAudioProcessor& processor;
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
                    processor.loadSynthSample(fileToLoad);

                    // Send sample duration [s]
                    double duration = 0.0;
                    if (auto* reader = formatManager.createReaderFor(fileToLoad))
                    {
                        duration = reader->lengthInSamples / reader->sampleRate;
                        processor.senderToPython.send("/sampleDuration", (float)duration);
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

//==============================================================================
// Main GUI container: synth-only UI container
//==============================================================================

CMProjectAudioProcessorEditor::CMProjectAudioProcessorEditor(CMProjectAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), tooltipWindow(this, 300)
{
    startingConfigurationGlobal(); //Function that handles the starting configuration
    clearFingersSetUp(); //Function that handles ClearFingers setup
    setToolTipFunction(); //Function that handles all the toolTip functions for both Synth and Drum page
    midiOnClickSetUpFunction(); //Function that handles all the oneclick setup functions
    pluginTitle(); //function that sets the plugin title
    setToolTipFunction(); //Function that handles all the toolTip functions for both Synth and Drum page
    addListenerToGLobal(); //function that sets all the addListeners
    fingersSetUp(); //Function that setUps the fingers
    int maxWidth = 1600;
    int maxHeight = 1500;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto userArea = display->userArea;
        maxWidth = juce::jmax(800, userArea.getWidth());
        maxHeight = juce::jmax(750, juce::roundToInt(maxWidth * (750.0 / 800.0)));

        if (maxHeight > userArea.getHeight())
        {
            maxHeight = juce::jmax(750, userArea.getHeight());
            maxWidth = juce::jmax(800, juce::roundToInt(maxHeight * (800.0 / 750.0)));
        }
    }

    setResizable(true, true);
    setResizeLimits(800, 750, maxWidth, maxHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(800.0 / 750.0);
    setSize(800, 750); //Total size of the plugin matching the new portrait layout
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
        synthPage->resetButton.removeListener(this);
        indexButton.removeListener(this);
        middleButton.removeListener(this);
        ringButton.removeListener(this);
        pinkyButton.removeListener(this);
        clearLookAndFeelRecursively (this);
        
        delete synthPage;
}

void CMProjectAudioProcessorEditor::startingConfigurationGlobal() {
    synthPage = new SynthPageComponent(audioProcessor);
    background = std::make_unique<GridBackgroundComponent>();
    handVisualizer = std::make_unique<HandVisualizerComponent>(audioProcessor);
    addAndMakeVisible(background.get()); //comes before anything else
    addAndMakeVisible(handVisualizer.get());
    addAndMakeVisible(synthPage);
}
void CMProjectAudioProcessorEditor::setToolTipFunction() {

    synthPage->grainPos.setTooltip("Grain Position\nUse this to shift the grain window around within the sample.");
    synthPage->grainDur.setTooltip("Grain Duration\nControls how long each grain plays before the next one starts.");
    synthPage->grainDensity.setTooltip("Grain Density\nMore density means more overlapping grains thicker sound.");
    synthPage->grainReverse.setTooltip("Grain Reverse\nToggle to play each grain backwards.");
    synthPage->grainPitch.setTooltip("Grain Pitch\nTransposes the pitch of each grain.");
    synthPage->grainCutOff.setTooltip("Filter Cut-off\nA low-pass cutoff on the granular output.");
    synthPage->startButton.setTooltip("Start Button");
    synthPage->stopButton.setTooltip("Stop Button");
    synthPage->startCamera.setTooltip("Start Camera");
    synthPage->loadSampleButton.setTooltip("Load your sample");
    synthPage->stopCamera.setTooltip("Stop Camera");

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
    indexButton.setVisible(false);
    addAndMakeVisible(middleButton); //Middle Finger
    middleButton.addListener(this);
    middleButton.setZoomFactor(2.5f);
    middleButton.setVisible(false);
    addAndMakeVisible(ringButton); //Ring Finger
    ringButton.addListener(this);
    ringButton.setZoomFactor(2.5f);
    ringButton.setVisible(false);
    addAndMakeVisible(pinkyButton); //Pinky Finger
    pinkyButton.addListener(this);
    pinkyButton.setZoomFactor(2.5f);
    pinkyButton.setVisible(false);
    addAndMakeVisible(statusDisplay); //Status Display
    statusDisplay.setVisible(false);
}
void CMProjectAudioProcessorEditor::clearFingersSetUp() {
    addAndMakeVisible(clearFingersButton);
    clearFingersButton.addListener(this);
    clearFingersButton.setLookAndFeel(&clearFingerButtonLookAndFeel);
    clearFingersButton.setVisible(false);
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
void CMProjectAudioProcessorEditor::addListenerToGLobal() {
    synthPage->startCamera.addListener(this);
    synthPage->stopCamera.addListener(this);
    synthPage->startButton.addListener(this);
    synthPage->stopButton.addListener(this);
    synthPage->resetButton.addListener(this);


    synthPage->grainPos.addListener(this);
    synthPage->grainDur.addListener(this);
    synthPage->grainDensity.addListener(this);
    synthPage->grainReverse.addListener(this);
    synthPage->grainPitch.addListener(this);
    synthPage->grainCutOff.addListener(this);

    auto attachParameterDrag = [this](HDImageButton& button, const juce::String& parameter)
    {
        button.onMouseDownCallback = [this, parameter](HDImageButton& source, const juce::MouseEvent& event)
        {
            draggedParameter = parameter;
            draggedParameterIcon = source.getNormalImage();
            dragStartPoint = event.getScreenPosition().toFloat();
            dragCurrentPoint = dragStartPoint;
            hoveredFingerButton = nullptr;
        };

        button.onMouseDragCallback = [this, parameter](HDImageButton& source, const juce::MouseEvent& event)
        {
            const auto screenPoint = event.getScreenPosition().toFloat();

            if (! isParameterDragActive)
            {
                if (screenPoint.getDistanceFrom(dragStartPoint) < 10.0f)
                    return;

                if (! isPythonOn)
                {
                    statusDisplay.showMessage("Open Camera first");
                    return;
                }

                beginParameterDrag(parameter,
                                   source.getNormalImage(),
                                   screenPoint,
                                   source.localPointToGlobal(source.getLocalBounds().getCentre()).toFloat());
                return;
            }

            updateParameterDrag(screenPoint);
        };

        button.onMouseUpCallback = [this](HDImageButton&, const juce::MouseEvent& event)
        {
            if (isParameterDragActive)
                finishParameterDrag(event.getScreenPosition().toFloat());
        };
    };

    attachParameterDrag(synthPage->grainPos, "GrainPos");
    attachParameterDrag(synthPage->grainDur, "GrainDur");
    attachParameterDrag(synthPage->grainDensity, "GrainDensity");
    attachParameterDrag(synthPage->grainPitch, "GrainPitch");
    attachParameterDrag(synthPage->grainCutOff, "GrainCutOff");


}
void CMProjectAudioProcessorEditor::paint(juce::Graphics&) {}
void CMProjectAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    if (! isParameterDragActive || draggedParameter.isEmpty())
        return;

    const auto start = getLocalPoint(nullptr, dragStartPoint.toInt()).toFloat();
    const auto current = getLocalPoint(nullptr, dragCurrentPoint.toInt()).toFloat();
    const auto accent = getParameterAccentColour(draggedParameter);
    const float pulse = 0.65f + 0.35f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.012));

    juce::Path dragPath;
    dragPath.startNewSubPath(start);
    dragPath.quadraticTo((start.x + current.x) * 0.5f, juce::jmin(start.y, current.y) - 32.0f, current.x, current.y);

    g.setColour(accent.withAlpha(0.18f * pulse));
    g.strokePath(dragPath, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(accent.withAlpha(0.95f));
    g.strokePath(dragPath, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (hoveredFingerButton != nullptr)
    {
        auto hoverBounds = getLocalArea(hoveredFingerButton, hoveredFingerButton->getLocalBounds()).toFloat().expanded(10.0f);
        g.setColour(accent.withAlpha(0.14f * pulse));
        g.fillEllipse(hoverBounds);
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.drawEllipse(hoverBounds, 2.2f);
    }

    auto chipBounds = juce::Rectangle<float>(0.0f, 0.0f, 128.0f, 40.0f).withCentre(current + juce::Point<float>(0.0f, -28.0f));
    g.setColour(juce::Colours::black.withAlpha(0.40f));
    g.fillRoundedRectangle(chipBounds.translated(0.0f, 3.0f), 18.0f);

    g.setColour(accent.withAlpha(0.18f));
    g.fillRoundedRectangle(chipBounds, 18.0f);
    g.setColour(accent.withAlpha(0.82f));
    g.drawRoundedRectangle(chipBounds, 18.0f, 1.2f);

    if (draggedParameterIcon.isValid())
    {
        auto iconArea = chipBounds.removeFromLeft(34.0f).reduced(6.0f);
        g.drawImageWithin(draggedParameterIcon,
                          iconArea.getX(), iconArea.getY(),
                          iconArea.getWidth(), iconArea.getHeight(),
                          juce::RectanglePlacement::centred,
                          false);
    }

    g.setColour(juce::Colours::white.withAlpha(0.94f));
    g.setFont(juce::Font { juce::FontOptions(12.0f) }.boldened());
    g.drawFittedText(draggedParameter,
                     chipBounds.reduced(6.0f, 0.0f).toNearestInt(),
                     juce::Justification::centred,
                     1);
}
void CMProjectAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / 800.0f;
    const auto scaled = [scale](int value) { return juce::roundToInt(value * scale); };

    if (background)
        background->setBounds(getLocalBounds());

    auto fullArea = getLocalBounds();
    synthPage->setBounds(fullArea);

    juce::Rectangle<int> visualizerArea(scaled(40), scaled(360), scaled(720), scaled(310));

    if (handVisualizer)
        handVisualizer->setBounds(visualizerArea);
        

    // Dynamic scaled mapping over the visualizer region
    float scaleX = visualizerArea.getWidth() / 800.0f;
    float scaleY = visualizerArea.getHeight() / 350.0f;
    float imageX = visualizerArea.getX() - (15.0f * scaleX);
    float imageY = visualizerArea.getY() + (15.0f * scaleY);
    const int fingerButtonYOffset = juce::roundToInt(18.0f * scaleY);
    const auto circleDiameter = 20;

    //Values of the dot to perfectly sit on the index finger
    const int dotX = imageX + 560 - circleDiameter / 2;
    const int dotY = imageY + 15 + fingerButtonYOffset - circleDiameter / 2;

    indexButton.setBounds(dotX, dotY, circleDiameter, circleDiameter);
    //Values of the dot to perfectly sit on the middle finger
    const int midOffsetX = 511.8;
    const int midOffsetY = 3.9;
    const int midX = imageX + midOffsetX - circleDiameter / 2;
    const int midY = imageY + midOffsetY + fingerButtonYOffset - circleDiameter / 2;
    middleButton.setBounds(midX, midY, circleDiameter, circleDiameter);
    //Values of the dot to perfectly sit on the ring finger
    const int ringOffx = 468;
    const int ringOffy = 17;
    const int ringX = imageX + ringOffx - circleDiameter / 2;
    const int ringY = imageY + ringOffy + fingerButtonYOffset - circleDiameter / 2;
    ringButton.setBounds(ringX, ringY, circleDiameter, circleDiameter);

    //Values of the dot to perfectly sit on the pinky finger
    const int pinkyOffx = 438;
    const int pinkyOffy = 62;
    const int pinkyX = imageX + pinkyOffx - circleDiameter / 2;
    const int pinkyY = imageY + pinkyOffy + fingerButtonYOffset - circleDiameter / 2;
    pinkyButton.setBounds(pinkyX, pinkyY, circleDiameter, circleDiameter);
    
    statusDisplay.setBounds({});
    clearFingersButton.setBounds({});

    // Plugin title perfectly centered at top
    auto textWidth = pageTitleLabel.getFont().getStringWidth("HAND GRANULATOR");
    const int totalWidth = textWidth + scaled(20);
    pageTitleLabel.setBounds(getWidth() / 2 - totalWidth / 2, scaled(5), totalWidth, scaled(40));

}
void CMProjectAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& e,
    const juce::MouseWheelDetails& wheel)
{
    Component::mouseWheelMove(e, wheel);
}

int CMProjectAudioProcessorEditor::getFingerIndex(const CircleButton& fingerButton) const noexcept
{
    if (&fingerButton == &indexButton)  return 0;
    if (&fingerButton == &middleButton) return 1;
    if (&fingerButton == &ringButton)   return 2;
    if (&fingerButton == &pinkyButton)  return 3;
    return -1;
}

juce::String CMProjectAudioProcessorEditor::getFingerName(const CircleButton& fingerButton) const
{
    if (&fingerButton == &indexButton)  return "Index";
    if (&fingerButton == &middleButton) return "Middle";
    if (&fingerButton == &ringButton)   return "Ring";
    if (&fingerButton == &pinkyButton)  return "Pinky";
    return "Finger";
}

juce::Colour CMProjectAudioProcessorEditor::getParameterAccentColour(const juce::String& parameter) const
{
    if (parameter == "GrainPos")     return juce::Colour::fromRGB(84, 240, 255);
    if (parameter == "GrainDur")     return juce::Colour::fromRGB(255, 192, 92);
    if (parameter == "GrainDensity") return juce::Colour::fromRGB(108, 255, 140);
    if (parameter == "GrainPitch")   return juce::Colour::fromRGB(255, 122, 214);
    if (parameter == "GrainCutOff")  return juce::Colour::fromRGB(120, 168, 255);
    if (parameter == "GrainReverse") return juce::Colour::fromRGB(244, 250, 255);
    return juce::Colours::limegreen;
}

bool CMProjectAudioProcessorEditor::hasVisibleTrackedHands() const
{
    const auto trackedHands = audioProcessor.getTrackedHands();

    for (const auto& hand : trackedHands)
        if (hand.visible)
            return true;

    return false;
}

void CMProjectAudioProcessorEditor::updateFingerTargetVisibility()
{
    const bool handsVisible = hasVisibleTrackedHands();

    auto updateButton = [this, handsVisible](CircleButton& button)
    {
        const bool shouldShow = ! handsVisible && (isParameterDragActive || button.hasAssignedIcon());
        button.setPreviewActive(shouldShow);
        button.setVisible(shouldShow);

        if (! shouldShow)
            button.setDragHover(false);
    };

    updateButton(indexButton);
    updateButton(middleButton);
    updateButton(ringButton);
    updateButton(pinkyButton);
}

CMProjectAudioProcessorEditor::CircleButton* CMProjectAudioProcessorEditor::getFingerButtonAtScreenPosition(juce::Point<float> screenPosition)
{
    auto findTarget = [screenPosition](CircleButton& button) -> CMProjectAudioProcessorEditor::CircleButton*
    {
        if (! button.isVisible())
            return nullptr;

        const auto bounds = button.getScreenBounds().expanded(16);
        return bounds.contains(screenPosition.toInt()) ? &button : nullptr;
    };

    if (auto* button = findTarget(indexButton))  return button;
    if (auto* button = findTarget(middleButton)) return button;
    if (auto* button = findTarget(ringButton))   return button;
    if (auto* button = findTarget(pinkyButton))  return button;
    return nullptr;
}

void CMProjectAudioProcessorEditor::updateFingerDragHover(juce::Point<float> screenPosition)
{
    hoveredFingerButton = getFingerButtonAtScreenPosition(screenPosition);

    indexButton.setDragHover(hoveredFingerButton == &indexButton);
    middleButton.setDragHover(hoveredFingerButton == &middleButton);
    ringButton.setDragHover(hoveredFingerButton == &ringButton);
    pinkyButton.setDragHover(hoveredFingerButton == &pinkyButton);
}

void CMProjectAudioProcessorEditor::beginParameterDrag(const juce::String& parameter,
                                                       const juce::Image& icon,
                                                       juce::Point<float> screenPosition,
                                                       juce::Point<float> dragOrigin)
{
    setCurrentParameter(parameter);
    currentParameterIcon = icon;
    draggedParameter = parameter;
    draggedParameterIcon = icon;
    dragStartPoint = dragOrigin;
    dragCurrentPoint = screenPosition;
    isParameterDragActive = true;
    updateFingerTargetVisibility();
    updateFingerDragHover(screenPosition);
    repaint();
}

void CMProjectAudioProcessorEditor::updateParameterDrag(juce::Point<float> screenPosition)
{
    if (! isParameterDragActive)
        return;

    dragCurrentPoint = screenPosition;
    updateFingerDragHover(screenPosition);
    repaint();
}

void CMProjectAudioProcessorEditor::clearFingerAssignmentVisuals(int fingerIndex)
{
    auto clearGlow = [](juce::ImageComponent& glow)
    {
        glow.setVisible(false);
        glow.setImage(juce::Image(), juce::RectanglePlacement::centred);
    };

    switch (fingerIndex)
    {
        case 0:
            indexButton.clearIcon();
            indexButton.setTooltip({});
            clearGlow(indexGlow);
            break;
        case 1:
            middleButton.clearIcon();
            middleButton.setTooltip({});
            clearGlow(middleGlow);
            break;
        case 2:
            ringButton.clearIcon();
            ringButton.setTooltip({});
            clearGlow(ringGlow);
            break;
        case 3:
            pinkyButton.clearIcon();
            pinkyButton.setTooltip({});
            clearGlow(pinkyGlow);
            break;
        default:
            break;
    }
}

bool CMProjectAudioProcessorEditor::assignParameterToFinger(const juce::String& parameter,
                                                            const juce::Image& icon,
                                                            CircleButton& fingerButton)
{
    if (! isPythonOn)
    {
        statusDisplay.showMessage("Open Camera first");
        return false;
    }

    if (parameter.isEmpty())
    {
        statusDisplay.showMessage("Select a parameter");
        return false;
    }

    const auto fingerIndex = getFingerIndex(fingerButton);

    if (fingerIndex < 0)
        return false;

    for (int i = 0; i < 4; ++i)
    {
        if (i != fingerIndex && audioProcessor.fingerControls[i] == parameter)
        {
            audioProcessor.fingerControls[i].clear();
            clearFingerAssignmentVisuals(i);
        }
    }

    audioProcessor.senderToPython.send("/activePage", currentPage);
    audioProcessor.fingerControls[fingerIndex] = parameter;
    audioProcessor.sendFingerAssignementsOSC();

    setCurrentParameter(parameter);
    currentParameterIcon = icon;

    fingerButton.setIconImage(icon);
    fingerButton.setTooltip(parameter);

    auto clearGlow = [](juce::ImageComponent& glow)
    {
        glow.setVisible(false);
        glow.setImage(juce::Image(), juce::RectanglePlacement::centred);
    };

    clearGlow(indexGlow);
    clearGlow(middleGlow);
    clearGlow(ringGlow);
    clearGlow(pinkyGlow);

    updateFingerTargetVisibility();
    statusDisplay.showMessage(getFingerName(fingerButton) + "->" + parameter);
    return true;
}

void CMProjectAudioProcessorEditor::finishParameterDrag(juce::Point<float> screenPosition)
{
    if (! isParameterDragActive)
        return;

    updateParameterDrag(screenPosition);

    if (auto* fingerButton = getFingerButtonAtScreenPosition(screenPosition))
        assignParameterToFinger(draggedParameter, draggedParameterIcon, *fingerButton);

    isParameterDragActive = false;
    draggedParameter.clear();
    draggedParameterIcon = juce::Image();
    hoveredFingerButton = nullptr;
    updateFingerDragHover(screenPosition);
    updateFingerTargetVisibility();
    repaint();
}



void CMProjectAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &synthPage->resetButton)
    {
        if (!isPythonOn)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }

        audioProcessor.senderToPython.send("/resetParameters");
        statusDisplay.showMessage("Resetting parameters!");
        return;
    }

    if (button == &synthPage->startCamera)
    {
        if (launchPythonHandTracker())
        {
            synthPage->startCamera.setEnabled(false);
            synthPage->stopCamera.setEnabled(true);
            statusDisplay.showMessage("Camera Started");
        }
        else
        {
            synthPage->startCamera.setEnabled(true);
            synthPage->stopCamera.setEnabled(false);
        }

        return;
    }

    if (button == &synthPage->stopCamera)
    {
        isPythonOn = false;
        stopPythonHandTracker();
        synthPage->startCamera.setEnabled(true);
        synthPage->stopCamera.setEnabled(false);
        statusDisplay.showMessage("Camera Stopped");
        return;
    }

    if (button == &synthPage->startButton)
    {
        synthPage->startButton.setEnabled(false);
        synthPage->stopButton.setEnabled(true);
    }
    else if (button == &synthPage->stopButton)
    {
        synthPage->startButton.setEnabled(true);
        synthPage->stopButton.setEnabled(false);
    }

    if (button == &synthPage->grainPos)
    {
        setCurrentParameter("GrainPos");
        currentParameterIcon = synthPage->grainPos.getNormalImage();
        statusDisplay.showMessage("GrainPos selected");
    }
    else if (button == &synthPage->grainDur)
    {
        setCurrentParameter("GrainDur");
        currentParameterIcon = synthPage->grainDur.getNormalImage();
        statusDisplay.showMessage("GrainDur selected");
    }
    else if (button == &synthPage->grainDensity)
    {
        setCurrentParameter("GrainDensity");
        currentParameterIcon = synthPage->grainDensity.getNormalImage();
        statusDisplay.showMessage("GrainDensity selected");
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
        statusDisplay.showMessage("GrainCutOff selected");
    };

    if (button == &indexButton)
    {
        assignParameterToFinger(currentParameter, currentParameterIcon, indexButton);
    }
    else if (button == &middleButton)
    {
        assignParameterToFinger(currentParameter, currentParameterIcon, middleButton);
    }
    else if (button == &ringButton)
    {
        assignParameterToFinger(currentParameter, currentParameterIcon, ringButton);
    }
    else if (button == &pinkyButton)
    {
        assignParameterToFinger(currentParameter, currentParameterIcon, pinkyButton);
    }
    else if (button == &clearFingersButton)
    {
        if (!isPythonOn)
        {
            statusDisplay.showMessage("Open Camera first");
            return;
        }

        for (int i = 0; i < 4; ++i)
        {
            audioProcessor.fingerControls[i].clear();
            clearFingerAssignmentVisuals(i);
        }
        audioProcessor.sendFingerAssignementsOSC();
        updateFingerTargetVisibility();

        statusDisplay.showMessage("Finger mappings cleared");
    }
}

//==============================================================================
//PYTHON PROCESS//
//==============================================================================

//This function starts the python process
bool CMProjectAudioProcessorEditor::launchPythonHandTracker()
{
    if (cameraRunning)
        return true;

    const juce::File script = getHandTrackerScript(); //Find the python script 

    if (! script.existsAsFile())
    {
        DBG("❌ Hand tracker script not found: " << script.getFullPathName());
        statusDisplay.showMessage("Hand tracker script not found");
        return false;
    }

#if JUCE_WINDOWS
    auto pythonExe = getHandTrackerPythonExecutable();
    if (! pythonExe.existsAsFile())
    {
        statusDisplay.showMessage("Python env not found");
        return false;
    }
    if (pythonProcess.isRunning())
        return true;

    juce::StringArray cmd{ pythonExe.getFullPathName(), "-u", script.getFullPathName() };
 
    if (!pythonProcess.start(cmd))
    {
        DBG("❌ Failed to launch Python tracker");
        statusDisplay.showMessage("Could not launch tracker");
        return false;
    }

    juce::Thread::sleep(750);

    if (! pythonProcess.isRunning())
    {
        auto output = pythonProcess.readAllProcessOutput().trim();
        DBG("❌ Python tracker exited early. Output: " << output);
        statusDisplay.showMessage(output.isNotEmpty() ? output : "Camera failed to start");
        return false;
    }

#else
    auto pythonExe = getHandTrackerPythonExecutable();

    if (! pythonExe.existsAsFile())
    {
        statusDisplay.showMessage("Python not found");
        DBG("❌ Could not resolve a Python executable for the hand tracker");
        return false;
    }

    juce::StringArray cmd { pythonExe.getFullPathName(), "-u", script.getFullPathName() };
    
    if (pythonProcess.isRunning())
        return true;
        
    DBG("Launching: " << cmd.joinIntoString(" "));

    if (! pythonProcess.start(cmd))
    {
        DBG("❌ Couldn’t launch Python hand-tracker");
        statusDisplay.showMessage("Could not launch tracker");
        return false;
    }

    juce::Thread::sleep(750); // allow immediate startup failures to surface

    if (! pythonProcess.isRunning())
    {
        auto output = pythonProcess.readAllProcessOutput().trim();
        DBG("❌ Python tracker exited early. Output: " << output);
        statusDisplay.showMessage(output.isNotEmpty() ? output : "Camera failed to start");
        return false;
    }
#endif

    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (audioProcessor.senderToPython.connect("127.0.0.1", 9002))
        {
            cameraRunning = true;
            isPythonOn = true;
            audioProcessor.senderToPython.send("/activePage", currentPage);
            return true;
        }

        if (! pythonProcess.isRunning())
            break;

        juce::Thread::sleep(100);
    }

    auto output = pythonProcess.readAllProcessOutput().trim();
    DBG("❌ Could not connect to Python OSC server on port 9002. Output: " << output);

    if (pythonProcess.isRunning())
    {
        pythonProcess.kill();
        pythonProcess.waitForProcessToFinish(2000);
    }

    cameraRunning = false;
    isPythonOn = false;
    statusDisplay.showMessage(output.isNotEmpty() ? output : "Tracker did not start");
    return false;
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
    isPythonOn = false;
    audioProcessor.clearTrackedHands();

    if (handVisualizer)
        handVisualizer->repaint();
}

//This function controls if the camera has been closed from the X
void CMProjectAudioProcessorEditor::timerCallback()
{
    if (synthPage)
    {
        synthPage->currentGrainPos = audioProcessor.getGrainPos();
    }

    if (handVisualizer)
        handVisualizer->tick(juce::Time::getMillisecondCounterHiRes() * 0.001);

    updateFingerTargetVisibility();
    synthPage->repaint(); //force waveform + bar to redraw

    if (isParameterDragActive)
        repaint();

    //Closed the camera from an external X
    if (cameraRunning && !pythonProcess.isRunning())
    {
        cameraRunning = false;
        synthPage->startCamera.setEnabled(true);
        synthPage->stopCamera.setEnabled(false);
        isPythonOn = false;
        audioProcessor.clearTrackedHands();

        auto output = pythonProcess.readAllProcessOutput().trim();

        if (output.isNotEmpty())
        {
            DBG("❌ Python tracker stopped. Output: " << output);
            statusDisplay.showMessage(output);
        }
        else
        {
            statusDisplay.showMessage("Camera stopped");
        }
    }
}
