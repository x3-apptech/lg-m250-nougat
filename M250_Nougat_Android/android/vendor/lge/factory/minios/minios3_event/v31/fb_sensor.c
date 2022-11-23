#include "fb_sensor.h"

extern Bool MSensorService_GetData(int32_t sensor_type, SensorData* data);
extern int MSensorService_Command(SensorCommand *cmd);
extern Bool MSensorService_GetIsReady(void);

Bool SensorManager_Status()
{
    return MSensorService_GetIsReady();
}

Bool SensorManager_GetInfo(int32_t sensor_type, SensorData* data)
{
    Bool result = false;

    if(MSensorService_GetIsReady()) {
        result = MSensorService_GetData(sensor_type, data);
    }
    else {
        printf("Error : sensor is not ready\n");
        result = false;
    }

    return result;
}

int SensorManager_SetCommand(int32_t sensor_type, int32_t command)
{
    int result = 0;
    SensorCommand cmd;

    if(MSensorService_GetIsReady()) {
        memset(&cmd, 0x0, sizeof(SensorCommand));
        cmd.type = sensor_type;
        cmd.command = command;
        result = MSensorService_Command(&cmd);
    }
    else {
        printf("Error : sensor is not ready\n");
    }

    return result;
}
