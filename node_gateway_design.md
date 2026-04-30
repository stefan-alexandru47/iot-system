# Node: Gateway (Sink Node)

**Led by:** Stefan Necsoiu & Nojus Lankelis

## Role and Functionalities
The Gateway node acts as the **sink and central hub** of the system. Its core responsibilities are:
- Receiving LoRa packets from one or more Sender nodes.
- Parsing the JSON telemetry payload and storing the most recent readings (RAM-based history for trend visualisation).
- Running the wildfire-risk heuristic algorithm that combines temperature, humidity, soil moisture, light intensity and pressure to produce a real-time risk level (Green / Orange / Red).
- Serving a live HTTP web dashboard with interactive graphs and current status.
- Sending ACK packets back to the Sender to confirm successful reception.

This node provides the user-facing interface and enables immediate visualisation and alerting, directly supporting rapid community response to wildfire risk.

## Suitable Single Board Computers
| SBC Option       | Power Consumption | Cost per Unit | Scalability (1000+ nodes) | Justification for this project |
|------------------|-------------------|---------------|---------------------------|--------------------------------|
| **ESP32** (chosen) | ~200 mA active (Wi-Fi on) | ~£4–6 | Excellent | Built-in Wi-Fi + LoRa support, sufficient RAM for web server and short-term history, low cost for multiple gateways in a large deployment. |
| Raspberry Pi 5   | ~500–1000 mA     | ~£60+        | Good                      | More powerful but higher power draw and cost make it unsuitable for large-scale, low-power deployments. |
| Arduino + Ethernet shield | Moderate      | ~£20         | Limited                   | No native Wi-Fi, limited processing for web dashboard. |

**Recommendation:** ESP32 remains the optimal choice for a scalable system because one low-cost gateway can serve hundreds of Sender nodes while hosting its own dashboard without requiring a separate server.

## Sensors, Actuators and Hardware
- **SX1276/78 LoRa module** (SPI): Primary communication hardware for receiving data from Sender nodes.
- No environmental sensors are attached directly to the Gateway (all sensing occurs at the edge).
- **Built-in ESP32 Wi-Fi radio**: Used to create a local access point or connect to an existing network for the web dashboard.

Hardware selection prioritises minimal components while maximising range and user accessibility.

## Connectivity and Messaging Protocols
- **Physical layer:** LoRa (915 MHz, SPI) – identical configuration to the Sender node for interoperability.
    - Pin mapping (same as Sender – see `node_sender_design.md`).
- **Application layer:** Same custom protocol (`MSG:<id>:<JSON payload>` and `ACK:<id>`). The Gateway parses the JSON and immediately replies with an ACK.
- **User interface:** HTTP web server (ESP32 AsyncWebServer) providing real-time graphs (temperature, humidity, soil moisture, light, pressure) and a prominent wildfire-risk indicator.
- **Future scalability:** The Gateway can be extended to forward aggregated data to cloud services (MQTT, HTTP POST) without changing the LoRa layer.

## Additional Considerations
- **Scalability:** A single Gateway can support hundreds of Sender nodes. Multiple Gateways can be deployed for geographic coverage, all feeding into a central dashboard if required.
- **Legal/Ethical/Privacy:** No personal data is stored or transmitted. Dashboard is intentionally public-facing to encourage community awareness. All code is open-source.
- **Security:** Basic ACK prevents silent packet loss; production deployments would add LoRa payload encryption and HTTPS on the dashboard.
- **Advanced techniques:** Wildfire-risk heuristic (implemented in code) demonstrates on-device data analysis. Altitude compensation and sensor calibration are applied for accuracy.

This design completes the end-to-end IoT pipeline and directly addresses SDG 11 by turning raw environmental data into actionable community-level alerts.

---
