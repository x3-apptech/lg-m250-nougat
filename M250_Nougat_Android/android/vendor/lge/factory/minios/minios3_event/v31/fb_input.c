#define LOG_NDEBUG 0
#define LOG_TAG "FB_Input"

#include <pthread.h>
#include <unistd.h>

#ifndef bool
typedef unsigned bool;
#endif

#include "MInput.h"
#include "events.h"
#include "events_c.h"

pthread_t mThreadId = 0;
static bool mIsRunning = false;

/* Keyboard */
struct KeyboardMap mKeyboardMap[] = {
    {KEY_VOLUMEDOWN,     "vol_down"},
    {KEY_VOLUMEUP,          "vol_up"},
    {KEY_POWER,                "power"},
    {KEY_HOMEPAGE,          "home"},
    {KEY_HOME,                   "home"},
    {KEY_BACK,                    "escape"},
    {KEY_MENU,                   "menu"},
    {KEY_MEDIA,                   "hook"},
    {KEY_KBDILLUMTOGGLE,  "dual_sim"},
    {KEY_SEARCH,                 "dual_sim"},
    {166,                              "dual_sim"},
    {0xF9,                            "dual_sim"},     // 249
    {0xFA,                            "quick_memo"},    // 250
    {165,                              "quick_memo"},
    {0xFF,                            "touch_key_up"},   // 255
    {KEY_0,     "0"},
    {KEY_1,     "1"},
    {KEY_2,     "2"},
    {KEY_3,     "3"},
    {KEY_4,     "4"},
    {KEY_5,     "5"},
    {KEY_6,     "6"},
    {KEY_7,     "7"},
    {KEY_8,     "8"},
    {KEY_9,     "9"},
    {KEY_LEFT,     "left"},
    {KEY_RIGHT,     "right"},
    {KEY_F1,     "lang"},
    {KEY_F2,     "message"},
    {KEY_F3,     "phonebook"},
    {KEY_F4,     "kakao"},
    {KEY_F5,     "*"},
    {KEY_F6,     "#"},
    {KEY_CAMERA,     "camera"},
    {KEY_ENTER,     "ok"},
    {KEY_PAGEUP,     "pageup"},
    {KEY_PAGEDOWN,     "pagedown"},
    {KEY_DOCUMENTS,     "call"},
    {KEY_SEND,     "send"},
    {KEY_UP,     "up"},
    {KEY_DOWN,   "down"},
    {KEY_EXIT, "clr"},
#if defined(AAT_y30t) || defined(AAT_y30tc)
    {0x244,                   "menu"},
#endif
};

/* Switch */
struct SwitchMap mSwitchMap[] = {
    {SW_HEADPHONE_INSERT,    "headset"},
    {SW_MICROPHONE_INSERT,  "headset"},
    {SW_ADVANCED_HEADPHONE_INSERT,  "headset"},
    {SW_AUX_ACCESSORY_INSERT,  "headset"}
};

extern bool MInputService_SetClear();
extern bool MInputService_GetData(struct MInputData *data);

static void SendEvent_Touch(int event, int touchstate, int finger, int x, int y)
{
    switch(event) {
        case TOUCH_RELEASE :
            PrivateMouseButton(touchstate, finger, x, y);
            ALOGD("%s release event: %d state: %d, finger: %d, x: %d, y: %d", __FUNCTION__, event, touchstate, finger, x, y);
            break;
        case TOUCH_DOWN :
            PrivateMouseButton(touchstate, finger, x, y);
            ALOGD("%s down event: %d state: %d, finger: %d, x: %d, y: %d", __FUNCTION__, event, touchstate, finger, x, y);
            break;
        case TOUCH_MOVE :
            PrivateMouseMotion(touchstate, finger, x, y);
            break;
        default :
            ALOGD("%s unknown event: %d state: %d, finger: %d, x: %d, y: %d", __FUNCTION__, event, touchstate, finger, x, y);
            break;
    }
}

static bool Keyboard_CheckValue(int32_t keycode, int32_t value)
{
    int32_t index;
    int32_t count;
    count = (sizeof(mKeyboardMap) / sizeof(struct KeyboardMap));

    for(index = 0; index < count; index++) {
        if(mKeyboardMap[index].keyCode == keycode) {
            if((value == 0) || (value == 1) || (value == 0xFF) || (value == 0x4000000)) {
                return true;
            }
        }
    }

    return false;
}

static bool Keyboard_TranslateKey(struct keysym *ks, int keycode)
{
    int index;
    int count;
    int code;
    char *name;

    count = (sizeof(mKeyboardMap) / sizeof(struct KeyboardMap));
    for(index = 0; index < count; index++) {
        if(mKeyboardMap[index].keyCode == keycode) {
            name = mKeyboardMap[index].name;
            break;
        }
    }

    if(index != count) {
        code = GetKeyCode(name);
        if(code >= 0) {
            ks->scancode = keycode;
            ks->sym = code;
            ks->mod = KMOD_NONE;
            ks->unicode = 0;
            return true;
        }
        else {
            ALOGD("%s unknown key code: %d", __FUNCTION__, code);
        }
    }
    else {
        ALOGD("%s unsupported key code: %d", __FUNCTION__, code);
    }

    return false;

}


static void SendEvent_Key(int keycode, int value, int flag)
{
    struct keysym ks;

    if(flag && Keyboard_CheckValue(keycode, value)) {
        memset((void *)&ks, 0x00, sizeof(keysym));
        if(Keyboard_TranslateKey(&ks, keycode)) {
            if((value == 0) || (value == 0xFF)) {
                PrivateKeyboard(RELEASED, &ks);
                ALOGD("%s keycode: %d release", __FUNCTION__, keycode);
            }
            else {
                PrivateKeyboard(PRESSED, &ks);
                ALOGD("%s keycode: %d pressed", __FUNCTION__, keycode);
            }
        }
    }
}

static bool Switch_CheckValue(int keycode, int value)
{
    int32_t index;
    int32_t count;

    count = (sizeof(mSwitchMap) / sizeof(struct SwitchMap));

    for(index = 0; index < count; index++) {
        if(mSwitchMap[index].switchCode == keycode) {
            if((value == 0) || (value == 1) || (value == 0xFF)) {
                return true;
            }
        }
    }

    return false;
}

static void SendEvent_Switch(int code, int value)
{
    Event_t event;

    if(Switch_CheckValue(code, value)) {
        if(ProcessEvents[USEREVENT] == ENABLE) {
            memset(&event, 0x00, sizeof(Event_t));
            event.user.type = USEREVENT;
            event.user.code = code;
            event.user.state = value;

            if((EventOK == NULL) || EventOK(&event)) {
                PushEvent(&event);
                ALOGD("%s switch: %d, state:%d", __FUNCTION__, event.user.code, event.user.state);
            }
        }
    }
}

static void ProcessEvent(struct MInputData *data)
{
    switch(data->type) {
        case MEVENT_TOUCH :
            SendEvent_Touch(data->code, data->value, data->reserved1, data->reserved2, data->reserved3);
            break;
        case MEVENT_KEY :
            SendEvent_Key(data->code, data->value, data->reserved1);
            break;
        case MEVENT_SWITCH :
            SendEvent_Switch(data->code, data->value);
            break;
        case MEVENT_NONE :
            break;
        default :
            ALOGD("%s unknown event type: %d", __FUNCTION__, data->type);
            break;
    }
}

void * InputManager_Loop(void *param)
{
    struct MInputData data;

    usleep(100*1000);

    while(mIsRunning) {
        if(MInputService_SetClear()) {
            break;
        }
        else {
            usleep(1500*1000);
        }
    }

    while(mIsRunning) {
        memset((void *)&data, 0x00, sizeof(struct MInputData));
        if(mIsRunning && MInputService_GetData(&data)) {
            ProcessEvent(&data);
        }
        else {
            break;
        }
    }

    mIsRunning = false;
    mThreadId = 0;

    ALOGD("%s exit: %d", __FUNCTION__, (int)getpid());

    pthread_exit(0);

    return NULL;
}

void InputManager_Start()
{
    ALOGD("%s start", __FUNCTION__);

    if(!mIsRunning && (mThreadId == 0)) {
        mIsRunning = true;
        if(pthread_create(&mThreadId, NULL, InputManager_Loop, NULL) < 0) {
            ALOGD("%s pthread creation failed", __FUNCTION__);
            mIsRunning = false;
            mThreadId = 0;
        }
    }
    else {
        ALOGD("%s invalid status (mIsRunning:%d, mThreadId:%d", __FUNCTION__, (int)mIsRunning, (int)mThreadId);
    }

    ALOGD("%s end", __FUNCTION__);
}

void InputManager_Stop()
{
    ALOGD("%s start", __FUNCTION__);

    int wait_time = 0;
    if(mIsRunning) {
        mIsRunning = false;
        do {
            if((mThreadId == 0) || (wait_time >= 3)) {
                break;
            }
            else {
                wait_time++;
                usleep(100*1000);
            }
        }while(1);
    }

    ALOGD("%s end", __FUNCTION__);
}

