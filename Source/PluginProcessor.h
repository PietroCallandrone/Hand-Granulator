/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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
    float getlfoRate() const{ return lfoRate.load(); }
    
    void setGrainDur(float x)  { grainDur.store(x); }
    void setGrainPos(float x) { grainPos.store(x); }
    void setCutoff(float x)  { cutoff.store(x);}
    void setDensity(float x)  { density.store(x);}
    void setPitch(float x) { pitch.store(x); }
    void setReverse(float x)  { reverse.store(x);}
    void setlfoRate(float x) { lfoRate.store(x); }

    void updateParameters();

    bool sendLfoTargetOSC (const juce::String& param,
                               float min = 0.0f,
                               float max = 1.0f);
    juce::OSCSender processingSender;
    juce::OSCSender oscSender;
    

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
    std::atomic<float> grainDur{ 0.0f };
    std::atomic<float> grainPos{ 0.0f };
    std::atomic<float> cutoff{ 0.0f };
    std::atomic<float> density{ 0.0f };
    std::atomic<float> pitch{ 0.0f };
    std::atomic<float> reverse{ 0.0f };
    std::atomic<float> lfoRate{0.0f};
    std::atomic<float> currentBpm{ 120.0f };
    

public:
    void loadSampleForTrack(int trackIndex, const juce::File& file);
    void triggerSamplePlayback(int trackIndex);
    void oscMessageReceived(const juce::OSCMessage& message) override;
    std::array<float, 4> trackVolumes = { 1.0f, 1.0f, 1.0f, 1.0f };
    



};
