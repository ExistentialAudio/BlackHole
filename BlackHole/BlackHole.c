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

//	System Includes
#include "BlackHole.h"


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


#pragma mark Factory

void*	BlackHole_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
	//	This is the CFPlugIn factory function. Its job is to create the implementation for the given
	//	type provided that the type is supported. Because this driver is simple and all its
	//	initialization is handled via static iniitalization when the bundle is loaded, all that
	//	needs to be done is to return the AudioServerPlugInDriverRef that points to the driver's
	//	interface. A more complicated driver would create any base line objects it needs to satisfy
	//	the IUnknown methods that are used to discover that actual interface to talk to the driver.
	//	The majority of the driver's initilization should be handled in the Initialize() method of
	//	the driver's AudioServerPlugInDriverInterface.
	
	#pragma unused(inAllocator)
    void* theAnswer = NULL;
    if(CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID))
    {
		theAnswer = gAudioServerPlugInDriverRef;
    }
    return theAnswer;
}

#pragma mark Inheritence

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
	//	maintains (such as the device list) to get the inital set of objects the driver is
	//	publishing. So, there is no need to notifiy the HAL about any objects created as part of the
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
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_AddDeviceClient: bad device ID");

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
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_RemoveDeviceClient: bad device ID");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
	//	This method is called to tell the device that it can perform the configuation change that it
	//	had requested via a call to the host method, RequestDeviceConfigurationChange(). The
	//	arguments, inChangeAction and inChangeInfo are the same as what was passed to
	//	RequestDeviceConfigurationChange().
	//
	//	The HAL guarantees that IO will be stopped while this method is in progress. The HAL will
	//	also handle figuring out exactly what changed for the non-control related properties. This
	//	means that the only notifications that would need to be sent here would be for either
	//	custom properties the HAL doesn't know about or for controls.
	//
	//	For the device implemented by this driver, only sample rate changes go through this process
	//	as it is the only state that can be changed for the device that isn't a control. For this
	//	change, the new sample rate is passed in the inChangeAction argument.
	
	#pragma unused(inChangeInfo)

	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad device ID");
	FailWithAction((inChangeAction != 44100)
                   && (inChangeAction != 48000)
                   && (inChangeAction != 88200)
                   && (inChangeAction != 96000)
                   && (inChangeAction != 176400)
                   && (inChangeAction != 192000)
                   && (inChangeAction != 352800)
                   && (inChangeAction != 384000)
                   && (inChangeAction != 705600)
                   && (inChangeAction != 768000)
                   && (inChangeAction != 8000)
                   && (inChangeAction != 16000),
                   theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad sample rate");
	
	//	lock the state mutex
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
	//	change the sample rate
	gDevice_SampleRate = inChangeAction;
	
	//	recalculate the state that depends on the sample rate
	struct mach_timebase_info theTimeBaseInfo;
	mach_timebase_info(&theTimeBaseInfo);
    Float64 theHostClockFrequency = (Float64)theTimeBaseInfo.denom / (Float64)theTimeBaseInfo.numer;
	theHostClockFrequency *= 1000000000.0;
	gDevice_HostTicksPerFrame = theHostClockFrequency / gDevice_SampleRate;

	//	unlock the state mutex
	pthread_mutex_unlock(&gPlugIn_StateMutex);
    
    // DebugMsg("BlackHole theTimeBaseInfo.numer: %u \t theTimeBaseInfo.denom: %u", theTimeBaseInfo.numer, theTimeBaseInfo.denom);
	
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
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_PerformDeviceConfigurationChange: bad device ID");

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
				*outDataSize = sizeof(AudioClassID);
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
			{
				FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToBox");
				FailWithAction(inQualifierDataSize == sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: the qualifier is the wrong size for kAudioPlugInPropertyTranslateUIDToBox");
				FailWithAction(inQualifierData == NULL, theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: no qualifier for kAudioPlugInPropertyTranslateUIDToBox");
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
			}
			break;
			
		case kAudioPlugInPropertyDeviceList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of devices this driver implements (which is just 1 if the
			//	box has been acquired)
			if(theNumberItemsToFetch > (gBox_Acquired ? 1 : 0))
			{
				theNumberItemsToFetch = (gBox_Acquired ? 1 : 0);
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
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
			{
				FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToDevice");
				FailWithAction(inQualifierDataSize == sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: the qualifier is the wrong size for kAudioPlugInPropertyTranslateUIDToDevice");
				FailWithAction(inQualifierData == NULL, theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetPlugInPropertyData: no qualifier for kAudioPlugInPropertyTranslateUIDToDevice");
				CFStringRef formattedString = CFStringCreateWithFormat(NULL, NULL, CFSTR(kBox_UID), kNumber_Of_Channels);
				if(CFStringCompare(*((CFStringRef*)inQualifierData), formattedString, 0) == kCFCompareEqualTo)
				{
					*((AudioObjectID*)outData) = kObjectID_Device;
				}
				else
				{
					*((AudioObjectID*)outData) = kAudioObjectUnknown;
				}
				*outDataSize = sizeof(AudioObjectID);
				CFRelease(formattedString);
			}
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
				*outDataSize = gBox_Acquired ? sizeof(AudioObjectID) : 0;
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
			*((CFStringRef*)outData) = CFSTR("Null Model");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");
			*((CFStringRef*)outData) = CFSTR("Apple Inc.");
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
			*((CFStringRef*)outData) = CFSTR("00000001");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyFirmwareVersion:
			//	This is the human readable firmware version of the box.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyFirmwareVersion for the box");
			*((CFStringRef*)outData) = CFSTR("1.0");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyBoxUID:
			//	Boxes have UIDs the same as devices
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the box");
			*((CFStringRef*)outData) = CFStringCreateWithFormat(NULL, NULL, CFSTR(kBox_UID), kNumber_Of_Channels);
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
				FailWithAction(inDataSize < sizeof(AudioObjectID), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetBoxPropertyData: not enough space for the return value of kAudioBoxPropertyDeviceList for the box");
				*((AudioObjectID*)outData) = kObjectID_Device;
				*outDataSize = sizeof(AudioObjectID);
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
				outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
			}
			break;
			
		case kAudioObjectPropertyIdentify:
			//	since we don't have any actual hardware to flash, we will schedule a notificaiton for
			//	this property off into the future as a testing thing. Note that a real implementation
			//	of this property should only send the notificaiton if the hardware wants the app to
			//	flash it's UI for the device.
			{
				syslog(LOG_NOTICE, "The identify property has been set on the Box implemented by the BlackHole driver.");
				FailWithAction(inDataSize != sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_SetBoxPropertyData: wrong size for the data for kAudioObjectPropertyIdentify");
				dispatch_after(dispatch_time(0, 2ULL * 1000ULL * 1000ULL * 1000ULL), dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																																		{
																																			AudioObjectPropertyAddress theAddress = { kAudioObjectPropertyIdentify, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
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
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
					outChangedAddresses[1].mSelector = kAudioBoxPropertyDeviceList;
					outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[1].mElement = kAudioObjectPropertyElementMaster;
					
					//	but it also means that the device list has changed for the plug-in too
					dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																									{
																										AudioObjectPropertyAddress theAddress = { kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
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
	FailIf(inObjectID != kObjectID_Device, Done, "BlackHole_HasDeviceProperty: not the device object");
	
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
	FailWithAction(inObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_IsDevicePropertySettable: not the device object");
	
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
	FailWithAction(inObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyDataSize: not the device object");
	
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
			switch(inAddress->mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					*outDataSize = 8 * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeInput:
					*outDataSize = 4 * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeOutput:
					*outDataSize = 4 * sizeof(AudioObjectID);
					break;
			};
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
			switch(inAddress->mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					*outDataSize = 2 * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeInput:
					*outDataSize = 1 * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeOutput:
					*outDataSize = 1 * sizeof(AudioObjectID);
					break;
			};
			break;

		case kAudioObjectPropertyControlList:
			*outDataSize = 6 * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			*outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			*outDataSize = 6 * sizeof(AudioValueRange);
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
	FailWithAction(inObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetDevicePropertyData: not the device object");
	
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
			*((CFStringRef*)outData) = CFStringCreateWithFormat(NULL, NULL, CFSTR(kDevice_Name), kNumber_Of_Channels);
			*outDataSize = sizeof(CFStringRef);
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
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	The device owns its streams and controls. Note that what is returned here
			//	depends on the scope requested.
			switch(inAddress->mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all objects
					if(theNumberItemsToFetch > 6)
					{
						theNumberItemsToFetch = 6;
					}
					
					//	fill out the list with as many objects as requested, which is everything
					for(theItemIndex = 0; theItemIndex < theNumberItemsToFetch; ++theItemIndex)
					{
						((AudioObjectID*)outData)[theItemIndex] = kObjectID_Stream_Input + theItemIndex;
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > 3)
					{
						theNumberItemsToFetch = 3;
					}
					
					//	fill out the list with the right objects
					for(theItemIndex = 0; theItemIndex < theNumberItemsToFetch; ++theItemIndex)
					{
						((AudioObjectID*)outData)[theItemIndex] = kObjectID_Stream_Input + theItemIndex;
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > 3)
					{
						theNumberItemsToFetch = 3;
					}
					
					//	fill out the list with the right objects
					for(theItemIndex = 0; theItemIndex < theNumberItemsToFetch; ++theItemIndex)
					{
						((AudioObjectID*)outData)[theItemIndex] = kObjectID_Stream_Output + theItemIndex;
					}
					break;
			};
			
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			//	This is a CFString that is a persistent token that can identify the same
			//	audio device across boot sessions. Note that two instances of the same
			//	device must have different values for this property.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");
			*((CFStringRef*)outData) = CFStringCreateWithFormat(NULL, NULL, CFSTR(kDevice_UID), kNumber_Of_Channels);
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			//	This is a CFString that is a persistent token that can identify audio
			//	devices that are the same kind of device. Note that two instances of the
			//	save device must have the same value for this property.
			FailWithAction(inDataSize < sizeof(CFStringRef), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyModelUID for the device");
			*((CFStringRef*)outData) = CFStringCreateWithFormat(NULL, NULL, CFSTR(kDevice_ModelUID), kNumber_Of_Channels);
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
				((AudioObjectID*)outData)[0] = kObjectID_Device;
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
			//	note uncommon for a device to be dead but still momentarily availble in the
			//	device list. In the case of this device, it will always be alive.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsAlive for the device");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			//	This property returns whether or not IO is running for the device. Note that
			//	we need to take both the state lock to check this value for thread safety.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsRunning for the device");
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((UInt32*)outData) = ((gDevice_IOIsRunning > 0) > 0) ? 1 : 0;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			//	This property returns whether or not the device wants to be able to be the
			//	default device for content. This is the device that iTunes and QuickTime
			//	will use to play their content on and FaceTime will use as it's microhphone.
			//	Nearly all devices should allow for this.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultDevice for the device");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			//	This property returns whether or not the device wants to be the system
			//	default device. This is the device that is used to play interface sounds and
			//	other incidental or UI-related sounds on. Most devices should allow this
			//	although devices with lots of latency may not want to.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultSystemDevice for the device");
			*((UInt32*)outData) = 1;
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
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Note that what is returned here depends on the scope requested.
			switch(inAddress->mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all streams
					if(theNumberItemsToFetch > 2)
					{
						theNumberItemsToFetch = 2;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						((AudioObjectID*)outData)[0] = kObjectID_Stream_Input;
					}
					if(theNumberItemsToFetch > 1)
					{
						((AudioObjectID*)outData)[1] = kObjectID_Stream_Output;
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > 1)
					{
						theNumberItemsToFetch = 1;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						((AudioObjectID*)outData)[0] = kObjectID_Stream_Input;
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > 1)
					{
						theNumberItemsToFetch = 1;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						((AudioObjectID*)outData)[0] = kObjectID_Stream_Output;
					}
					break;
			};
			
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			if(theNumberItemsToFetch > 4)
			{
				theNumberItemsToFetch = 4;
			}
			
			//	fill out the list with as many objects as requested, which is everything
			for(theItemIndex = 0; theItemIndex < theNumberItemsToFetch; ++theItemIndex)
			{
				if(theItemIndex < 2)
				{
					((AudioObjectID*)outData)[theItemIndex] = kObjectID_Volume_Input_Master + theItemIndex;
				}
				else
				{
					((AudioObjectID*)outData)[theItemIndex] = kObjectID_Volume_Output_Master + (theItemIndex - 2);
				}
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
			if(theNumberItemsToFetch > 12)
			{
				theNumberItemsToFetch = 12;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
				((AudioValueRange*)outData)[0].mMinimum = 44100.0;
				((AudioValueRange*)outData)[0].mMaximum = 44100.0;
			}
			if(theNumberItemsToFetch > 1)
			{
				((AudioValueRange*)outData)[1].mMinimum = 48000.0;
				((AudioValueRange*)outData)[1].mMaximum = 48000.0;
			}
            if(theNumberItemsToFetch > 2)
            {
                ((AudioValueRange*)outData)[2].mMinimum = 88200.0;
                ((AudioValueRange*)outData)[2].mMaximum = 88200.0;
            }
            if(theNumberItemsToFetch > 3)
            {
                ((AudioValueRange*)outData)[3].mMinimum = 96000.0;
                ((AudioValueRange*)outData)[3].mMaximum = 96000.0;
            }
            if(theNumberItemsToFetch > 4)
            {
                ((AudioValueRange*)outData)[4].mMinimum = 176400.0;
                ((AudioValueRange*)outData)[4].mMaximum = 176400.0;
            }
            if(theNumberItemsToFetch > 5)
            {
                ((AudioValueRange*)outData)[5].mMinimum = 192000.0;
                ((AudioValueRange*)outData)[5].mMaximum = 192000.0;
            }
            if(theNumberItemsToFetch > 6)
            {
                ((AudioValueRange*)outData)[6].mMinimum = 352800.0;
                ((AudioValueRange*)outData)[6].mMaximum = 352800.0;
            }
            if(theNumberItemsToFetch > 7)
            {
                ((AudioValueRange*)outData)[7].mMinimum = 384000.0;
                ((AudioValueRange*)outData)[7].mMaximum = 384000.0;
            }
            if(theNumberItemsToFetch > 8)
            {
                ((AudioValueRange*)outData)[8].mMinimum = 705600.0;
                ((AudioValueRange*)outData)[8].mMaximum = 705600.0;
            }
            if(theNumberItemsToFetch > 9)
            {
                ((AudioValueRange*)outData)[9].mMinimum = 768000.0;
                ((AudioValueRange*)outData)[9].mMaximum = 768000.0;
            }
            if(theNumberItemsToFetch > 10)
            {
                ((AudioValueRange*)outData)[10].mMinimum = 8000.0;
                ((AudioValueRange*)outData)[10].mMaximum = 8000.0;
            }
            if(theNumberItemsToFetch > 11)
            {
                ((AudioValueRange*)outData)[11].mMinimum = 16000.0;
                ((AudioValueRange*)outData)[11].mMaximum = 16000.0;
            }
			
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			//	This returns whether or not the device is visible to clients.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetDevicePropertyData: not enough space for the return value of kAudioDevicePropertyIsHidden for the device");
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			//	This property returns which two channesl to use as left/right for stereo
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
				//	calcualte how big the
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
				CFURLRef theURL = CFBundleCopyResourceURL(theBundle, CFSTR("BlackHole.icns"), NULL, NULL);
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
	UInt64 theNewSampleRate;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetDevicePropertyData: bad driver reference");
	FailWithAction(inAddress == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no address");
	FailWithAction(outNumberPropertiesChanged == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no place to return the number of properties that changed");
	FailWithAction(outChangedAddresses == NULL, theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: no place to return the properties that changed");
	FailWithAction(inObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_SetDevicePropertyData: not the device object");
	
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
			FailWithAction((*((const Float64*)inData) != 44100.0) && (*((const Float64*)inData) != 48000.0) && (*((const Float64*)inData) != 88200.0) && (*((const Float64*)inData) != 96000.0) && (*((const Float64*)inData) != 176400.0) && (*((const Float64*)inData) != 192000.0) && (*((const Float64*)inData) != 352800.0) && (*((const Float64*)inData) != 384000.0) && (*((const Float64*)inData) != 705600.0) && (*((const Float64*)inData) != 768000.0) && (*((const Float64*)inData) != 8000.0) && (*((const Float64*)inData) != 16000.0), theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetDevicePropertyData: unsupported value for kAudioDevicePropertyNominalSampleRate");
			
			//	make sure that the new value is different than the old value
			pthread_mutex_lock(&gPlugIn_StateMutex);
			theOldSampleRate = gDevice_SampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(*((const Float64*)inData) != theOldSampleRate)
			{
				//	we dispatch this so that the change can happen asynchronously
				theOldSampleRate = *((const Float64*)inData);
				theNewSampleRate = (UInt64)theOldSampleRate;
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, theNewSampleRate, NULL); });
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
			*outDataSize = 12 * sizeof(AudioStreamRangedDescription);
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
			//	the stream. For exmaple, if a device has two output streams with two
			//	channels each, then the starting channel number for the first stream is 1
			//	and ths starting channel number fo the second stream is 3.
			FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetStreamPropertyData: not enough space for the return value of kAudioStreamPropertyStartingChannel for the stream");
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyLatency:
			//	This property returns any additonal presentation latency the stream has.
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
			if(theNumberItemsToFetch > 12)
			{
				theNumberItemsToFetch = 12;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mSampleRate = 44100.0;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[0].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[0].mSampleRateRange.mMinimum = 44100.0;
                ((AudioStreamRangedDescription*)outData)[0].mSampleRateRange.mMaximum = 44100.0;
			}
			if(theNumberItemsToFetch > 1)
			{
				((AudioStreamRangedDescription*)outData)[1].mFormat.mSampleRate = 48000.0;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFormatID = kAudioFormatLinearPCM;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBytesPerPacket = kBytes_Per_Frame;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFramesPerPacket = 1;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBytesPerFrame = kBytes_Per_Frame;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBitsPerChannel = kBits_Per_Channel;
				((AudioStreamRangedDescription*)outData)[1].mSampleRateRange.mMinimum = 48000.0;
				((AudioStreamRangedDescription*)outData)[1].mSampleRateRange.mMaximum = 48000.0;
			}
            if(theNumberItemsToFetch > 2)
            {
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mSampleRate = 88200.0;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[2].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[2].mSampleRateRange.mMinimum = 88200.0;
                ((AudioStreamRangedDescription*)outData)[2].mSampleRateRange.mMaximum = 88200.0;
            }
            if(theNumberItemsToFetch > 3)
            {
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mSampleRate = 96000.0;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[3].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[3].mSampleRateRange.mMinimum = 96000.0;
                ((AudioStreamRangedDescription*)outData)[3].mSampleRateRange.mMaximum = 96000.0;
            }
            if(theNumberItemsToFetch > 4)
            {
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mSampleRate = 176400.0;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[4].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[4].mSampleRateRange.mMinimum = 176400.0;
                ((AudioStreamRangedDescription*)outData)[4].mSampleRateRange.mMaximum = 176400.0;
            }
            if(theNumberItemsToFetch > 5)
            {
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mSampleRate = 192000.0;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[5].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[5].mSampleRateRange.mMinimum = 192000.0;
                ((AudioStreamRangedDescription*)outData)[5].mSampleRateRange.mMaximum = 192000.0;
            }
            if(theNumberItemsToFetch > 6)
            {
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mSampleRate = 352800.0;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[6].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[6].mSampleRateRange.mMinimum = 352800.0;
                ((AudioStreamRangedDescription*)outData)[6].mSampleRateRange.mMaximum = 352800.0;
            }
            if(theNumberItemsToFetch > 7)
            {
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mSampleRate = 384000.0;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[7].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[7].mSampleRateRange.mMinimum = 384000.0;
                ((AudioStreamRangedDescription*)outData)[7].mSampleRateRange.mMaximum = 384000.0;
            }
            if(theNumberItemsToFetch > 8)
            {
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mSampleRate = 705600.0;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[8].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[8].mSampleRateRange.mMinimum = 705600.0;
                ((AudioStreamRangedDescription*)outData)[8].mSampleRateRange.mMaximum = 705600.0;
            }
            if(theNumberItemsToFetch > 9)
            {
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mSampleRate = 768000.0;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[9].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[9].mSampleRateRange.mMinimum = 768000.0;
                ((AudioStreamRangedDescription*)outData)[9].mSampleRateRange.mMaximum = 768000.0;
            }
            if(theNumberItemsToFetch > 10)
            {
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mSampleRate = 8000.0;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[10].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[10].mSampleRateRange.mMinimum = 8000.0;
                ((AudioStreamRangedDescription*)outData)[10].mSampleRateRange.mMaximum = 8000.0;
            }
            if(theNumberItemsToFetch > 11)
            {
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mSampleRate = 16000.0;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[11].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[11].mSampleRateRange.mMinimum = 16000.0;
                ((AudioStreamRangedDescription*)outData)[11].mSampleRateRange.mMaximum = 16000.0;
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
	UInt64 theNewSampleRate;
	
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
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
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
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
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
			FailWithAction((((const AudioStreamBasicDescription*)inData)->mSampleRate != 44100.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 48000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 88200.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 96000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 176400.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 192000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 352800.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 384000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 705600.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 768000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 8000.0) && (((const AudioStreamBasicDescription*)inData)->mSampleRate != 16000.0), theAnswer = kAudioHardwareIllegalOperationError, Done, "BlackHole_SetStreamPropertyData: unsupported sample rate for kAudioStreamPropertyPhysicalFormat");
			
			//	If we made it this far, the requested format is something we support, so make sure the sample rate is actually different
			pthread_mutex_lock(&gPlugIn_StateMutex);
			theOldSampleRate = gDevice_SampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(((const AudioStreamBasicDescription*)inData)->mSampleRate != theOldSampleRate)
			{
				//	we dispatch this so that the change can happen asynchronously
				theOldSampleRate = ((const AudioStreamBasicDescription*)inData)->mSampleRate;
				theNewSampleRate = (UInt64)theOldSampleRate;
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, theNewSampleRate, NULL); });
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
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMaster;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioLevelControlPropertyScalarValue:
					//	This returns the value of the control in the normalized range of 0 to 1.
					//	Note that we need to take the state lock to examine the value.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyScalarValue for the volume control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((Float32*)outData) = volume_to_scalar((inObjectID == kObjectID_Volume_Input_Master) ? gVolume_Master_Value : gVolume_Master_Value);
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(Float32);
					break;

				case kAudioLevelControlPropertyDecibelValue:
					//	This returns the dB value of the control.
					//	Note that we need to take the state lock to examine the value.
					FailWithAction(inDataSize < sizeof(Float32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((Float32*)outData) = (inObjectID == kObjectID_Volume_Input_Master) ? gVolume_Master_Value : gVolume_Master_Value;
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
					*((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMaster;
					*outDataSize = sizeof(AudioObjectPropertyElement);
					break;

				case kAudioBooleanControlPropertyValue:
					//	This returns the value of the mute control where 0 means that mute is off
					//	and audio can be heard and 1 means that mute is on and audio cannot be heard.
					//	Note that we need to take the state lock to examine this value.
					FailWithAction(inDataSize < sizeof(UInt32), theAnswer = kAudioHardwareBadPropertySizeError, Done, "BlackHole_GetControlPropertyData: not enough space for the return value of kAudioBooleanControlPropertyValue for the mute control");
					pthread_mutex_lock(&gPlugIn_StateMutex);
					*((UInt32*)outData) = (inObjectID == kObjectID_Mute_Input_Master) ? (gMute_Master_Value ? 1 : 0) : (gMute_Master_Value ? 1 : 0);
					pthread_mutex_unlock(&gPlugIn_StateMutex);
					*outDataSize = sizeof(UInt32);
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
					if(inObjectID == kObjectID_Volume_Input_Master)
					{
						if(gVolume_Master_Value != theNewVolume)
						{
							gVolume_Master_Value = theNewVolume;
							*outNumberPropertiesChanged = 2;
							outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
							outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
							outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[1].mElement = kAudioObjectPropertyElementMaster;
						}
					}
					else
					{
						if(gVolume_Master_Value != theNewVolume)
						{
							gVolume_Master_Value = theNewVolume;
							*outNumberPropertiesChanged = 2;
							outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
							outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
							outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[1].mElement = kAudioObjectPropertyElementMaster;
						}
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
					if(inObjectID == kObjectID_Volume_Input_Master)
					{
						if(gVolume_Master_Value != theNewVolume)
						{
							gVolume_Master_Value = theNewVolume;
							*outNumberPropertiesChanged = 2;
							outChangedAddresses[0].mSelector = gVolume_Master_Value;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
							outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
							outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[1].mElement = kAudioObjectPropertyElementMaster;
						}
					}
					else
					{
						if(gVolume_Master_Value != theNewVolume)
						{
							gVolume_Master_Value = theNewVolume;
							*outNumberPropertiesChanged = 2;
							outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
							outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
							outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[1].mElement = kAudioObjectPropertyElementMaster;
						}
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
					if(inObjectID == kObjectID_Mute_Input_Master)
					{
						if(gMute_Master_Value != (*((const UInt32*)inData) != 0))
						{
							gMute_Master_Value = *((const UInt32*)inData) != 0;
							*outNumberPropertiesChanged = 1;
							outChangedAddresses[0].mSelector = kAudioBooleanControlPropertyValue;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
						}
					}
					else
					{
						if(gMute_Master_Value != (*((const UInt32*)inData) != 0))
						{
							gMute_Master_Value = *((const UInt32*)inData) != 0;
							*outNumberPropertiesChanged = 1;
							outChangedAddresses[0].mSelector = kAudioBooleanControlPropertyValue;
							outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
							outChangedAddresses[0].mElement = kAudioObjectPropertyElementMaster;
						}
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
	
	#pragma unused(inClientID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StartIO: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StartIO: bad device ID");

	//	we need to hold the state lock
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
	//	figure out what we need to do
	if(gDevice_IOIsRunning == UINT64_MAX)
	{
		//	overflowing is an error
		theAnswer = kAudioHardwareIllegalOperationError;
	}
	else if(gDevice_IOIsRunning == 0)
	{
		//	We need to start the hardware, which in this case is just anchoring the time line.
		gDevice_IOIsRunning = 1;
		gDevice_NumberTimeStamps = 0;
		gDevice_AnchorSampleTime = 0;
		gDevice_AnchorHostTime = mach_absolute_time();
        
        // allocate ring buffer
        gRingBuffer = calloc(kDevice_RingBufferSize * kNumber_Of_Channels, sizeof(Float32));
	}
	else
	{
		//	IO is already running, so just bump the counter
		++gDevice_IOIsRunning;
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
	
	#pragma unused(inClientID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StopIO: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_StopIO: bad device ID");

	//	we need to hold the state lock
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
	//	figure out what we need to do
	if(gDevice_IOIsRunning == 0)
	{
		//	underflowing is an error
		theAnswer = kAudioHardwareIllegalOperationError;
	}
	else if(gDevice_IOIsRunning == 1)
	{
		//	We need to stop the hardware, which in this case means that there's nothing to do.
		gDevice_IOIsRunning = 0;
        free(gRingBuffer);
	}
	else
	{
		//	IO is still running, so just bump the counter
		--gDevice_IOIsRunning;
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
	
	#pragma unused(inClientID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	UInt64 theCurrentHostTime;
	Float64 theHostTicksPerRingBuffer;
	Float64 theHostTickOffset;
	UInt64 theNextHostTime;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetZeroTimeStamp: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_GetZeroTimeStamp: bad device ID");

	//	we need to hold the locks
	pthread_mutex_lock(&gDevice_IOMutex);
	
	//	get the current host time
	theCurrentHostTime = mach_absolute_time();
	
	//	calculate the next host time
	theHostTicksPerRingBuffer = gDevice_HostTicksPerFrame * ((Float64)kDevice_RingBufferSize);
    
	theHostTickOffset = ((Float64)(gDevice_NumberTimeStamps + 1)) * theHostTicksPerRingBuffer;
    
	theNextHostTime = gDevice_AnchorHostTime + ((UInt64)theHostTickOffset);
	
	//	go to the next time if the next host time is less than the current time
	if(theNextHostTime <= theCurrentHostTime)
	{
		++gDevice_NumberTimeStamps;
	}
	
	//	set the return values
	*outSampleTime = gDevice_NumberTimeStamps * kDevice_RingBufferSize;
	*outHostTime = gDevice_AnchorHostTime + (((Float64)gDevice_NumberTimeStamps) * theHostTicksPerRingBuffer);
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
	
	#pragma unused(inClientID)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_WillDoIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_WillDoIOOperation: bad device ID");

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
	
	#pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_BeginIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_BeginIOOperation: bad device ID");

Done:
	return theAnswer;
}

static OSStatus	BlackHole_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
	//	This is called to actually perform a given operation. 
	
	#pragma unused(inClientID, inIOCycleInfo, ioSecondaryBuffer)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DoIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_DoIOOperation: bad device ID");
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
        // If mute is one let's just fill the buffer with zeros or if there's no apps outputing audio
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
            cblas_scopy(firstPartFrameSize * kNumber_Of_Channels, gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, 1, ioMainBuffer, 1);
            cblas_scopy(secondPartFrameSize * kNumber_Of_Channels, gRingBuffer, 1, (Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, 1);

            // Finally we'll apply the output volume to the buffer.
            vDSP_vsmul(ioMainBuffer, 1, &gVolume_Master_Value, ioMainBuffer, 1, inIOBufferFrameSize * kNumber_Of_Channels);
        }
        

    }
    
    // From Application to BlackHole
    if(inOperationID == kAudioServerPlugInIOOperationWriteMix)
    {
        // Save the last output time.
        lastOutputSampleTime= inIOCycleInfo->mOutputTime.mSampleTime;
        isBufferClear = false;
        
        // Copy the buffers.
        cblas_scopy(firstPartFrameSize * kNumber_Of_Channels, ioMainBuffer, 1, gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, 1);
        cblas_scopy(secondPartFrameSize * kNumber_Of_Channels, (Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, 1, gRingBuffer, 1);
        
    }

Done:
	return theAnswer;
}

static OSStatus	BlackHole_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
	//	This is called at the end of an IO operation. This device doesn't do anything, so just check
	//	the arguments and return.
	
	#pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo)
	
	//	declare the local variables
	OSStatus theAnswer = 0;
	
	//	check the arguments
	FailWithAction(inDriver != gAudioServerPlugInDriverRef, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_EndIOOperation: bad driver reference");
	FailWithAction(inDeviceObjectID != kObjectID_Device, theAnswer = kAudioHardwareBadObjectError, Done, "BlackHole_EndIOOperation: bad device ID");

Done:
	return theAnswer;
}
