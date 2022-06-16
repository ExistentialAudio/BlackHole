//
//  main.cpp
//  BlackHoleTests
//
//  Created by Devin Roth on 2022-06-16.
//

#include <stdio.h>


#define kDevice_HasInput false
#define kDevice_HasOutput true

#include "../BlackHole/BlackHole.c"



int main(int argc, const char * argv[]) {
    
#pragma unused(argc, argv)
    // insert code here...
    
    UInt32 size;
    UInt32* data;
    AudioObjectPropertyAddress address;
    
    // Owned Objects
    address.mSelector = kAudioObjectPropertyOwnedObjects;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    assert(BlackHole_HasProperty(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address));

    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 12);
    
    address.mScope = kAudioObjectPropertyScopeInput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 0);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 0);
    free(data);
    
    address.mScope = kAudioObjectPropertyScopeOutput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 12);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 7);
    assert(data[1] == 8);
    assert(data[2] == 9);
    free(data);
    
    // Streams
    address.mSelector = kAudioDevicePropertyStreams;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    assert(BlackHole_HasProperty(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address));

    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 4);
    
    address.mScope = kAudioObjectPropertyScopeInput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 0);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 0);
    free(data);
    
    address.mScope = kAudioObjectPropertyScopeOutput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 4);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 7);
    free(data);
    
    
    // Controls
    address.mSelector = kAudioClockDevicePropertyControlList;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    assert(BlackHole_HasProperty(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address));

    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 8);
    
    address.mScope = kAudioObjectPropertyScopeInput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 0);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 0);
    free(data);
    
    address.mScope = kAudioObjectPropertyScopeOutput;
    BlackHole_GetPropertyDataSize(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, &size);
    assert(size == 8);
    
    data = calloc(size, 1);
    BlackHole_GetPropertyData(gAudioServerPlugInDriverRef, kObjectID_Device, 0, &address, 0, NULL, size, &size, data);
    assert(data[0] == 8);
    assert(data[1] == 9);
    free(data);

    

    
    return 0;
}
