import cv2
import numpy as np
import paho.mqtt.client as mqtt
import threading
import time
import queue
import logging
import platform
from dataclasses import dataclass
from typing import Optional, Tuple
from scipy.spatial import distance as dist

from mediapipe.python.solutions import face_mesh as mp_face_mesh
from mediapipe.python.solutions import hands as mp_hands
from mediapipe.python.solutions import drawing_utils as mp_draw
from mediapipe.python.solutions import drawing_styles as mp_styles

logging.basicConfig(level=logging.INFO, format="[%(asctime)s] [%(levelname)-8s] %(message)s")
log = logging.getLogger("DriverMonitor")

IS_WINDOWS = platform.system() == "Windows"
CAM_BACKEND = cv2.CAP_DSHOW if IS_WINDOWS else cv2.CAP_V4L2

@dataclass
class Config:
    CAMERA_INDEX:  int = 0
    FRAME_WIDTH:   int = 640
    FRAME_HEIGHT:  int = 480
    CAMERA_FPS:    int = 30
    QUEUE_SIZE:    int = 2

    BROKER_HOST:     str = "broker.emqx.io"
    BROKER_PORT:     int = #port here
    MQTT_TOPIC:      str = "" #topic here
    MQTT_CLIENT_ID:  str = "jetson_tx2_vision_pub"


    EAR_THRESHOLD:    float = 0.20  
    DROWSY_CONSEC_SEC: float = 2.0

    GAZE_LEFT_THRESH:  float = 0.43 # Decreases when looking left
    GAZE_RIGHT_THRESH: float = 0.51 # Increases when looking right
    IND_CONSEC_SEC:    float = 0.4  # Debounce timer 

    HUD_FONT: int = cv2.FONT_HERSHEY_SIMPLEX

CFG = Config()

# Landmarks
LEFT_EYE_INDICES  = [362, 385, 387, 263, 373, 380] #  Left Eye
RIGHT_EYE_INDICES = [33,  160, 158, 133, 153, 144] #  Right Eye


LEFT_IRIS = 473  
RIGHT_IRIS = 468 

FINGER_TIPS = [4, 8, 12, 16, 20]
FINGER_PIPS = [3, 6, 10, 14, 18]

class ThreadedVideoCapture:
    def __init__(self, index, width, height, fps):
        self._cap = cv2.VideoCapture(index, CAM_BACKEND)
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self._cap.set(cv2.CAP_PROP_FPS, fps)
        self._queue = queue.Queue(maxsize=CFG.QUEUE_SIZE)
        self._stopped = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        while not self._stopped.is_set():
            ret, frame = self._cap.read()
            if not ret: continue
            if not self._queue.empty():
                try: self._queue.get_nowait()
                except queue.Empty: pass
            self._queue.put(frame)

    def read(self):
        try: return self._queue.get(timeout=0.05)
        except queue.Empty: return None

    def stop(self):
        self._stopped.set()
        self._cap.release()

class MQTTPublisher:
    def __init__(self):
        try:
            self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CFG.MQTT_CLIENT_ID)
        except AttributeError:
            self._client = mqtt.Client(client_id=CFG.MQTT_CLIENT_ID)
        self._connected = False
        self._connect()

    def _connect(self):
        try:
            self._client.connect(CFG.BROKER_HOST, CFG.BROKER_PORT, 60)
            self._client.loop_start()
            self._connected = True
        except Exception as e: log.error(f"MQTT Error: {e}")

    def publish(self, payload):
        if self._connected:
            self._client.publish(CFG.MQTT_TOPIC, payload, qos=1)
            log.info(f"Published: {payload}")

    def disconnect(self):
        self._client.loop_stop()
        self._client.disconnect()

@dataclass
class SystemState:
    alarm: str = "ALARM_OFF"
    indicator: str = "IND_OFF"
    headlights: str = "OFF"

class StateManager:
    def __init__(self, publisher):
        self._pub = publisher
        self._state = SystemState()

    def update(self, alarm, indicator, headlights):
        if alarm != self._state.alarm:
            self._state.alarm = alarm
            self._pub.publish(alarm)
        if indicator != self._state.indicator:
            self._state.indicator = indicator
            self._pub.publish(indicator)
        if headlights != self._state.headlights:
            self._state.headlights = headlights
            self._pub.publish(headlights)

    @property
    def current(self): return self._state

# FN
def eye_aspect_ratio(landmarks, eye_indices, w, h):
    pts = np.array([(landmarks[i].x * w, landmarks[i].y * h) for i in eye_indices])
    v1 = dist.euclidean(pts[1], pts[5])
    v2 = dist.euclidean(pts[2], pts[4])
    ho = dist.euclidean(pts[0], pts[3])
    return (v1 + v2) / (2.0 * ho + 1e-6)

def gaze_ratio(landmarks, iris_idx, left_corner_idx, right_corner_idx, w, h):
    iris = np.array([landmarks[iris_idx].x * w, landmarks[iris_idx].y * h])
    left_corner = np.array([landmarks[left_corner_idx].x * w, landmarks[left_corner_idx].y * h])
    right_corner = np.array([landmarks[right_corner_idx].x * w, landmarks[right_corner_idx].y * h])
    
    dist_to_left = dist.euclidean(iris, left_corner)
    total_width = dist.euclidean(left_corner, right_corner)
    return dist_to_left / (total_width + 1e-6)

def count_extended_fingers(hand_landmarks):
    lm = hand_landmarks.landmark
    wrist = np.array([lm[0].x, lm[0].y])
    count = 0

    tip = np.array([lm[FINGER_TIPS[0]].x, lm[FINGER_TIPS[0]].y])
    pip = np.array([lm[FINGER_PIPS[0]].x, lm[FINGER_PIPS[0]].y])
    if dist.euclidean(tip, wrist) > dist.euclidean(pip, wrist): count += 1

    for tip_idx, pip_idx in zip(FINGER_TIPS[1:], FINGER_PIPS[1:]):
        tip = np.array([lm[tip_idx].x, lm[tip_idx].y])
        pip = np.array([lm[pip_idx].x, lm[pip_idx].y])
        if dist.euclidean(tip, wrist) > dist.euclidean(pip, wrist): count += 1
    return count

def main():
    cap = ThreadedVideoCapture(CFG.CAMERA_INDEX, CFG.FRAME_WIDTH, CFG.FRAME_HEIGHT, CFG.CAMERA_FPS)
    pub = MQTTPublisher()
    mgr = StateManager(pub)

    face_mesh_model = mp_face_mesh.FaceMesh(max_num_faces=1, refine_landmarks=True, min_detection_confidence=0.5)
    hands_model = mp_hands.Hands(max_num_hands=1, min_detection_confidence=0.6)

    drowsy_start = None
    ind_start = None
    target_ind_state = "IND_OFF"
    
    # 
    live_gaze = 0.5
    live_fingers = 0

    while True:
        frame = cap.read()
        if frame is None: continue

        frame = cv2.flip(frame, 1)
        h, w = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        face_results = face_mesh_model.process(rgb)
        hand_results = hands_model.process(rgb)

        alarm_state = "ALARM_OFF"
        indicator_state = mgr.current.indicator
        headlights_state = mgr.current.headlights

        # 1. FACE
        if face_results.multi_face_landmarks:
            lm = face_results.multi_face_landmarks[0].landmark
            
            # Drowsiness Detector
            ear_l = eye_aspect_ratio(lm, LEFT_EYE_INDICES, w, h)
            ear_r = eye_aspect_ratio(lm, RIGHT_EYE_INDICES, w, h)
            ear_avg = (ear_l + ear_r) / 2.0

            if ear_avg < CFG.EAR_THRESHOLD:
                if drowsy_start is None: drowsy_start = time.time()
                if (time.time() - drowsy_start) >= CFG.DROWSY_CONSEC_SEC:
                    alarm_state = "ALARM_ON"
            else:
                drowsy_start = None

            # Gaze 
            if len(lm) >= 478:
                ratio_l = gaze_ratio(lm, LEFT_IRIS, 362, 263, w, h)
                ratio_r = gaze_ratio(lm, RIGHT_IRIS, 33, 133, w, h)
                live_gaze = (ratio_l + ratio_r) / 2.0

                raw_ind = "IND_OFF"
                if live_gaze < CFG.GAZE_LEFT_THRESH: raw_ind = "IND_LEFT"
                elif live_gaze > CFG.GAZE_RIGHT_THRESH: raw_ind = "IND_RIGHT"

                if raw_ind != target_ind_state:
                    target_ind_state = raw_ind
                    ind_start = time.time()
                elif ind_start and (time.time() - ind_start) >= CFG.IND_CONSEC_SEC:
                    indicator_state = target_ind_state
        else:
            drowsy_start = None

        # HAND TRACKING
        live_fingers = 0
        if hand_results.multi_hand_landmarks:
            hand_lm = hand_results.multi_hand_landmarks[0]
            mp_draw.draw_landmarks(frame, hand_lm, mp_hands.HAND_CONNECTIONS)
            
            live_fingers = count_extended_fingers(hand_lm)
            
            if live_fingers >= 4: headlights_state = "ON"
            elif live_fingers <= 1: headlights_state = "OFF"

        mgr.update(alarm_state, indicator_state, headlights_state)

        # UI
        cv2.rectangle(frame, (0, 0), (320, 160), (20, 20, 20), -1)
        cv2.putText(frame, f"EAR    : {ear_avg:.2f}", (10, 30), CFG.HUD_FONT, 0.6, (0, 255, 0), 2)
        cv2.putText(frame, f"GAZE   : {live_gaze:.2f}", (10, 60), CFG.HUD_FONT, 0.6, (255, 255, 0), 2)
        cv2.putText(frame, f"FINGERS: {live_fingers}", (10, 90), CFG.HUD_FONT, 0.6, (255, 100, 100), 2)
        
        cv2.putText(frame, f"IND: {indicator_state}", (10, 120), CFG.HUD_FONT, 0.6, (0, 255, 255) if indicator_state != "IND_OFF" else (150,150,150), 2)
        cv2.putText(frame, f"HEADLIGHTS: {headlights_state}", (10, 150), CFG.HUD_FONT, 0.6, (0, 255, 0) if headlights_state == "ON" else (0,0,255), 2)

        if alarm_state == "ALARM_ON":
            cv2.putText(frame, "WAKE UP!", (180, 250), CFG.HUD_FONT, 2.0, (0, 0, 255), 4)

        cv2.imshow("Dashboard Monitor", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'): break

    cap.stop()
    pub.disconnect()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
