#include "TAudioDeviceSetup.h"

TAudioDeviceSetup::TAudioDeviceSetup() {
    rate = 44100;
    bufferSize = 1024;
    driverType = "default";
    playback = capture = true;
    cardDevice = "";
    ditherShape = "None";
}
