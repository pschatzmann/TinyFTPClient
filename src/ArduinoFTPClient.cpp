#include "ArduinoFTPClient.h"
#include <stdio.h>

namespace ftp_client {

/**
 * @brief FTPLogger
 * To activate logging define the output stream e.g. with FTPLogger.setOutput(Serial);
 * and optionally set the log level
 */

void FTPLogger::setLogLevel(LogLevel level){     
    min_log_level  = level;   
}
LogLevel FTPLogger::getLogLevel(){
    return min_log_level;
}
void FTPLogger::setOutput(Stream &out){
    out_ptr = &out;
}

void FTPLogger::writeLog(LogLevel level, const char* module, const char* msg){
    if (out_ptr!=nullptr && level>=min_log_level) {
        out_ptr->print("FTP ");
        switch (level){
            case LOG_DEBUG:
                out_ptr->print("DEBUG - ");
                break;
            case LOG_INFO:
                out_ptr->print("INFO - ");
                break;
            case LOG_WARN:
                out_ptr->print("WARN - ");
                break;
            case LOG_ERROR:
                out_ptr->print("ERROR - ");
                break;
        }
        out_ptr->print(module);
        if (msg!=nullptr) {
            out_ptr->print(" ");
            out_ptr->print(msg);
        }
        out_ptr->println();
    } else {
        //delay(100);
    }
}

// initialize static variables
LogLevel FTPLogger::min_log_level = LOG_ERROR;
Stream *FTPLogger::out_ptr = nullptr; 


/**
 * @brief FTPBasicAPI
 * Implementation of Low Level FTP protocol. In order to simplify the logic we always use Passive FTP where
 * it is our responsibility to open the data conection.
 */

FTPBasicAPI::FTPBasicAPI(){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI");      
}
FTPBasicAPI::~FTPBasicAPI(){
    FTPLogger::writeLog( LOG_DEBUG, "~FTPBasicAPI");      
}

bool FTPBasicAPI::open(Client *cmdPar, Client *dataPar, IPAddress &address, int port, int data_port, const char* username, const char *password){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI ", "open");
    command_ptr = cmdPar;
    data_ptr = dataPar;  
    remote_address = address;

    if(!connect( address, port, command_ptr, true)) return false;
    if (username!=nullptr) {
        const char* ok_result[] = {"331","230","530", nullptr};
        if(!cmd("USER", username, ok_result)) return false;
    }
    if (password!=nullptr) {
        const char* ok_result[] = {"230","202",nullptr};
        if(!cmd("PASS", password, ok_result)) return false;
    }
    
    is_open = true;;
    return true;
}

bool FTPBasicAPI::quit() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "quit");      
    const char* ok_result[] = {"221","226", nullptr};
    bool result = cmd("QUIT", nullptr, ok_result, false);  
    if (!result) {
        result = cmd("BYE", nullptr, ok_result, false);  
    }
    if (!result) {
        result = cmd("DISCONNECT", nullptr, ok_result, false);  
    }
    return result;  
}

bool FTPBasicAPI::connected(){
    return is_open;
}

bool FTPBasicAPI::passv() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","passv");      
    bool ok = cmd("PASV", nullptr, "227");
    if (ok) {
        char buffer[80];
        FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", result_reply);
        // determine data port
        int start1 = CStringFunctions::findNthInStr(result_reply,',',4)+1;
        int p1 = atoi(result_reply+start1);
        
        sprintf(buffer,"*** port1 -> %d ",  p1 );
        FTPLogger::writeLog(LOG_DEBUG,"FTPBasicAPI::passv", buffer);

        int start2 = CStringFunctions::findNthInStr(result_reply,',',5)+1;
        int p2 = atoi(result_reply+start2);
        
        sprintf(buffer,"*** port2 -> %d ", p2 );
        FTPLogger::writeLog(LOG_DEBUG,"FTPBasicAPI::passv", buffer);

        int data_port = (p1 * 256) + p2;
        sprintf(buffer,"*** data port: %d", data_port);
        FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

        ok = connect(remote_address, data_port, data_ptr) == 1;
    }
    return ok;
}

bool FTPBasicAPI::del(const char * file) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","del");      
    return cmd("DELE", file, "250");
}

bool FTPBasicAPI::mkdir(const char * dir) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","mkdir");      
    return cmd("MKD", dir, "257");
}

bool FTPBasicAPI::rmd(const char * dir){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","rd");      
    return cmd("RMD", dir, "250");
}

size_t FTPBasicAPI::size(const char * file){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","size");      
    if (cmd("SIZE", file, "213")) {
        return atol(result_reply+4);
    }
    return 0;
}

ObjectType FTPBasicAPI::objectType(const char * file) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","objectType"); 
    const char* ok_result[] = {"213","550", nullptr}; 
    ObjectType result =  TypeDirectory;   
    if (cmd("SIZE", file, ok_result)) {
        if (strncmp(result_reply,"213",3)==0){
            result = TypeFile;
        } 
    }
    return result;
}

bool FTPBasicAPI::abort() {
    bool rc = true;
    if (current_operation == READ_OP || current_operation == WRITE_OP || current_operation == LS_OP ) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","abort");      
        if (data_ptr->connected())data_ptr->stop();

        const char* ok[] = {"426", "226", "225",nullptr};
        setCurrentOperation(NOP);
        rc = cmd("ABOR", nullptr, ok);
        delay(FTP_ABORT_DELAY_MS);
        while(command_ptr->available() > 0)
           checkResult(ok, "ABOR", false);
    }
    return rc;
}

bool FTPBasicAPI::binary() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","binary");      
    return cmd("BIN", nullptr, "200");
}

bool FTPBasicAPI::ascii() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","ascii");      
    return cmd("ASC", nullptr, "200");
}

bool FTPBasicAPI::type(const char* txt) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","type");      
    return cmd("TYPE", txt, "200");
}

Stream *FTPBasicAPI::read(const char* file_name ) {
    if (current_operation != READ_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "read");      
        const char* ok[] = {"150","125", nullptr};
        cmd("RETR", file_name, ok);
        setCurrentOperation(READ_OP);
    }
    checkClosed(data_ptr);
    return data_ptr;
}

Stream *FTPBasicAPI::write(const char* file_name, FileMode mode) {
    if (current_operation!=WRITE_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "write");      
        const char* ok_write[] = {"125", "150", nullptr};
        cmd(mode == WRITE_APPEND_MODE ? "APPE": "STOR", file_name, ok_write);
        setCurrentOperation(WRITE_OP);
    }
    checkClosed(data_ptr);
    return data_ptr;
}

Stream* FTPBasicAPI::ls(const char* file_name){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "ls");      
    const char* ok[] = {"125", "150", nullptr};
    cmd("NLST",file_name, ok);
    setCurrentOperation(LS_OP);
    return data_ptr;
}

void FTPBasicAPI::closeData() {
    FTPLogger::writeLog( LOG_INFO, "FTPBasicAPI","closeData");      
    data_ptr->stop();
}

void FTPBasicAPI::setCurrentOperation(CurrentOperation op){
    char msg[80];
    sprintf(msg, "setCurrentOperation: %d", (int)op);
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", msg);      
    current_operation = op;
}

void FTPBasicAPI::flush() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","flush");      
    data_ptr->flush();
}

bool FTPBasicAPI::connect(IPAddress adr, int port, Client *client_ptr, bool doCheckResult  ){
    char buffer[80];
    bool ok = true;
#if defined(ESP32) || defined(ESP8266)
    sprintf(buffer,"connect %s:%d", adr.toString().c_str(), port);
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::connect", buffer); 
#endif         
    // try to connect 10 times
    client_ptr->stop(); // make sure we start with a clean state
    delay(100);
    for (int j=0;j < 10; j++){
        ok = client_ptr->connect(adr, port);
        if (ok) break;
        delay(500);
    }
    //ok = client_ptr->connect(adr, port);
    ok = client_ptr->connected();
    if (ok && doCheckResult){
        const char* ok_result[] = {"220","200",nullptr};
        ok = checkResult(ok_result, "connect");

        // there might be some more messages: e.g. in FileZilla: we just ignore them
        while(command_ptr->available() > 0){
            command_ptr->read();
        }
    }
    // log result
    if (ok) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::connected", buffer);      
    } else {
        FTPLogger::writeLog( LOG_ERROR, "FTPBasicAPI::connected", buffer);      
    }
    return ok;
}

bool FTPBasicAPI::cmd(const char* command, const char* par, const char* expected, bool wait_for_data) {
    const char* expected_array[] = { expected, nullptr };
    return cmd(command, par, expected_array, wait_for_data);
}

bool FTPBasicAPI::cmd(const char* command_str, const char* par, const char* expected[], bool wait_for_data) {
    char command_buffer[FTP_COMMAND_BUFFER_SIZE];
    Stream *stream_ptr = command_ptr;
    if (par==nullptr){
        strncpy(command_buffer,FTP_COMMAND_BUFFER_SIZE, command_str);
        stream_ptr->println(command_buffer);
    } else {
        snprintf(command_buffer,FTP_COMMAND_BUFFER_SIZE, "%s %s", command_str, par);
        stream_ptr->println(command_buffer);
    }
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::cmd", command_buffer);

    return checkResult(expected, command_buffer, wait_for_data);
}

bool FTPBasicAPI::checkResult(const char* expected[],const char* command, bool wait_for_data) {
    // consume all result lines
    bool ok = false;
    result_reply[0] = '\0';
    Stream *stream_ptr = command_ptr;

    char result_str[FTP_RESULT_BUFFER_SIZE];
    if (wait_for_data || stream_ptr->available()) {
        // wait for reply
        while(stream_ptr->available()==0){
            delay(100);
        }
        // read reply
        //result_str = stream_ptr->readStringUntil('\n').c_str();
        CStringFunctions::readln(*stream_ptr, result_str, FTP_RESULT_BUFFER_SIZE);

        if (strlen(result_str)>3) {  
            FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::checkResult", result_str);
            strncpy(result_reply, result_str, 80);
            // if we did not expect anything
            if (expected[0]==nullptr){
                ok = true;
                FTPLogger::writeLog( LOG_DEBUG,"FTPBasicAPI::checkResult", "success because of not expected result codes");
            } else {
                // check for valid codes
                char msg[80];
                for (int j=0; expected[j]!=nullptr; j++) {
                    sprintf(msg,"- checking with %s", expected[j]);
                    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::checkResult", msg);
                    if (strncmp(result_str, expected[j], 3)==0) {
                        sprintf(msg," -> success with %s", expected[j]);
                        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI::checkResult", msg);
                        ok = true;
                        break;
                    }
                }
            }
        } else {
            // if we got am empty line and we dont need to wait we are still ok
            if (!wait_for_data)
                ok = true;
        }
    } else {
        ok = true;
    }

    // log error
    if (!ok){
        FTPLogger::writeLog( LOG_ERROR,"FTPBasicAPI::checkResult", command);  
        FTPLogger::writeLog( LOG_ERROR,"FTPBasicAPI::checkResult", result_reply);
    }
    return ok;    
}

bool FTPBasicAPI::checkClosed(Client *client){
    if (!client->connected()){
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","checkClosed -> client is closed"); 
        // mark the actual command as completed     
        setCurrentOperation(IS_EOF);
        return true;
    }
    return false;
} 


/**
 * @brief FTPFile
 * A single file which supports read and write operations. This class is implemented
 * as an Arduino Stream
 * 
 */

FTPFile::FTPFile(FTPBasicAPI *api_ptr, const char* name, FileMode mode, bool autoClose){
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", name);
    auto_close = autoClose;
    if (name!=nullptr)
        file_name = name;
    this->mode = mode;
    this->api_ptr = api_ptr;
}


FTPFile::~FTPFile(){
    if (auto_close) close();
}

size_t FTPFile::write(uint8_t data) {
    if (!is_open) return 0;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "write");
    if (mode==READ_MODE) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "Can not write - File has been opened in READ_MODE");
        return 0;
    }
    Stream *result_ptr = api_ptr->write(file_name.c_str(), mode );
    return result_ptr->write(data);
}

size_t FTPFile::write(char* data, int len) {
    if (!is_open) return 0;
    int count = 0;
    for (int j=0;j<len;j++){
        count += write(data[j]);
    }
    return count;
}

int FTPFile::read() {
    if (!is_open) return -1;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "read");
    Stream *result_ptr =  api_ptr->read(file_name.c_str());
    return result_ptr->read();
}
size_t FTPFile::readBytes(uint8_t *buf, size_t nbyte) {
    if (!is_open) return 0;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "readBytes");
    memset(buf,0, nbyte);
    Stream *result_ptr =  api_ptr->read(file_name.c_str());
    return result_ptr->readBytes((char*)buf, nbyte);
}
size_t FTPFile::readln(char *buf, size_t nbyte) {
    if (!is_open) return 0;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "readln");
    memset(buf,0, nbyte);
    Stream *result_ptr =  api_ptr->read(file_name.c_str());
    return result_ptr->readBytesUntil(eol[0], (char*)buf, nbyte );
}
int FTPFile::peek() {
    if (!is_open) return -1;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "peek");
    Stream *result_ptr =  api_ptr->read(file_name.c_str());
    return result_ptr->peek();
}
int FTPFile::available(){
    if (api_ptr->currentOperation()== IS_EOF) return 0;
    if (!is_open) return 0;

    char msg[80];
    Stream *result_ptr =  api_ptr->read(file_name.c_str());
    int len = result_ptr->available();
    sprintf(msg,"available: %d", len);
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", msg);
    return len;
}

size_t FTPFile::size() const {
    if (!is_open) return 0;
    char msg[80];
    size_t size =  api_ptr->size(file_name.c_str());
    sprintf(msg,"size: %d", size);
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", msg);
    return size;
}

bool FTPFile::isDirectory() const {
    if (!is_open) return false;
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "isDirectory");
    return  api_ptr->objectType(file_name.c_str())==TypeDirectory;
}

void FTPFile::flush() {
    if (!is_open) return;
    if (api_ptr->currentOperation()==WRITE_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "flush");
        Stream *result_ptr =  api_ptr->write(file_name.c_str(), mode);
        result_ptr->flush();
    }
}

void FTPFile::close() {
    if (is_open){
        FTPLogger::writeLog( LOG_INFO, "FTPFile", "close");
        const char* ok[] = {"226", nullptr};
        bool is_ok = api_ptr->checkResult(ok, "close", false);
        if (api_ptr->currentOperation()==WRITE_OP) {
            flush();
        }
        api_ptr->closeData();
        is_open = false;
    }
}

const char * FTPFile::name() const {
    return file_name.c_str();
}

void FTPFile::setEOL(char* eol){
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "setEOL");
    this->eol = eol;
}    

/**
 * @brief FTPClient 
 * Basic FTP access class which supports directory operatations and the opening of a files 
 */
FTPClient::FTPClient(Client &command, Client &data, int port, int data_port){
    FTPLogger::writeLog( LOG_INFO, "FTPClient()");
    init(&command, &data, port, data_port);
}


void FTPClient::init(Client *command, Client *data, int port, int data_port){
    FTPLogger::writeLog( LOG_DEBUG, "FTPClient");
    this->command_ptr = command;
    this->data_ptr = data;
    this->port = port;
    this->data_port = data_port;
}

// opens the ftp connection  
bool FTPClient::begin(IPAddress remote_addr, const char* user, const char* password ) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "begin");
    this->userid = user;
    this->password = password;
    this->remote_addr = remote_addr;

    return api.open(this->command_ptr, this->data_ptr, remote_addr, this->port, this->data_port, user, password);
}

//call this when a card is removed. It will allow you to insert and initialise a new card.
bool FTPClient::end() {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "end");
    bool result = api.quit();

    api.setCurrentOperation(NOP);

    if (command_ptr) command_ptr->stop();
    if (data_ptr) data_ptr->stop();
    return result;
}

// get the file 
FTPFile FTPClient::open(const char *filename, FileMode mode, bool autoClose) {
    char msg[200];
    snprintf(msg, sizeof(msg), "open: %s", filename);
    FTPLogger::writeLog( LOG_INFO, "FTPClient", msg);
    // abort any open processing
    api.abort();
    // open new data connection
    api.passv();

    api.setCurrentOperation(NOP);

    return FTPFile(&api, filename, mode, autoClose );
}

// Create the requested directory heirarchy--if intermediate directories
// do not exist they will be created.
bool FTPClient::mkdir(const char *filepath) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "mkdir");
    return api.mkdir(filepath);
}

// Delete the file.
bool FTPClient::remove(const char *filepath) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "remove");
    return api.del(filepath);
}

// Removes a directory
bool FTPClient::rmdir(const char *filepath){
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "rmdir");
    return api.rmd(filepath);
}

// lists all file names in the specified directory
FTPFileIterator FTPClient::ls(const char* path, FileMode mode) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "ls");
    FTPFileIterator it(&api, path, mode);
    return it;
}

/**
 * @brief FTPFileIterator
 * The file name iterator can be used to list all available files and directories. We open 
 * a separate session for the ls operation so that we do not need to keep the result in memory
 * and we dont loose the data when we mix it with read and write operations.
 */

FTPFileIterator::FTPFileIterator(FTPBasicAPI *api, const char* dir, FileMode mode){
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator()");
    this->directory_name = dir;
    this->api_ptr = api;
    this->file_mode = mode;
} 
FTPFileIterator &FTPFileIterator::begin() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "begin");
    if (api_ptr!=nullptr && directory_name!=nullptr) {
        stream_ptr = api_ptr->ls(directory_name);
        readLine();
    } else {
        FTPLogger::writeLog( LOG_ERROR, "FTPFileIterator", "api_ptr is null");
        buffer = "";
    }
    return *this;
}
FTPFileIterator& FTPFileIterator::end() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "end");
    static FTPFileIterator end;
    end.buffer = ""; 
    return end;
}
FTPFileIterator &FTPFileIterator::operator++() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "++");
    readLine();
    return *this;
}
FTPFileIterator &FTPFileIterator::operator++(int na) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "++(1)");
    for (int j=0;j<na;j++)
        readLine();
    return *this;
}
FTPFile FTPFileIterator::operator*() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "*");
    // return file that does not autoclose
    return FTPFile(api_ptr, buffer.c_str(), file_mode, false);
}
void FTPFileIterator::readLine() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "readLine");
    buffer = "";
    if (stream_ptr!=nullptr) {
        buffer = stream_ptr->readStringUntil('\n');
        FTPLogger::writeLog( LOG_DEBUG, "line", buffer.c_str());

        // End of ls
        if (api_ptr->currentOperation() == LS_OP && buffer[0]==0){
            const char* ok[] = {"226", nullptr};
            bool is_ok = api_ptr->checkResult(ok, "close-ls", false);
            api_ptr->setCurrentOperation(NOP);
        }

    } else {
        FTPLogger::writeLog( LOG_ERROR, "FTPFileIterator", "stream_ptr is null");
    }
}

} // ns

