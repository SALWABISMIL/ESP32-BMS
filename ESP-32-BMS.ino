/*
  ============================================================
  ESP32 BMS - FINAL TEST FIRMWARE
  ============================================================

  Hardware:
    ESP32-WROOM-32

  DHT11:
    DATA -> GPIO13

  Outputs:
    Output 1 -> GPIO2
    Output 2 -> GPIO4
    Output 3 -> GPIO5
    Output 4 -> GPIO18

  Inputs:
    Input 1 -> GPIO19
    Input 2 -> GPIO21
    Input 3 -> GPIO22
    Input 4 -> GPIO23

  Modbus TCP:
    Port 502

  Modbus:
    Coils:
      0 = Output 1
      1 = Output 2
      2 = Output 3
      3 = Output 4

    Holding Registers:
      0 = Output command
      1 = Temperature setpoint x10
      2 = Operating mode
      3 = Alarm

    Input Registers:
      0 = Temperature x10
      1 = Humidity x10
      2 = Light/Input status
      3 = Counter

  WiFiManager:
    AP Name: ESP32-BMS-SETUP

  ============================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <ModbusIP_ESP8266.h>

// ------------------------------------------------------------
// PIN DEFINITIONS
// ------------------------------------------------------------

#define DHT_PIN 13
#define DHT_TYPE DHT11

#define OUTPUT_1 2
#define OUTPUT_2 4
#define OUTPUT_3 5
#define OUTPUT_4 18

#define INPUT_1 19
#define INPUT_2 21
#define INPUT_3 22
#define INPUT_4 23

// ------------------------------------------------------------
// OBJECTS
// ------------------------------------------------------------

DHT dht(DHT_PIN, DHT_TYPE);

ModbusIP mb;

// ------------------------------------------------------------
// MODBUS ADDRESSES
// ------------------------------------------------------------

// Coils
#define COIL_OUTPUT_1 0
#define COIL_OUTPUT_2 1
#define COIL_OUTPUT_3 2
#define COIL_OUTPUT_4 3

// Holding registers
#define HREG_COMMAND   0
#define HREG_SETPOINT  1
#define HREG_MODE      2
#define HREG_ALARM     3

// Input registers
#define IREG_TEMP      0
#define IREG_HUMIDITY  1
#define IREG_LIGHT     2
#define IREG_COUNTER   3

// ------------------------------------------------------------
// SETTINGS
// ------------------------------------------------------------

float temperature = 0.0;
float humidity = 0.0;

float temperatureSetpoint = 25.2;

// Automatic control:
// Output 2 ON when temperature > setpoint
// Output 2 OFF when temperature <= setpoint

bool automaticMode = true;

bool alarmState = false;

unsigned long counterValue = 0;

unsigned long lastDHTRead = 0;
unsigned long lastCounterUpdate = 0;

const unsigned long DHT_INTERVAL = 2000;
const unsigned long COUNTER_INTERVAL = 1000;

// ------------------------------------------------------------
// OUTPUT STATES
// ------------------------------------------------------------

bool output1State = false;
bool output2State = false;
bool output3State = false;
bool output4State = false;

// ------------------------------------------------------------
// INPUT STATES
// ------------------------------------------------------------

bool input1State = false;
bool input2State = false;
bool input3State = false;
bool input4State = false;

// ------------------------------------------------------------
// FUNCTION: SET OUTPUT
// ------------------------------------------------------------

void setOutput(uint8_t pin, bool state)
{
  digitalWrite(pin, state ? HIGH : LOW);
}

// ------------------------------------------------------------
// FUNCTION: READ INPUTS
// ------------------------------------------------------------

void readInputs()
{
  input1State = digitalRead(INPUT_1);
  input2State = digitalRead(INPUT_2);
  input3State = digitalRead(INPUT_3);
  input4State = digitalRead(INPUT_4);
}

// ------------------------------------------------------------
// FUNCTION: UPDATE OUTPUTS FROM MODBUS
// ------------------------------------------------------------

void updateOutputs()
{
  output1State = mb.Coil(COIL_OUTPUT_1);
  output3State = mb.Coil(COIL_OUTPUT_3);
  output4State = mb.Coil(COIL_OUTPUT_4);

  setOutput(OUTPUT_1, output1State);
  setOutput(OUTPUT_3, output3State);
  setOutput(OUTPUT_4, output4State);
}

// ------------------------------------------------------------
// FUNCTION: AUTOMATIC TEMPERATURE CONTROL
// ------------------------------------------------------------

void automaticTemperatureControl()
{
  if (!automaticMode)
    return;

  if (temperature > temperatureSetpoint)
  {
    output2State = true;
  }
  else
  {
    output2State = false;
  }

  mb.Coil(COIL_OUTPUT_2, output2State);
  setOutput(OUTPUT_2, output2State);
}

// ------------------------------------------------------------
// FUNCTION: UPDATE INPUT REGISTERS
// ------------------------------------------------------------

void updateInputRegisters()
{
  int tempValue = (int)round(temperature * 10.0);
  int humidityValue = (int)round(humidity * 10.0);

  // Combine digital input status into one register
  uint16_t lightStatus = 0;

  if (input1State)
    lightStatus |= 1;

  if (input2State)
    lightStatus |= 2;

  if (input3State)
    lightStatus |= 4;

  if (input4State)
    lightStatus |= 8;

  mb.Ireg(IREG_TEMP, tempValue);
  mb.Ireg(IREG_HUMIDITY, humidityValue);
  mb.Ireg(IREG_LIGHT, lightStatus);
  mb.Ireg(IREG_COUNTER, counterValue);
}

// ------------------------------------------------------------
// FUNCTION: UPDATE HOLDING REGISTERS
// ------------------------------------------------------------

void updateHoldingRegisters()
{
  int setpointRegister = mb.Hreg(HREG_SETPOINT);

  if (setpointRegister > 0)
  {
    temperatureSetpoint = setpointRegister / 10.0;
  }

  int modeRegister = mb.Hreg(HREG_MODE);

  if (modeRegister == 0)
  {
    automaticMode = false;
  }
  else
  {
    automaticMode = true;
  }

  mb.Hreg(HREG_ALARM, alarmState ? 1 : 0);
}

// ------------------------------------------------------------
// FUNCTION: READ DHT11
// ------------------------------------------------------------

void readDHT()
{
  if (millis() - lastDHTRead < DHT_INTERVAL)
    return;

  lastDHTRead = millis();

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (isnan(newHumidity) || isnan(newTemperature))
  {
    return;
  }

  temperature = newTemperature;
  humidity = newHumidity;
}

// ------------------------------------------------------------
// FUNCTION: ALARM
// ------------------------------------------------------------

void updateAlarm()
{
  /*
     Alarm example:
     Temperature >= 40 C
  */

  if (temperature >= 40.0)
  {
    alarmState = true;
  }
  else
  {
    alarmState = false;
  }

  mb.Hreg(HREG_ALARM, alarmState ? 1 : 0);
}

// ------------------------------------------------------------
// FUNCTION: COUNTER
// ------------------------------------------------------------

void updateCounter()
{
  if (millis() - lastCounterUpdate < COUNTER_INTERVAL)
    return;

  lastCounterUpdate = millis();

  counterValue++;
}

// ------------------------------------------------------------
// WIFI
// ------------------------------------------------------------

void connectWiFi()
{
  WiFiManager wm;

  /*
     Try saved Wi-Fi first.

     If it cannot connect, ESP32 creates:

       ESP32-BMS-SETUP

     Connect your phone to that network and configure
     the new Wi-Fi.

     The credentials are saved automatically.
  */

  wm.setConnectTimeout(30);

  bool connected = wm.autoConnect("ESP32-BMS-SETUP");

  if (!connected)
  {
    Serial.println();
    Serial.println("Wi-Fi connection failed.");
    Serial.println("Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("BMS Wi-Fi Connected");
  Serial.println("================================");

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("Signal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

// ------------------------------------------------------------
// SETUP
// ------------------------------------------------------------

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 BMS STARTING");
  Serial.println("================================");

  // ----------------------------------------------------------
  // OUTPUTS
  // ----------------------------------------------------------

  pinMode(OUTPUT_1, OUTPUT);
  pinMode(OUTPUT_2, OUTPUT);
  pinMode(OUTPUT_3, OUTPUT);
  pinMode(OUTPUT_4, OUTPUT);

  // Start outputs OFF
  digitalWrite(OUTPUT_1, LOW);
  digitalWrite(OUTPUT_2, LOW);
  digitalWrite(OUTPUT_3, LOW);
  digitalWrite(OUTPUT_4, LOW);

  // ----------------------------------------------------------
  // INPUTS
  // ----------------------------------------------------------

  pinMode(INPUT_1, INPUT);
  pinMode(INPUT_2, INPUT);
  pinMode(INPUT_3, INPUT);
  pinMode(INPUT_4, INPUT);

  // ----------------------------------------------------------
  // DHT
  // ----------------------------------------------------------

  dht.begin();

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  connectWiFi();

  // ----------------------------------------------------------
  // MODBUS TCP
  // ----------------------------------------------------------

  mb.server();

  // Coils
  mb.addCoil(COIL_OUTPUT_1, false);
  mb.addCoil(COIL_OUTPUT_2, false);
  mb.addCoil(COIL_OUTPUT_3, false);
  mb.addCoil(COIL_OUTPUT_4, false);

  // Holding registers
  mb.addHreg(HREG_COMMAND, 0);
  mb.addHreg(HREG_SETPOINT, 252);
  mb.addHreg(HREG_MODE, 1);
  mb.addHreg(HREG_ALARM, 0);

  // Input registers
  mb.addIreg(IREG_TEMP, 0);
  mb.addIreg(IREG_HUMIDITY, 0);
  mb.addIreg(IREG_LIGHT, 0);
  mb.addIreg(IREG_COUNTER, 0);

  // ----------------------------------------------------------
  // INITIAL VALUES
  // ----------------------------------------------------------

  mb.Hreg(HREG_SETPOINT, 252);
  mb.Hreg(HREG_MODE, 1);
  mb.Hreg(HREG_ALARM, 0);

  mb.Coil(COIL_OUTPUT_1, false);
  mb.Coil(COIL_OUTPUT_2, false);
  mb.Coil(COIL_OUTPUT_3, false);
  mb.Coil(COIL_OUTPUT_4, false);

  Serial.println();
  Serial.println("Modbus TCP Server Started");
  Serial.println("Port: 502");

  Serial.println();
  Serial.println("BMS READY");
}

// ------------------------------------------------------------
// LOOP
// ------------------------------------------------------------

void loop()
{
  // Process Modbus TCP
  mb.task();

  // Read sensors
  readDHT();

  // Read physical inputs
  readInputs();

  // Read commands from dashboard
  updateHoldingRegisters();

  // Manual outputs
  updateOutputs();

  // Automatic temperature control
  automaticTemperatureControl();

  // Alarm
  updateAlarm();

  // Counter
  updateCounter();

  // Input registers
  updateInputRegisters();

  delay(10);
}
