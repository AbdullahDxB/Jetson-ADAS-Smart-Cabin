<h1 align="center">Edge AI-Enabled ADAS & Touchless Cabin Control</h1>

<p align="center">
  <b>Submission for the Bharat AI-SoC Student Challenge (Hardware-Software Integration)</b><br>
  <i>Decentralized Cyber-Physical System (CPS) for Automotive Environments</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/NVIDIA_Jetson-76B900?style=for-the-badge&logo=nvidia&logoColor=white" alt="NVIDIA Jetson"/>
  <img src="https://img.shields.io/badge/Python_3-3776AB?style=for-the-badge&logo=python&logoColor=white" alt="Python 3"/>
  <img src="https://img.shields.io/badge/OpenCV_DSP-5C3EE8?style=for-the-badge&logo=opencv&logoColor=white" alt="OpenCV"/>
  <img src="https://img.shields.io/badge/MQTT_EMQX-3C5280?style=for-the-badge&logo=mqtt&logoColor=white" alt="MQTT"/>
  <img src="https://img.shields.io/badge/C++_Embedded-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++"/>
</p>

<div align="center">
  
**[Read the Full Project Report Here](./docs/TOUCHLESS_HCI_Report_Bharat_AI_SoC.pdf)**

</div>

---

## Team Details
* **Team Members:** Abdullah Ajmal, Anuj Deep, Ayman Abdul Jaleel
* **Faculty Mentor:** Dr. Basant Kumar
* **Institution:** Motilal Nehru National Institute of Technology (MNNIT) Allahabad

---

## Project Overview
This project implements a decentralized Cyber-Physical System (CPS) engineered for local, high-speed automotive safety monitoring and cabin control. By shifting heavy digital image processing (DIP) workloads from the cloud directly to an **NVIDIA Jetson TX2**, the architecture guarantees absolute data privacy and ultra-low inference latency.

The system utilizes parallel perception pipelines to passively monitor driver fatigue and gaze, while concurrently providing an active, touchless Human-Computer Interface (HCI) for secondary cabin controls (media/HVAC). Real-time digital inferences are bridged to physical hardware via MQTT, mimicking the distributed Electronic Control Unit (ECU) node architecture of modern vehicles.

## Core Vision & DSP Subsystems

| Subsystem | Algorithm & Heuristics | Technical Implementation |
| :--- | :--- | :--- |
| **Sleep Detection** | **Eye Aspect Ratio (EAR)** | Parses 478 MediaPipe facial landmarks. A scalar threshold of `< 0.20` paired with a strict `2.0s` temporal filter isolates true drowsiness from natural blinking. |
| **Gaze Tracking** | **Euclidean Vector Mapping** | Computes the dynamic distance of the iris relative to the eye's canthus. A `0.4s` state-debounce timer triggers automotive turn indicators intentionally. |
| **Touchless Control** | **Scale-Invariant Hand Tracking** | Calculates wrist-to-fingertip vs. wrist-to-PIP distances. This spatial heuristic ensures accurate gesture decoding regardless of seating distance. |

## System Architecture

* **Perception Node (NVIDIA Jetson):** Built on Python, OpenCV, and Google MediaPipe. Utilizes a custom multithreaded `VideoCapture` queue tailored to the TX2’s ARM Cortex‑A57 cores, maintaining **46.6 FPS** (14.7 ms AI latency) during localized neural inference.
* **Actuator Node (ESP8266):** Programmed in Embedded C++. Operates strictly on **non-blocking `millis()` timers** to safely multiplex high-priority hardware tasks (e.g., pulsing active alarms, driving logic-level signals to headlight/HVAC modules) without thread locking.
* **Transport Layer:** Real-time IPC handled via an EMQX MQTT Broker. Ensures sub-150ms end-to-end latency from optical detection to physical hardware actuation.

## Repository Files

* **[`driver_monitor.py`](./src/perception/driver_monitor.py):** Main perception pipeline. Handles multithreaded frame capture, MediaPipe inference, DSP mathematics, and MQTT state publishing.
* **[`esp8266_code.ino`](./src/actuation/esp8266_code.ino):** Embedded C++ firmware for the actuator node. Features a custom state-machine for non-blocking asynchronous hardware interrupts.
* **[`jetson_performance_data.csv`](./logs/jetson_performance_data.csv):** Raw 8,000-frame stress-test log of FPS and latency.
* **[`run_demo.sh`](./run_demo.sh):** Automated deployment script for hardware and software initialization on JetPack OS.

## Hardware Wiring Guide (Actuator Node)

The ESP8266 actuator node requires the following GPIO pin mapping. 

| NodeMCU Pin | ESP8266 GPIO | Connected Component | State Logic |
| :---: | :---: | :--- | :--- |
| **D1** | `GPIO 5` | 5V Active Buzzer | `HIGH` = Driver Fatigue Alarm |
| **D2** | `GPIO 4` | Red LED (Dash Alert) | `HIGH` = Visual Warning |
| **D3** | `GPIO 0` | Yellow LED (Left Turn Ind.) | `HIGH` = Debounced Pulse |
| **D4** | `GPIO 2` | Yellow LED (Right Turn Ind.) | `HIGH` = Debounced Pulse |
| **D5** | `GPIO 14` | Logic-Level Out (L298N/Relay) | `HIGH` = HVAC / Headlights ON |

---

## Deployment Instructions

Follow these steps to deploy the system natively on an NVIDIA Jetson TX2 running JetPack OS.

### 1. Hardware Setup (MCU)
1. Open [`esp8266_code.ino`](./src/actuation/esp8266_code.ino) in the Arduino IDE.
2. Update `WIFI_SSID`, `WIFI_PASSWORD`, and verify the `MQTT_BROKER` IP.
3. Compile and flash to the embedded board.

### 2. Edge Host Setup (Jetson)
Clone the repository and install the required DSP/Vision dependencies:

```bash
git clone [https://github.com/AbdullahDxB/Bharat-AI-SoC-ADAS.git](https://github.com/AbdullahDxB/Bharat-AI-SoC-ADAS.git)
cd Bharat-AI-SoC-ADAS

# Install system dependencies
sudo apt-get update
sudo apt-get install -y mpv xdotool

# Install Python requirements
pip install opencv-python mediapipe paho-mqtt numpy scipy

# Execute the deployment script
chmod +x run_demo.sh
./run_demo.sh
