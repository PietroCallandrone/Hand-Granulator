/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
CMProjectAudioProcessor::CMProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif

{
    formatManager.registerBasicFormats();
    updateParameters();
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices)
    DBG("MIDI Device: " + d.name);

}

CMProjectAudioProcessor::~CMProjectAudioProcessor()
{
}

//==============================================================================
const juce::String CMProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CMProjectAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

//function that handles the fingers assignement in the synth page
void CMProjectAudioProcessor::sendFingerAssignementsOSC() {
    senderToPython.send("/fingerParameters", fingerControls[0],
        fingerControls[1],
        fingerControls[2],
        fingerControls[3]);
}

//function that handles the fingers assignement in the drum page
void CMProjectAudioProcessor::sendFingerDrumMappingOSC()
{
    auto toInt = [](const juce::String& s) -> int
        {
            return s.isNotEmpty() ? s.getIntValue() : -1;
        };

    senderToPython.send("/fingerDrums",
        toInt(fingerDrumMapping[0]),
        toInt(fingerDrumMapping[1]),
        toInt(fingerDrumMapping[2]),
        toInt(fingerDrumMapping[3]));
}

bool CMProjectAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CMProjectAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CMProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CMProjectAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CMProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CMProjectAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CMProjectAudioProcessor::getProgramName (int index)
{
    return {};
}

void CMProjectAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

// Handle OSC messages arriving from Python tracker
void CMProjectAudioProcessor::oscMessageReceived(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (address == "/handGrain" && message.size() == 7 &&
        message[0].isFloat32() && message[1].isFloat32() && message[2].isFloat32() &&
        message[3].isFloat32() && message[4].isFloat32() && message[5].isFloat32() && message[6].isFloat32())
    {
        grainDur = message[0].getFloat32();
        grainPos = message[1].getFloat32();
        cutoff = message[2].getFloat32();
        density = message[3].getFloat32();
        pitch = message[4].getFloat32();
        reverse = message[5].getFloat32();
        lfoRate = message[6].getFloat32();

    }
    
    else if (address == "/triggerDrum" && message.size() == 1 && message[0].isInt32())
    {
        int fingerIndex = message[0].getInt32();
        DBG(" Triggering drum from finger " << fingerIndex);
        triggerSamplePlayback(fingerIndex);
    }
    else
    {
        DBG(" Unknown or malformed OSC message: " << address << ", size=" << message.size());
    }
    
}

void CMProjectAudioProcessor::updateParameters() {
    
    grainDur.store(0.06f);
    grainPos.store(0.01f); 
    cutoff.store(3000.0f); 
    density.store(0.8f); 
    pitch.store(0.0f); 
    reverse.store(0.0); 
    lfoRate.store(0.0f); 

}


//==============================================================================
void CMProjectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    samplesUntilNextGrain = 0.0;
    heldSynthNotes = 0;
    synthGate = false;
    synthEnvLevel = 0.0f;
    currentPitchRatio = 1.0f;
    pitchWheelSemitones = 0.0f;
    activeGrains.clear();

    //formatManager.registerBasicFormats(); //Register WAV, AIFF, MP3 support

    if(!processingSender.connect("127.0.0.1", 9003))
        DBG("Could not connect to Processing");
    else
        DBG("Connected to Processing via OSC");
        
    // Python receiver
    if (!oscReceiver.connect(9001)) // match Python port
        DBG("❌ Could not bind OSC receiver on 9001");
    else {
        oscReceiver.addListener(this, "/handGrain");
        oscReceiver.addListener(this, "/triggerDrum");
        DBG("✅ JUCE OSC Receiver listening on port 9001");
    }

}


void CMProjectAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CMProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void CMProjectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // MIDI handling
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            auto note = msg.getNoteNumber(); // 0–127
            auto vel = msg.getVelocity() / 127.0f; // normalized 0.0–1.0
            startManualSynthNote(note, vel);
        }
        else if (msg.isNoteOff())
        {
            auto note = msg.getNoteNumber();
            stopManualSynthNote(note);
        }
        else if (msg.isPitchWheel())
        {
            const int raw = msg.getPitchWheelValue();
            const float norm = (raw - 8192) / 8192.0f; // -1..+1
            pitchWheelSemitones = norm * 2.0f;         // SC behavior: +-2 semitones
        }

        if (isRecordingMidi)
            recordedSequence.addEvent(msg);
    }

    // ===============================
    // 🎵 Drum sample mixing logic
    // ===============================
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    std::scoped_lock lock(sampleMutex);

    for (int track = 0; track < 4; ++track)
    {
        if (triggerPlayback[track] && sampleLoaded[track])
        {
            
            const auto& sample = drumSamples[track];
            const int sampleLength = sample.getNumSamples();

            for (int i = 0; i < numSamples; ++i)
            {
                if (playbackPositions[track] >= sampleLength)
                    break;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* out = buffer.getWritePointer(ch);
                    const float* in = sample.getReadPointer(juce::jmin(ch, sample.getNumChannels() - 1));
                    out[i] += in[playbackPositions[track]] * trackVolumes[track];
                }

                playbackPositions[track]++;
            }
            if (playbackPositions[track] >= sampleLength)
                triggerPlayback[track] = false;
        }
    }

    // Built-in granular synth
    std::scoped_lock synthLock(synthSampleMutex);
    if (synthSampleLoaded && synthSample.getNumSamples() > 1
        && (heldSynthNotes > 0 || synthEnvLevel > 0.0f || !activeGrains.empty()))
    {
        const int sampleLength = synthSample.getNumSamples();
        const int sourceChannels = synthSample.getNumChannels();

        const float densityValue = juce::jmax(0.01f, density.load());
        const double bpm = 120.0;
        // Match SC: trigRate = ((bpm / 60) * 4 * density).max(0.1)
        const double grainsPerSecond = juce::jmax(0.1, ((bpm / 60.0) * 4.0 * (double)densityValue));
        const double spawnIntervalSamples = currentSampleRate / grainsPerSecond;
        const float cutoffValue = juce::jlimit(20.0f, 20000.0f, cutoff.load());
        const double dt = 1.0 / juce::jmax(1.0, currentSampleRate);
        const double rc = 1.0 / (2.0 * juce::MathConstants<double>::pi * cutoffValue);
        const float lowpassAlpha = (float)juce::jlimit(0.0, 1.0, dt / (rc + dt));
        const float sustainLevel = juce::jlimit(0.0f, 1.0f, adsrSustain.load());
        const float attackStep = (float)(1.0 / juce::jmax(1.0, (double)(adsrAttack.load() * (float)currentSampleRate)));
        const float decayStep = (float)((1.0 - sustainLevel) / juce::jmax(1.0, (double)(adsrDecay.load() * (float)currentSampleRate)));
        const float releaseStep = (float)(1.0 / juce::jmax(1.0, (double)(adsrRelease.load() * (float)currentSampleRate)));

        for (int i = 0; i < numSamples; ++i)
        {
            if (synthGate)
            {
                if (synthEnvLevel < 1.0f)
                    synthEnvLevel = juce::jmin(1.0f, synthEnvLevel + attackStep);
                else if (synthEnvLevel > sustainLevel)
                    synthEnvLevel = juce::jmax(sustainLevel, synthEnvLevel - decayStep);
            }
            else
            {
                synthEnvLevel = juce::jmax(0.0f, synthEnvLevel - releaseStep);
            }

            samplesUntilNextGrain -= 1.0;
            while (synthGate && heldSynthNotes > 0 && samplesUntilNextGrain <= 0.0 && (int)activeGrains.size() < 96)
            {
                spawnGrain();
                samplesUntilNextGrain += spawnIntervalSamples;
            }

            for (int g = (int)activeGrains.size() - 1; g >= 0; --g)
            {
                auto& grain = activeGrains[(size_t)g];

                if (grain.remainingSamples <= 0)
                {
                    activeGrains.erase(activeGrains.begin() + g);
                    continue;
                }

                const int idx0 = juce::jlimit(0, sampleLength - 1, (int)grain.samplePos);
                const int idx1 = juce::jlimit(0, sampleLength - 1, idx0 + 1);
                const float frac = (float)(grain.samplePos - (double)idx0);
                const float progress = 1.0f - ((float)grain.remainingSamples / (float)grain.totalSamples);
                const float env = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * progress);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* src = synthSample.getReadPointer(juce::jmin(ch, sourceChannels - 1));
                    const float s0 = src[idx0];
                    const float s1 = src[idx1];
                    const float raw = (s0 + (s1 - s0) * frac) * grain.gain * env * synthEnvLevel;
                    grain.lowpassState += lowpassAlpha * (raw - grain.lowpassState);
                    buffer.addSample(ch, i, grain.lowpassState);
                }

                grain.samplePos += grain.sampleStep;
                grain.remainingSamples--;

                if (grain.samplePos < 0.0 || grain.samplePos >= (double)(sampleLength - 1))
                    grain.remainingSamples = 0;
            }
        }
    }
}


//==============================================================================
bool CMProjectAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CMProjectAudioProcessor::createEditor()
{
    return new CMProjectAudioProcessorEditor (*this);
}

//==============================================================================
void CMProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void CMProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CMProjectAudioProcessor();
}

//Method to load a sample/oneshot 
void CMProjectAudioProcessor::loadSampleForTrack(int trackIndex, const juce::File& file)
{
    if (trackIndex < 0 || trackIndex >= 4)
        return;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader != nullptr)
    {
        //DBG("Loading sample for track " << trackIndex << ": " << file.getFullPathName());
        //DBG("Channels: " << reader->numChannels << ", Samples: " << reader->lengthInSamples);
        juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&tempBuffer, 0, (int)reader->lengthInSamples, 0, true, true);

        std::scoped_lock lock(sampleMutex);
        drumSamples[trackIndex] = std::move(tempBuffer);
        sampleLoaded[trackIndex] = true;
        playbackPositions[trackIndex] = 0;
    }
}

void CMProjectAudioProcessor::triggerSamplePlayback(int trackIndex)
{
    if (trackIndex >= 0 && trackIndex < 4 && sampleLoaded[trackIndex])
    {
        std::scoped_lock lock(sampleMutex);

        //Force reset position
        playbackPositions[trackIndex] = 0;
        triggerPlayback[trackIndex] = false; // cancel any residual state
        triggerPlayback[trackIndex] = true;
    }
}

bool CMProjectAudioProcessor::saveMidiRecording(const juce::File& file)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    midiFile.addTrack(recordedSequence);

    if (auto stream = file.createOutputStream())
    {
        midiFile.writeTo(*stream);
        return true;
    }
    return false;
}

void CMProjectAudioProcessor::loadSynthSample(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return;

    juce::AudioBuffer<float> temp((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&temp, 0, (int)reader->lengthInSamples, 0, true, true);

    std::scoped_lock lock(synthSampleMutex);
    synthSample = std::move(temp);
    synthSampleLoaded = true;
    activeGrains.clear();
    samplesUntilNextGrain = 0.0;
    synthEnvLevel = 0.0f;
}

void CMProjectAudioProcessor::startManualSynthNote(int noteNumber, float velocity)
{
    synthVelocity = juce::jlimit(0.0f, 1.0f, velocity);
    heldSynthNotes++;
    synthGate = true;
    const double freq = juce::MidiMessage::getMidiNoteInHertz(noteNumber);
    currentPitchRatio = (float)(freq / 440.0); // SC: pitchRatio = note.midicps / 440
}

void CMProjectAudioProcessor::stopManualSynthNote(int noteNumber)
{
    juce::ignoreUnused(noteNumber);
    heldSynthNotes = juce::jmax(0, heldSynthNotes - 1);
    if (heldSynthNotes <= 0)
    {
        synthGate = false;
    }
}

void CMProjectAudioProcessor::setSynthADSR(float attackSec, float decaySec, float sustainLevel, float releaseSec)
{
    adsrAttack.store(juce::jmax(0.001f, attackSec));
    adsrDecay.store(juce::jmax(0.001f, decaySec));
    adsrSustain.store(juce::jlimit(0.0f, 1.0f, sustainLevel));
    adsrRelease.store(juce::jmax(0.001f, releaseSec));
}

void CMProjectAudioProcessor::spawnGrain()
{
    if (!synthSampleLoaded || synthSample.getNumSamples() <= 1)
        return;

    const int sampleLength = synthSample.getNumSamples();
    const double sampleDurationSeconds = (double)sampleLength / juce::jmax(1.0, currentSampleRate);
    const float posSeconds = juce::jlimit(0.0f, (float)sampleDurationSeconds, grainPos.load());
    const float durSeconds = juce::jlimit(0.005f, 0.5f, grainDur.load());
    const bool isReverse = reverse.load() >= 0.5f;

    // SC behavior: playbackRate = basePitchRatio * shiftFactor * wheelFactor
    const float shiftSemitones = juce::jlimit(-24.0f, 24.0f, pitch.load());
    const float wheelSemitones = juce::jlimit(-2.0f, 2.0f, pitchWheelSemitones);
    const double shiftFactor = std::pow(2.0, shiftSemitones / 12.0);
    const double wheelFactor = std::pow(2.0, wheelSemitones / 12.0);
    const double rate = (double)currentPitchRatio * shiftFactor * wheelFactor;

    Grain grain;
    grain.totalSamples = juce::jmax(16, (int)std::round(durSeconds * (float)currentSampleRate));
    grain.remainingSamples = grain.totalSamples;
    grain.sampleStep = isReverse ? -rate : rate;
    grain.samplePos = juce::jlimit(0.0, (double)(sampleLength - 1), posSeconds * currentSampleRate);
    grain.gain = juce::jlimit(0.02f, 1.0f, synthVelocity * 0.2f);
    grain.lowpassState = 0.0f;

    activeGrains.push_back(grain);
}




