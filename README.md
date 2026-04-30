# IoT Wildfire Tracker – Sustainable Cities & Communities

## Group Members
- Stefan Necsoiu
- Nojus Lankelis

**Group Name:** GrowTopia
**Module:** CM3142 Internet of Things  
**Submission Date:** 30/04/2026

## System Description

**Selected SDG:** SDG 11 – Sustainable Cities and Communities.

Wildfires pose an increasing threat to urban-rural interfaces and sustainable communities worldwide, destroying infrastructure, endangering lives, and disrupting ecosystems. Early detection of high-risk environmental conditions (prolonged dryness, low humidity, elevated temperature, and high light levels) is essential for rapid response and prevention.

**Aim of the system:**  
This IoT Wildfire Tracker provides real-time environmental monitoring and early wildfire-risk alerts using low-power, long-range LoRa communication. It enables communities, emergency services, and local authorities to receive live data and visual dashboards, allowing timely intervention before fires escalate.

**How the system contributes to SDG 11:**  
By deploying scalable networks of low-cost sensor nodes, the system supports resilient and sustainable cities through proactive environmental monitoring. It directly addresses the need for innovative infrastructure (SDG 9) that protects communities (SDG 11) from climate-related hazards.

**Required system functionalities:**
- Collect accurate multi-sensor data (temperature, humidity, soil moisture, pressure, light) at edge nodes.
- Transmit data reliably over long distances using LoRa.
- Aggregate data at a gateway and present it via a live web dashboard with trend graphs and a wildfire-risk heuristic.
- Provide acknowledgements and basic user control (device naming, location, update frequency via code configuration).
- Scale to hundreds or thousands of nodes across multiple deployments using star/mesh LoRa topology and edge computing.
- Demonstrate consideration of legal, ethical, and security aspects (data privacy, power efficiency, open-source licensing).

When fully satisfied, these functionalities deliver actionable intelligence that helps communities mitigate wildfire risk and build more sustainable, safer urban environments.

## System Design

### Overall System Design
The system follows a **star topology** with edge computing (sender nodes perform local sensing and basic processing) and a central **gateway/sink node**. This design supports massive scalability: thousands of low-power sender nodes can communicate with multiple gateways that forward aggregated data to cloud services if required in future deployments.

**IoT node types and roles:**
- **Sender node** (edge node): collects environmental data and transmits via LoRa.
- **Gateway node** (sink node): receives LoRa packets, stores recent history, serves a web dashboard, and provides acknowledgements.

**Network topology & computing paradigm:** Edge + gateway (fog) computing. Data processing happens as close to the source as possible to minimise latency and bandwidth usage.

**Connectivity protocols:** LoRa (physical layer – 915 MHz, long range, low power) for node-to-gateway communication. This choice is ideal for rural/urban-fringe deployments where Wi-Fi or cellular coverage may be unreliable or expensive.

**Application-layer messaging:** Compact JSON payloads over a simple custom protocol (`MSG:<id>:<payload>` with `ACK:<id>`). JSON ensures future interoperability with cloud services.

**User control:** Device name, location, and transmission interval are configurable in source code (easily editable for different deployments). Future versions could add a simple web configuration interface.

**Additional considerations:**
- **Legal/ethical/privacy:** No personal data is collected. All sensor data is anonymised and publicly viewable on the dashboard. Open-source licensing encourages community contributions.
- **Security:** Basic ACK protocol prevents packet loss; production systems would add encryption.
- **Scalability:** LoRa supports thousands of nodes per gateway with appropriate duty-cycle management.
- **Advanced techniques:** A heuristic wildfire-risk algorithm combines multiple sensor streams (implemented in the gateway).

**Node names for reference:**
- `src/node_sender/main.cpp`
- `src/node_gateway/main.cpp`

**Hardware selection:**
The ESP32 (Freenove ESP32-WROVER) was selected for both the Sender and Gateway nodes due to its ultra-low power consumption (~150 mA active, <10 µA sleep), low unit cost (~£4–6), and excellent suitability for scalable, battery-powered LoRa deployments. This is significantly better than a Raspberry Pi 5 (high power draw and cost) or Arduino-based solutions for a system that must scale to hundreds or thousands of nodes. Detailed SBC comparison tables and per-node justifications are provided in the individual node design files.

(Full detailed design for each node is in the separate `node_sender_design.md` and `node_gateway_design.md` files.)

## Setup Guide

### General Instructions
The system is developed for ESP32 boards using PlatformIO (C++). All code is documented and follows industry best practices.

**Prerequisites:**
- Two ESP32 development boards + LoRa modules (see hardware maps in the node design files).
- PlatformIO (recommended) or Arduino IDE.
- Python 3 not required for core operation (ESP32 firmware is C++ – justified in node design files for power and cost reasons).
- Wi-Fi network for the gateway.

**Cloud/web services:** None used in the current implementation. The gateway hosts its own HTTP dashboard.

**Repository structure:**
- `src/node_sender/main.cpp` – Sender node firmware
- `src/node_gateway/main.cpp` – Gateway node firmware
- `node_sender_design.md` / `node_sender_setup.md`
- `node_gateway_design.md` / `node_gateway_setup.md`

Detailed per-node setup (wiring diagrams, library installation, upload commands) is provided in the individual `node_<name>_setup.md` files.

**Video demonstration:**  
[link to demo to be updated]

## Generative AI Acknowledgement
- I acknowledge use of **Grok (xAI)** from https://grok.x.ai to assist with structuring documentation and expanding explanations of system design. Prompts used on 30/04/2026 included requests for coursework-compliant Markdown templates and section wording. All generated content was reviewed, edited, and integrated by the team.
(Include acknowledgements from chatgpt or other AIs later)

- I acknowledge use of **OpenAI Codex** (via GitHub Copilot) from https://github.com/features/copilot to assist with coding purposes. 
Codex was used to help generate and suggest C++ code snippets for sensor integration, LoRa packet handling, JSON telemetry formatting, ACK protocol logic, and the AsyncWebServer dashboard implementation on the ESP32. 
All generated content was reviewed, tested, debugged, and significantly modified by the team before final integration.

---
