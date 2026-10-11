#pragma once
// Entry point for the FTP server functionality
#include "server/FTPServer.h"
#include "server/CallbackFS.h"

#ifndef FTP_USE_NAMESPACE
#define FTP_USE_NAMESPACE true
#endif

#if FTP_USE_NAMESPACE
using namespace tinyftp;
#endif
