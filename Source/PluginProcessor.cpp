/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices)
    DBG("MIDI Device: " + d.name);

}

CMProjectAudioProcessor::~CMProjectAudioProcessor()
{
    oscSender.send("/disconnect"); //Disconnects from SuperCollider method
    oscSender.disconnect();
}

bool CMProjectAudioProcessor::sendLfoTargetOSC (const juce::String& param,
                                                float min, float max)
{
    juce::OSCMessage msg("/lfoTarget", juce::String(param), min, max);
    return oscSender.send (msg);     // return true / false to caller

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

//function that sends messages to supercollider uplaoding the granulator
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

        oscSender.send("/grain",
            grainDur.load(),
            grainPos.load(),
            cutoff.load(),
            density.load(),
            pitch.load()
           );
        oscSender.send("/lfoRate", lfoRate.load());
        
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
    
    grainDur.store(0.02f);
    grainPos.store(0.0f); 
    cutoff.store(3000.0f); 
    density.store(0.001f); 
    pitch.store(1.0f); 
    reverse.store(0.0); 
    lfoRate.store(0.0f); 

}


//==============================================================================
void CMProjectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    //formatManager.registerBasicFormats(); //Register WAV, AIFF, MP3 support

    // SuperCollider bind
    if (!oscSender.connect("127.0.0.1", 57121))
        DBG("Could not connect to SuperCollider");
    else
        DBG("Connected to SuperCollider via OSC");
    
    if(!processingSender.connect("127.0.0.1", 9003))
        DBG("Could not connect to Processing");
    else
        DBG("Connected to SuperCollider via OSC");
        
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

    //MIDI → OSC handling
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            auto note = msg.getNoteNumber(); // 0–127
            auto vel = msg.getVelocity() / 127.0f; // normalized 0.0–1.0
            oscSender.send("/start", note, vel); //send it to supercollider
        }
        else if (msg.isNoteOff())
        {
            auto note = msg.getNoteNumber();
            oscSender.send("/stop", note); //send it to supercollider
        }
        else if (msg.isController())
        {
            // if you want CCs:
            oscSender.send("/cc",
                msg.getControllerNumber(),
                msg.getControllerValue());
        }
        else if (msg.isPitchWheel()) {
            //PitchWheel algorithm
            int raw = msg.getPitchWheelValue();
            float norm = (raw - 8192) / 8192.0f;   // → –1.0 … +1.0
            oscSender.send("/pitchWheel", norm);
        }

        if (isRecordingMidi)
        {
            juce::MidiBuffer::Iterator it(midiMessages);
            juce::MidiMessage msg;
            int samplePos;
            while (it.getNextEvent(msg, samplePos))
                recordedSequence.addEvent(msg);
        }
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
                    float val = in[playbackPositions[track]] * trackVolumes[track];
                    out[i] += in[playbackPositions[track]] * trackVolumes[track];
                }

                playbackPositions[track]++;
            }
            if (playbackPositions[track] >= sampleLength)
                triggerPlayback[track] = false;
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

