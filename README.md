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

Here is the PCB design for the extender module:

![PCB Design](CCT_Tester_I2C%20Expander/12C_expander_Board_for_CCT_Tester.png)

Here is the wiring diagram:

![Wiring Diagram](CCT_Tester_I2C%20Expander/12Cexpander_wiring_diagram_for_CCT_Tester.png)

**Calibration Update Guide**
-

If swapping sensors or re-calibrating against a new integrating sphere, update the cal_table_s1, cal_table_s2, and cal_table_s3 arrays in the main loop.

Format: {Raw_Wand_Reading, True_Sphere_Reading}

Requirement: Data must be listed in strictly ascending order for the piecewise logic to route properly.


## In-Field Use Case & Hardware

### 1. The QC Monitor Station (In Use)
*The fully assembled CCT Tester stationed on the test bench.*
![CCT Tester in use](CCT_Tester_Photos/CCT_Tester_in_use.jpg)

### 2. Custom 3D Printed Housing
*The custom 3D-printed housing and aperture designed for the CCT Tester.*
![3D Printed Housing](CCT_Tester_Photos/3D_Printed_housing_and_appature_for_CCT_Tester.jpg)

### 3. Warm-Up Sequence
*The system initialization and warm-up message.*
![Warm Up Message](CCT_Tester_Photos/CCT_Tester_Warm_up_message.jpg)

### 4. Live UI Dashboard
*The LVGL interface updating dynamically during live testing.*
![UI In Use](CCT_Tester_Photos/CCT_Tester_UI_in_use.jpg)


