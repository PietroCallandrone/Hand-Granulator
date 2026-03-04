/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <vector>

//==============================================================================
/**
*/
class CMProjectAudioProcessor  : public juce::AudioProcessor,
                                 public juce::OSCReceiver::ListenerWithOSCAddress<juce::OSCReceiver::MessageLoopCallback>
{
public:
    //==============================================================================
    CMProjectAudioProcessor();
    ~CMProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void startMidiRecording() noexcept { recordedSequence.clear(); isRecordingMidi = true; }
    void stopMidiRecording() noexcept { isRecordingMidi = false; }
    bool saveMidiRecording(const juce::File& file);

    juce::String fingerControls[4] = { {}, {}, {}, {} };
    juce::String fingerDrumMapping[4] = { {}, {}, {}, {} }; // default R-idx,R-mid,L-idx,L-mid

    juce::OSCSender senderToPython;
    
    void sendFingerAssignementsOSC();
    void sendFingerDrumMappingOSC();

    //Getter methods for GUI update
    float getGrainDur() const { return grainDur.load(); }
    float getGrainPos() const { return grainPos.load(); }
    float getCutoff() const { return cutoff.load(); }
    float getDensity() const { return density.load(); }
    float getPitch() const { return pitch.load(); }
    float getReverse() const { return reverse.load(); }
    
    void setGrainDur(float x)  { grainDur.store(x); }
    void setGrainPos(float x) { grainPos.store(x); }
    void setCutoff(float x)  { cutoff.store(x);}
    void setDensity(float x)  { density.store(x);}
    void setPitch(float x) { pitch.store(x); }
    void setReverse(float x)  { reverse.store(x);}

    void updateParameters();
    void loadSynthSample(const juce::File& file);
    void startManualSynthNote(int noteNumber, float velocity);
    void stopManualSynthNote(int noteNumber);
    void setCurrentBpm(float bpm) { currentBpm.store(juce::jmax(1.0f, bpm)); }

    juce::OSCSender processingSender;
    

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CMProjectAudioProcessor)
    
   
    juce::OSCReceiver oscReceiver;
    
    bool isRecordingMidi = false;
    juce::MidiMessageSequence recordedSequence;

    juce::AudioFormatManager formatManager;
    std::array<juce::AudioBuffer<float>, 4> drumSamples;
    std::array<bool, 4> sampleLoaded = { false, false, false, false };
    std::array<int, 4> playbackPositions = { 0, 0, 0, 0 };
    std::array<bool, 4> triggerPlayback = { false, false, false, false };
    std::mutex sampleMutex; // optional but safe

    // GUI update parameters
    std::atomic<float> grainDur{ 0.06f };
    std::atomic<float> grainPos{ 0.01f };
    std::atomic<float> cutoff{ 8000.0f };
    std::atomic<float> density{ 0.8f };
    std::atomic<float> pitch{ 0.0f };
    std::atomic<float> reverse{ 0.0f };
    std::atomic<float> currentBpm{ 120.0f };
    

public:
    void loadSampleForTrack(int trackIndex, const juce::File& file);
    void triggerSamplePlayback(int trackIndex);
    void oscMessageReceived(const juce::OSCMessage& message) override;
    std::array<float, 4> trackVolumes = { 1.0f, 1.0f, 1.0f, 1.0f };
    juce::AudioBuffer<float> synthSample;
    bool synthSampleLoaded = false;
    std::mutex synthSampleMutex;
    double currentSampleRate = 44100.0;
    double samplesUntilNextGrain = 0.0;
    int heldSynthNotes = 0;
    float synthVelocity = 1.0f;
    float currentPitchRatio = 1.0f;
    float pitchWheelSemitones = 0.0f;

    struct Grain
    {
        double samplePos = 0.0;
        double sampleStep = 1.0;
        int remainingSamples = 0;
        int totalSamples = 0;
        float gain = 0.0f;
        float lowpassState = 0.0f;
    };

    std::vector<Grain> activeGrains;

    void spawnGrain();
    



};
