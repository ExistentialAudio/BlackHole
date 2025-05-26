/*
     File: BlackHole.c
  
 Copyright (C) 2019 Existential Audio Inc.
  
*/
/*==================================================================================================
	BlackHole.c
==================================================================================================*/

//==================================================================================================
//	Includes
//==================================================================================================

#include <CoreAudio/AudioServerPlugIn.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/syslog.h>
#include <Accelerate/Accelerate.h>
#include <Availability.h>

//==================================================================================================
#pragma mark -
#pragma mark Macros
//==================================================================================================

#if TARGET_RT_BIG_ENDIAN
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[0], ((char*)&the4CC)[1], ((char*)&the4CC)[2], ((char*)&the4CC)[3], 0 }
#else
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[3], ((char*)&the4CC)[2], ((char*)&the4CC)[1], ((char*)&the4CC)[0], 0 }
#endif

#ifndef __MAC_12_0
#define kAudioObjectPropertyElementMain kAudioObjectPropertyElementMaster
#endif

#if DEBUG

    #define    DebugMsg(inFormat, ...)    syslog(LOG_NOTICE, inFormat, ## __VA_ARGS__)

    #define    FailIf(inCondition, inHandler, inMessage)                           \
    if(inCondition)                                                                \
    {                                                                              \
        DebugMsg(inMessage);                                                       \
        goto inHandler;                                                            \
    }

    #define    FailWithAction(inCondition, inAction, inHandler, inMessage)         \
    if(inCondition)                                                                \
    {                                                                              \
        DebugMsg(inMessage);                                                       \
        { inAction; }                                                              \
        goto inHandler;                                                            \
        }

#else

    #define    DebugMsg(inFormat, ...)

    #define    FailIf(inCondition, inHandler, inMessage)                           \
    if(inCondition)                                                                \
    {                                                                              \
    goto inHandler;                                                                \
    }

    #define    FailWithAction(inCondition, inAction, inHandler, inMessage)         \
    if(inCondition)                                                                \
    {                                                                              \
    { inAction; }                                                                  \
    goto inHandler;                                                                \
    }

#endif


//==================================================================================================
#pragma mark -
#pragma mark BlackHole State
//==================================================================================================

//    The driver has the following
//    qualities:
//    - a box
//    - a device
//        - supports 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000, 8000, 16000 sample rates


//        - provides a rate scalar of 1.0 via hard coding
//    - a single output stream
//        - supports 16 channels of 32 bit float LPCM samples
//        - writes to ring buffer
//    - a single input stream
//        - supports 16 channels of 32 bit float LPCM samples
//        - reads from ring buffer
//    - controls
//        - master input volume
//        - master output volume
//        - master input mute
//        - master output mute


//    Declare the internal object ID numbers for all the objects this driver implements. Note that
//    because the driver has fixed set of objects that never grows or shrinks. If this were not the
//    case, the driver would need to have a means to dynamically allocate these IDs. It is important
//    to realize that a lot of the structure of this driver is vastly simpler when the IDs are all
//    known a priori. Comments in the code will try to identify some of these simplifications and
//    point out what a more complicated driver will need to do.
enum
{
    kObjectID_PlugIn                    = kAudioObjectPlugInObject,
    kObjectID_Box                       = 2,
    kObjectID_Device                    = 3,
    kObjectID_Stream_Input              = 4,
    kObjectID_Volume_Input_Master       = 5,
    kObjectID_Mute_Input_Master         = 6,
    kObjectID_Stream_Output             = 7,
    kObjectID_Volume_Output_Master      = 8,
    kObjectID_Mute_Output_Master        = 9,
    kObjectID_Pitch_Adjust              = 10,
    kObjectID_ClockSource               = 11,
    kObjectID_Device2                   = 12,
};

enum
{
    ChangeAction_SetSampleRate          = 1,
    ChangeAction_EnablePitchControl     = 2,
    ChangeAction_DisablePitchControl    = 3,
};

enum ObjectType
{
    kObjectType_Stream,
    kObjectType_Control
};

struct ObjectInfo {
    AudioObjectID id;
    enum ObjectType type;
    AudioObjectPropertyScope scope;
};

//    Declare the stuff that tracks the state of the plug-in, the device and its sub-objects.
//    Note that we use global variables here because this driver only ever has a single device. If
//    multiple devices were supported, this state would need to be encapsulated in one or more structs
//    so that each object's state can be tracked individually.
//    Note also that we share a single mutex across all objects to be thread safe for the same reason.


#ifndef kDriver_Name
#define                             kDriver_Name                        "BlackHole"
#endif

#ifndef kPlugIn_BundleID
#define                             kPlugIn_BundleID                    "audio.existential.BlackHole2ch"
#endif

#ifndef kPlugIn_Icon
#define                             kPlugIn_Icon                        "BlackHole.icns"
#endif

#ifndef kHas_Driver_Name_Format
#define                             kHas_Driver_Name_Format             true
#endif

#if kHas_Driver_Name_Format
#define                             kDriver_Name_Format                 "%ich"
#define                             kBox_UID                            kDriver_Name kDriver_Name_Format "_UID"
#define                             kDevice_UID                         kDriver_Name kDriver_Name_Format "_UID"
#define                             kDevice2_UID                        kDriver_Name kDriver_Name_Format "_2_UID"
#define                             kDevice_ModelUID                    kDriver_Name kDriver_Name_Format "_ModelUID"


#ifndef kDevice_Name
#define                             kDevice_Name                        kDriver_Name " %ich"
#endif

#ifndef kDevice2_Name
#define                             kDevice2_Name                       kDriver_Name " %ich 2"
#endif


#else
#define                             kBox_UID                            kDriver_Name "_UID"
#define                             kDevice_UID                         kDriver_Name "_UID"
#define                             kDevice2_UID                        kDriver_Name "_2_UID"
#define                             kDevice_ModelUID                    kDriver_Name "_ModelUID"


#ifndef kDevice_Name
#define                             kDevice_Name                        kDriver_Name " "
#endif

#ifndef kDevice2_Name
#define                             kDevice2_Name                       kDriver_Name " Mirror"
#endif

#endif

#ifndef kDevice_IsHidden
#define                             kDevice_IsHidden                    false
#endif

#ifndef kDevice2_IsHidden
#define                             kDevice2_IsHidden                   true
#endif



#ifndef kDevice_HasInput
#define                             kDevice_HasInput                    true
#endif

#ifndef kDevice_HasOutput
#define                             kDevice_HasOutput                   true
#endif

#ifndef kDevice2_HasInput
#define                             kDevice2_HasInput                   true
#endif

#ifndef kDevice2_HasOutput
#define                             kDevice2_HasOutput                  true
#endif



#ifndef kManufacturer_Name
#define                             kManufacturer_Name                  "Existential Audio Inc."
#endif

#define                             kLatency_Frame_Size                 0

#ifndef kNumber_Of_Channels
#define                             kNumber_Of_Channels                 2
#endif

#ifndef kEnableVolumeControl
#define                             kEnableVolumeControl                 true
#endif

#ifndef kCanBeDefaultDevice
#define                             kCanBeDefaultDevice                 true
#endif

#ifndef kCanBeDefaultSystemDevice
#define                             kCanBeDefaultSystemDevice           true
#endif

static pthread_mutex_t              gPlugIn_StateMutex                  = PTHREAD_MUTEX_INITIALIZER;
static UInt32                       gPlugIn_RefCount                    = 0;
static AudioServerPlugInHostRef     gPlugIn_Host                        = NULL;


static CFStringRef                  gBox_Name                           = NULL;

#ifndef kBox_Aquired
#define                             kBox_Aquired                 	true
#endif
static Boolean                      gBox_Acquired                       = kBox_Aquired;


static pthread_mutex_t              gDevice_IOMutex                     = PTHREAD_MUTEX_INITIALIZER;
static Float64                      gDevice_SampleRate                  = 48000.0;
static Float64                      gDevice_RequestedSampleRate         = 0.0;
static UInt64                       gDevice_IOIsRunning                 = 0;
static UInt64                       gDevice2_IOIsRunning                = 0;
static const UInt32                 kDevice_RingBufferSize              = 16384;
static Float64                      gDevice_HostTicksPerFrame           = 0.0;
static Float64                      gDevice_AdjustedTicksPerFrame       = 0.0;
static Float64                      gDevice_PreviousTicks               = 0.0;
static UInt64                       gDevice_NumberTimeStamps            = 0;
static Float64                      gDevice_AnchorSampleTime            = 0.0;
static UInt64                       gDevice_AnchorHostTime              = 0;

static bool                         gStream_Input_IsActive              = true;
static bool                         gStream_Output_IsActive             = true;

static const Float32                kVolume_MinDB                       = -64.0;
static const Float32                kVolume_MaxDB                       = 0.0;
static Float32                      gVolume_Master_Value                = 1.0;
static Float32                      gPitch_Adjust                       = 0.5;
static bool                         gMute_Master_Value                  = false;
static UInt32                       kClockSource_NumberItems            = 2;
#define                             kClockSource_InternalFixed         "Internal Fixed"
#define                             kClockSource_InternalAdjustable    "Internal Adjustable"
static UInt32                       gClockSource_Value                  = 0;
static bool                         gPitch_Adjust_Enabled               = false;

static struct ObjectInfo            kDevice_ObjectList[]                = {
#if kDevice_HasInput
    { kObjectID_Stream_Input,           kObjectType_Stream,     kAudioObjectPropertyScopeInput  },
    { kObjectID_Volume_Input_Master,    kObjectType_Control,    kAudioObjectPropertyScopeInput  },
    { kObjectID_Mute_Input_Master,      kObjectType_Control,    kAudioObjectPropertyScopeInput  },
#endif
#if kDevice_HasOutput
    { kObjectID_Stream_Output,          kObjectType_Stream,     kAudioObjectPropertyScopeOutput },
    { kObjectID_Volume_Output_Master,   kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Pitch_Adjust,           kObjectType_Control,    kAudioObjectPropertyScopeOutput },
#endif
    { kObjectID_ClockSource,            kObjectType_Control,    kAudioObjectPropertyScopeGlobal }
};

static struct ObjectInfo            kDevice2_ObjectList[]                = {
#if kDevice2_HasInput
    { kObjectID_Stream_Input,           kObjectType_Stream,     kAudioObjectPropertyScopeInput  },
    { kObjectID_Volume_Input_Master,    kObjectType_Control,    kAudioObjectPropertyScopeInput  },
    { kObjectID_Mute_Input_Master,      kObjectType_Control,    kAudioObjectPropertyScopeInput  },
#endif
#if kDevice2_HasOutput
    { kObjectID_Stream_Output,          kObjectType_Stream,     kAudioObjectPropertyScopeOutput },
    { kObjectID_Volume_Output_Master,   kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput },
#endif
};

static const UInt32                 kDevice_ObjectListSize              = sizeof(kDevice_ObjectList) / sizeof(struct ObjectInfo);
static const UInt32                 kDevice2_ObjectListSize              = sizeof(kDevice2_ObjectList) / sizeof(struct ObjectInfo);

#ifndef kSampleRates
#define                             kSampleRates       8000, 16000, 24000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
#endif

static Float64                      kDevice_SampleRates[]               = { kSampleRates };

static const UInt32                 kDevice_SampleRatesSize             = sizeof(kDevice_SampleRates) / sizeof(Float64);



#define                             kBits_Per_Channel                   32
#define                             kBytes_Per_Channel                  (kBits_Per_Channel/ 8)
#define                             kBytes_Per_Frame                    (kNumber_Of_Channels * kBytes_Per_Channel)
#define                             kRing_Buffer_Frame_Size             ((65536 + kLatency_Frame_Size))
static Float32*                     gRingBuffer = NULL;


//==================================================================================================
#pragma mark -
#pragma mark AudioServerPlugInDriverInterface Implementation
//==================================================================================================

#pragma mark Prototypes

//    Entry points for the COM methods
void*                BlackHole_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);
static HRESULT        BlackHole_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG        BlackHole_AddRef(void* inDriver);
static ULONG        BlackHole_Release(void* inDriver);
static OSStatus        BlackHole_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus        BlackHole_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus        BlackHole_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);
static OSStatus        BlackHole_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus        BlackHole_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus        BlackHole_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus        BlackHole_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static Boolean        BlackHole_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);
static OSStatus        BlackHole_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus        BlackHole_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus        BlackHole_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus        BlackHole_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus        BlackHole_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus        BlackHole_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
static OSStatus        BlackHole_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

//    Implementation
static Boolean        BlackHole_HasPlugInProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsPlugInPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetPlugInPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetPlugInPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetPlugInPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        BlackHole_HasBoxProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsBoxPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetBoxPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetBoxPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetBoxPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        BlackHole_HasDeviceProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsDevicePropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetDevicePropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetDevicePropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetDevicePropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        BlackHole_HasStreamProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsStreamPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetStreamPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetStreamPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetStreamPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        BlackHole_HasControlProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        BlackHole_IsControlPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        BlackHole_GetControlPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        BlackHole_GetControlPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        BlackHole_SetControlPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

#pragma mark The Interface

static AudioServerPlugInDriverInterface    gAudioServerPlugInDriverInterface =
{
    NULL,
    BlackHole_QueryInterface,
    BlackHole_AddRef,
    BlackHole_Release,
    BlackHole_Initialize,
    BlackHole_CreateDevice,
    BlackHole_DestroyDevice,
    BlackHole_AddDeviceClient,
    BlackHole_RemoveDeviceClient,
    BlackHole_PerformDeviceConfigurationChange,
    BlackHole_AbortDeviceConfigurationChange,
    BlackHole_HasProperty,
    BlackHole_IsPropertySettable,
    BlackHole_GetPropertyDataSize,
    BlackHole_GetPropertyData,
    BlackHole_SetPropertyData,
    BlackHole_StartIO,
    BlackHole_StopIO,
    BlackHole_GetZeroTimeStamp,
    BlackHole_WillDoIOOperation,
    BlackHole_BeginIOOperation,
    BlackHole_DoIOOperation,
    BlackHole_EndIOOperation
};
static AudioServerPlugInDriverInterface*    gAudioServerPlugInDriverInterfacePtr    = &gAudioServerPlugInDriverInterface;
static AudioServerPlugInDriverRef            gAudioServerPlugInDriverRef                = &gAudioServerPlugInDriverInterfacePtr;


#define RETURN_FORMATTED_STRING(_string_fmt)                          \
if(kHas_Driver_Name_Format)                                           \
{                                                                     \
	return CFStringCreateWithFormat(NULL, NULL, CFSTR(_string_fmt), kNumber_Of_Channels); \
}                                                                     \
else                                                                  \
{                                                                     \
	return CFStringCreateWithCString(NULL, _string_fmt, kCFStringEncodingUTF8); \
}

static CFStringRef get_box_uid(void)          { RETURN_FORMATTED_STRING(kBox_UID) }
static CFStringRef get_device_uid(void)       { RETURN_FORMATTED_STRING(kDevice_UID) }
static CFStringRef get_device_name(void)      { RETURN_FORMATTED_STRING(kDevice_Name) }
static CFStringRef get_device2_uid(void)      { RETURN_FORMATTED_STRING(kDevice2_UID) }
static CFStringRef get_device2_name(void)     { RETURN_FORMATTED_STRING(kDevice2_Name) }
static CFStringRef get_device_model_uid(void) { RETURN_FORMATTED_STRING(kDevice_ModelUID) }

// Volume conversions

static Float32 volume_to_decibel(Float32 volume)
{
	if (volume <= powf(10.0f, kVolume_MinDB / 20.0f))
		return kVolume_MinDB;
	else
		return 20.0f * log10f(volume);
}

static Float32 volume_from_decibel(Float32 decibel)
{
	if (decibel <= kVolume_MinDB)
		return 0.0f;
	else
		return powf(10.0f, decibel / 20.0f);
}

static Float32 volume_to_scalar(Float32 volume)
{
	Float32 decibel = volume_to_decibel(volume);
	return (decibel - kVolume_MinDB) / (kVolume_MaxDB - kVolume_MinDB);
}

static Float32 volume_from_scalar(Float32 scalar)
{
	Float32 decibel = scalar * (kVolume_MaxDB - kVolume_MinDB) + kVolume_MinDB;
	return volume_from_decibel(decibel);
}

static UInt32 device_object_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
            {
                if (scope == kAudioObjectPropertyScopeGlobal)
                {
                    return kDevice_ObjectListSize;
                }

                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
                {
                    count += (kDevice_ObjectList[i].scope == scope);
                }

                return count;
            }
            break;
            
        case kObjectID_Device2:
            {
                if (scope == kAudioObjectPropertyScopeGlobal)
                {
                    return kDevice2_ObjectListSize;
                }

                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
                {
                    count += (kDevice2_ObjectList[i].scope == scope);
                }

                return count;
            }
            break;
            
        default:
            return 0;
            break;
    }
}

static UInt32 device_stream_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
            {
                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
                {
                    count += (kDevice_ObjectList[i].type == kObjectType_Stream && (kDevice_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
                }

                return count;
            }
            break;
            
        case kObjectID_Device2:
            {
                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
                {
                    count += (kDevice2_ObjectList[i].type == kObjectType_Stream && (kDevice2_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
                }

                return count;
            }
            break;
            
        default:
            return 0;
            break;
    }
    

}

static UInt32 device_control_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
        {
            
            UInt32 count = 0;
            for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
            {
                count += (kDevice_ObjectList[i].type == kObjectType_Control && (kDevice_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
            }

            return count;
        }
            break;
        case kObjectID_Device2:
        {
            
            UInt32 count = 0;
            for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
            {
                count += (kDevice2_ObjectList[i].type == kObjectType_Control && (kDevice2_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
            }

            return count;
        }
            break;
            
        default:
            return 0;
            break;
    }

}

static UInt32 minimum(UInt32 a, UInt32 b) {
    return a < b ? a : b;
}

static bool is_valid_sample_rate(Float64 sample_rate)
{
    for(UInt32 i = 0; i < kDevice_SampleRatesSize; i++)
    {
        if (sample_rate == kDevice_SampleRates[i])
        {
            return true;
        }
    }

    return false;
}

#pragma mark Factory

void*	BlackHole_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
	//	This is the CFPlugIn factory function. Its job is to create the implementation for the given
	//	type provided that the type is supported. Because this driver is simple and all its
	//	initialization is handled via static initialization when the bundle is loaded, all that
	//	needs to be done is to return the AudioServerPlugInDriverRef that points to the driver's
	//	interface. A more complicated driver would create any base line objects it needs to satisfy
	//	the IUnknown methods that are used to discover that actual interface to talk to the driver.
	//	The majority of the driver's initialization should be handled in the Initialize() method of
	//	the driver's AudioServerPlugInDriverInterface.
	
	#pragma unused(inAllocator)
    void* theAnswer = NULL;
    if(CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID))
    {
		theAnswer = gAudioServerPlugInDriverRef;
    }
    return theAnswer;
}

#pragma mark Inheritance

static HRESULT	BlackHole_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface)
{
	//	This function is called by the HAL to get the interface to talk to the plug-in through.
	//	AudioServerPlugIns are required to support the IUnknown interface and the
	//	AudioServerPlugInDriverInterface. As it happens, all interfaces must also provide the
	//	IUnknown interface, so we can always just return the single interface we made with
	//	gAudioServerPlugInDriverInterfacePtr regardless of which one is asked for.

	//	declare the local variables
	HRESULT theAnswer = 0;
	CFUUIDRef theRequestedUUID = NULL;
	
	//	validate the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_QueryInterface: bad driver reference");
	FailWithAction(outInterface == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_QueryInterface: no place to store the returned interface");

	//	make a CFUUIDRef from inUUID
	theRequestedUUID = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
	FailWithAction(theRequestedUUID == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_QueryInterface: failed to create the CFUUIDRef");

	//	AudioServerPlugIns only support two interfaces, IUnknown (which has to be supported by all
	//	CFPlugIns and AudioServerPlugInDriverInterface (which is the actual interface the HAL will
	//	use).
	if(CFEqual(theRequestedUUID, IUnknownUUID) || CFEqual(theRequestedUUID, kAudioServerPlugInDriverInterfaceUUID))
	{
		pthread_mutex_lock(&gPlugIn_StateMutex);
		++gPlugIn_RefCount;
		pthread_mutex_unlock(&gPlugIn_StateMutex);
		*outInterface = gAudioServerPlugInDriverRef;
	}
	else
	{
		theAnswer = E_NOINTERFACE;
	}
	
	//	make sure to release the UUID we created
	CFRelease(theRequestedUUID);
		
Done:
	return theAnswer;
}

static ULONG	BlackHole_AddRef(void* inDriver)
{
	//	This call returns the resulting reference count after the increment.
	
	//	declare the local variables
	ULONG theAnswer = 0;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_AddRef: bad driver reference");

	//	increment the refcount
	pthread_mutex_lock(&gPlugIn_StateMutex);
	if(gPlugIn_RefCount < UINT32_MAX)
	{
		++gPlugIn_RefCount;
	}
	theAnswer = gPlugIn_RefCount;
	pthread_mutex_unlock(&gPlugIn_StateMutex);

Done:
	return theAnswer;
}

static ULONG	BlackHole_Release(void* inDriver)
{
	//	This call returns the resulting reference count after the decrement.

	//	declare the local variables
	ULONG theAnswer = 0;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_Release: bad driver reference");

	//	decrement the refcount
	pthread_mutex_lock(&gPlugIn_StateMutex);
	if(gPlugIn_RefCount > 0)
	{
		--gPlugIn_RefCount;
		//	Note that we don't do anything special if the refcount goes to zero as the HAL
		//	will never fully release a plug-in it opens. We keep managing the refcount so that
		//	the API semantics are correct though.
	}
	theAnswer = gPlugIn_RefCount;
	pthread_mutex_unlock(&gPlugIn_StateMutex);

Done:
	return theAnswer;
}

#pragma mark Basic Operations

static OSStatus	BlackHole_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost)
{
	//	The job of this method is, as the name implies, to get the driver initialized. One specific
	//	thing that needs to be done is to store the AudioServerPlugInHostRef so that it can be used
	//	later. Note that when this call returns, the HAL will scan the various lists the driver
	//	maintains (such as the device list) to get the initial set of objects the driver is
	//	publishing. So, there is no need to notify the HAL about any objects created as part of the
	//	execution of this method.

	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_Initialize: bad driver reference");
	
	//	store the AudioServerPlugInHostRef
	gPlugIn_Host = inHost;
	
	//	initialize the box acquired property from the settings
	CFPropertyListRef theSettingsData = NULL;
	gPlugIn_Host->CopyFromStorage(gPlugIn_Host, CFSTR("box acquired"), &theSettingsData);
	if(theSettingsData != NULL)
	{
		if(CFGetTypeID(theSettingsData) == CFBooleanGetTypeID())
		{
			gBox_Acquired = CFBooleanGetValue((CFBooleanRef)theSettingsData);
		}
		else if(CFGetTypeID(theSettingsData) == CFNumberGetTypeID())
		{
			SInt32 theValue = 0;
			CFNumberGetValue((CFNumberRef)theSettingsData, kCFNumberSInt32Type, &theValue);
			gBox_Acquired = theValue ? 1 : 0;
		}
		CFRelease(theSettingsData);
	}
	
	//	initialize the box name from the settings
	gPlugIn_Host->CopyFromStorage(gPlugIn_Host, CFSTR("box acquired"), &theSettingsData);
	if(theSettingsData != NULL)
	{
		if(CFGetTypeID(theSettingsData) == CFStringGetTypeID())
		{
			gBox_Name = (CFStringRef)theSettingsData;
			CFRetain(gBox_Name);
		}
		CFRelease(theSettingsData);
	}
	
	//	set the box name directly as a last resort
	if(gBox_Name == NULL)
	{
		gBox_Name = CFSTR("BlackHole Box");
	}
	
	//	calculate the host ticks per frame
	struct mach_timebase_info theTimeBaseInfo;
	mach_timebase_info(&theTimeBaseInfo);
	Float64 theHostClockFrequency = (Float64)theTimeBaseInfo.denom / (Float64)theTimeBaseInfo.numer;
	theHostClockFrequency *= 1000000000.0;
	gDevice_HostTicksPerFrame = theHostClockFrequency / gDevice_SampleRate;
    gDevice_AdjustedTicksPerFrame = gDevice_HostTicksPerFrame - gDevice_HostTicksPerFrame/100.0 * 2.0*(gPitch_Adjust - 0.5);
    
    // DebugMsg("BlackHole theTimeBaseInfo.numer: %u \t theTimeBaseInfo.denom: %u", theTimeBaseInfo.numer, theTimeBaseInfo.denom);
	
Done:
	return theAnswer;
}

static OSStatus	BlackHole_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID)
{
	//	This method is used to tell a driver that implements the Transport Manager semantics to
	//	create an AudioEndpointDevice from a set of AudioEndpoints. Since this driver is not a
	//	Transport Manager, we just check the arguments and return
	//	kAudioHardwareUnsupportedOperationError.
	
	#pragma unused(inDescription, inClientInfo, outDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = kAudioHardwareUnsupportedOperationError;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_CreateDevice: bad driver reference");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID)
{
	//	This method is used to tell a driver that implements the Transport Manager semantics to
	//	destroy an AudioEndpointDevice. Since this driver is not a Transport Manager, we just check
	//	the arguments and return kAudioHardwareUnsupportedOperationError.
	
	#pragma unused(inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = kAudioHardwareUnsupportedOperationError;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DestroyDevice: bad driver reference");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
	//	This method is used to inform the driver about a new client that is using the given device.
	//	This allows the device to act differently depending on who the client is. This driver does
	//	not need to track the clients using the device, so we just check the arguments and return
	//	successfully.
	
	#pragma unused(inClientInfo)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_AddDeviceClient: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_AddDeviceClient: bad device ID");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
	//	This method is used to inform the driver about a client that is no longer using the given
	//	device. This driver does not track clients, so we just check the arguments and return
	//	successfully.
	
	#pragma unused(inClientInfo)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_RemoveDeviceClient: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_RemoveDeviceClient: bad device ID");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
	//	This method is called to tell the device that it can perform the configuration change that it
	//	had requested via a call to the host method, RequestDeviceConfigurationChange(). The
	//	arguments, inChangeAction and inChangeInfo are the same as what was passed to
	//	RequestDeviceConfigurationChange().
	//
	//	The HAL guarantees that IO will be stopped while this method is in progress. The HAL will
	//	also handle figuring out exactly what changed for the non-control related properties. This
	//	means that the only notifications that would need to be sent here would be for either
	//	custom properties the HAL doesn't know about or for controls.
	//
	//	For the device implemented by this driver, sample rate changes and enabling/disabling
	//	the pitch adjust go through this process.
	//	These are the only states that can be changed for the device that aren't controls.
	//	Which change is requested is passed in the inChangeAction argument.
	
	#pragma unused(inChangeInfo)

	//	declare the local variables
	OSStatus theAnswer = 0;
    Float64 newSampleRate = 0.0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad driver reference");
    FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad device ID");
    switch(inChangeAction)
    {
        case ChangeAction_EnablePitchControl:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            gPitch_Adjust_Enabled = true;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            break;
        case ChangeAction_DisablePitchControl:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            gPitch_Adjust_Enabled = false;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            break;
        case ChangeAction_SetSampleRate:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            newSampleRate = gDevice_RequestedSampleRate;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            FailWithAction(!is_valid_sample_rate(newSampleRate), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad sample rate");
            
            //	lock the state mutex
            pthread_mutex_lock(&gPlugIn_StateMutex);
            
            //	change the sample rate
            gDevice_SampleRate = newSampleRate;
            
            //	recalculate the state that depends on the sample rate
            struct mach_timebase_info theTimeBaseInfo;
            mach_timebase_info(&theTimeBaseInfo);
            Float64 theHostClockFrequency = (Float64)theTimeBaseInfo.denom / (Float64)theTimeBaseInfo.numer;
            theHostClockFrequency *= 1000000000.0;
            gDevice_HostTicksPerFrame = theHostClockFrequency / gDevice_SampleRate;
            gDevice_AdjustedTicksPerFrame = gDevice_HostTicksPerFrame - gDevice_HostTicksPerFrame/100.0 * 2.0*(gPitch_Adjust - 0.5);
            
            //	unlock the state mutex
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            
            // DebugMsg("BlackHole theTimeBaseInfo.numer: %u \t theTimeBaseInfo.denom: %u", theTimeBaseInfo.numer, theTimeBaseInfo.denom);
            break;
    };
	
Done:
	return theAnswer;
}

static OSStatus	BlackHole_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
	//	This method is called to tell the driver that a request for a config change has been denied.
	//	This provides the driver an opportunity to clean up any state associated with the request.
	//	For this driver, an aborted config change requires no action. So we just check the arguments
	//	and return

	#pragma unused(inChangeAction, inChangeInfo)

	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad device ID");

Done:
	return theAnswer;
}

#pragma mark Property Operations

static Boolean	BlackHole_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasProperty: no address");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			theAnswer = BlackHole_HasPlugInProperty(inDriver, inObjectID, inClientProcessID, inAddress);
			break;
		
		case kObjectID_Box:
			theAnswer = BlackHole_HasBoxProperty(inDriver, inObjectID, inClientProcessID, inAddress);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			theAnswer = BlackHole_HasDeviceProperty(inDriver, inObjectID, inClientProcessID, inAddress);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			theAnswer = BlackHole_HasStreamProperty(inDriver, inObjectID, inClientProcessID, inAddress);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
		case kObjectID_Volume_Input_Master:
		case kObjectID_Mute_Input_Master:
		case kObjectID_Pitch_Adjust:
        case kObjectID_ClockSource:
			theAnswer = BlackHole_HasControlProperty(inDriver, inObjectID, inClientProcessID, inAddress);
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsPropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsPropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsPropertySettable: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			theAnswer = BlackHole_IsPlugInPropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Box:
			theAnswer = BlackHole_IsBoxPropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			theAnswer = BlackHole_IsDevicePropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			theAnswer = BlackHole_IsStreamPropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;

		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
		case kObjectID_Volume_Input_Master:
		case kObjectID_Mute_Input_Master:
		case kObjectID_Pitch_Adjust:
        case kObjectID_ClockSource:
			theAnswer = BlackHole_IsControlPropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;

		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPropertyDataSize: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			theAnswer = BlackHole_GetPlugInPropertyDataSize(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Box:
			theAnswer = BlackHole_GetBoxPropertyDataSize(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			theAnswer = BlackHole_GetDevicePropertyDataSize(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			theAnswer = BlackHole_GetStreamPropertyDataSize(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
			
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
		case kObjectID_Volume_Input_Master:
		case kObjectID_Mute_Input_Master:
		case kObjectID_Pitch_Adjust:
        case kObjectID_ClockSource:
			theAnswer = BlackHole_GetControlPropertyDataSize(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
			
		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPropertyData: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			theAnswer = BlackHole_GetPlugInPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Box:
			theAnswer = BlackHole_GetBoxPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			theAnswer = BlackHole_GetDevicePropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			theAnswer = BlackHole_GetStreamPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
		case kObjectID_Volume_Input_Master:
		case kObjectID_Mute_Input_Master:
		case kObjectID_Pitch_Adjust:
        case kObjectID_ClockSource:
			theAnswer = BlackHole_GetControlPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
			
		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt32 theNumberPropertiesChanged = 0;
	AudioObjectPropertyAddress theChangedAddresses[2];
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetPropertyData: no address");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			theAnswer = BlackHole_SetPlugInPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Box:
			theAnswer = BlackHole_SetBoxPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			theAnswer = BlackHole_SetDevicePropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			theAnswer = BlackHole_SetStreamPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
			
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
		case kObjectID_Volume_Input_Master:
		case kObjectID_Mute_Input_Master:
		case kObjectID_Pitch_Adjust:
        case kObjectID_ClockSource:
			theAnswer = BlackHole_SetControlPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
			
		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

	//	send any notifications
	if(theNumberPropertiesChanged > 0)
	{
		gPlugIn_Host->PropertiesChanged(gPlugIn_Host, inObjectID, theNumberPropertiesChanged, theChangedAddresses);
	}

Done:
	return theAnswer;
}

#pragma mark PlugIn Property Operations

static Boolean	BlackHole_HasPlugInProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the plug-in object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasPlugInProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasPlugInProperty: no address");
	FailIf(inObjectID != kObjectID_PlugIn, Done, "BlackHole_HasPlugInProperty: not the plug-in object");
	
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPlugInPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyBoxList:
		case kAudioPlugInPropertyTranslateUIDToBox:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
		case kAudioPlugInPropertyResourceBundle:
			theAnswer = true;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsPlugInPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the plug-in object can have its
	//	value changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsPlugInPropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsPlugInPropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsPlugInPropertySettable: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_PlugIn, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsPlugInPropertySettable: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPlugInPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyBoxList:
		case kAudioPlugInPropertyTranslateUIDToBox:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
		case kAudioPlugInPropertyResourceBundle:
			*outIsSettable = false;
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetPlugInPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPlugInPropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPlugInPropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPlugInPropertyDataSize: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_PlugIn, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPlugInPropertyDataSize: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPlugInPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			if(gBox_Acquired)
			{
				*outDataSize = 2 * sizeof(AudioClassID);
			}
			else
			{
				*outDataSize = sizeof(AudioClassID);
			}
			break;
			
		case kAudioPlugInPropertyBoxList:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToBox:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyDeviceList:
			if(gBox_Acquired)
			{
				*outDataSize = sizeof(AudioClassID)*2;
			}
			else
			{
				*outDataSize = 0;
			}
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetPlugInPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt32 theNumberItemsToFetch;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPlugInPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPlugInPropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPlugInPropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetPlugInPropertyData: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_PlugIn, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetPlugInPropertyData: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioPlugInClassID is kAudioObjectClassID
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the plug-in");
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	The class is always kAudioPlugInClassID for regular drivers
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the plug-in");
			*((AudioClassID*)outData) = kAudioPlugInClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The plug-in doesn't have an owning object
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the plug-in");
			*((AudioObjectID*)outData) = kAudioObjectUnknown;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the plug-in");
			*((CFStringRef*)outData) = CFSTR("Apple Inc.");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of boxes this driver implements (which is just 1)
			if(theNumberItemsToFetch > (gBox_Acquired ? 2 : 1))
			{
				theNumberItemsToFetch = (gBox_Acquired ? 2 : 1);
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 1)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
				((AudioObjectID*)outData)[0] = kObjectID_Device;
			}
			else if(theNumberItemsToFetch > 0)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
			}
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyBoxList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of boxes this driver implements (which is just 1)
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
			}
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToBox:
			//	This property takes the CFString passed in the qualifier and converts that
			//	to the object ID of the box it corresponds to. For this driver, there is
			//	just the one box. Note that it is not an error if the string in the
			//	qualifier doesn't match any devices. In such case, kAudioObjectUnknown is
			//	the object ID to return.
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToBox");
			FailWithAction(inQualifierDataSize == sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: the qualifier is the wrong size for kAudioPlugInPropertyTranslateUIDToBox");
			FailWithAction(inQualifierData == NULL, theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: no qualifier for kAudioPlugInPropertyTranslateUIDToBox");

			CFStringRef boxUID = get_box_uid();

			if(CFStringCompare(*((CFStringRef*)inQualifierData), boxUID, 0) == kCFCompareEqualTo)
			{
				CFStringRef formattedString = CFStringCreateWithFormat(NULL, NULL, CFSTR(kBox_UID), kNumber_Of_Channels);
				if(CFStringCompare(*((CFStringRef*)inQualifierData), formattedString, 0) == kCFCompareEqualTo)
				{
					*((AudioObjectID*)outData) = kObjectID_Box;
				}
				else
				{
					*((AudioObjectID*)outData) = kAudioObjectUnknown;
				}
				*outDataSize = sizeof(AudioObjectID);
				CFRelease(formattedString);

				*((AudioObjectID*)outData) = kObjectID_Box;
			}
			else
			{
				*((AudioObjectID*)outData) = kAudioObjectUnknown;
			}
			*outDataSize = sizeof(AudioObjectID);
			CFRelease(boxUID);
			break;
			
		case kAudioPlugInPropertyDeviceList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of devices this driver implements (which is just 1 if the
			//	box has been acquired)
			if(theNumberItemsToFetch > (gBox_Acquired ? 2 : 0))
			{
				theNumberItemsToFetch = (gBox_Acquired ? 2 : 0);
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 1)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Device;
                ((AudioObjectID*)outData)[1] = kObjectID_Device2;
			}
            else if(theNumberItemsToFetch > 0)
            {
                ((AudioObjectID*)outData)[0] = kObjectID_Device;
            }
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:
			//	This property takes the CFString passed in the qualifier and converts that
			//	to the object ID of the device it corresponds to. For this driver, there is
			//	just the one device. Note that it is not an error if the string in the
			//	qualifier doesn't match any devices. In such case, kAudioObjectUnknown is
			//	the object ID to return.
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToDevice");
			FailWithAction(inQualifierDataSize == sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: the qualifier is the wrong size for kAudioPlugInPropertyTranslateUIDToDevice");
			FailWithAction(inQualifierData == NULL, theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: no qualifier for kAudioPlugInPropertyTranslateUIDToDevice");
            
            
			
			CFStringRef deviceUID = get_device_uid();
            CFStringRef device2UID = get_device2_uid();

			if(CFStringCompare(*((CFStringRef*)inQualifierData), deviceUID, 0) == kCFCompareEqualTo)
			{
				*((AudioObjectID*)outData) = kObjectID_Device;
			}
            else if(CFStringCompare(*((CFStringRef*)inQualifierData), device2UID, 0) == kCFCompareEqualTo)
            {
                *((AudioObjectID*)outData) = kObjectID_Device2;
            }
			else
			{
				*((AudioObjectID*)outData) = kAudioObjectUnknown;
			}
			*outDataSize = sizeof(AudioObjectID);
			CFRelease(deviceUID);
            CFRelease(device2UID);
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			//	The resource bundle is a path relative to the path of the plug-in's bundle.
			//	To specify that the plug-in bundle itself should be used, we just return the
			//	empty string.
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyResourceBundle");
			*((CFStringRef*)outData) = CFSTR("");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetPlugInPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData, inDataSize, inData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetPlugInPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetPlugInPropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetPlugInPropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetPlugInPropertyData: no place to return the properties that changed");
	FailWithAction(inObjectID != kObjectID_PlugIn, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetPlugInPropertyData: not the plug-in object");
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPlugInPropertyData() method.
	switch(inAddress->mSelector)
	{
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

#pragma mark Box Property Operations

static Boolean	BlackHole_HasBoxProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the box object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasBoxProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasBoxProperty: no address");
	FailIf(inObjectID != kObjectID_Box, Done, "BlackHole_HasBoxProperty: not the box object");
	
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetBoxPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyModelName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioObjectPropertyIdentify:
		case kAudioObjectPropertySerialNumber:
		case kAudioObjectPropertyFirmwareVersion:
		case kAudioBoxPropertyBoxUID:
		case kAudioBoxPropertyTransportType:
		case kAudioBoxPropertyHasAudio:
		case kAudioBoxPropertyHasVideo:
		case kAudioBoxPropertyHasMIDI:
		case kAudioBoxPropertyIsProtected:
		case kAudioBoxPropertyAcquired:
		case kAudioBoxPropertyAcquisitionFailed:
		case kAudioBoxPropertyDeviceList:
			theAnswer = true;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsBoxPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the plug-in object can have its
	//	value changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsBoxPropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsBoxPropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsBoxPropertySettable: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Box, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsBoxPropertySettable: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetBoxPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyModelName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioObjectPropertySerialNumber:
		case kAudioObjectPropertyFirmwareVersion:
		case kAudioBoxPropertyBoxUID:
		case kAudioBoxPropertyTransportType:
		case kAudioBoxPropertyHasAudio:
		case kAudioBoxPropertyHasVideo:
		case kAudioBoxPropertyHasMIDI:
		case kAudioBoxPropertyIsProtected:
		case kAudioBoxPropertyAcquisitionFailed:
		case kAudioBoxPropertyDeviceList:
			*outIsSettable = false;
			break;
		
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyIdentify:
		case kAudioBoxPropertyAcquired:
			*outIsSettable = true;
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetBoxPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetBoxPropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetBoxPropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetBoxPropertyDataSize: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Box, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetBoxPropertyDataSize: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetBoxPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyModelName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			*outDataSize = 0;
			break;
			
		case kAudioObjectPropertyIdentify:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioObjectPropertySerialNumber:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyFirmwareVersion:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyBoxUID:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyTransportType:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasAudio:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasVideo:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasMIDI:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyIsProtected:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquired:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquisitionFailed:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyDeviceList:
			{
				pthread_mutex_lock(&gPlugIn_StateMutex);
				*outDataSize = gBox_Acquired ? sizeof(AudioObjectID) * 2 : 0;
				pthread_mutex_unlock(&gPlugIn_StateMutex);
			}
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetBoxPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetBoxPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetBoxPropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetBoxPropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetBoxPropertyData: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Box, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetBoxPropertyData: not the plug-in object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioBoxClassID is kAudioObjectClassID
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the box");
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	The class is always kAudioBoxClassID for regular drivers
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the box");
			*((AudioClassID*)outData) = kAudioBoxClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The owner is the plug-in object
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the box");
			*((AudioObjectID*)outData) = kObjectID_PlugIn;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			//	This is the human readable name of the maker of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((CFStringRef*)outData) = gBox_Name;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(*((CFStringRef*)outData) != NULL)
			{
				CFRetain(*((CFStringRef*)outData));
			}
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyModelName:
			//	This is the human readable name of the maker of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");
			*((CFStringRef*)outData) = CFSTR("BlackHole");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");
			*((CFStringRef*)outData) = CFSTR("Existential Audio Inc.");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	This returns the objects directly owned by the object. Boxes don't own anything.
			*outDataSize = 0;
			break;
			
		case kAudioObjectPropertyIdentify:
			//	This is used to highling the device in the UI, but it's value has no meaning
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyIdentify for the box");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioObjectPropertySerialNumber:
			//	This is the human readable serial number of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertySerialNumber for the box");
			*((CFStringRef*)outData) = CFSTR("dd658747-4b9a-4de8-a001-c6a2ef1bb235");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyFirmwareVersion:
			//	This is the human readable firmware version of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyFirmwareVersion for the box");
			*((CFStringRef*)outData) = CFSTR("0.5.1");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyBoxUID:
			//	Boxes have UIDs the same as devices
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");

			*((CFStringRef*)outData) = get_box_uid();
			break;
			
		case kAudioBoxPropertyTransportType:
			//	This value represents how the device is attached to the system. This can be
			//	any 32 bit integer, but common values for this property are defined in
			//	<CoreAudio/AudioHardwareBase.h>
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioDevicePropertyTransportType for the box");
			*((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasAudio:
			//	Indicates whether or not the box has audio capabilities
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyHasAudio for the box");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasVideo:
			//	Indicates whether or not the box has video capabilities
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyHasVideo for the box");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasMIDI:
			//	Indicates whether or not the box has MIDI capabilities
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyHasMIDI for the box");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyIsProtected:
			//	Indicates whether or not the box has requires authentication to use
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyIsProtected for the box");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquired:
			//	When set to a non-zero value, the device is acquired for use by the local machine
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyAcquired for the box");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((UInt32*)outData) = gBox_Acquired ? 1 : 0;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquisitionFailed:
			//	This is used for notifications to say when an attempt to acquire a device has failed.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyAcquisitionFailed for the box");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyDeviceList:
			//	This is used to indicate which devices came from this box
			pthread_mutex_lock(&gPlugIn_StateMutex);
			if(gBox_Acquired)
			{
                if(inDataSize < sizeof(AudioObjectID))
                {
                    theAnswer = kAudioHardwareBadPropertySizeError;
                    *outDataSize = 0;
                }
                else
                {
                    if (inDataSize >= sizeof(AudioObjectID) * 2)
                    {
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        ((AudioObjectID*)outData)[1] = kObjectID_Device2;
                        *outDataSize = sizeof(AudioObjectID) * 2;
                    }
                    else
                    {
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        *outDataSize = sizeof(AudioObjectID) * 1;
                    }
                }
			}
			else
			{
				*outDataSize = 0;
			}
            
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetBoxPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData, inDataSize, inData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetBoxPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetBoxPropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetBoxPropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetBoxPropertyData: no place to return the properties that changed");
	FailWithAction(inObjectID != kObjectID_Box, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetBoxPropertyData: not the box object");
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetPlugInPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyName:
			//	Boxes should allow their name to be editable
			{
				FailWithAction(inData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetBoxPropertyData: NULL data for kAudioObjectPropertyName");
				FailWithAction(inDataSize != sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetBoxPropertyData: wrong size for the data for kAudioObjectPropertyName");
				CFStringRef* theNewName = (CFStringRef*)inData;
				pthread_mutex_lock(&gPlugIn_StateMutex);
				if((theNewName != NULL) && (*theNewName != NULL))
				{
					CFRetain(*theNewName);
				}
				if(gBox_Name != NULL)
				{
					CFRelease(gBox_Name);
				}
				gBox_Name = *theNewName;
				pthread_mutex_unlock(&gPlugIn_StateMutex);
				*outNumberPropertiesChanged = 1;
				outChangedAddresses[0].mSelector = kAudioObjectPropertyName;
				outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
				outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
			}
			break;
			
		case kAudioObjectPropertyIdentify:
			//	since we don't have any actual hardware to flash, we will schedule a notification for
			//	this property off into the future as a testing thing. Note that a real implementation
			//	of this property should only send the notification if the hardware wants the app to
			//	flash it's UI for the device.
			{
				syslog(LOG_NOTICE, "The identify property has been set on the Box implemented by the BlackHole driver.");
				FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetBoxPropertyData: wrong size for the data for kAudioObjectPropertyIdentify");
				dispatch_after(dispatch_time(0, 2ULL * 1000ULL * 1000ULL * 1000ULL), dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																																		{
																																			AudioObjectPropertyAddress theAddress = { kAudioObjectPropertyIdentify, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
																																			gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Box, 1, &theAddress);
																																		});
			}
			break;
			
		case kAudioBoxPropertyAcquired:
			//	When the box is acquired, it means the contents, namely the device, are available to the system
			{
				FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetBoxPropertyData: wrong size for the data for kAudioBoxPropertyAcquired");
				pthread_mutex_lock(&gPlugIn_StateMutex);
				if(gBox_Acquired != (*((UInt32*)inData) != 0))
				{
					//	the new value is different from the old value, so save it
					gBox_Acquired = *((UInt32*)inData) != 0;
					gPlugIn_Host->WriteToStorage(gPlugIn_Host, CFSTR("box acquired"), gBox_Acquired ? kCFBooleanTrue : kCFBooleanFalse);
					
					//	and it means that this property and the device list property have changed
					*outNumberPropertiesChanged = 2;
					outChangedAddresses[0].mSelector = kAudioBoxPropertyAcquired;
					outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
					outChangedAddresses[1].mSelector = kAudioBoxPropertyDeviceList;
					outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
					
					//	but it also means that the device list has changed for the plug-in too
					dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																									{
																										AudioObjectPropertyAddress theAddress = { kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
																										gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_PlugIn, 1, &theAddress);
																									});
				}
				pthread_mutex_unlock(&gPlugIn_StateMutex);
			}
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

#pragma mark Device Property Operations

static Boolean	BlackHole_HasDeviceProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasDeviceProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasDeviceProperty: no address");
	FailIf(inObjectID != kObjectID_Device && inObjectID != kObjectID_Device2, Done, "BlackHole_HasDeviceProperty: not the device object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetDevicePropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertyNominalSampleRate:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyZeroTimeStampPeriod:
		case kAudioDevicePropertyIcon:
		case kAudioDevicePropertyStreams:
			theAnswer = true;
			break;
			
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
			theAnswer = (inAddress->mScope == kAudioObjectPropertyScopeInput) || (inAddress->mScope == kAudioObjectPropertyScopeOutput);
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsDevicePropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsDevicePropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsDevicePropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsDevicePropertySettable: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Device && inObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsDevicePropertySettable: not the device object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetDevicePropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertyStreams:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyZeroTimeStampPeriod:
		case kAudioDevicePropertyIcon:
			*outIsSettable = false;
			break;
		
		case kAudioDevicePropertyNominalSampleRate:
			*outIsSettable = true;
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetDevicePropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetDevicePropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetDevicePropertyDataSize: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Device && inObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyDataSize: not the device object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetDevicePropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
            *outDataSize = device_object_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			*outDataSize = sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			*outDataSize = sizeof(AudioClassID);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
            *outDataSize = device_stream_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
            *outDataSize = device_control_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			*outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			*outDataSize = kDevice_SampleRatesSize * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			*outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			*outDataSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (kNumber_Of_Channels * sizeof(AudioChannelDescription));
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyIcon:
			*outDataSize = sizeof(CFURLRef);
			break;

		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetDevicePropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt32 theNumberItemsToFetch;
	UInt32 theItemIndex;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetDevicePropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetDevicePropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetDevicePropertyData: no place to put the return value");
	FailWithAction(inObjectID != kObjectID_Device && inObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyData: not the device object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioDeviceClassID is kAudioObjectClassID
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the device");
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	The class is always kAudioDeviceClassID for devices created by drivers
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioObjectPropertyClass for the device");
			*((AudioClassID*)outData) = kAudioDeviceClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The device's owner is the plug-in object
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the device");
			*((AudioObjectID*)outData) = kObjectID_PlugIn;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			//	This is the human readable name of the device.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
            
            switch (inObjectID) {
                case kObjectID_Device:
                    *((CFStringRef*)outData) = get_device_name();
                    *outDataSize = sizeof(CFStringRef);
                    break;
                    
                case kObjectID_Device2:
                    *((CFStringRef*)outData) = get_device2_name();
                    *outDataSize = sizeof(CFStringRef);
                    break;
            }
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
			*((CFStringRef*)outData) = CFSTR(kManufacturer_Name);
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_object_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with the right objects
            switch (inObjectID) {
                case kObjectID_Device:
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        if (kDevice_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal)
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                        }
                    }
                    break;

                case kObjectID_Device2:
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        if (kDevice2_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal)
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice2_ObjectList[i].id;
                        }
                    }
                    break;
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			//	This is a CFString that is a persistent token that can identify the same
			//	audio device across boot sessions. Note that two instances of the same
			//	device must have different values for this property.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");

            switch (inObjectID) {
                case kObjectID_Device:
                    *((CFStringRef*)outData) = get_device_uid();
                    *outDataSize = sizeof(CFStringRef);
                    break;
                    
                case kObjectID_Device2:
                    *((CFStringRef*)outData) = get_device2_uid();
                    *outDataSize = sizeof(CFStringRef);
                    break;
            }
            

			break;

		case kAudioDevicePropertyModelUID:
			//	This is a CFString that is a persistent token that can identify audio
			//	devices that are the same kind of device. Note that two instances of the
			//	save device must have the same value for this property.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyModelUID for the device");

            *((CFStringRef*)outData) = get_device_model_uid();
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			//	This value represents how the device is attached to the system. This can be
			//	any 32 bit integer, but common values for this property are defined in
			//	<CoreAudio/AudioHardwareBase.h>
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyTransportType for the device");
			*((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			//	The related devices property identifys device objects that are very closely
			//	related. Generally, this is for relating devices that are packaged together
			//	in the hardware such as when the input side and the output side of a piece
			//	of hardware can be clocked separately and therefore need to be represented
			//	as separate AudioDevice objects. In such case, both devices would report
			//	that they are related to each other. Note that at minimum, a device is
			//	related to itself, so this list will always be at least one item long.

			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	we only have the one device...
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
			{
                switch (inObjectID) {
                    case kObjectID_Device:
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        break;
                        
                    case kObjectID_Device2:
                        ((AudioObjectID*)outData)[0] = kObjectID_Device2;
                        break;
                }
				
			}
			
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			//	This property allows the device to declare what other devices it is
			//	synchronized with in hardware. The way it works is that if two devices have
			//	the same value for this property and the value is not zero, then the two
			//	devices are synchronized in hardware. Note that a device that either can't
			//	be synchronized with others or doesn't know should return 0 for this
			//	property.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyClockDomain for the device");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			//	This property returns whether or not the device is alive. Note that it is
			//	not uncommon for a device to be dead but still momentarily available in the
			//	device list. In the case of this device, it will always be alive.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsAlive for the device");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

        case kAudioDevicePropertyDeviceIsRunning:
            //    This property returns whether or not IO is running for the device. Note that
            //    we need to take both the state lock to check this value for thread safety.
            FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsRunning for the device");
            pthread_mutex_lock(&gPlugIn_StateMutex);
            if (inObjectID == kObjectID_Device) {
                *((UInt32*)outData) = ((gDevice_IOIsRunning > 0) > 0) ? 1 : 0;
            }
            
            switch (inObjectID) {
                case kObjectID_Device:
                    *((UInt32*)outData) = ((gDevice_IOIsRunning > 0) > 0) ? 1 : 0;
                    break;
                case kObjectID_Device2:
                    *((UInt32*)outData) = ((gDevice2_IOIsRunning > 0) > 0) ? 1 : 0;
                    break;
                default:
                    *((UInt32*)outData) = 0;
                    break;
            }
            
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            *outDataSize = sizeof(UInt32);
            break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			//	This property returns whether or not the device wants to be able to be the
			//	default device for content. This is the device that iTunes and QuickTime
			//	will use to play their content on and FaceTime will use as it's microhphone.
			//	Nearly all devices should allow for this.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultDevice for the device");
			*((UInt32*)outData) = kCanBeDefaultDevice;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			//	This property returns whether or not the device wants to be the system
			//	default device. This is the device that is used to play interface sounds and
			//	other incidental or UI-related sounds on. Most devices should allow this
			//	although devices with lots of latency may not want to.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultSystemDevice for the device");
			*((UInt32*)outData) = kCanBeDefaultSystemDevice;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			//	This property returns the presentation latency of the device. For this,
			//	device, the value is 0 due to the fact that it always vends silence.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyLatency for the device");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_stream_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with as many objects as requested
            switch (inObjectID) {
                case kObjectID_Device:
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        if ((kDevice_ObjectList[i].type == kObjectType_Stream) &&
                            (kDevice_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal))
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                        }
                    }
                    break;

                case kObjectID_Device2:
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        if ((kDevice2_ObjectList[i].type == kObjectType_Stream) &&
                            (kDevice2_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal))
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice2_ObjectList[i].id;
                        }
                    }
                    break;
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned

            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_control_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with as many objects as requested
            switch (inObjectID) {
                case kObjectID_Device:
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        // TODO remove hack! There must be a better way than looking for a fixed i
                        if ((kDevice_ObjectList[i].type == kObjectType_Control) && !(!gPitch_Adjust_Enabled && kDevice_ObjectList[i].id==kObjectID_Pitch_Adjust))
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                        }
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    break;

                case kObjectID_Device2:
                    for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
                    {
                        if ((kDevice_ObjectList[i].type == kObjectType_Control) && !(!gPitch_Adjust_Enabled && kDevice_ObjectList[i].id==kObjectID_Pitch_Adjust))
                        {
                            ((AudioObjectID*)outData)[k++] = kDevice2_ObjectList[i].id;
                        }
                    }
                    break;
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			//	This property returns the how close to now the HAL can read and write. For
			//	this, device, the value is 0 due to the fact that it always vends silence.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertySafetyOffset for the device");
			*((UInt32*)outData) = kLatency_Frame_Size;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			//	This property returns the nominal sample rate of the device. Note that we
			//	only need to take the state lock to get this value.
			FailWithAction(inDataSize < sizeof(Float64), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyNominalSampleRate for the device");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((Float64*)outData) = gDevice_SampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			//	This returns all nominal sample rates the device supports as an array of
			//	AudioValueRangeStructs. Note that for discrete sampler rates, the range
			//	will have the minimum value equal to the maximum value.
			
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioValueRange);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > kDevice_SampleRatesSize)
			{
				theNumberItemsToFetch = kDevice_SampleRatesSize;
			}
			
            //	fill out the return array
            for(UInt32 i = 0; i < theNumberItemsToFetch; i++)
            {
                ((AudioValueRange*)outData)[i].mMinimum = kDevice_SampleRates[i];
                ((AudioValueRange*)outData)[i].mMaximum = kDevice_SampleRates[i];
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			//	This returns whether or not the device is visible to clients.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyIsHidden for the device");
            
            switch (inObjectID) {
                case kObjectID_Device:
                    *((UInt32*)outData) = kDevice_IsHidden;
                    break;
                
                case kObjectID_Device2:
                    *((UInt32*)outData) = kDevice2_IsHidden;
                    break;
            }
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			//	This property returns which two channels to use as left/right for stereo
			//	data by default. Note that the channel numbers are 1-based.xz
			FailWithAction(inDataSize < (2 * sizeof(UInt32)), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelsForStereo for the device");
			((UInt32*)outData)[0] = 1;
			((UInt32*)outData)[1] = 2;
			*outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			//	This property returns the default AudioChannelLayout to use for the device
			//	by default. For this device, we return a stereo ACL.
			{
				//	calculate how big the
				UInt32 theACLSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (kNumber_Of_Channels * sizeof(AudioChannelDescription));
				FailWithAction(inDataSize < theACLSize, theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelLayout for the device");
				((AudioChannelLayout*)outData)->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
				((AudioChannelLayout*)outData)->mChannelBitmap = 0;
				((AudioChannelLayout*)outData)->mNumberChannelDescriptions = kNumber_Of_Channels;
				for(theItemIndex = 0; theItemIndex < kNumber_Of_Channels; ++theItemIndex)
				{
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelLabel = kAudioChannelLabel_Left + theItemIndex;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelFlags = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[0] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[1] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[2] = 0;
				}
				*outDataSize = theACLSize;
			}
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			//	This property returns how many frames the HAL should expect to see between
			//	successive sample times in the zero time stamps this device provides.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyZeroTimeStampPeriod for the device");
			*((UInt32*)outData) = kDevice_RingBufferSize;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyIcon:
			{
				//	This is a CFURL that points to the device's Icon in the plug-in's resource bundle.
				FailWithAction(inDataSize < sizeof(CFURLRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");
				CFBundleRef theBundle = CFBundleGetBundleWithIdentifier(CFSTR(kPlugIn_BundleID));
				FailWithAction(theBundle == NULL, theAnswer = kAudioHardwareUnspecifiedError, Done, "BlackHole_GetDevicePropertyData: could not get the plug-in bundle for kAudioDevicePropertyIcon");
				CFURLRef theURL = CFBundleCopyResourceURL(theBundle, CFSTR(kPlugIn_Icon), NULL, NULL);
				FailWithAction(theURL == NULL, theAnswer = kAudioHardwareUnspecifiedError, Done, "BlackHole_GetDevicePropertyData: could not get the URL for kAudioDevicePropertyIcon");
				*((CFURLRef*)outData) = theURL;
				*outDataSize = sizeof(CFURLRef);
			}
			break;
			
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetDevicePropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	Float64 theOldSampleRate;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetDevicePropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no place to return the properties that changed");
	FailWithAction(inObjectID != kObjectID_Device && inObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetDevicePropertyData: not the device object");
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetDevicePropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioDevicePropertyNominalSampleRate:
			//	Changing the sample rate needs to be handled via the
			//	RequestConfigChange/PerformConfigChange machinery.

			//	check the arguments
			FailWithAction(inDataSize != sizeof(Float64), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetDevicePropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
			FailWithAction(!is_valid_sample_rate(*(const Float64*)inData), theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: unsupported value for kAudioDevicePropertyNominalSampleRate");
			
			//	make sure that the new value is different than the old value
			pthread_mutex_lock(&gPlugIn_StateMutex);
			theOldSampleRate = gDevice_SampleRate;
			gDevice_RequestedSampleRate = *((const Float64*)inData);
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(*((const Float64*)inData) != theOldSampleRate)
			{
				//	we dispatch this so that the change can happen asynchronously
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, ChangeAction_SetSampleRate, NULL); });
			}
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

#pragma mark Stream Property Operations

static Boolean	BlackHole_HasStreamProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasStreamProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasStreamProperty: no address");
	FailIf((inObjectID != kObjectID_Stream_Input) && (inObjectID != kObjectID_Stream_Output), Done, "BlackHole_HasStreamProperty: not a stream object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetStreamPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			theAnswer = true;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsStreamPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsStreamPropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsStreamPropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsStreamPropertySettable: no place to put the return value");
	FailWithAction((inObjectID != kObjectID_Stream_Input) && (inObjectID != kObjectID_Stream_Output), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsStreamPropertySettable: not a stream object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetStreamPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			*outIsSettable = false;
			break;
		
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			*outIsSettable = true;
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetStreamPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetStreamPropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetStreamPropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetStreamPropertyDataSize: no place to put the return value");
	FailWithAction((inObjectID != kObjectID_Stream_Input) && (inObjectID != kObjectID_Stream_Output), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetStreamPropertyDataSize: not a stream object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetStreamPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;

		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;

		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyOwnedObjects:
			*outDataSize = 0 * sizeof(AudioObjectID);
			break;

		case kAudioStreamPropertyIsActive:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyDirection:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyTerminalType:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyStartingChannel:
			*outDataSize = sizeof(UInt32);
			break;
		
		case kAudioStreamPropertyLatency:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			*outDataSize = sizeof(AudioStreamBasicDescription);
			break;

		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			*outDataSize = kDevice_SampleRatesSize * sizeof(AudioStreamRangedDescription);
			break;

		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetStreamPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt32 theNumberItemsToFetch;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetStreamPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetStreamPropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetStreamPropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetStreamPropertyData: no place to put the return value");
	FailWithAction((inObjectID != kObjectID_Stream_Input) && (inObjectID != kObjectID_Stream_Output), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetStreamPropertyData: not a stream object");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioStreamClassID is kAudioObjectClassID
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the stream");
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	The class is always kAudioStreamClassID for streams created by drivers
			FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the stream");
			*((AudioClassID*)outData) = kAudioStreamClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The stream's owner is the device object
			FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the stream");
			*((AudioObjectID*)outData) = kObjectID_Device;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Streams do not own any objects
			*outDataSize = 0 * sizeof(AudioObjectID);
			break;

		case kAudioStreamPropertyIsActive:
			//	This property tells the device whether or not the given stream is going to
			//	be used for IO. Note that we need to take the state lock to examine this
			//	value.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyIsActive for the stream");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? gStream_Input_IsActive : gStream_Output_IsActive;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyDirection:
			//	This returns whether the stream is an input stream or an output stream.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyDirection for the stream");
			*((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? 1 : 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyTerminalType:
			//	This returns a value that indicates what is at the other end of the stream
			//	such as a speaker or headphones, or a microphone. Values for this property
			//	are defined in <CoreAudio/AudioHardwareBase.h>
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyTerminalType for the stream");
			*((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyStartingChannel:
			//	This property returns the absolute channel number for the first channel in
			//	the stream. For example, if a device has two output streams with two
			//	channels each, then the starting channel number for the first stream is 1
			//	and the starting channel number fo the second stream is 3.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyStartingChannel for the stream");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyLatency:
			//	This property returns any additional presentation latency the stream has.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyStartingChannel for the stream");
			*((UInt32*)outData) = kLatency_Frame_Size;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			//	This returns the current format of the stream in an
			//	AudioStreamBasicDescription. Note that we need to hold the state lock to get
			//	this value.
			//	Note that for devices that don't override the mix operation, the virtual
			//	format has to be the same as the physical format.
			FailWithAction(inDataSize < sizeof(AudioStreamBasicDescription), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyVirtualFormat for the stream");
			pthread_mutex_lock(&gPlugIn_StateMutex);
            ((AudioStreamBasicDescription*)outData)->mSampleRate = gDevice_SampleRate;
            ((AudioStreamBasicDescription*)outData)->mFormatID = kAudioFormatLinearPCM;
            ((AudioStreamBasicDescription*)outData)->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
            ((AudioStreamBasicDescription*)outData)->mBytesPerPacket = kBytes_Per_Channel * kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mFramesPerPacket = 1;
            ((AudioStreamBasicDescription*)outData)->mBytesPerFrame = kBytes_Per_Channel * kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mChannelsPerFrame = kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mBitsPerChannel = kBits_Per_Channel;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(AudioStreamBasicDescription);
			break;

		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			//	This returns an array of AudioStreamRangedDescriptions that describe what
			//	formats are supported.

			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioStreamRangedDescription);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > kDevice_SampleRatesSize)
			{
				theNumberItemsToFetch = kDevice_SampleRatesSize;
			}

            //	fill out the return array
            for(UInt32 i = 0; i < theNumberItemsToFetch; i++)
            {
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mSampleRate = kDevice_SampleRates[i];
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[i].mSampleRateRange.mMinimum = kDevice_SampleRates[i];
                ((AudioStreamRangedDescription*)outData)[i].mSampleRateRange.mMaximum = kDevice_SampleRates[i];
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioStreamRangedDescription);
			break;

		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetStreamPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	Float64 theOldSampleRate;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetStreamPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetStreamPropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetStreamPropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetStreamPropertyData: no place to return the properties that changed");
	FailWithAction((inObjectID != kObjectID_Stream_Input) && (inObjectID != kObjectID_Stream_Output), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetStreamPropertyData: not a stream object");
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetStreamPropertyData() method.
	switch(inAddress->mSelector)
	{
		case kAudioStreamPropertyIsActive:
			//	Changing the active state of a stream doesn't affect IO or change the structure
			//	so we can just save the state and send the notification.
			FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetStreamPropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			if(inObjectID == kObjectID_Stream_Input)
			{
				if(gStream_Input_IsActive != (*((const UInt32*)inData) != 0))
				{
					gStream_Input_IsActive = *((const UInt32*)inData) != 0;
					*outNumberPropertiesChanged = 1;
					outChangedAddresses[0].mSelector = kAudioStreamPropertyIsActive;
					outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
				}
			}
			else
			{
				if(gStream_Output_IsActive != (*((const UInt32*)inData) != 0))
				{
					gStream_Output_IsActive = *((const UInt32*)inData) != 0;
					*outNumberPropertiesChanged = 1;
					outChangedAddresses[0].mSelector = kAudioStreamPropertyIsActive;
					outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
				}
			}
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			break;
			
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			//	Changing the stream format needs to be handled via the
			//	RequestConfigChange/PerformConfigChange machinery. Note that because this
			//	device only supports 2 channel 32 bit float data, the only thing that can
			//	change is the sample rate.
			FailWithAction(inDataSize != sizeof(AudioStreamBasicDescription), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetStreamPropertyData: wrong size for the data for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mFormatID != kAudioFormatLinearPCM, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported format ID for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mFormatFlags != (kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked), theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported format flags for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mBytesPerPacket != kBytes_Per_Frame, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported bytes per packet for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mFramesPerPacket != 1, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported frames per packet for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mBytesPerFrame != kBytes_Per_Frame, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported bytes per frame for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mChannelsPerFrame != kNumber_Of_Channels, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported channels per frame for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(((const AudioStreamBasicDescription*)inData)->mBitsPerChannel != kBits_Per_Channel, theAnswer = kAudioDeviceUnsupportedFormatError, Done, "BlackHole_SetStreamPropertyData: unsupported bits per channel for kAudioStreamPropertyPhysicalFormat");
			FailWithAction(!is_valid_sample_rate(((const AudioStreamBasicDescription*)inData)->mSampleRate), theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetStreamPropertyData: unsupported sample rate for kAudioStreamPropertyPhysicalFormat");
			
			//	If we made it this far, the requested format is something we support, so make sure the sample rate is actually different
			pthread_mutex_lock(&gPlugIn_StateMutex);
			theOldSampleRate = gDevice_SampleRate;
			gDevice_RequestedSampleRate = ((const AudioStreamBasicDescription*)inData)->mSampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(((const AudioStreamBasicDescription*)inData)->mSampleRate != theOldSampleRate)
			{
				//	we dispatch this so that the change can happen asynchronously
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, ChangeAction_SetSampleRate, NULL); });
			}
			break;
		
		default:
			theAnswer = kAudioHardwareUnknownPropertyError;
			break;
	};

Done:
	return theAnswer;
}

#pragma mark Control Property Operations

static Boolean	BlackHole_HasControlProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean theAnswer = false;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "BlackHole_HasControlProperty: bad driver reference");
	FailIf(inAddress == NULL, Done, "BlackHole_HasControlProperty: no address");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetControlPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_Volume_Input_Master:
		case kObjectID_Volume_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
				case kAudioLevelControlPropertyScalarValue:
				case kAudioLevelControlPropertyDecibelValue:
				case kAudioLevelControlPropertyDecibelRange:
				case kAudioLevelControlPropertyConvertScalarToDecibels:
				case kAudioLevelControlPropertyConvertDecibelsToScalar:
					theAnswer = true;
					break;
			};
			break;
		
		case kObjectID_Mute_Input_Master:
		case kObjectID_Mute_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
				case kAudioBooleanControlPropertyValue:
					theAnswer = true;
					break;
			};
			break;

		case kObjectID_Pitch_Adjust:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
				case kAudioStereoPanControlPropertyValue:
					theAnswer = true;
					break;
			};
			break;
			
		case kObjectID_ClockSource:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
				case kAudioSelectorControlPropertyCurrentItem:
				case kAudioSelectorControlPropertyAvailableItems:
				case kAudioSelectorControlPropertyItemName:
					theAnswer = true;
					break;
			};
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_IsControlPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsControlPropertySettable: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsControlPropertySettable: no address");
	FailWithAction(outIsSettable == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_IsControlPropertySettable: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetControlPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_Volume_Input_Master:
		case kObjectID_Volume_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
				case kAudioLevelControlPropertyDecibelRange:
				case kAudioLevelControlPropertyConvertScalarToDecibels:
				case kAudioLevelControlPropertyConvertDecibelsToScalar:
					*outIsSettable = false;
					break;
				
				case kAudioLevelControlPropertyScalarValue:
				case kAudioLevelControlPropertyDecibelValue:
					*outIsSettable = true;
					break;
				
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		
		case kObjectID_Mute_Input_Master:
		case kObjectID_Mute_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
					*outIsSettable = false;
					break;
				
				case kAudioBooleanControlPropertyValue:
					*outIsSettable = true;
					break;
				
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;

		case kObjectID_Pitch_Adjust:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
					*outIsSettable = false;
					break;

				case kAudioStereoPanControlPropertyValue:
					*outIsSettable = true;
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		case kObjectID_ClockSource:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
				case kAudioObjectPropertyClass:
				case kAudioObjectPropertyOwner:
				case kAudioObjectPropertyOwnedObjects:
				case kAudioControlPropertyScope:
				case kAudioControlPropertyElement:
					*outIsSettable = false;
					break;
					
				case kAudioSelectorControlPropertyCurrentItem:
					*outIsSettable = true;
					break;
					
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;

		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetControlPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetControlPropertyDataSize: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyDataSize: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyDataSize: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetControlPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_Volume_Input_Master:
		case kObjectID_Volume_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyOwner:
					*outDataSize = sizeof(AudioObjectID);
					break;

				case kAudioObjectPropertyOwnedObjects:
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioLevelControlPropertyScalarValue:
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyDecibelValue:
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyDecibelRange:
					*outDataSize = sizeof(AudioValueRange);
					break;

				case kAudioLevelControlPropertyConvertScalarToDecibels:
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyConvertDecibelsToScalar:
					*outDataSize = sizeof(Float32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		
		case kObjectID_Mute_Input_Master:
		case kObjectID_Mute_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyOwner:
					*outDataSize = sizeof(AudioObjectID);
					break;

				case kAudioObjectPropertyOwnedObjects:
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioBooleanControlPropertyValue:
					*outDataSize = sizeof(UInt32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
			
		case kObjectID_Pitch_Adjust:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyClass:
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyOwner:
					*outDataSize = sizeof(AudioObjectID);
					break;

				case kAudioObjectPropertyOwnedObjects:
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioStereoPanControlPropertyValue:
					*outDataSize = sizeof(Float32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
			
		case kObjectID_ClockSource:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					*outDataSize = sizeof(AudioClassID);
					break;
				case kAudioObjectPropertyClass:
					*outDataSize = sizeof(AudioClassID);
					break;
				case kAudioObjectPropertyOwner:
					*outDataSize = sizeof(AudioObjectID);
					break;
				case kAudioObjectPropertyOwnedObjects:
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;
				case kAudioControlPropertyScope:
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;
				case kAudioControlPropertyElement:
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;
					
				case kAudioSelectorControlPropertyCurrentItem:
					*outDataSize = sizeof(UInt32);
					break;
					
				case kAudioSelectorControlPropertyAvailableItems:
					*outDataSize = kClockSource_NumberItems * sizeof(UInt32);
					break;
				case kAudioSelectorControlPropertyItemName:
					*outDataSize = sizeof(CFStringRef);
					break;
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetControlPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierData, inQualifierDataSize)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
    UInt32 theNumberItemsToFetch;
    UInt32 theItemIndex;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetControlPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyData: no address");
	FailWithAction(outDataSize == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyData: no place to put the return value size");
	FailWithAction(outData == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyData: no place to put the return value");
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required.
	//
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	switch(inObjectID)
	{
		case kObjectID_Volume_Input_Master:
		case kObjectID_Volume_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					//	The base class for kAudioVolumeControlClassID is kAudioLevelControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the volume control");
					*((AudioClassID*)outData) = kAudioLevelControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyClass:
					//	Volume controls are of the class, kAudioVolumeControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the volume control");
					*((AudioClassID*)outData) = kAudioVolumeControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyOwner:
					//	The control's owner is the device object
					FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the volume control");
					*((AudioObjectID*)outData) = kObjectID_Device;
					*outDataSize = sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyOwnedObjects:
					//	Controls do not own any objects
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					//	This property returns the scope that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyScope), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyScope for the volume control");
					*((AudioObjectPropertyScope*)outData) = (inObjectID == kObjectID_Volume_Input_Master) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					//	This property returns the element that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyElement), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyElement for the volume control");
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioLevelControlPropertyScalarValue:
					//	This returns the value of the control in the normalized range of 0 to 1.
					//	Note that we need to take the state lock to examine the value.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyScalarValue for the volume control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((Float32*)outData) = volume_to_scalar(gVolume_Master_Value);
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyDecibelValue:
					//	This returns the dB value of the control.
					//	Note that we need to take the state lock to examine the value.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((Float32*)outData) = gVolume_Master_Value;
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*((Float32*)outData) = volume_to_decibel(*((Float32*)outData));
					
					//	report how much we wrote
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyDecibelRange:
					//	This returns the dB range of the control.
					FailWithAction(inDataSize < sizeof(AudioValueRange), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelRange for the volume control");
					((AudioValueRange*)outData)->mMinimum = kVolume_MinDB;
					((AudioValueRange*)outData)->mMaximum = kVolume_MaxDB;
					*outDataSize = sizeof(AudioValueRange);
					break;

				case kAudioLevelControlPropertyConvertScalarToDecibels:
					//	This takes the scalar value in outData and converts it to dB.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
					
					//	clamp the value to be between 0 and 1
					if(*((Float32*)outData) < 0.0)
					{
						*((Float32*)outData) = 0;
					}
					if(*((Float32*)outData) > 1.0)
					{
						*((Float32*)outData) = 1.0;
					}
					
					//	Note that we square the scalar value before converting to dB so as to
					//	provide a better curve for the slider
					*((Float32*)outData) *= *((Float32*)outData);
					*((Float32*)outData) = kVolume_MinDB + (*((Float32*)outData) * (kVolume_MaxDB - kVolume_MinDB));
					
					//	report how much we wrote
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyConvertDecibelsToScalar:
					//	This takes the dB value in outData and converts it to scalar.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
					
					//	clamp the value to be between kVolume_MinDB and kVolume_MaxDB
					if(*((Float32*)outData) < kVolume_MinDB)
					{
						*((Float32*)outData) = kVolume_MinDB;
					}
					if(*((Float32*)outData) > kVolume_MaxDB)
					{
						*((Float32*)outData) = kVolume_MaxDB;
					}
					
					//	Note that we square the scalar value before converting to dB so as to
					//	provide a better curve for the slider. We undo that here.
					*((Float32*)outData) = *((Float32*)outData) - kVolume_MinDB;
					*((Float32*)outData) /= kVolume_MaxDB - kVolume_MinDB;
					*((Float32*)outData) = sqrtf(*((Float32*)outData));
					
					//	report how much we wrote
					*outDataSize = sizeof(Float32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		
		case kObjectID_Mute_Input_Master:
		case kObjectID_Mute_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					//	The base class for kAudioMuteControlClassID is kAudioBooleanControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the mute control");
					*((AudioClassID*)outData) = kAudioBooleanControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyClass:
					//	Mute controls are of the class, kAudioMuteControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the mute control");
					*((AudioClassID*)outData) = kAudioMuteControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyOwner:
					//	The control's owner is the device object
					FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the mute control");
					*((AudioObjectID*)outData) = kObjectID_Device;
					*outDataSize = sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyOwnedObjects:
					//	Controls do not own any objects
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					//	This property returns the scope that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyScope), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyScope for the mute control");
					*((AudioObjectPropertyScope*)outData) = (inObjectID == kObjectID_Mute_Input_Master) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					//	This property returns the element that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyElement), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyElement for the mute control");
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioBooleanControlPropertyValue:
					//	This returns the value of the mute control where 0 means that mute is off
					//	and audio can be heard and 1 means that mute is on and audio cannot be heard.
					//	Note that we need to take the state lock to examine this value.
					FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioBooleanControlPropertyValue for the mute control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((UInt32*)outData) = gMute_Master_Value ? 1 : 0;
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(UInt32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;

		case kObjectID_Pitch_Adjust:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					//    The base class for kAudioMuteControlClassID is kAudioBooleanControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the pitch control");
					*((AudioClassID*)outData) = kAudioStereoPanControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyClass:
					//    Level controls are of the class, kAudioLevelControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the pitch control");
					*((AudioClassID*)outData) = kAudioStereoPanControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;

				case kAudioObjectPropertyOwner:
					//    The control's owner is the device object
					FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the pitch control");
					*((AudioObjectID*)outData) = kObjectID_Device;
					*outDataSize = sizeof(AudioObjectID);
					break;

				case kAudioObjectPropertyOwnedObjects:
					//    Controls do not own any objects
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;

				case kAudioControlPropertyScope:
					//    This property returns the scope that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyScope), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyScope for the pitch control");
					*((AudioObjectPropertyScope*)outData) = kAudioObjectPropertyScopeOutput;
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;

				case kAudioControlPropertyElement:
					//    This property returns the element that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyElement), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyElement for the pitch control");
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioStereoPanControlPropertyValue:
					//    This returns the value of the pitch control.
					//    Note that we need to take the state lock to examine this value.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlScalarValue for the pitch control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((Float32*)outData) = (inObjectID == kObjectID_Pitch_Adjust) ? gPitch_Adjust : 0.5;
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(Float32);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		case kObjectID_ClockSource:
			switch(inAddress->mSelector)
			{
				case kAudioObjectPropertyBaseClass:
					//    The base class for kAudioDataSourceControlClassID is kAudioSelectorControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the data source control");
					*((AudioClassID*)outData) = kAudioSelectorControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyClass:
					//    Data Source controls are of the class, kAudioDataSourceControlClassID
					FailWithAction(inDataSize < sizeof(AudioClassID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the data source control");
					*((AudioClassID*)outData) = kAudioClockSourceControlClassID;
					*outDataSize = sizeof(AudioClassID);
					break;
					
				case kAudioObjectPropertyOwner:
					//    The control's owner is the device object
					FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the data source control");
					*((AudioObjectID*)outData) = kObjectID_Device;
					*outDataSize = sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyOwnedObjects:
					//    Controls do not own any objects
					*outDataSize = 0 * sizeof(AudioObjectID);
					break;
					
				case kAudioControlPropertyScope:
					//    This property returns the scope that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyScope), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyScope for the data source control");
					*((AudioObjectPropertyScope*)outData) = kAudioObjectPropertyScopeGlobal;
					*outDataSize = sizeof(AudioObjectPropertyScope);
					break;
					
				case kAudioControlPropertyElement:
					//    This property returns the element that the control is attached to.
					FailWithAction(inDataSize < sizeof(AudioObjectPropertyElement), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioControlPropertyElement for the data source control");
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;
					
				case kAudioSelectorControlPropertyCurrentItem:
					//    This returns the value of the data source selector.
					//    Note that we need to take the state lock to examine this value.
					FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioSelectorControlPropertyCurrentItem for the data source control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((UInt32*)outData) = gClockSource_Value;
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(UInt32);
					break;
					
				case kAudioSelectorControlPropertyAvailableItems:
					//    This returns the IDs for all the items the data source control supports.
					
					//    Calculate the number of items that have been requested. Note that this
					//    number is allowed to be smaller than the actual size of the list. In such
					//    case, only that number of items will be returned
					theNumberItemsToFetch = inDataSize / sizeof(UInt32);
					
					//    clamp it to the number of items we have
					if(theNumberItemsToFetch > kClockSource_NumberItems)
					{
						theNumberItemsToFetch = kClockSource_NumberItems;
					}
					
					//    fill out the return array
					for(theItemIndex = 0; theItemIndex < theNumberItemsToFetch; ++theItemIndex)
					{
						((UInt32*)outData)[theItemIndex] = theItemIndex;
					}
					
					//    report how much we wrote
					*outDataSize = theNumberItemsToFetch * sizeof(UInt32);

					break;

				case kAudioSelectorControlPropertyItemName:
					//    This returns the user-readable name for the selector item in the qualifier
					FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioSelectorControlPropertyItemName for the clock source control");
					FailWithAction(inQualifierDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: wrong size for the qualifier of kAudioSelectorControlPropertyItemName for the clock source control");
					FailWithAction(*((const UInt32*)inQualifierData) >= kClockSource_NumberItems, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_GetControlPropertyData: the item in the qualifier is not valid for kAudioSelectorControlPropertyItemName for the data source control");
					if (*(UInt32*)inQualifierData == 0) {
						*(CFStringRef*)outData = CFSTR(kClockSource_InternalFixed);
					}
					else if (*(UInt32*)inQualifierData == 1) {
						*(CFStringRef*)outData = CFSTR(kClockSource_InternalAdjustable);
					}
					//else {
					//    *(CFStringRef*)outData = CFSTR("Unknown");
					//}
					*outDataSize = sizeof(CFStringRef);

					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

static OSStatus	BlackHole_SetControlPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	Float32 theNewVolume;
    Float32 theNewPitch;
    UInt32 theNewSource;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetControlPropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetControlPropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetControlPropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetControlPropertyData: no place to return the properties that changed");
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	//	Note that for each object, this driver implements all the required properties plus a few
	//	extras that are useful but not required. There is more detailed commentary about each
	//	property in the BlackHole_GetControlPropertyData() method.
	switch(inObjectID)
	{
		case kObjectID_Volume_Input_Master:
		case kObjectID_Volume_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioLevelControlPropertyScalarValue:
					//	For the scalar volume, we clamp the new value to [0, 1]. Note that if this
					//	value changes, it implies that the dB value changed too.
					FailWithAction(inDataSize != sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetControlPropertyData: wrong size for the data for kAudioLevelControlPropertyScalarValue");
					theNewVolume = volume_from_scalar(*((const Float32*)inData));
					if(theNewVolume < 0.0)
					{
						theNewVolume = 0.0;
					}
					else if(theNewVolume > 1.0)
					{
						theNewVolume = 1.0;
					}
					pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gVolume_Master_Value != theNewVolume)
                    {
                        gVolume_Master_Value = theNewVolume;
                        *outNumberPropertiesChanged = 2;
                        outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                        outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
                    }
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					break;
				
				case kAudioLevelControlPropertyDecibelValue:
					//	For the dB value, we first convert it to a scalar value since that is how
					//	the value is tracked. Note that if this value changes, it implies that the
					//	scalar value changes as well.
					FailWithAction(inDataSize != sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetControlPropertyData: wrong size for the data for kAudioLevelControlPropertyScalarValue");
					theNewVolume = *((const Float32*)inData);
					if(theNewVolume < kVolume_MinDB)
					{
						theNewVolume = kVolume_MinDB;
					}
					else if(theNewVolume > kVolume_MaxDB)
					{
						theNewVolume = kVolume_MaxDB;
					}
					theNewVolume = volume_from_decibel(theNewVolume);
					pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gVolume_Master_Value != theNewVolume)
                    {
                        gVolume_Master_Value = theNewVolume;
                        *outNumberPropertiesChanged = 2;
                        outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                        outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
                    }
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					break;
				
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
		
		case kObjectID_Mute_Input_Master:
		case kObjectID_Mute_Output_Master:
			switch(inAddress->mSelector)
			{
				case kAudioBooleanControlPropertyValue:
					FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetControlPropertyData: wrong size for the data for kAudioBooleanControlPropertyValue");
					pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gMute_Master_Value != (*((const UInt32*)inData) != 0))
                    {
                        gMute_Master_Value = *((const UInt32*)inData) != 0;
                        *outNumberPropertiesChanged = 1;
                        outChangedAddresses[0].mSelector = kAudioBooleanControlPropertyValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                    }
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					break;
				
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;

		case kObjectID_Pitch_Adjust:
			switch(inAddress->mSelector)
			{
				case kAudioStereoPanControlPropertyValue:
					//    For the scalar pitch, we clamp the new value to [0, 1].
					FailWithAction(inDataSize != sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetControlPropertyData: wrong size for the data for kAudioLevelControlPropertyScalarValue");
					theNewPitch = *((const Float32*)inData);
					if(theNewPitch < 0.0)
					{
						theNewPitch = 0.0;
					}
					else if(theNewPitch > 1.0)
					{
						theNewPitch = 1.0;
					}
					pthread_mutex_lock(&gPlugIn_StateMutex);

					if(gPitch_Adjust != theNewPitch)
					{
						gPitch_Adjust = theNewPitch;
						gDevice_AdjustedTicksPerFrame = gDevice_HostTicksPerFrame - gDevice_HostTicksPerFrame/100.0 * 2.0*(gPitch_Adjust - 0.5);
						*outNumberPropertiesChanged = 1;
						outChangedAddresses[0].mSelector = kAudioStereoPanControlPropertyValue;
						outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
						outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
					}
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					break;
					
				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;
			
		case kObjectID_ClockSource:
			switch(inAddress->mSelector)
			{
				case kAudioSelectorControlPropertyCurrentItem:
					FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetControlPropertyData: wrong size for the data for kAudioSelectorControlPropertyCurrentItem");
					theNewSource = *((const UInt32*)inData);
					if(theNewSource >= kClockSource_NumberItems)
					{
						theNewSource = kClockSource_NumberItems - 1;
					}
					pthread_mutex_lock(&gPlugIn_StateMutex);
					if(gClockSource_Value != theNewSource)
					{
						gClockSource_Value = theNewSource;
						UInt64 changeAction = (theNewSource > 0) ? ChangeAction_EnablePitchControl : ChangeAction_DisablePitchControl;

						*outNumberPropertiesChanged = 1;
						outChangedAddresses[0].mSelector = kAudioSelectorControlPropertyCurrentItem;
						outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
						outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;

						// Notify HAL about device configuration change
						dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
							gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, changeAction, NULL);
						});
					}
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					break;

				default:
					theAnswer = kAudioHardwareUnknownPropertyError;
					break;
			};
			break;

		default:
			theAnswer = kAudioHardwareBadObjectError;
			break;
	};

Done:
	return theAnswer;
}

#pragma mark IO Operations

static OSStatus	BlackHole_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
	//	This call tells the device that IO is starting for the given client. When this routine
	//	returns, the device's clock is running and it is ready to have data read/written. It is
	//	important to note that multiple clients can have IO running on the device at the same time.
	//	So, work only needs to be done when the first client starts. All subsequent starts simply
	//	increment the counter.
    
    DebugMsg("BlackHole_StartIO");
	
	#pragma unused(inClientID, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StartIO: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StartIO: bad device ID");
    FailWithAction(inDeviceObjectID == kObjectID_Device && gDevice_IOIsRunning == UINT64_MAX, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_StartIO: overflow error.");
    FailWithAction(inDeviceObjectID == kObjectID_Device2 && gDevice2_IOIsRunning == UINT64_MAX, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_StartIO: overflow error.");

	//	we need to hold the state lock
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
    
    if (inDeviceObjectID == kObjectID_Device) { gDevice_IOIsRunning += 1; }
    if (inDeviceObjectID == kObjectID_Device2) { gDevice2_IOIsRunning += 1; }
    
    // allocate ring buffer
    if ((gDevice_IOIsRunning || gDevice2_IOIsRunning) && gRingBuffer == NULL)
    {
        gDevice_NumberTimeStamps = 0;
        gDevice_AnchorSampleTime = 0;
        gDevice_AnchorHostTime = mach_absolute_time();
        gDevice_PreviousTicks = 0;
        gRingBuffer = calloc(kRing_Buffer_Frame_Size * kNumber_Of_Channels, sizeof(Float32));
    }
    
    
	//	unlock the state lock
	pthread_mutex_unlock(&gPlugIn_StateMutex);
	
Done:
	return theAnswer;
}

static OSStatus	BlackHole_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
	//	This call tells the device that the client has stopped IO. The driver can stop the hardware
	//	once all clients have stopped.
	
	#pragma unused(inClientID, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StopIO: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StopIO: bad device ID");
    FailWithAction(inDeviceObjectID == kObjectID_Device && gDevice_IOIsRunning == 0, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_StartIO: underflow error.");
    FailWithAction(inDeviceObjectID == kObjectID_Device2 && gDevice2_IOIsRunning == 0, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_StartIO: underflow error.");

	//	we need to hold the state lock
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
    
    if (inDeviceObjectID == kObjectID_Device) { gDevice_IOIsRunning -= 1; }
    if (inDeviceObjectID == kObjectID_Device2) { gDevice2_IOIsRunning -= 1; }
    
    // free the ring buffer
    if (!gDevice_IOIsRunning && !gDevice2_IOIsRunning && gRingBuffer != NULL)
    {
        free(gRingBuffer);
        gRingBuffer = NULL;
    }
	
	//	unlock the state lock
	pthread_mutex_unlock(&gPlugIn_StateMutex);
	
Done:
	return theAnswer;
}

static OSStatus	BlackHole_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed)
{
	//	This method returns the current zero time stamp for the device. The HAL models the timing of
	//	a device as a series of time stamps that relate the sample time to a host time. The zero
	//	time stamps are spaced such that the sample times are the value of
	//	kAudioDevicePropertyZeroTimeStampPeriod apart. This is often modeled using a ring buffer
	//	where the zero time stamp is updated when wrapping around the ring buffer.
	//
	//	For this device, the zero time stamps' sample time increments every kDevice_RingBufferSize
	//	frames and the host time increments by kDevice_RingBufferSize * gDevice_HostTicksPerFrame.
	
	#pragma unused(inClientID, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt64 theCurrentHostTime;
	Float64 theHostTicksPerRingBuffer;
	Float64 theAdjustedTicksPerRingBuffer;
	Float64 theNextTickOffset;
	UInt64 theNextHostTime;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetZeroTimeStamp: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetZeroTimeStamp: bad device ID");

	//	we need to hold the locks
	pthread_mutex_lock(&gDevice_IOMutex);
	
	//	get the current host time
	theCurrentHostTime = mach_absolute_time();
	
	//	calculate the next host time
	theHostTicksPerRingBuffer = gDevice_HostTicksPerFrame * ((Float64)kDevice_RingBufferSize);
    if (gClockSource_Value > 0) {
        theAdjustedTicksPerRingBuffer = gDevice_AdjustedTicksPerFrame * ((Float64)kDevice_RingBufferSize);
    }
    else {
        theAdjustedTicksPerRingBuffer = gDevice_HostTicksPerFrame * ((Float64)kDevice_RingBufferSize);
    }
    
	theNextTickOffset = gDevice_PreviousTicks + theAdjustedTicksPerRingBuffer;
    
	theNextHostTime = gDevice_AnchorHostTime + ((UInt64)theNextTickOffset);
	
	//	go to the next time if the next host time is less than the current time
	if(theNextHostTime <= theCurrentHostTime)
	{
		++gDevice_NumberTimeStamps;
		gDevice_PreviousTicks = theNextTickOffset;
	}
	
	//	set the return values
	*outSampleTime = gDevice_NumberTimeStamps * kDevice_RingBufferSize;
	*outHostTime = gDevice_AnchorHostTime + gDevice_PreviousTicks;
	*outSeed = 1;
    
    // DebugMsg("SampleTime: %f \t HostTime: %llu", *outSampleTime, *outHostTime);
	
	//	unlock the state lock
	pthread_mutex_unlock(&gDevice_IOMutex);
	
Done:
	return theAnswer;
}

static OSStatus	BlackHole_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace)
{
	//	This method returns whether or not the device will do a given IO operation. For this device,
	//	we only support reading input data and writing output data.
	
	#pragma unused(inClientID, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_WillDoIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_WillDoIOOperation: bad device ID");

	//	figure out if we support the operation
	bool willDo = false;
	bool willDoInPlace = true;
	switch(inOperationID)
	{
		case kAudioServerPlugInIOOperationReadInput:
			willDo = true;
			willDoInPlace = true;
			break;
			
		case kAudioServerPlugInIOOperationWriteMix:
			willDo = true;
			willDoInPlace = true;
			break;
			
	};
	
	//	fill out the return values
	if(outWillDo != NULL)
	{
		*outWillDo = willDo;
	}
	if(outWillDoInPlace != NULL)
	{
		*outWillDoInPlace = willDoInPlace;
	}

Done:
	return theAnswer;
}

static OSStatus	BlackHole_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
	//	This is called at the beginning of an IO operation. This device doesn't do anything, so just
	//	check the arguments and return.
	
	#pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_BeginIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_BeginIOOperation: bad device ID");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
	//	This is called to actually perform a given operation. 
	
	#pragma unused(inClientID, inIOCycleInfo, ioSecondaryBuffer, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DoIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DoIOOperation: bad device ID");
	FailWithAction((inStreamObjectID != kObjectID_Stream_Input) && (inStreamObjectID != kObjectID_Stream_Output), theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DoIOOperation: bad stream ID");

    // Calculate the ring buffer offsets and splits.
    UInt64 mSampleTime = inOperationID == kAudioServerPlugInIOOperationReadInput ? inIOCycleInfo->mInputTime.mSampleTime : inIOCycleInfo->mOutputTime.mSampleTime;
    UInt32 ringBufferFrameLocationStart = mSampleTime % kRing_Buffer_Frame_Size;
    UInt32 firstPartFrameSize = kRing_Buffer_Frame_Size - ringBufferFrameLocationStart;
    UInt32 secondPartFrameSize = 0;
    
    if (firstPartFrameSize >= inIOBufferFrameSize)
    {
        firstPartFrameSize = inIOBufferFrameSize;
    }
    else
    {
        secondPartFrameSize = inIOBufferFrameSize - firstPartFrameSize;
    }
    
    // Keep track of last outputSampleTime and the cleared buffer status.
    static Float64 lastOutputSampleTime = 0;
    static Boolean isBufferClear = true;
    
    // From BlackHole to Application
    if(inOperationID == kAudioServerPlugInIOOperationReadInput)
    {
        // If mute is one let's just fill the buffer with zeros or if there's no apps outputting audio
        if (gMute_Master_Value || lastOutputSampleTime - inIOBufferFrameSize < inIOCycleInfo->mInputTime.mSampleTime)
        {
            // Clear the ioMainBuffer
            vDSP_vclr(ioMainBuffer, 1, inIOBufferFrameSize * kNumber_Of_Channels);
            
            // Clear the ring buffer.
            if (!isBufferClear)
            {
                vDSP_vclr(gRingBuffer, 1, kRing_Buffer_Frame_Size * kNumber_Of_Channels);
                isBufferClear = true;
            }
        }
        else
        {
            // Copy the buffers.
            memcpy(ioMainBuffer, gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, firstPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
            memcpy((Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, gRingBuffer, secondPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
            
            // Finally we'll apply the output volume to the buffer.
	    if(kEnableVolumeControl)
	    {
	 	vDSP_vsmul(ioMainBuffer, 1, &gVolume_Master_Value, ioMainBuffer, 1, inIOBufferFrameSize * kNumber_Of_Channels);
	    }

        }
    }
    
    // From Application to BlackHole
    if(inOperationID == kAudioServerPlugInIOOperationWriteMix)
    {
        
        // Overload error.
        if (inIOCycleInfo->mCurrentTime.mSampleTime > inIOCycleInfo->mOutputTime.mSampleTime + inIOBufferFrameSize + kLatency_Frame_Size)
        {
            DebugMsg("BlackHole overload error. kAudioServerPlugInIOOperationWriteMix was unable to complete operation before the deadline. Try increasing the buffer frame size.");
            return kAudioHardwareUnspecifiedError;
        }
        
        
        // Copy the buffers.
        memcpy(gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, ioMainBuffer, firstPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
        memcpy(gRingBuffer, (Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, secondPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
        
        // Save the last output time.
        lastOutputSampleTime = inIOCycleInfo->mOutputTime.mSampleTime + inIOBufferFrameSize;
        isBufferClear = false;
    }

Done:
	return theAnswer;
}

static OSStatus	BlackHole_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
	//	This is called at the end of an IO operation. This device doesn't do anything, so just check
	//	the arguments and return.
	
	#pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo, inDeviceObjectID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_EndIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device2, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_EndIOOperation: bad device ID");

Done:
	return theAnswer;
}
