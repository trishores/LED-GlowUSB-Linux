 /*  
 *  Copyright 2018-2021 ledmaker.org
 *  
 *  This file is part of GlowUSB-Linux.
 *  
 *  GlowUSB-Linux is free software: you can redistribute it and/or modify 
 *  it under the terms of the GNU General Public License as published 
 *  by the Free Software Foundation, either version 3 of the License, 
 *  or any later version.
 *  
 *  GlowUSB-Linux is distributed in the hope that it will be useful, but 
 *  WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License 
 *  along with GlowUSB-Linux. If not, see https://www.gnu.org/licenses/.
 */


#include <stdio.h>
#include <stdlib.h>
#include "hidapi.h"
#include "unistd.h"
#include "pugixml.hpp"
#include <iostream>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>

// Device status defines:
#define CONTROL_FLAG 0
#define STORE_FLAG 1
// Logging defines:
#define LOG_INFO 1
#define LOG_DBG 2
#define LOGGING_LEVEL LOG_INFO

void Log(std::string msg, int logLevel);
bool ParseXml(pugi::xml_document *doc, int *usbVendorId, int *usbProductId, int *usbPacketByteLen, std::string *action, std::string *packetStr);
bool SendBreakPacket(hid_device *device, unsigned char *packetBuf, size_t packetBufLen);
bool SendDataPacket(hid_device *device, unsigned char *packetBuf, size_t packetBufLen);
std::vector<std::string> Split(const std::string& s, char delimiter);

int main(int argc, char* argv[])
{
    // Process args:
    if (argc != 4) 
    {
        Log("Incorrect number of args\n", LOG_DBG);
        return -1;
    }
	if ((std::string)argv[1] != "-InputFilePath") 
    {
        Log("Unrecognized switch\n", LOG_DBG);
        return -1;
    }
    std::string const xmlPath = argv[2];
	std::string action = argv[3]; 
	std::transform(action.begin(), action.end(), action.begin(), ::tolower);

	// Read xml contents:
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
    if (result.status != pugi::status_ok) 
    {
        return -1;
    }

	// Parse xml contents:
	int usbVendorId, usbProductId, usbPacketByteLen;
	std::string packetStr;
	if (!ParseXml(&doc, &usbVendorId, &usbProductId, &usbPacketByteLen, &action, &packetStr))
	{
		return -1;
	}

	// Generate data bytes:
	packetStr.erase(std::remove(packetStr.begin(), packetStr.end(), ' '), packetStr.end());
	packetStr.erase(std::remove(packetStr.begin(), packetStr.end(), '\n'), packetStr.end());
	std::vector<std::string> strArr = Split(packetStr, ',');
	std::vector<unsigned char> packetBytes;
	for (unsigned int i = 0; i < strArr.size(); i++)
	{
		packetBytes.push_back((unsigned char)std::strtol(strArr[i].c_str(), 0, 16));
	}

	// Open hidapi library:
	int res = hid_init();
	if (res != 0)
    {
        return -1;
    }

	// Open the device using the VID, PID, and optionally the serial number:
	hid_device *deviceHandle = hid_open(usbVendorId, usbProductId, NULL);

	// Send break packet:
	std::vector<unsigned char> packetBuf(usbPacketByteLen + 1,  0xFF);
	packetBuf[0] = 0;
	Log("Break packet: ", LOG_DBG);
	if (!SendBreakPacket(deviceHandle, &packetBuf[0], packetBuf.size()))
	{
		Log("USB transfer failed.\n", LOG_INFO);
		return -1;
	}

	// Transfer data packets:
	unsigned int packetIdx = 0;
	unsigned int packetSuccessCount = 0;
	unsigned int totalPackets = packetBytes.size() / usbPacketByteLen;
	for (unsigned int i = 0; i < packetBytes.size(); i++)
	{
		packetBuf[(i % usbPacketByteLen) + 1] = packetBytes[i];
		if ((i + 1) % usbPacketByteLen == 0)	// packetBuf filled.
		{
			Log("Data packet " + std::to_string(++packetIdx) + ": ", LOG_DBG);
			if (!SendDataPacket(deviceHandle, &packetBuf[0], packetBuf.size()))
			{
				Log("USB transfer failed: ", LOG_INFO);
				Log(std::to_string(packetSuccessCount) + " out of " + std::to_string(totalPackets) + " data-packets sent.\n", LOG_INFO);
				return -1;
			}
			packetSuccessCount++;
		}
	}

	// Send break packet if download action:
	if (action == "-download")
	{
		std::fill_n(packetBuf.begin(), packetBuf.size(), 0xFF);
		packetBuf[0] = 0;
		Log("Break packet: ", LOG_DBG);
		if (!SendBreakPacket(deviceHandle, &packetBuf[0], packetBuf.size()))
		{
			Log("USB transfer failed.\n", LOG_INFO);
			return -1;
		}
	}

	Log("USB transfer succeeded: ", LOG_INFO);
	Log(std::to_string(packetSuccessCount) + " out of " + std::to_string(totalPackets) + " data-packets sent.\n", LOG_INFO);

	// Close hidapi library:
	hid_close(deviceHandle);
	res = hid_exit();
	if (res != 0) 
    {
        return -1;
    }

	return 0;
}

void Log(std::string msg, int logLevel)
{
	if (logLevel <= LOGGING_LEVEL)
	{
		std::cout << msg;
	}
}

bool SendBreakPacket(hid_device *device, unsigned char *packetBuf, size_t packetBufLen)
{
	int res = hid_write(device, packetBuf, packetBufLen);
	if (res < 0)
	{
		Log("write failed unexpectedly.\n", LOG_DBG);
		return false;
	}

	int retryCount = 10;
	while (retryCount--)
	{
        // Get report from device:
		res = hid_readc(device, packetBuf, packetBufLen);
		bool isActiveAnimation = (packetBuf[1] != 0);
		bool isActiveMemWrite = (packetBuf[2] != 0);
		int isControlMode = (packetBuf[3] == CONTROL_FLAG);
		if (res > 0 && !isActiveAnimation && !isActiveMemWrite && isControlMode)
		{
			Log("sent successfully.\n", LOG_DBG);
			return true;
		}
		else if (res > 0 && isActiveAnimation)
		{
			Log("animation in progress... recheck in 100ms... ", LOG_DBG);
			usleep(100000);
			continue;
		}
		else if (res > 0 && isActiveMemWrite)
		{
			Log("write in progress... recheck in 100ms... ", LOG_DBG);
			usleep(100000);
			continue;
		}
		else
		{
			Log("read failed unexpectedly.\n", LOG_DBG);
			return false;
		}
	}
	return false;
}

bool SendDataPacket(hid_device *device, unsigned char *packetBuf, size_t packetBufLen)
{
	int retryCount = 10;

	int res = hid_write(device, packetBuf, packetBufLen);
	if (res < 0)
	{
		Log("write failed unexpectedly.\n", LOG_DBG);
		return false;
	}

	while (retryCount--)
	{
		res = hid_readc(device, packetBuf, packetBufLen);
		bool isActiveMemWrite = (packetBuf[2] != 0);
		if (res > 0 && !isActiveMemWrite)
		{
			Log("sent successfully.\n", LOG_DBG);
			return true;
		}
		else if (res > 0 && isActiveMemWrite)
		{
			Log("write in progress... recheck in 100ms... ", LOG_DBG);
			usleep(100000);
			continue;
		}
		else
		{
			Log("read failed unexpectedly.\n", LOG_DBG);
			return false;
		}
	}
	return false;
}

bool ParseXml(pugi::xml_document *doc, int *usbVendorId, int *usbProductId, int *usbPacketByteLen, std::string *action, std::string *packetStr)
{
	*usbVendorId = doc->child("compiler").child("device").child("usbVendorId").text().as_int();
	*usbProductId = doc->child("compiler").child("device").child("usbProductId").text().as_int();
	*usbPacketByteLen = doc->child("compiler").child("device").child("usbPacketByteLen").text().as_int();

	if (*action == "-download")
	{
		*packetStr = doc->child("compiler").child_value("downloadLightshowPackets");
	}
	else if (*action == "-start")
	{
		*packetStr = doc->child("compiler").child_value("startLightshowPackets");
	}
	else if (*action == "-stop" || *action == "pause")
	{
		*packetStr = doc->child("compiler").child_value("pauseLightshowPackets");
	}
	else if (*action == "-resume")
	{
		*packetStr = doc->child("compiler").child_value("resumeLightshowPackets");
	}
	else
	{
		return false;
	}

	return true;
}

std::vector<std::string> Split(const std::string& s, char delimiter)
{
   std::vector<std::string> tokens;
   std::string token;
   std::istringstream tokenStream(s);
   while (std::getline(tokenStream, token, delimiter))
   {
      tokens.push_back(token);
   }
   return tokens;
}
