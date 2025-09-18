import cv2
import mediapipe as mp
import math
import threading
from pythonosc import udp_client, dispatcher, osc_server

import subprocess
import os
import platform
import shutil

from pathlib import Path

def launch_processing_sketch() -> subprocess.Popen | None:
    """
    Avvia uno sketch Processing da Python, sia su macOS che su Linux/Windows.
    """
    # 1) directory di questo script
    base_dir = Path(__file__).resolve().parent

    # 2) cartella dello sketch (non il .pde)
    sketch_dir = (base_dir / "../Processing/hand_filter").resolve()
    if not sketch_dir.is_dir():
        raise FileNotFoundError(f"La cartella dello sketch non esiste:\n  {sketch_dir}")

    # 3) Enhanced Processing detection for macOS
    processing_cmd = None
    
    if platform.system() == "Darwin":
        # Try multiple common Processing locations on macOS
        possible_paths = [
            "/Applications/Processing.app/Contents/MacOS/processing-java",
            "/Applications/Processing 4/Processing.app/Contents/MacOS/processing-java", 
            "/usr/local/bin/processing-java",
            shutil.which("processing-java")
        ]
        
        for path in possible_paths:
            if path and Path(path).exists():
                processing_cmd = str(path)
                break
    else:
        # Non-macOS systems
        processing_cmd = shutil.which("processing-java")

    if processing_cmd is None:
        raise FileNotFoundError(
            "processing-java not found. Install Processing or add "
            "processing-java to PATH.\n"
            "Example (macOS Homebrew):\n"
            "  brew install --cask processing\n"
            "  ln -s /Applications/Processing.app/Contents/MacOS/processing-java "
            "/usr/local/bin/processing-java"
        )

    # 4) Make sure the sketch directory path is absolute and properly formatted
    sketch_path = str(sketch_dir.resolve())
    
    # 5) Build command with explicit parameters
    cmd = [
        processing_cmd,
        f"--sketch={sketch_path}",
        "--run"
    ]

    print("Processing command:", " ".join(cmd))
    print("Sketch directory:", sketch_path)
    print("Working directory:", os.getcwd())
    
    try:
        # Set environment variables that might be missing when run from Xcode app
        env = os.environ.copy()
        
        # Add common paths that might be missing
        if platform.system() == "Darwin":
            path_additions = [
                "/usr/local/bin",
                "/opt/homebrew/bin", 
                "/Applications/Processing.app/Contents/MacOS"
            ]
            current_path = env.get("PATH", "")
            for path_add in path_additions:
                if path_add not in current_path:
                    env["PATH"] = f"{current_path}:{path_add}"
        
        # Use absolute path for working directory
        proc = subprocess.Popen(
            cmd, 
            shell=False,
            env=env,
            cwd=str(base_dir),  # Set working directory explicitly
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        print("Processing sketch launched with PID", proc.pid)
        
        # Check if process started successfully
        try:
            # Wait a short time to see if process exits immediately with error
            exit_code = proc.poll()
            if exit_code is not None:
                stdout, stderr = proc.communicate()
                print(f"Process exited immediately with code {exit_code}")
                print(f"STDOUT: {stdout.decode()}")
                print(f"STDERR: {stderr.decode()}")
                return None
        except:
            pass
            
        return proc

    except Exception as e:
        print("Error launching Processing sketch:", e)
        print("Command that failed:", " ".join(cmd))
        return None


# === OSC CLIENTS ===

# JUCE link
client = udp_client.SimpleUDPClient("127.0.0.1", 9001)

# Processing GUI
processing_client = udp_client.SimpleUDPClient("127.0.0.1", 9003)

# === GLOBAL STATE ===
finger_to_param = [None, None, None, None]  # Index, Middle, Ring, Pinky
finger_to_drum = [None, None, None, None]  # default mapping: right-index, right-middle, left-index, left-middle

sample_duration = 10.0  # default fallback
freeze_parameters = False
active_page = "synth"


last_pinched = [False, False, False, False]  # index 0→R-index, 1→R-middle, 2→L-index, 3→L-middle
pinch_threshold = 0.05
release_threshold = 0.08


# Default values for all 6 parameters (minimum values)

current_values = {
    "GrainDur":    0.02,
    "GrainPos":    0.01,
    "GrainCutOff": 3000.0,
    "GrainDensity":0.8,
    "GrainPitch":  1.0,
    "GrainReverse":0.0,
    "lfoRate":     0.0
}
            

# === OSC SERVER HANDLER ===
def handle_finger_assignments(address, *args):
    global finger_to_param
    finger_to_param = list(args)
    print("Updated finger assignments:", finger_to_param)
    
def handle_sample_duration(address, duration):
    global sample_duration
    sample_duration = duration
    print("Received new sample duration:", sample_duration)

def handle_active_page(address, page):
    global active_page
    active_page = page
    print("Active page is now:", active_page)

def handle_drum_assignments(address, *args):
    global finger_to_drum
    for i, val in enumerate(args[:4]):
        if val >= 0:
            print(finger_to_drum)
            finger_to_drum[i] = int(val)
            print("Dio cane: " , finger_to_drum)
        else:                            # NEW: clear that finger
            finger_to_drum[i] = None

    print("Updated finger drum mapping:", finger_to_drum)

def handle_reset_parameters(address, *args):

    global freeze_parameters, current_values

    freeze_parameters = False
    
    # Re-initialise the same dictionary object so every reference sees the change
    current_values.update({
        "GrainDur":     0.02,
        "GrainPos":     0.01,
        "GrainCutOff":  3000.0,
        "GrainDensity": 0.8,
        "GrainPitch":   1.0,
        "GrainReverse": 0.0,
        "lfoRate":      0.0,
    })

    client.send_message("/handGrain", list(current_values.values()))
    

# === OSC SERVER SETUP ===
osc_disp = dispatcher.Dispatcher()
osc_disp.map("/fingerParameters", handle_finger_assignments)
osc_disp.map("/sampleDuration", handle_sample_duration)
osc_disp.map("/activePage", handle_active_page)
osc_disp.map("/fingerDrums", handle_drum_assignments)
osc_disp.map("/resetParameters", handle_reset_parameters)


server = osc_server.ThreadingOSCUDPServer(("127.0.0.1", 9002), osc_disp)
print(" Python OSC Server listening on port 9002")

osc_thread = threading.Thread(target=server.serve_forever)
osc_thread.daemon = True
osc_thread.start()

# === MEDIAPIPE SETUP ===
mp_hands = mp.solutions.hands
hands = mp_hands.Hands(max_num_hands=2, min_detection_confidence=0.7)
mp_draw = mp.solutions.drawing_utils

# === CAMERA ===
cap = cv2.VideoCapture(0)

# === LOGIC FUNCTIONS ===
def posmap(x, in_min, in_max, out_min=0.0, out_max=sample_duration, power=2.5):
    # Normalizza tra 0 e 1
    norm = (x - in_min) / (in_max - in_min)
    norm = max(0.0, min(1.0, norm))  # Clamp

    # Applica curva non lineare
    curved = norm ** power

    # Mappa sull’intervallo di uscita
    return out_min + curved * (out_max - out_min)

def linmap(x, in_min, in_max, out_min, out_max, power=2.5):
    # Normalizza tra 0 e 1
    norm = (x - in_min) / (in_max - in_min)
    norm = max(0.0, min(1.0, norm))  # Clamp

    # Applica curva non lineare
    curved = norm ** power

    # Mappa sull’intervallo di uscita
    return out_min + curved * (out_max - out_min)

def is_fist(landmarks):
    finger_tips = [8, 12, 16, 20]
    finger_pips = [6, 10, 14, 18]
    return all(landmarks[tip].y > landmarks[pip].y for tip, pip in zip(finger_tips, finger_pips))

def trigger_drum_finger(finger_index):
    sample_index = finger_to_drum[finger_index]
    if sample_index is not None:
        client.send_message("/triggerDrum", sample_index)


processing_proc = launch_processing_sketch()

# === MAIN LOOP ===
while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.flip(frame, 1)
    img = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = hands.process(img)

    if results.multi_hand_landmarks and results.multi_handedness:

        left_is_fist = False

        for hand_landmarks, hand_handedness in zip(results.multi_hand_landmarks, results.multi_handedness):
            label = hand_handedness.classification[0].label  # "Left" or "Right"
            landmarks = hand_landmarks.landmark
            hand_index = 0 if label == "Left" else 1

            # Send hand points to Processing
            for i, lm in enumerate(landmarks):
                processing_client.send_message(f"/hand/{hand_index}/{i}", [lm.x, lm.y])

            mp_draw.draw_landmarks(frame, hand_landmarks, mp_hands.HAND_CONNECTIONS)

            # === DRUM PAGE ===
            if active_page == "drum":
                #print("we are in drum page")
                thumb = landmarks[4]
                index = landmarks[8]
                middle = landmarks[12]

                dist_index = math.hypot(index.x - thumb.x, index.y - thumb.y)
                dist_middle = math.hypot(middle.x - thumb.x, middle.y - thumb.y)

                if label == "Right":
                   # print("we are in right")
                    # Finger 0 = Right Index
                    if dist_index < pinch_threshold and not last_pinched[0]:
                      
                        trigger_drum_finger(0)
                        last_pinched[0] = True
                    elif dist_index > release_threshold and last_pinched[0]:
                        last_pinched[0] = False

                    # Finger 1 = Right Middle
                    if dist_middle < pinch_threshold and not last_pinched[1]:
                        trigger_drum_finger(1)
                        last_pinched[1] = True
                    elif dist_middle > release_threshold and last_pinched[1]:
                        last_pinched[1] = False

                elif label == "Left":
                    # Finger 2 = Left Index
                    if dist_index < pinch_threshold and not last_pinched[2]:
                        trigger_drum_finger(2)
                        last_pinched[2] = True
                    elif dist_index > release_threshold and last_pinched[2]:
                        last_pinched[2] = False

                    # Finger 3 = Left Middle
                    if dist_middle < pinch_threshold and not last_pinched[3]:
                        trigger_drum_finger(3)
                        last_pinched[3] = True
                    elif dist_middle > release_threshold and last_pinched[3]:
                        last_pinched[3] = False


            # === SYNTH PAGE ===
            elif active_page == "synth":
                #print("we are in synth page")
                if label == "Left" and is_fist(landmarks):
                    left_is_fist = True
                    freeze_parameters = True
                    continue

                elif label == "Right":
                    # Compute distances from thumb to each fingertip
                    thumb = landmarks[4]
                    index_dist = math.hypot(landmarks[8].x - thumb.x, landmarks[8].y - thumb.y)
                    middle_dist = math.hypot(landmarks[12].x - thumb.x, landmarks[12].y - thumb.y)
                    ring_dist = math.hypot(landmarks[16].x - thumb.x, landmarks[16].y - thumb.y)
                    pinky_dist = math.hypot(landmarks[20].x - thumb.x, landmarks[20].y - thumb.y)

                    values = [index_dist, middle_dist, ring_dist, pinky_dist]

                    if not freeze_parameters:
                        # Update only the parameters controlled by each finger
                        for finger_id, param in enumerate(finger_to_param):
                            dist = values[finger_id]
                            if param == "GrainDur":
                                max_grain_dur = min(0.5, sample_duration * 0.1)
                                current_values["GrainDur"] = linmap(dist, 0.02, 0.70, 0.005, max_grain_dur, power=2.5)
                            elif param == "GrainPos":
                                current_values["GrainPos"] = posmap(dist, 0.02, 0.70, 0.0, sample_duration, power=2.5)
                            elif param == "GrainCutOff":
                                current_values["GrainCutOff"] = linmap(dist, 0.02, 0.70, 50, 15000, power=2.5)
                            elif param == "GrainDensity":
                                current_values["GrainDensity"] = linmap(dist, 0.02, 0.70, 0.005, 5, power=2.5)
                            elif param == "GrainPitch":
                                current_values["GrainPitch"] = linmap(dist, 0.02, 0.70, -12, +12, power=2.5)
                            elif param == "GrainReverse":
                                current_values["GrainReverse"] = 1.0 if dist < 0.05 else 0.0
                            elif param == "lfoRate":
                                current_values["lfoRate"] = linmap(dist, 0.02, 0.70, 100, 20000, power=2.5)

                # Only send synth params if on synth page
                if active_page == "synth" and not freeze_parameters:
                    
                    client.send_message("/handGrain", [
                        current_values["GrainDur"],
                        current_values["GrainPos"],
                        current_values["GrainCutOff"],
                        current_values["GrainDensity"],
                        current_values["GrainPitch"],
                        current_values["GrainReverse"],
                        current_values["lfoRate"]
                    ])

                    processing_client.send_message("/handGrain", [
                    current_values["GrainDur"],
                    current_values["GrainPos"],
                    current_values["GrainCutOff"],
                    current_values["GrainDensity"],
                    current_values["GrainPitch"],
                    current_values["GrainReverse"],
                    current_values["lfoRate"],
                ])

        # Unfreeze if hand is open again
        if active_page == "synth" and not left_is_fist:
            freeze_parameters = False

        

# Show window
# cv2.imshow("Hand Tracker", frame)
# if cv2.waitKey(1) & 0xFF == ord('q'):
#     break
# if cv2.getWindowProperty("Hand Tracker", cv2.WND_PROP_VISIBLE) < 1:
#     break

if processing_proc is not None:
    processing_proc.terminate()

cap.release()

#cv2.destroyAllWindows()
