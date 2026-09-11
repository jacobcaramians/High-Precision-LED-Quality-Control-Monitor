An industrial metrology tool designed for high-accuracy LED color temperature (CCT) and Illuminance (Lux) verification on the factory assembly line.
Built on an ESP32-S3 CrowPanel HMI Display, this instrument aggregates data from a custom wand containing three I2C multiplexed DFRobot color temperature sensors. To overcome the inherent non-linear response curves of silicon optical sensors, the firmware utilizes individual, multi-point Piecewise Linear Interpolation Lookup Tables (LUTs), strictly calibrated against an Everfine Integrating Sphere and Sekonic C-7000 spectrometer.


**Hardware Stack**
-

MCU/Display: Elecrow CrowPanel ESP32-S3 Display (800x480 resolution)

Sensors: 3x DFRobot Color Temperature & Lux Sensors (I2C)

Multiplexer: 8-Channel I2C Multiplexer (Address 0x70)

Housing: Custom 3D-printed wand with a locked 20cm focal standoff and ambient light shrouding.

**Software Dependencies**
-

To compile this project in the Arduino IDE or PlatformIO, you will need the following libraries:

esp_display_panel.hpp (Elecrow's display driver)

lvgl.h (Light and Versatile Graphics Library - v8.x)

DFRobot_ColorTemperature.h (Sensor driver)

Standard Wire.h for I2C communication

**Wiring & Setup**
-

Connect the I2C Multiplexer to the ESP32-S3 SDA (Pin 19) and SCL (Pin 20).

Connect the three DFRobot sensors to channels 0, 1, and 2 of the multiplexer.

Supply clean 3.3V/5V power to the sensor array.

Here is the pcb design for the extender module

<img width="462" height="686" alt="image" src="https://github.com/user-attachments/assets/b8b2f9bb-526d-412b-86e4-19260b5121a9" />

here is the wiring diagram

<img width="631" height="646" alt="image" src="https://github.com/user-attachments/assets/a3dc4a34-9fe6-4668-a6a5-26b2c9f0277b" />

**Calibration Update Guide**
-

If swapping sensors or re-calibrating against a new integrating sphere, update the cal_table_s1, cal_table_s2, and cal_table_s3 arrays in the main loop.

Format: {Raw_Wand_Reading, True_Sphere_Reading}

Requirement: Data must be listed in strictly ascending order for the piecewise logic to route properly.



