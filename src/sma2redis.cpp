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

Log logger;
std::string loadFile(const char *name);
bool loadConfig(JsonObject cfg, int argc, char **argv);
void deepMerge(JsonVariant dst, JsonVariant src);
using namespace std;

void process_data(vector<vec_data> data_vector, int debug, string &line, string &header);
void dataToRedis(Redis &redis, vector<vec_data> &data_vector, std::string serial);
std::unordered_map<std::string, std::string> unitToLabels = {
    {"power_ac", "unit watt acdc ac"},
    {"voltage_ac_l1", "unit voltage acdc ac line l1"},
    {"voltage_ac_l2", "unit voltage acdc ac line l2"},
    {"voltage_ac_l3", "unit voltage acdc ac line l3"},
    {"power_ac_l1", "unit watt acdc ac line l1"},
    {"power_ac_l2", "unit watt acdc ac line l2"},
    {"power_ac_l3", "unit watt acdc ac line l3"},
    {"voltage_dc_1", "unit voltage acdc dc line 1"},
    {"voltage_dc_2", "unit voltage acdc dc line 2"},
    {"voltage_dc_3", "unit voltage acdc dc line 3"},
    {"power_dc_1", "unit watt acdc dc line 1"},
    {"power_dc_2", "unit watt acdc dc line 2"},
    {"power_dc_3", "unit watt acdc dc line 3"},
    {"current_ac_l1", "unit ampere acdc ac line l1"},
    {"current_ac_l2", "unit ampere acdc ac line l2"},
    {"current_ac_l3", "unit ampere acdc ac line l3"},
    {"power_ac_max_l1", "unit watt_max acdc ac line l1"},
    {"power_ac_max_l2", "unit watt_max acdc ac line l2"},
    {"power_ac_max_l3", "unit watt_max acdc ac line l3"},
    {"yield_total", "unit kwh acdc ac"}

};

int main(int argc, char **argv)
{
    INFO("Loading configuration.");
    DynamicJsonDocument config(10240);
    loadConfig(config.to<JsonObject>(), argc, argv);
    Thread workerThread("worker");

    Redis redis(workerThread, config["redis"].as<JsonObject>());
    redis.connect();

    TimerSource clock(workerThread, 5000, true, "clock");

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

/* This function processes the data that is in a vector of structs */
void process_data(vector<vec_data> data_vector, int debug, string &line, string &header)
{
    string pac_header, yield_header, pdc1_header, pdc2_header, vdc1_header, vdc2_header, vac1_header, vac2_header, vac3_header, iac1_header, iac2_header, iac3_header;
    string pac_str, yield_str, pdc1_str, pdc2_str, vdc1_str, vdc2_str, vac1_str, vac2_str, vac3_str, iac1_str, iac2_str, iac3_str;
    stringstream stream_vac1, stream_vac2, stream_vac3, stream_iac1, stream_iac2, stream_iac3, stream_vdc1, stream_vdc2, stream_pdc1, stream_pdc2, stream_pac, stream_yield;

    for (auto iter = data_vector.begin(); iter != data_vector.end(); ++iter)
    {
        float value = iter->value;
        string name = iter->name;

        /* If any value (except yield) is greater than 167772, it means it is max as FFFF. Set to 0
           But only for voltage or current */
        if (name == "power_ac")
        {
            pac_header = "AC Power(" + iter->units + ")";
            stream_pac << fixed << setprecision(1) << value;
            pac_str = stream_pac.str();
        }

        if (name == "yield_total")
        {
            yield_header = "Yield(" + iter->units + ")";
            stream_yield << fixed << setprecision(1) << value;
            yield_str = stream_yield.str();
        }

        if (name == "power_dc_1")
        {
            pdc1_header = "DC Power 1(" + iter->units + ")";
            stream_pdc1 << fixed << setprecision(1) << value;
            pdc1_str = stream_pdc1.str();
        }

        if (name == "power_dc_2")
        {
            pdc2_header = "DC Power 2(" + iter->units + ")";
            stream_pdc2 << fixed << setprecision(1) << value;
            pdc2_str = stream_pdc2.str();
        }

        if (name == "voltage_dc_1")
        {
            if (value > 16700.0)
                value = 0.0;
            vdc1_header = "DC Voltage 1(" + iter->units + ")";
            stream_vdc1 << fixed << setprecision(1) << value;
            vdc1_str = stream_vdc1.str();
        }

        if (name == "voltage_dc_2")
        {
            if (value > 16700.0)
                value = 0.0;
            vdc2_header = "DC Voltage 2(" + iter->units + ")";
            stream_vdc2 << fixed << setprecision(1) << value;
            vdc2_str = stream_vdc2.str();
        }

        if (name == "voltage_ac_l1")
        {
            if (value > 16700.0)
                value = 0.0;
            vac1_header = "AC Voltage 1(" + iter->units + ")";
            stream_vac1 << fixed << setprecision(1) << value;
            vac1_str = stream_vac1.str();
        }

        if (name == "voltage_ac_l2")
        {
            if (value > 16700.0)
                value = 0.0;
            vac2_header = "AC Voltage 2(" + iter->units + ")";
            stream_vac2 << fixed << setprecision(1) << value;
            vac2_str = stream_vac2.str();
        }

        if (name == "voltage_ac_l3")
        {
            if (value > 16700.0)
                value = 0.0;
            vac3_header = "AC Voltage 3(" + iter->units + ")";
            stream_vac3 << fixed << setprecision(1) << value;
            vac3_str = stream_vac3.str();
        }

        if (name == "current_ac_l1")
        {
            if (value > 16700.0)
                value = 0.0;
            iac1_header = "AC Current 1(" + iter->units + ")";
            stream_iac1 << fixed << setprecision(2) << value;
            iac1_str = stream_iac1.str();
        }

        if (name == "current_ac_l2")
        {
            if (value > 16700.0)
                value = 0.0;
            iac2_header = "AC Current 2(" + iter->units + ")";
            stream_iac2 << fixed << setprecision(2) << value;
            iac2_str = stream_iac2.str();
        }

        if (name == "current_ac_l3")
        {
            if (value > 16700.0)
                value = 0.0;
            iac3_header = "AC Current 3(" + iter->units + ")";
            stream_iac3 << fixed << setprecision(2) << value;
            iac3_str = stream_iac3.str();
        }

        string datetime = get_current_datetime();
        header = "#Datetime," + pac_header + "," + yield_header + "," + pdc1_header + "," + pdc2_header + "," + vdc1_header + "," +
                 vdc2_header + "," + vac1_header + "," + vac2_header + "," + vac3_header + "," + iac1_header + "," + iac2_header + "," + iac3_header;

        line = datetime + "," + pac_str + "," + yield_str + "," + pdc1_str + "," + pdc2_str + "," + vdc1_str + "," + vdc2_str + "," +
               vac1_str + "," + vac2_str + "," + vac3_str + "," + iac1_str + "," + iac2_str + "," + iac3_str;

        INFO("%s = %f ( %s ) ", iter->name.c_str(), value, iter->units.c_str());
    }

    return;
}

bool loadConfig(JsonObject cfg, int argc, char **argv)
{
    // override args
    int c;
    while ((c = getopt(argc, argv, "h:p:s:b:f:t:v")) != -1)
        switch (c)
        {
        case 'f':
        {
            std::string s = loadFile(optarg);
            DynamicJsonDocument doc(10240);
            deserializeJson(doc, s);
            deepMerge(cfg, doc);
            break;
        }
        case 'h':
            cfg["redis"]["host"] = optarg;
            break;
        case 'p':
            cfg["redis"]["port"] = atoi(optarg);
            break;
        case 'v':
        {
            logger.setLevel(Log::L_DEBUG);
            break;
        }
        case '?':
            printf("Usage %s -h <host> -p <port> \n",
                   argv[0]);
            break;
        default:
            WARN("Usage %s -h <host> -p <port> \n",
                 argv[0]);
            abort();
        }
    std::string s;
    serializeJson(cfg, s);
    INFO("config:%s", s.c_str());
    return true;
};

void deepMerge(JsonVariant dst, JsonVariant src)
{
    if (src.is<JsonObject>())
    {
        for (auto kvp : src.as<JsonObject>())
        {
            deepMerge(dst.getOrAddMember(kvp.key()), kvp.value());
        }
    }
    else
    {
        dst.set(src);
    }
}

void dataToRedis(Redis &redis, std::vector<vec_data> &data, std::string serial)
{

    for (auto &iter : data)
    {
        auto r = unitToLabels.find(iter.name);
        std::string labels = "unit noIdea";
        if (r != unitToLabels.end())
        {
            labels = r->second;
        }
        std::string cmd =
            stringFormat("TS.ADD sma:%s:%s * %f LABELS %s ", serial.c_str(), iter.name.c_str(), iter.value, labels.c_str());
        INFO("Redis.command => %s", cmd.c_str());
        redis.command().on(cmd);
    }
}
