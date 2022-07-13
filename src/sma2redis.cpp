/*
 * Copyright (c) 2013-2017 Ardexa Pty Ltd
 *
 * This code is licensed under GPL v3
 *
 */

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <string>
//#include <iostream>
//#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "utils.hpp"
#include "in_bluetooth.h"
#include "in_smadata2plus.h"
#include <limero.h>
#include <hiredis.h>
#include <Redis.h>
#include <StringUtility.h>
#include <ConfigFile.h>

Log logger;
using namespace std;

void process_data(vector<vec_data> data_vector, int debug, string &line, string &header);
void dataToRedis(Redis &redis, vector<vec_data> &data_vector, std::string serial);
std::unordered_map<std::string, std::string> inputToRedisLabels = {
    {"power_ac", "input power acdc ac"},
    {"voltage_ac_l1", "input voltage acdc ac line l1"},
    {"voltage_ac_l2", "input voltage acdc ac line l2"},
    {"voltage_ac_l3", "input voltage acdc ac line l3"},
    {"power_ac_l1", "input power acdc ac line l1"},
    {"power_ac_l2", "input power acdc ac line l2"},
    {"power_ac_l3", "input power acdc ac line l3"},
    {"voltage_dc_1", "input voltage acdc dc line l1"},
    {"voltage_dc_2", "input voltage acdc dc line l2"},
    {"voltage_dc_3", "input voltage acdc dc line l3"},
    {"power_dc_1", "input power acdc dc line l1"},
    {"power_dc_2", "input power acdc dc line l2"},
    {"power_dc_3", "input power acdc dc line l3"},
    {"current_ac_l1", "input current acdc ac line l1"},
    {"current_ac_l2", "input current acdc ac line l2"},
    {"current_ac_l3", "input current acdc ac line l3"},
    {"power_ac_max_l1", "input maxPower acdc ac line l1"},
    {"power_ac_max_l2", "input maxPower acdc ac line l2"},
    {"power_ac_max_l3", "input maxPower acdc ac line l3"},
    {"yield_total", "input totalPower acdc ac"}

};

int main(int argc, char **argv)
{
    INFO("Loading configuration.");
    Json config;
    config["redis"]["host"] = "localhost";
    config["redis"]["port"] = 6379;
    configurator(config, argc, argv);
    Thread workerThread("worker");

    Redis redis(workerThread, config["redis"].as<JsonObject>());
    redis.connect();

    TimerSource clock(workerThread, 1 * 60 * 1000, true, "clock");

    std::vector<std::string> devices;
    for (auto dev : config["sma"]["devices"].as<JsonArray>())
    {
        devices.push_back(dev.as<std::string>());
    }

    redis.response() >> [&](const Json &resp)
    {
        std::string result;
        serializeJson(resp, result);
        INFO("Redis response: %s", result.c_str());
    };

    clock >> [&](const TimerMsg &)
    {
        for (auto device : devices)
        {
            INFO("Connecting to device: %s", device.c_str());
            std::string deviceName = get_bt_name(device);
            INFO("Device name: %s", deviceName.c_str());
            if (deviceName.empty())
            {
                INFO("Device not found: %s", device.c_str());
                continue;
            };
            std::string serial = get_serial(deviceName);
            INFO("Serial: %s", serial.c_str());

            // Inizialize Bluetooth Inverter
            vector<vec_data> data_vector;

            struct bluetooth_inverter inv = {{0}};
            strcpy(inv.macaddr, device.c_str()); /// Change to strncpy
            memcpy(inv.password, "0000", 5);
            in_bluetooth_connect(&inv);
            in_smadata2plus_connect(&inv);
            in_smadata2plus_login(&inv);
            in_smadata2plus_get_values(&inv, data_vector);
            close(inv.socket_fd);
            dataToRedis(redis, data_vector, serial);
        }
    };
    workerThread.run();
    return 0;
}



void dataToRedis(Redis &redis, std::vector<vec_data> &data, std::string serial)
{

    for (auto &iter : data)
    {
        auto r = inputToRedisLabels.find(iter.name);
        std::string labels = "input unknown";
        if (r != inputToRedisLabels.end())
        {
            labels = r->second;
        }
        if ((iter.name.rfind("voltage", 0) == 0 ||
             iter.name.rfind("current", 0) == 0 ||
             iter.name.rfind("power_ac_l3", 0) == 0) &&
            iter.value > 16700.0)
            iter.value = 0.0;
        std::string cmd =
            stringFormat("TS.ADD sma:%s:%s * %f LABELS %s ", serial.c_str(), iter.name.c_str(), iter.value, labels.c_str());
        INFO("Redis.command => %s", cmd.c_str());
        redis.command().on(cmd);
    }
}
