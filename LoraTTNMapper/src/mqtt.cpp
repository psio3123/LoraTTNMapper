#include "globals.h"
#include "mqtt.h"


#if (USE_MQTT)

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");

  u8g2log.print(topic);
  u8g2log.print("\n");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
    u8g2log.print((char)payload[i]);
  }
  Serial.println();
  u8g2log.print("\n");

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1')
  {
    digitalWrite(BUILTIN_LED, LOW); // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  }
  else
  {
    digitalWrite(BUILTIN_LED, HIGH); // Turn the LED off by making the voltage HIGH
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("Mqtt Client"))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("MrFlexi/nodemcu", "connected");
      // ... and resubscribe
      client.subscribe(mqtt_topic);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup_mqtt()
{

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  if (!client.connected())
  {
    reconnect();
  }

  log_display("Mqtt connected");
  client.publish("mrflexi/solarserver/info", "ESP32 is alive...");
}
#endif