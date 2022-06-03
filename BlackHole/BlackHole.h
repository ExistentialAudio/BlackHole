//
//  BlackHole.h
//  BlackHole
//
//  Copyright (C) 2019 Existential Audio Inc.
//
//  

#ifndef BlackHole_h
#define BlackHole_h

#include <CoreAudio/AudioServerPlugIn.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/syslog.h>
#include <Accelerate/Accelerate.h>

//==================================================================================================
#pragma mark -
#pragma mark Macros
//==================================================================================================

#if TARGET_RT_BIG_ENDIAN
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[0], ((char*)&the4CC)[1], ((char*)&the4CC)[2], ((char*)&the4CC)[3], 0 }
#else
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[3], ((char*)&the4CC)[2], ((char*)&the4CC)[1], ((char*)&the4CC)[0], 0 }
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
    kObjectID_Device2                   = 10,
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
#else
#define                             kBox_UID                            kDriver_Name "_UID"
#define                             kDevice_UID                         kDriver_Name "_UID"
#define                             kDevice2_UID                        kDriver_Name "_2_UID"
#define                             kDevice_ModelUID                    kDriver_Name "_ModelUID"
#endif

#ifndef kDevice_Name
#define                             kDevice_Name                        kDriver_Name " %ich"
#endif

#ifndef kDevice2_Name
#define                             kDevice2_Name                       kDriver_Name " %ich 2"
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

static pthread_mutex_t              gPlugIn_StateMutex                  = PTHREAD_MUTEX_INITIALIZER;
static UInt32                       gPlugIn_RefCount                    = 0;
static AudioServerPlugInHostRef     gPlugIn_Host                        = NULL;


static CFStringRef                  gBox_Name                           = NULL;
static Boolean                      gBox_Acquired                       = true;


static pthread_mutex_t              gDevice_IOMutex                     = PTHREAD_MUTEX_INITIALIZER;
static Float64                      gDevice_SampleRate                  = 44100.0;
static UInt64                       gDevice_IOIsRunning                 = 0;
static const UInt32                 kDevice_RingBufferSize              = 16384;
static Float64                      gDevice_HostTicksPerFrame           = 0.0;
static UInt64                       gDevice_NumberTimeStamps            = 0;
static Float64                      gDevice_AnchorSampleTime            = 0.0;
static UInt64                       gDevice_AnchorHostTime              = 0;

static bool                         gStream_Input_IsActive              = true;
static bool                         gStream_Output_IsActive             = true;

static const Float32                kVolume_MinDB                       = -64.0;
static const Float32                kVolume_MaxDB                       = 0.0;
static Float32                      gVolume_Master_Value                = 1.0;
static bool                         gMute_Master_Value                  = false;

static struct ObjectInfo            kDevice_ObjectList[]                = {
#if kDevice_HasInput
    { kObjectID_Stream_Input,           kObjectType_Stream,     kAudioObjectPropertyScopeInput  },
    { kObjectID_Volume_Input_Master,    kObjectType_Control,    kAudioObjectPropertyScopeInput  },
    { kObjectID_Mute_Input_Master,      kObjectType_Control,    kAudioObjectPropertyScopeInput  },
#endif
#if kDevice_HasOutput
    { kObjectID_Stream_Output,          kObjectType_Stream,     kAudioObjectPropertyScopeOutput },
    { kObjectID_Volume_Output_Master,   kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput }
#endif
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
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput }
#endif
};

static const UInt32                 kDevice_ObjectListSize              = sizeof(kDevice_ObjectList) / sizeof(struct ObjectInfo);
static const UInt32                 kDevice2_ObjectListSize              = sizeof(kDevice2_ObjectList) / sizeof(struct ObjectInfo);

static Float64                      kDevice_SampleRates[]               = {
                                                                              8000,
                                                                             16000,
                                                                             44100,
                                                                             48000,
                                                                             88200,
                                                                             96000,
                                                                            176400,
                                                                            192000,
                                                                            352800,
                                                                            384000,
                                                                            705600,
                                                                            768000
                                                                           };

static const UInt32                 kDevice_SampleRatesSize             = sizeof(kDevice_SampleRates) / sizeof(Float64);



#define                             kBits_Per_Channel                   32
#define                             kBytes_Per_Channel                  (kBits_Per_Channel/ 8)
#define                             kBytes_Per_Frame                    (kNumber_Of_Channels * kBytes_Per_Channel)
#define                             kRing_Buffer_Frame_Size             ((65536 + kLatency_Frame_Size))
static Float32*                     gRingBuffer;


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

#endif /* BlackHole_h */
