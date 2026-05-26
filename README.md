# PigeOff: Safeguarding Monuments with Smart IoT Deterrents

Ancient monuments face a modern, highly corrosive threat: urban pigeon populations. In cities like Rome, the issue extends beyond aesthetics—the high acidity of pigeon droppings physically degrades historical stone, marble, and architecture over time. 

**PigeOff** is an innovative IoT solution designed to preserve urban monuments and public spaces. By integrating smart sensing, localized deterrents, and long-range connectivity, the system offers an automated, data-driven approach to managing pigeon populations in smart city environments.

---

## The Core Challenge & Constraints

Deploying an autonomous system in an urban environment introduces a strict set of operational constraints. PigeOff was engineered around three primary operational requirements:

* **7-Day Minimum Battery Life:** Essential to align with the standard maintenance intervals of municipality services.
* **The 10-Second Processing Window:** Our analysis shows the average stay time of a pigeon on a monument ledge is just **16.3 seconds**. Crucially, defecation typically occurs right before leaving. The system must wake up, detect, classify, and trigger deterrents in under 10 seconds to be effective.

---

## System Architecture & Hardware Pinouts

The PigeOff architecture splits responsibilities between a low-power **Heltec ESP32 v3 LoRa** node (handling radar/LiDAR parsing and long-range transmission) and an **ESP32-CAM** (handling intensive edge AI classification).
Note, that the LiDAR is a stand-in sensor due shipping issues, the sensor that is powered all the time was intended to be more energy efficient.

### Task Structure Overview
The system relies on an asynchronous multi-task structure to handle sensor sampling, queue management, and camera triggers concurrently.

![Task structure](img/task_structure_v2.png)

### Hardware Interconnectivity

To simplify replication and maintenance, the physical wiring maps are structured across three distinct buses:

#### 1. Communication Link: Heltec LoRa to ESP32-CAM
| Heltec ESP32 v3 LoRa Pin | ESP32-CAM Board Pin |
| :--- | :--- |
| GPIO 2 | GPIO 13 |
| GPIO 3 | GPIO 14 |

#### 2. Range Sensing: Heltec LoRa to TF-Mini LiDAR
| Heltec ESP32 v3 LoRa Pin | TF-Mini LiDAR Wire |
| :--- | :--- |
| 5V | Red (Power) |
| GND | Black (GND) |
| GPIO 5 | White (TX) |
| GPIO 4 | Green (RX) |

#### 3. Debug & Flashing: ESP32-CAM to USB-to-TTL Adapter
| ESP32-CAM Board Pin | USB-to-TTL Adapter Pin |
| :--- | :--- |
| UOT | RX |
| GND | GND |
| U0R | TX |

---

## ESP32 Classifier: Optimizing for the Constraint Matrix

To prevent the power-hungry camera from draining the battery, the LiDAR sensor acts as a low-power trigger. When an object enters the detection threshold, the ESP32-CAM wakes up to run onboard image classification. Two model iterations were tested to find the optimal balance between accuracy and dataset constraints.

### Model Iteration 1: MobileNetV1 Transfer Learning
Using a robust dataset processed via **Roboflow**, we trained a **MobileNetV1 (96x96 image size, 0.25 alpha scaling)** model inside **Edge Impulse**. 

* **Dataset Size:** 1,740 images
* **Data Split:** 80% Train / 20% Test
* **Characteristics:** High generalization accuracy across diverse lighting environments.

![Classifier 1](img/classifier1.png)

### Model Iteration 2: Handcrafted Feature Extraction
As a lean alternative, a specialized handcrafted dataset and model architecture were built directly in Edge Impulse to minimize processing overhead.

* **Dataset Size:** 60 images
* **Data Split:** 80% Train / 20% Test
* **Characteristics:** Rapid training time, optimized strictly for targeted landmark profiles.

![Classifier 2](img/classifier2.png)

---

## LoRaWAN 

### Technical Details

We use LoRa to transmit the count of pidgeons every 15 minutes. The number is a way of balancing energy constrains with analysis power. This is a granularity that allows us to map how pidgeon behaviour is affected by opening times of restaurants

We use unconfirmed uplinks to conserve energy, since a missed uplink is not essential at this stage of the process.

In current location the uplinks are sent with a spreading factor of 7: This is a low spreading factor, that is energy efficient due to shorter radio times. We note, that this depends on the location of the node (specifically, its' distance to the nearest LoRa gateway).

### Sample Analysis

The final analysis of a day using the pidgeon counts would look something like this: 

![LoRa Sample Analysis](img/lora_analysis.png)

The following analysis questions could be tackled with this:
* Does human presence deter or attract pigeons?
* Do smells and crumbs coming from open food establishments attract pidgeons?
* Does the trash from closing restaurants attract pidgeons? 

**PigeOFF** deters pigeons but also allows us to perform analysis that can help us in future approaches to tackling the problem of pigeons in urban environments.

## System Performance

| Distance | Lidar FN | Lidar FP | Camera FN | Camera FN | Overall FN | Overall FP
| :--- | :--- | :--- |:--- |:--- |:--- |:--- |
| 20 | :--- | :--- |:--- |:--- |:--- |:--- |
| 40 | :--- | :--- |:--- |:--- |:--- |:--- |
| 80 | :--- | :--- |:--- |:--- |:--- |:--- |

## Team Members

| Name | ID (Matricola) | Profile |
| :--- | :--- | :--- |
| **Anja Škrlj** | 2285543 | [LinkedIn](https://www.linkedin.com/in/anja-škrlj-13aa852a1) |
| **Filippo Zanei** | 2285059 | [LinkedIn](https://www.linkedin.com/in/filippozanei/) |
| **Teun Boekholt** | 000000 | [LinkedIn](https://www.linkedin.com/in/teun-boekholt-a41205255/) |

---

## Project Presentations

Documentation and progress reports for the PigeOff system are cataloged below:

| Milestone | Date | Documentation |
| :--- | :--- | :--- |
| **1st Deliverable** | -- / -- / ---- | [📄 View Presentation](PigeOFF_presentation.pdf) |
| **2nd Deliverable** | 10.04.2026 | [📄 View Presentation](PigeOFF_presentation.pdf) |

---
