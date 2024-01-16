// SPDX-FileCopyrightText: 2022 Cosylab d.d.
//
// SPDX-License-Identifier: MIT

#include <cstring>
#include <string>
#include <sstream>
#include <map>
#include <memory>
#include <iocsh.h>
#include <errlog.h>
#include "ADSPortDriver.h"
#include <epicsExport.h>
#include "envDefs.h"

static const iocshArg ads_open_arg = {"parameters", iocshArgArgv};
static const iocshArg *ads_open_args[1] = {&ads_open_arg};
static const iocshFuncDef ads_open_func_def = {"AdsOpen", 1, ads_open_args};

static const iocshArg ads_find_io_arg = {"port_name", iocshArgString};
static const iocshArg *ads_find_io_args[] = {&ads_find_io_arg};
static const iocshFuncDef ads_find_io_func_def = {"AdsFindIOIntrRecord", 1,
                                                  ads_find_io_args};

static const iocshArg ads_set_ams_arg = {"ams_net_id", iocshArgString};
static const iocshArg *ads_set_ams_args[] = {&ads_set_ams_arg};
static const iocshFuncDef ads_set_local_amsNetID_func_def = {
    "AdsSetLocalAMSNetID", 1, ads_set_ams_args};

epicsShareFunc int ads_open(int argc, const char *const *argv) {
    std::string port_name;
    std::string ip_addr;
    std::string ams_net_id;
    int device_read_ads_port = defaultDeviceReadADSPort;
    int sum_buffer_nelem = defaultSumBuferNelem;
    int ads_function_timeout_ms = -1;

    if (argc < 3 || argc > 7) {
        errlogPrintf(
            "AdsOpen <port_name> <ip_addr> <ams_net_id>"
            " | optional: <sum_buffer_nelem (default: %u)> <ads_timeout "
            "(default: %u) [ms]> <device_read_ads_port (default:%u)>\n",
            defaultSumBuferNelem, defaultADSCallTimeout_ms, defaultDeviceReadADSPort);
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        switch (i) {
        case 1:
            port_name = argv[i];
            break;
        case 2:
            ip_addr = argv[i];
            break;
        case 3:
            ams_net_id = argv[i];
            break;
        case 4:
            sum_buffer_nelem = strtol(argv[i], nullptr, 10);
            if (sum_buffer_nelem < 1) {
                errlogPrintf(
                    "Error: sum_buffer_nelem must be a positive integer (%i)\n",
                    sum_buffer_nelem);
                return -1;
            }
            break;
        case 5:
            ads_function_timeout_ms = strtol(argv[i], nullptr, 10);
            if (ads_function_timeout_ms < 1) {
                errlogPrintf(
                    "ads_function_timeout must be positive integer!\n");
                return -1;
            }
            break;
        case 6:
            device_read_ads_port = strtol(argv[i], nullptr, 10);
            if (device_read_ads_port < 1) {
                errlogPrintf(
                    "Error: device_read_ads_port must be a positive integer (%i)\n",
                    device_read_ads_port);
                return -1;
            }

            break;
        default:
            break;
        }
    }

    new ADSPortDriver(port_name.c_str(), ip_addr.c_str(), ams_net_id.c_str(),
                      sum_buffer_nelem, ads_function_timeout_ms, device_read_ads_port);

    return 0;
}

epicsShareFunc void ads_set_local_amsNetID(const char *ams) {
    if (ams == NULL)
        errlogPrintf("Local Ams Net ID must be specified\n");
    else
#ifndef USE_TC_ADS
        AdsSetLocalAddress((std::string)ams);
#endif

    return;
}

static AmsNetId make_AmsNetId(const std::string& addr)
{
    std::istringstream iss(addr);
    std::string s;
    size_t i = 0;
    AmsNetId id {};

    while ((i < sizeof(id.b)) && std::getline(iss, s, '.')) {
        id.b[i] = atoi(s.c_str()) % 256;
        ++i;
    }

    if ((i != sizeof(id.b)) || std::getline(iss, s, '.')) {
        memset(id.b, 0, sizeof(id.b));
    }
    return id;
}


// Get an arbritary ADS variable value and set the given macro to this value. 
epicsShareFunc int getAdsVar(const char *macroname, char *varname,
                             const char *ipAddr, const char *AmsNetID,
                             int adsPort) {
    int result = 0;

    std::string id(AmsNetID);
    std::string adsvar(varname);
    printf("getting value for %s\n", adsvar.c_str());

    long      nErr, nPort; 
    AmsAddr   Addr; 
    long      lHdlVar, nData; 
    char szVar[32];
    strcpy(szVar, adsvar.c_str()); 

    // Open communication port on the ADS router
    nPort = AdsPortOpenEx();
    Addr.port = adsPort;
    Addr.netId = make_AmsNetId(id);

    nErr = AdsSyncReadWriteReqEx2(nPort, &Addr, ADSIGRP_SYM_HNDBYNAME, 0x0,
                               sizeof(lHdlVar), &lHdlVar, sizeof(szVar), szVar, nullptr);
    if (nErr) {
        errlogPrintf("Error: AdsSyncReadWriteReqEx2: %li \n" , nErr);
        return 1;
    } 

    // Read the value of a PLC-variable, via handle 
    nErr = AdsSyncReadReqEx2(nPort, &Addr, ADSIGRP_SYM_VALBYHND, lHdlVar, sizeof(nData), &nData, nullptr ); 
    if (nErr) {
        errlogPrintf("Error: AdsSyncReadReqEx2: %li \n" , nErr);
        return 1;
    }
    else {
        result = nData; 
    }

    // Release the handle of the PLC-variable
    nErr = AdsSyncWriteReqEx(nPort, &Addr, ADSIGRP_SYM_RELEASEHND, 0, sizeof(lHdlVar), &lHdlVar); 
    if (nErr) {
        errlogPrintf("Error: AdsSyncWriteReqEx: %li \n" , nErr);
        return 1;
    }

    // Close the communication port
    nErr = AdsPortCloseEx(nPort); 
    if (nErr) {
        errlogPrintf("Error: AdsPortClose: %li \n" , nErr);
        return 1;
    } 

    printf("Value is %ld\n", nData);

    char result_str[32];
    sprintf(result_str, "%i", result);
    epicsEnvSet(macroname, result_str);
    return 0;
}

static void ads_open_call_func(const iocshArgBuf *args) {
    ads_open(args[0].aval.ac, args[0].aval.av);
}

static void ads_set_local_amsNetID_call_func(const iocshArgBuf *args) {
    ads_set_local_amsNetID(args[0].sval);
}

static void ads_open_register_command(void) {
    static int already_registered = 0;

    if (already_registered == 0) {
        iocshRegister(&ads_open_func_def, ads_open_call_func);
        already_registered = 1;
    }
}

static void ads_set_local_amsNetID_register_command(void) {
    static int already_registered = 0;

    if (already_registered == 0) {
        iocshRegister(&ads_set_local_amsNetID_func_def,
                      ads_set_local_amsNetID_call_func);
        already_registered = -1;
    }
}

extern "C" {
static const iocshArg getAdsVarInitArg0 = { "macro", iocshArgString }; ///< The name of the macro to put the result of the calculation into.
static const iocshArg getAdsVarInitArg1 = { "varname", iocshArgString }; ///< The left hand side argument.
static const iocshArg getAdsVarInitArg2 = { "ipAddr", iocshArgString }; ///< IP address of the beckhoff.
static const iocshArg getAdsVarInitArg3 = { "AmsNetID", iocshArgString }; ///< The AMSNETID of the beckhoff.
static const iocshArg getAdsVarInitArg4 = { "adsPort", iocshArgInt}; /// < The ADS port. 

static const iocshArg * const getAdsVarInitArgs[] = { &getAdsVarInitArg0, &getAdsVarInitArg1, &getAdsVarInitArg2, &getAdsVarInitArg3, &getAdsVarInitArg4};

static const iocshFuncDef getAdsVarInitFuncDef = {"getAdsVar", sizeof(getAdsVarInitArgs) / sizeof(iocshArg*), getAdsVarInitArgs};

static void getAdsVarInitCallFunc(const iocshArgBuf *args)
{
    getAdsVar(args[0].sval, args[1].sval, args[2].sval, args[3].sval, args[4].ival);
}

static void ads_getAdsVar_register_command(void)
{
    iocshRegister(&getAdsVarInitFuncDef, getAdsVarInitCallFunc);
}

epicsExportRegistrar(ads_getAdsVar_register_command);
epicsExportRegistrar(ads_open_register_command);
epicsExportRegistrar(ads_set_local_amsNetID_register_command);
}
