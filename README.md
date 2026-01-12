# Indoor Positioning System (IPS) 📍

A real-time location tracking system engineered using ESP32-S3 and UWB technology. Unlike GPS, which fails indoors, this system provides accurate tracking for warehouse assets.

## 💡 Key Functions
- **Two-Way Ranging (TWR):** Calculates distance between Anchors and Tags.
- **Real-Time Visualization:** Displays live distance metrics on a connected OLED display.
- **Data Logging:** Distance data is sent to a hosted web server for trajectory analysis.

## 🔧 Hardware Used
- ESP32-S3
- UWB Modules (DWM1000 or similar)
- OLED Display (0.96" I2C)

## 💻 Implementation Details
The system utilizes the speed of light to calculate distance based on signal transmission time. The ESP32-S3 processes these signals and outputs coordinates to the serial monitor and web interface.
