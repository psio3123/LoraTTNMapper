
/*
 Copyright (c) 2014-present PlatformIO <contact@platformio.org>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
**/

#include <WiFiClientSecure.h>
#include <Update.h>
#include <BintrayClient.h>
#include "SecureOTA.h"
#include "globals.h"

const BintrayClient bintray(BINTRAY_USER, BINTRAY_REPO, BINTRAY_PACKAGE);

// Connection port (HTTPS)
const int port = 443;

// Connection timeout
const uint32_t RESPONSE_TIMEOUT_MS = 5000;

// Variables to validate firmware content
volatile int contentLength = 0;
volatile bool isValidContentType = false;

void checkFirmwareUpdates()
{
  // Fetch the latest firmware version
  const String latest = bintray.getLatestVersion();
  log_display("Firmware: " + String(VERSION) + " " + latest);
  delay(500);
 
  if (latest.length() == 0)
  {
    Serial.println("Could not load firmware info, ");
    return;
  }
  else if (atof(latest.c_str()) <= VERSION )
  {
    log_display("The firmware is up to date");
    return;
  }

  log_display("New firmware: v." + latest);
  processOTAUpdate(latest);
}

// A helper function to extract header value from header
inline String getHeaderValue(String header, String headerName)
{
  return header.substring(strlen(headerName.c_str()));
}

/**
 * OTA update processing
 */
void processOTAUpdate(const String &version)
{
  String firmwarePath = bintray.getBinaryPath(version);
  if (!firmwarePath.endsWith(".bin"))
  {
    Serial.println("Unsupported format.");
    return;
  }

  String currentHost = bintray.getStorageHost();
  String prevHost = currentHost;

  WiFiClientSecure client;
  client.setCACert(bintray.getCertificate(currentHost));

  if (!client.connect(currentHost.c_str(), port))
  {
    log_display("Cannot connect " + currentHost);   
    return;
  }

  bool redirect = true;
  while (redirect)
  {
    if (currentHost != prevHost)
    {
      client.stop();
      client.setCACert(bintray.getCertificate(currentHost));
      if (!client.connect(currentHost.c_str(), port))
      {
        Serial.println("Redirect detected! Cannot connect to " + currentHost );
        return;
      }
    }

    //Serial.println("Requesting: " + firmwarePath);

    client.print(String("GET ") + firmwarePath + " HTTP/1.1\r\n");
    client.print(String("Host: ") + currentHost + "\r\n");
    client.print("Cache-Control: no-cache\r\n");
    client.print("Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0)
    {
      if (millis() - timeout > RESPONSE_TIMEOUT_MS)
      {
       log_display("Client Timeout !");
        client.stop();
        return;
      }
    }

    while (client.available())
    {
      String line = client.readStringUntil('\n');
      // Check if the line is end of headers by removing space symbol
      line.trim();
      // if the the line is empty, this is the end of the headers
      if (!line.length())
      {
        break; // proceed to OTA update
      }

      // Check allowed HTTP responses
      if (line.startsWith("HTTP/1.1"))
      {
        if (line.indexOf("200") > 0)
        {
          //Serial.println("Got 200 status code from server. Proceeding to firmware flashing");
          redirect = false;
        }
        else if (line.indexOf("302") > 0)
        {
          //Serial.println("Got 302 status code from server. Redirecting to the new address");
          redirect = true;
        }
        else
        {
          //Serial.println("Could not get a valid firmware url");
          //Unexptected HTTP response. Retry or skip update?
          redirect = false;
        }
      }

      // Extracting new redirect location
      if (line.startsWith("Location: "))
      {
        String newUrl = getHeaderValue(line, "Location: ");
        //Serial.println("Got new url: " + newUrl);
        newUrl.remove(0, newUrl.indexOf("//") + 2);
        currentHost = newUrl.substring(0, newUrl.indexOf('/'));
        newUrl.remove(newUrl.indexOf(currentHost), currentHost.length());
        firmwarePath = newUrl;
        //Serial.println("firmwarePath: " + firmwarePath);
        continue;
      }

      // Checking headers
      if (line.startsWith("Content-Length: "))
      {
        contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
        log_display("Got " + String(contentLength) );
      }

      if (line.startsWith("Content-Type: "))
      {
        String contentType = getHeaderValue(line, "Content-Type: ");
        //Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream")
        {
          isValidContentType = true;
        }
      }
    }
  }

  // check whether we have everything for OTA update
  if (contentLength && isValidContentType)
  {
    if (Update.begin(contentLength))
    {
      log_display("Starting Over-The-Air update");
      size_t written = Update.writeStream(client);

      if (written == contentLength)
      {
        Serial.println("Written : " + String(written) + " successfully");
      }
      else
      {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
        // Retry??
      }

      if (Update.end())
      {
        if (Update.isFinished())
        {
          log_display("OTA completed. Rebooting ...");
          ESP.restart();
        }
        else
        {
          log_display("OTA error");
        }
      }
      else
      {
        Serial.println("An error Occurred. Error #: " + String(Update.getError()));
      }
    }
    else
    {
      log_display("Not enough space");
      client.flush();
    }
  }
  else
  {
    Serial.println("No valid response from server!");
    client.flush();
  }
}
