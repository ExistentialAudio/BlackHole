#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <signal.h>

#ifndef kSeverPort
#define                             kEventSeverPort                     25192
#endif

static unsigned long gDeviceId = 0;
static char*   gAppName;
static char*   gAppPath;
static char**  gAppArgv;

enum DeviceEvent
{
  DeviceEventNone    = 0,  // No event. Can be used for pings, etc.
  DeviceEventStarted = 1,
  DeviceEventStopped = 62,
  DeviceEventMax     = 63, // Maximum possible value of device event
};

static void BlackHole_listen(int socket)
{
  pid_t subprocessPID = -1;
  uint8_t eventMessage = 0;
  uint8_t eventMessageSize = sizeof(eventMessage);
  size_t result = 0;
  uint8_t deviceMask = 3 << 6;
  uint8_t eventMask = ~deviceMask;
  
  while(1) {
    result = read(socket, &eventMessage, eventMessageSize);
    if (result < eventMessageSize) {
      perror("Client: Socket read error");
      break;
    }
    
    uint8_t deviceId = (eventMessage & deviceMask) >> 6;
    uint8_t event = eventMessage & eventMask;
    
    printf("Client: Received event %i for device id %i\n", event, deviceId);
    
    if (deviceId != gDeviceId) {
      continue;
    }
    
    if (event == DeviceEventStarted) {
      if (subprocessPID > 0) {
        printf("Subprocess: Already running");
        continue;
      }
      if ((subprocessPID = fork()) == 0) {
        if (execv(gAppPath, gAppArgv) < 0) {
          perror("Subprocess: execv error ");
        }
      } else if (subprocessPID < 0) {
        printf("Subprocess: fork error");
      } else {
        printf("Subprocess: started");
      }
    } else if (event == DeviceEventStopped) {
      if (subprocessPID > 0) {
        printf("Subprocess: closed");
        kill(subprocessPID, SIGTERM);
        subprocessPID = -1;
      }
    }
    
  }
  close(socket);
}

static void print_help(char* errorMessage)
{
  if (errorMessage != NULL) {
    printf("Error: %s\n\n", errorMessage);
  }
  
  printf("BlackHole loopback driver client.\n\n");
  printf("Liestens to events of the specified BlackHole device.\n");
  printf("Starts the specified app when the device becomes active.\n");
  printf("Terminates the app when the device becomes inactive.\n");
  printf("\n");
  printf("Usage:\n");
  printf("%s (device id) (app path) [app arguments]\n\n", gAppName);
  printf("device id:          ID (either 1 or 2) of the BackHole device to subscribe to.\n");
  printf("app path:           Path to the app to run when the device activates.\n");
  printf("app arguments:      Arguments to pass to the app.\n");
}

int main(int argc, char *argv[])
{
  gAppName = argv[0];
  char* searchResult = gAppName;
  
  while (searchResult != NULL) {
    searchResult = strstr(searchResult + 1, "/");
    if (searchResult != NULL) {
      gAppName = searchResult + 1;
    }
  }
  
  if (argc < 3) {
    print_help("Too few arguments");
    return 0;
  }
  
  int appPathIndex = 2;
  size_t appArgvCount = argc - appPathIndex;
  
  gAppArgv = (char **) malloc(sizeof(char *) * (appPathIndex + 1));
  
  int appArgvIndex = 0;
  for (int i = appPathIndex; i < argc; i++) {
    gAppArgv[appArgvIndex] = argv[i];
    appArgvIndex++;
  }
  
  gAppArgv[appArgvCount] = NULL;
  gDeviceId = strtoul(argv[1], NULL, 0);
  gAppPath = argv[appPathIndex];
  
  printf("DeviceId: %lu\n", gDeviceId);
  printf("App to run: ");
  
  for (uint i = 0; i < appArgvCount; i++) {
    if (gAppArgv[i] == NULL) {
      break;
    }
    printf(" %s", gAppArgv[i]);
  }
  printf("\n\n");
  
  while (1) {
    int server_socket;
    int result;
    struct sockaddr_in server_addr;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(kEventSeverPort);
    result = connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (result == -1) {
      perror("Error:");
      sleep(1);
      printf("Reconnecting...\n");
      continue;
    }
    
    BlackHole_listen(server_socket);
    printf("Disconnected\n");
    sleep(1);
    printf("Reconnecting...\n");
  }
}
