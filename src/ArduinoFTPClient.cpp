#include "ArduinoFTPClient.h"
#include <stdio.h>

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
    // set passive mode
    if (!passv()) return false;
    
    // set binary mode
    if (!binary()) return false;
    is_open = true;;
    return true;
}

bool FTPBasicAPI::quit() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "quit");      
    const char* ok_result[] = {"221","226", nullptr};
    return cmd("QUIT", nullptr, ok_result, false);    
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
    if (current_operation!=NOP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","abort");      
        const char* ok[] = {"225", "226",nullptr};
        current_operation = NOP;
        return cmd("ABOR", nullptr, ok);
    }
    return true;
}

bool FTPBasicAPI::binary() {
    return cmd("BIN", nullptr, "200");
}

bool FTPBasicAPI::ascii() {
    return cmd("ASC", nullptr, "200");
}


Stream *FTPBasicAPI::read(const char* file_name ) {
    if (current_operation!=READ_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "read");      
        abort();
        const char* ok[] = {"150","125", nullptr};
        cmd("RETR", file_name, ok);
        current_operation = READ_OP;
    }
    checkClosed(data_ptr);
    return data_ptr;
}

Stream *FTPBasicAPI::write(const char* file_name, FileMode mode) {
    if (current_operation!=WRITE_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "write");      
        abort();
        const char* ok_write[] = {"125", "150", nullptr};
        cmd(mode == WRITE_APPEND_MODE ? "APPE": "STOR", file_name, ok_write);
        current_operation = WRITE_OP;
    }
    checkClosed(data_ptr);
    return data_ptr;
}

Stream* FTPBasicAPI::ls(const char* file_name){
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI", "ls");      
    abort();
    const char* ok[] = {"125", "150", nullptr};
    cmd("NLST",file_name, ok);
    current_operation = LS_OP;
    return data_ptr;
}

void FTPBasicAPI::closeData() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","closeData");      
    data_ptr->stop();
    setCurrentOperation(NOP);
}

void FTPBasicAPI::setCurrentOperation(CurrentOperation op){
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
    ok = client_ptr->connect(adr, port);
    if (ok && doCheckResult){
        const char* ok_result[] = {"220","200",nullptr};
        ok = checkResult(ok_result, "connect");
    }
    if (!ok){
        FTPLogger::writeLog( LOG_ERROR, "FTPBasicAPI::connect", buffer);      
    }
    return ok;
}

bool FTPBasicAPI::cmd(const char* command, const char* par, const char* expected, bool wait_for_data) {
    const char* expected_array[] = { expected, nullptr };
    return cmd(command, par, expected_array, wait_for_data);
}

bool FTPBasicAPI::cmd(const char* command_str, const char* par, const char* expected[], bool wait_for_data) {
    char command_buffer[512];
    Stream *stream_ptr = command_ptr;
    if (par==nullptr){
        strcpy(command_buffer, command_str);
        stream_ptr->println(command_buffer);
    } else {
        sprintf(command_buffer,"%s %s", command_str, par);
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

    char result_str[80];
    if (wait_for_data || stream_ptr->available()) {
        // wait for reply
        while(stream_ptr->available()==0){
            delay(100);
        }
        // read reply
        //result_str = stream_ptr->readStringUntil('\n').c_str();
        CStringFunctions::readln(*stream_ptr, result_str, 80);

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

void FTPBasicAPI::checkClosed(Client *client){
    if (!client->connected()){
        FTPLogger::writeLog( LOG_DEBUG, "FTPBasicAPI","checkClosed -> client is closed"); 
        // mark the actual command as completed     
        current_operation = NOP;
    }
} 


/**
 * @brief FTPFile
 * A single file which supports read and write operations. This class is implemented
 * as an Arduino Stream
 * 
 */

FTPFile::FTPFile(FTPBasicAPI *api_ptr, const char* name, FileMode mode){
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile");
    if (name!=nullptr)
        this->file_name = strdup(name);
    this->mode = mode;
    this->api_ptr = api_ptr;
}

FTPFile::FTPFile(FTPFile &cpy){
    file_name = strdup(cpy.file_name);
    eol = cpy.eol;
    mode = cpy.mode;
    api_ptr = cpy.api_ptr;
    object_type = cpy.object_type;
}

FTPFile::FTPFile(FTPFile &&move){
    file_name = move.file_name;
    eol = move.eol;
    mode = move.mode;
    api_ptr = move.api_ptr;
    object_type = move.object_type;
    // clear source
    move.file_name = nullptr;
    api_ptr = nullptr;
}

FTPFile& FTPFile::operator=(const FTPFile &cpy) {
    file_name = strdup(cpy.file_name);
    eol = cpy.eol;
    mode = cpy.mode;
    api_ptr = cpy.api_ptr;
    object_type = cpy.object_type;
    return *this;
}

FTPFile::~FTPFile(){
    if (this->file_name!=nullptr)
        free((void*)this->file_name);
}

size_t FTPFile::write(uint8_t data) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "write");
    if (mode==READ_MODE) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "Can not write - File has been opened in READ_MODE");
        return 0;
    }
    Stream *result_ptr = api_ptr->write(file_name, mode );
    return result_ptr->write(data);
}

size_t FTPFile::write(char* data, int len) {
    int count = 0;
    for (int j=0;j<len;j++){
        count += write(data[j]);
    }
    return count;
}

int FTPFile::read() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "read");
    Stream *result_ptr =  api_ptr->read(file_name);
    return result_ptr->read();
}
int FTPFile::read(void *buf, size_t nbyte) {
    FTPLogger::writeLog( LOG_INFO, "FTPFile", "read-n");
    memset(buf,0, nbyte);
    Stream *result_ptr =  api_ptr->read(file_name);
    return result_ptr->readBytes((char*)buf, nbyte);
}
int FTPFile::readln(void *buf, size_t nbyte) {
    FTPLogger::writeLog( LOG_INFO, "FTPFile", "readln");
    memset(buf,0, nbyte);
    Stream *result_ptr =  api_ptr->read(file_name);
    return result_ptr->readBytesUntil(eol[0], (char*)buf, nbyte );
}
int FTPFile::peek() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "peek");
    Stream *result_ptr =  api_ptr->read(file_name);
    return result_ptr->peek();
}
int FTPFile::available(){
    char msg[80];
    Stream *result_ptr =  api_ptr->read(file_name);
    int len = result_ptr->available();
    sprintf(msg,"available: %d", len);
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", msg);
    return len;
}

size_t FTPFile::size(){
    char msg[80];
    size_t size =  api_ptr->size(file_name);
    sprintf(msg,"size: %d", size);
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", msg);
    return size;
}

bool FTPFile::isDirectory(){
    FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "isDirectory");
    return  api_ptr->objectType(file_name)==TypeDirectory;
}

void FTPFile::flush() {
    if (api_ptr->currentOperation()==WRITE_OP) {
        FTPLogger::writeLog( LOG_DEBUG, "FTPFile", "flush");
        Stream *result_ptr =  api_ptr->write(file_name, mode);
        result_ptr->flush();
    }
}

void FTPFile::close() {
    FTPLogger::writeLog( LOG_INFO, "FTPFile", "close");
    const char* ok[] = {"226", nullptr};
    api_ptr->checkResult(ok, "close", false);
    if (api_ptr->currentOperation()==WRITE_OP) {
        flush();
    }
    api_ptr->closeData();
}

const char * FTPFile::name() {
    return file_name;
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

#if defined(FTP_DEFAULT_CLIENT)
FTPClient(int port = COMMAND_PORT, int data_port = DATA_PORT ){
    FTPLogger::writeLog( LOG_INFO, "FTPClient()");
    init(command_ptr, data_ptr, &command, &data, port, data_port);
}
#endif



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

//call this when a card is removed. It will allow you to inster and initialise a new card.
bool FTPClient::end() {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "end");
    return api.quit();
}

// get the file 
FTPFile& FTPClient::open(const char *filename, FileMode mode) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "open");
    return *(new FTPFile(&api, filename, mode ));
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
FTPFileIterator::FTPFileIterator(FTPFileIterator &copy) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator()-copy");
    this->api_ptr = copy.api_ptr;
    this->stream_ptr = copy.stream_ptr;
    this->directory_name = copy.directory_name;  
    this->file_mode = copy.file_mode;
}    
FTPFileIterator::FTPFileIterator(FTPFileIterator &&copy) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator()-move");
    this->api_ptr = copy.api_ptr;
    this->stream_ptr = copy.stream_ptr;
    this->directory_name = copy.directory_name;  
    this->file_mode = copy.file_mode;
    copy.api_ptr = nullptr;
    copy.stream_ptr = nullptr;
    copy.directory_name = nullptr;
}    
FTPFileIterator::~FTPFileIterator() {
    FTPLogger::writeLog( LOG_DEBUG, "~FTPFileIterator()");
}
FTPFileIterator &FTPFileIterator::begin() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "begin");
    if (api_ptr!=nullptr && directory_name!=nullptr) {
        stream_ptr = api_ptr->ls(directory_name);
        readLine();
    } else {
        FTPLogger::writeLog( LOG_ERROR, "FTPFileIterator", "api_ptr is null");
    }
    return *this;
}
FTPFileIterator FTPFileIterator::end() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "end");
    FTPFileIterator end = *this;
    end.buffer[0]=0;
    return end;
}
FTPFileIterator &FTPFileIterator::operator++() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "++");
    readLine();
    return *this;
}
FTPFileIterator &FTPFileIterator::operator++(int na) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "++(1)");
    readLine();
    return *this;
}
FTPFile FTPFileIterator::operator*() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "*");
    //return buffer;
    return FTPFile(api_ptr,buffer,file_mode);
}
void FTPFileIterator::readLine() {
    FTPLogger::writeLog( LOG_DEBUG, "FTPFileIterator", "readLine");
    if (stream_ptr!=nullptr) {
        String str = stream_ptr->readStringUntil('\n');
        strcpy(buffer, str.c_str());
        if (strlen(buffer)){
            api_ptr->setCurrentOperation(NOP);
        }
        if (strcmp(buffer, directory_name)==0){
            // when ls is called on a file it returns the file itself
            // which we just ignore
            strcpy(buffer, "");
        }
    } else {
        FTPLogger::writeLog( LOG_ERROR, "FTPFileIterator", "stream_ptr is null");
        strcpy(buffer, "");
    }
}



