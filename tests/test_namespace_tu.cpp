// Separate translation unit compiled w/o 'using namespace': verifies that the
// client and server functionality is in the tinyftp namespace
#define FTP_USE_NAMESPACE false
#include "FTPClient.h"
#include "MockFTPServer.h"

bool qualifiedNamesWork() {
  tinyftp::FTPLogger.setLogLevel(tinyftp::LOG_ERROR);
  tinyftp::FTPClient<MockClient> client;
  client.begin(IPAddress(127, 0, 0, 1), "user", "pwd");
  tinyftp::FTPFile file = client.open("/ns.txt", tinyftp::WRITE_MODE);
  file.print("ns");
  file.close();
  return server().files["/ns.txt"] == "ns";
}
