# HAND GRANULATOR

**HAND GRANULATOR** is a real-time audio plugin that combines granular synthesis and gesture-based control into a single, intuitive interface. Built using **JUCE** for the plugin framework, **SuperCollider** for sound synthesis and **Processing** for camera-based hand tracking, the plugin allows users to interact with audio in a dynamic and performative way.

![Screenshot 2025-05-24 124838](https://github.com/user-attachments/assets/3fd26fe8-7e22-4e70-8751-aafa27e21886)

---

![Screenshot 2025-05-22 172413](https://github.com/user-attachments/assets/45863c3c-180a-4508-a8f0-0eae5f02c844)



The plugin features two main pages:

- **Synth Page**: for sample-based granular synthesis with gestural control
- **Drum Page**: for hand-triggered rhythm creation via an integrated step sequencer

---


## Synth Page

On the Synth page users can load a sample, trigger it via MIDI or play it directly using a GUI button, and apply granular synthesis to model the sound. The core innovation lies in the modulation system: rather than using conventional knobs or sliders, the plugin uses the computer’s webcam to track the user’s right hand in real time. Through this system each granular parameter (**grain position**, **duration**, **density**, **pitch**, **LFO amount**, and **filter cutoff**) can be mapped to one of four fingers (index to pinky), while the **reverse** toggle adds further variation. To assign a parameter, the user first clicks on the parameter’s symbol button in the GUI, then selects the desired finger by clicking its on-screen representation. Once assigned, the value of each parameter is modulated by changing the distance between that finger and the thumb, enabling expressive and continuous control. A reset button is also included to reset all the current parameters value and closing the left hand into a fist, currently assigned parameters will be freezed, allowing the user to temporarily hold their state during modulation.

**Up to four parameters can be modulated simultaneously**, each assigned to a different finger and controlled independently in real time. This allows highly expressive sound manipulation using only hand gestures. Additionally, there's a dynamic ADSR envelope visualizer that reflects the amplitude shaping of the loaded sample, offering real-time feedback. Users can reset all finger-parameter assignments with the "clear-fingers" command, streamlining the creative process. An auxiliary LFO button is available to assign low-frequency modulation to any of the parameters and projected icons appears at the fingertips of each assigned parameter, providing immediate visual identification of the mapping during performance.

To use the Synth page begin by loading a sample, where you can then either play it manually via the **Play** button or trigger it using a connected **MIDI device** (you can also save and export the played midi). Once the sample is active, click on any of the parameter buttons (e.g., position, pitch, duration) and then select a finger (index to pinky) to assign it: moving that finger closer or farther from the thumb changes its value continuously and you can repeat this process up to four parameters, enabling complex, multi-dimensional modulation with nothing but hand motion.

---

## Drum Page

The Drum page integrates a four-track step sequencer, where each track can be loaded with a custom drum sample. Each row corresponds to a separate sound and the step sequencer provides 16 steps that can be manually activated via mouse click. Once activated, the sample will be triggered in sync with the internal or external clock, creating rhythmic patterns in real time. In addition to manual control, the plugin also enables **gesture-based triggering** using both hands. Specifically, the user can assign the **index and middle fingers** of each hand (up to four fingers total) to trigger sounds: when a selected finger touches its respective thumb it triggers the sample associated with that row.

Each drum row is equipped with dedicated controls: a **gain knob** to adjust the output volume, a **mute button** to silence the track and a **visual indicator light** that confirms whether a sample is currently loaded for that row. The light serves as a quick reference during live sessions, ensuring you always know which drum slots are active. To map a finger to a specific drum row, a **hand icon button** is available at the end of each row: clicking this button enters assignment mode; then, by clicking on one of the four on-screen finger tips, the row is linked to that finger. This setup allows for fast, natural triggering of samples using physical gestures, with full visual and audio feedback directly within the plugin interface.

To use the Drum page begin by loading samples into the available four tracks and here you can then build your beat by clicking directly on the steps in the sequencer grid. Alternatively, you can assign a finger (index or middle of either hand) to a specific track by first clicking the hand button next to the track, and then selecting the desired fingertip on screen. Once assigned, tapping that finger against the thumb will play the selected sample in real time, enabling the user to experiment different combinations of samples togheter in real time. You can also adjust the volume of each sample using the gain knob, mute it instantly and always see whether the track is loaded thanks to the built-in status light.

---

## Global Features and Synchronization

Both the Synth and Drum pages share a **synchronized BPM system**, allowing you to control the tempo of the entire plugin from a single value. This BPM setting is persistent across pages, meaning that if you switch from the Synth page to the Drum page (or vice versa), any sound already playing will continue seamlessly, maintaining rhythm and temporal alignment. This makes possible to craft **complex, evolving melodies** on the Synth page while simultaneously building **rhythmic layers** on the Drum page, resulting in a cohesive and immersive audio experience.

At the bottom-right corner of the interface, a small **parameter display** provides live feedback, showing which parameter has been selected whenever you click on it, helping you to keep track of your modulation setup and preventing confusion during performance or sound design. Additionally, it’s important to note that in order to assign parameters to your fingers—whether on the Synth or Drum page—you must **start the camera** first, so the webcam feed is essential for enabling gesture recognition and activating the finger-mapping functionality, ensuring smooth and accurate control over all real-time interactions.

---

## Webcam Visual Interaction

Once the camera is activated, the user is not presented with a simple webcam feed but instead, the interface renders a **stylized, abstract visualization of the hands**, rather than the user’s full image. This visualization focuses entirely on the tracked hand landmarks, displaying them through a dynamic and artistic representation with multiple visual effects and animations that respond in real time to hand movement and parameter modulation, enhancing the connection between physical gesture and sound manipulation.

This approach is not only functional, it also adds a **performative and immersive layer** to the experience because as each finger controls a different synthesis or rhythmic parameter, the corresponding visual feedback makes modulation gestures immediately recognizable and the result is a visually rich and musically expressive interface, where the motion of the hands becomes part of the performance itself.


<img width="752" alt="Screenshot 2025-05-22 alle 18 11 11" src="https://github.com/user-attachments/assets/91f56a7f-3128-4445-9233-5a4815c37277" />

---

## System Architecture and Communication

HAND GRANULATOR operates through a coordinated interaction between four core technologies: **JUCE**, **SuperCollider**, **Python**, and **Processing**, each with a specific role in the system: 

when a user loads a sample, JUCE sends an OSC message to SuperCollider to **configure and instantiate the SynthDef**, setting up the selected sample for real-time granular synthesis. From this point on, SuperCollider is responsible for generating the sound and updating it according to incoming control values.

As the user begins to modulate parameters with their hand, JUCE communicates with the **Python hand-tracking script**, sending it information about which finger is being monitored. Python, using computer vision, processes the hand gestures and sends back continuous values (such as finger-to-thumb distances) to JUCE, that translates these values into corresponding granular parameters (e.g., grain position, pitch, duration) and forwards them to SuperCollider, which **updates the running synthesizer in real time** without stopping the playback.

Each component in the system has a clearly defined role: **Python handles the hand-tracking logic**, **JUCE manages the plugin interface and all parameter mapping/effect logic**, **SuperCollider performs the audio synthesis**, and **Processing renders a dynamic, stylized visual representation of the user’s hands**, reacting in real time to both movement and parameter modulation. This modular yet tightly integrated architecture allows for a smooth and expressive performance experience, turning gestures into sound with immediacy and visual feedback.

Made by Pietro Callandrone, Lorenzo Bianco, Alessandro Lillo and Flavio De Lellis
