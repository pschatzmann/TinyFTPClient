// Separate translation unit: verifies that the FTPLogger settings are shared
#include "FTPClient.h"

void setLoggerOutputFromOtherTU(Stream &out) { FTPLogger.setOutput(out); }
