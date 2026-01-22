#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ini/minIni.h"
#include "i2c.h"
#include "battery.h"

#include <switch.h>

#define INNER_HEAP_SIZE 0x30000
#define CONFIG_PATH "sdmc:/config/syslihv/config.ini"

u32 __nx_applet_type = AppletType_None;

u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }


    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    rc = i2cInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    rc = splInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    rc = bpcInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();

    smExit();
}

// Service deinitialization.
void __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    i2cExit();
    splExit();
    bpcExit();
}


typedef struct {
    int enabled;
    int volt;
    int current;
    int temp_shutoff;
    int temp_no_charge;
    int temp_default_charge;
} SysLihvConfig;

typedef enum
{
    HorizonOCConsoleType_Icosa = 0,
    HorizonOCConsoleType_Copper,
    HorizonOCConsoleType_Hoag,
    HorizonOCConsoleType_Iowa,
    HorizonOCConsoleType_Calcio,
    HorizonOCConsoleType_Aula,
    HorizonOCConsoleType_EnumMax,
} HorizonOCConsoleType;

u64 sku;

Result LoadSysLihvConfig(SysLihvConfig *cfg)
{
    if (!cfg)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    /* Defaults */
    cfg->enabled               = 0;
    cfg->volt                  = 4208;
    cfg->current               = 2048;
    cfg->temp_shutoff          = 60;
    cfg->temp_no_charge        = 55;
    cfg->temp_default_charge   = 45;

    minIni ini(CONFIG_PATH);

    cfg->enabled             = ini.getl("config", "enabled", cfg->enabled);
    cfg->volt                = ini.getl   ("config", "volt", cfg->volt);
    cfg->current             = ini.getl   ("config", "current", cfg->current);
    cfg->temp_shutoff        = ini.getl   ("config", "temp_shutoff", cfg->temp_shutoff);
    cfg->temp_no_charge      = ini.getl   ("config", "temp_no_charge", cfg->temp_no_charge);
    cfg->temp_default_charge = ini.getl   ("config", "temp_default_charge", cfg->temp_default_charge);

    return 0;
}


int main(int argc, char* argv[])
{
    SysLihvConfig cfg;
    BatteryChargeInfo info;
    LoadSysLihvConfig(&cfg);

    splGetConfig(SplConfigItem_HardwareType, &sku);
    sku = (HorizonOCConsoleType)sku;
    bool isHoag = sku == HorizonOCConsoleType_Hoag ? true : false;
    u32 defaultChargeCurrent = isHoag ? 1792 : 2048;

    if(cfg.enabled)
        for(;;) {
            psmGetBatteryChargeInfoFields(&info);
            if(info.BatteryTemperature > (cfg.temp_shutoff * 1000)) {
                bpcShutdownSystem(); // SHUT DOWN THE CONSOLE!
            } else if (info.BatteryTemperature > (cfg.temp_no_charge * 1000)) {
                batteryInfoDisableCharging();
            } else if (info.BatteryTemperature > (cfg.temp_default_charge * 1000)) {
                batteryInfoEnableCharging();
                I2c_Bq24193_SetFastChargeCurrentLimit(defaultChargeCurrent);
                I2c_Bq24193_SetChargeVoltageLimit(4208);
            } else {
                batteryInfoEnableCharging();
                I2c_Bq24193_SetChargeVoltageLimit(cfg.volt);
                I2c_Bq24193_SetFastChargeCurrentLimit(cfg.current);
            }
        }

    return 0;
}
