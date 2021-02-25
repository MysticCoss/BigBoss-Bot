
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"
#include <nlohmann/json.hpp>
#include "curl/curl.h"
#include <iostream>
#include <ppltasks.h>
#include <time.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <algorithm>

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef nlohmann::json json;
//#define CURL_STATICLIB


//class helper {
//private:
//    std::string id = "";
//    std::string channelId = "";
//public: 
//    //constructor
//    helper(json data) {
//        if (data["d"]["mentions"].is_null()) {
//            id = "";
//        }
//        else {
//            id = data["d"]["mentions"][0]["id"];
//        }
//    }
//    //return ID of mention if exist. else return "" <blank>
//    std::string get_mention(json custom = NULL) { 
//        if (custom = NULL) {
//            return id;
//        }
//        else {
//            std::string custom_id = "";
//            if (!custom["d"]["mentions"].is_null()) {
//                custom_id = custom["d"]["mentions"][0]["id"];
//            }
//            return custom_id;
//        }
//    }
//    std::string get_channel(json custom = NULL) {
//        if (custom = NULL) {
//            return channelId;
//        }
//        else {
//            std::string custom_cid = "";
//            if (custom["d"]["channel_id"].is_null()) {
//                custom_cid = "";
//            }
//            else {
//                custom_cid = custom["d"]["channel_id"];
//            }
//            return custom_cid;
//        }
//    }
//};



//{
//    \"op\": 2,
//        \"d\" : {
//        \"token\": \"ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss\",
//            \"intents\" : 648,
//            \"properties\" : {
//            \"$os\": \"window\",
//                \"$browser\" : \"\",
//                \"$device" : \"\"
//        }
//    }
//}

//global var
int heartbeat_interval = 50;
int seq_num = 0;
bool is_websocket_restart = false;
json ready;
//client::connection_ptr con_ptr;
concurrency::cancellation_token_source cts;       
//this and
concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
concurrency::task<void> t;
std::string youtubeAPIKey = "AIzaSyB0_DpMR1gi_iuNrPsKgn1LcAN5t5d8_j4";
///////////

struct Playload {
    Playload() {
        memory = NULL;
        size = 0;
    }
    char* memory;
    size_t size;
};

static size_t write_data(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    struct Playload* mem = (struct Playload*)userp;

    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }

    return escaped.str();
}
//ping to ip, return roundtrip time. if not works return -1
int ping(std::string ip) {
#pragma warning(disable : 4996)
    HANDLE hIcmpFile;
    unsigned long ipaddr = INADDR_NONE;
    DWORD dwRetVal = 0;
    DWORD dwError = 0;
    char SendData[] = "Data Buffer";
    LPVOID ReplyBuffer = NULL;
    DWORD ReplySize = 0;
    ipaddr = inet_addr(ip.c_str());
    if (ipaddr == INADDR_NONE) {
        std::cout << "Ip not valid" << std::endl;
        return -1;
    }
    hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE) {
        std::cout << "Unable to open handle.\n";
        std::cout << "IcmpCreatefile returned error: " << GetLastError() << std::endl;
        return -1;
    }
    // Allocate space for at a single reply
    ReplySize = sizeof(ICMP_ECHO_REPLY) + sizeof(SendData) + 8;
    ReplyBuffer = (VOID*)malloc(ReplySize);
    if (ReplyBuffer == NULL) {
        std::cout << "Unable to allocate memory for reply buffer\n";
        return -1;
    }

    dwRetVal = IcmpSendEcho2(hIcmpFile, NULL, NULL, NULL,
        ipaddr, SendData, sizeof(SendData), NULL,
        ReplyBuffer, ReplySize, 1000);
    if (dwRetVal != 0) {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
        struct in_addr ReplyAddr;
        ReplyAddr.S_un.S_addr = pEchoReply->Address;
        if (dwRetVal > 1) {
            //printf("\tReceived %ld icmp message responses\n", dwRetVal);
            //printf("\tInformation from the first response:\n");
        }
        else {
            //printf("\tReceived %ld icmp message response\n", dwRetVal);
            //printf("\tInformation from this response:\n");
        }
        //printf("\t  Received from %s\n", inet_ntoa(ReplyAddr));
        //printf("\t  Status = %ld  ", pEchoReply->Status);
        switch (pEchoReply->Status) {
        case IP_DEST_HOST_UNREACHABLE:
            printf("(Destination host was unreachable)\n");
            break;
        case IP_DEST_NET_UNREACHABLE:
            printf("(Destination Network was unreachable)\n");
            break;
        case IP_REQ_TIMED_OUT:
            printf("(Request timed out)\n");
            break;
        default:
            printf("\n");
            break;
        }

        /*printf("\t  Roundtrip time = %ld milliseconds\n",
            pEchoReply->RoundTripTime);*/
        return pEchoReply->RoundTripTime;
    }
    else {
        printf("Call to IcmpSendEcho2 failed.\n");
        dwError = GetLastError();
        switch (dwError) {
        case IP_BUF_TOO_SMALL:
            printf("\tReplyBufferSize to small\n");
            break;
        case IP_REQ_TIMED_OUT:
            printf("\tRequest timed out\n");
            break;
        default:
            printf("\tExtended error returned: %ld\n", dwError);
            break;
        }
        return 1;
    }
    return 0;
}


void sleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool isOwner(json data) {
    std::string ownerid = "308556224910327808";
    if (data["d"]["author"]["id"].is_string()) {
        return data["d"]["author"]["id"] == ownerid ? true : false;
    }
    else {
        std::cout << "Can not parse author id" << std::endl;
        return false;
    }
}

bool isStartWith(std::string source, std::string prefix) {
    return source.rfind(prefix, 0) == 0 ? true : false;
}
json youtubePerformQuerry(std::string querry, bool debug = false) {
    CURL* curl = curl_easy_init();
    if (curl) {
        struct curl_slist* list = NULL;

        //SSL option
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
        /* Provide CA Certs from http://curl.haxx.se/docs/caextract.html */
        curl_easy_setopt(curl, CURLOPT_CAINFO, "curl-ca-bundle.crt");

        // url encode example https://youtube.googleapis.com/youtube/v3/search?part=snippet&q=this&key=[YOUR_API_KEY]
        std::string url = "https://www.googleapis.com/youtube/v3/search?part=snippet&q=";
        //std::replace(querry.begin(), querry.end(), " ", "+");
        url += url_encode(querry); //URL encode
        url += "&key=";
        url += youtubeAPIKey;
        if (debug) std::cout << "Querry URL: " << url << std::endl;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        list = curl_slist_append(list, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        struct Playload chunk;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        if (curl_easy_perform(curl) != CURLE_OK) {
            std::cout << "[sendMsg] Perform error!\n";
            return NULL;
        }
        if (debug) {
            std::cout << "[sendMsg] Payload size: " << chunk.size << " bytes" << std::endl;
            std::cout << "[sendMsg] Playload data: \n" << chunk.memory << std::endl;
        }
        std::string stringPlayload(chunk.memory, chunk.size + 1);
        curl_slist_free_all(list); /* free the list again */
        curl_easy_reset(curl);
        curl_easy_cleanup(curl);
        json jsonPlayload = json::parse(stringPlayload);
        return jsonPlayload;
    }
    else {
        std::cout << "[sendMsg] Invalid handle\n";
        return NULL;
    }
}
json sendMsg(std::string msg, std::string channelID, bool debug = false) {
    if (debug) std::cout << "=========================================================================\n";
    CURL* curl = curl_easy_init();
    if (curl) {
        struct curl_slist* list = NULL;
        json postData;
        postData["content"] = msg;
        //postData["nonce"] = "";
        postData["tts"] = "false";
        //string data = "{\"content\":\" + cf 2000\",\"nonce\":\"\",\"tts\":false}";
        std::string data = postData.dump();
        std::string url = "https://discord.com/api/v8/channels/" + channelID + "/messages";
        //url = "https://discord.com/api/v8/channels/" + channelID + "/messages";
        //cout << "Url= " << url << endl;
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
        /* Provide CA Certs from http://curl.haxx.se/docs/caextract.html */
        curl_easy_setopt(curl, CURLOPT_CAINFO, "curl-ca-bundle.crt");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        list = curl_slist_append(list, "Authorization: Bot ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss");
        list = curl_slist_append(list, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        struct Playload chunk;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        if (curl_easy_perform(curl) != CURLE_OK) {
            std::cout << "[sendMsg] Perform error!\n";
            return NULL;
        }
        if (debug) {
            std::cout << "[sendMsg] Payload size: " << chunk.size << " bytes" << std::endl;
            std::cout << "[sendMsg] Playload data: \n" << chunk.memory << std::endl;
        }
        std::string stringPlayload(chunk.memory, chunk.size + 1);
        curl_slist_free_all(list); /* free the list again */
        curl_easy_reset(curl);
        //free(chunk.memory);
        curl_easy_cleanup(curl);
        json jsonPlayload = json::parse(stringPlayload);
        //sleep(delay);     //prevent accidental DDOS which leads to token revoked
        return jsonPlayload;
    }
    else {
        std::cout << "[sendMsg] Invalid handle\n";
        return NULL;
    }
}
//json readyy;
void auth(websocketpp::connection_hdl hdl, client* c) {
    websocketpp::lib::error_code ec;
    std::string auth_str = "{\"op\":2,\"d\":{\"token\":\"ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss\",\"intents\":648,\"properties\":{\"$os\":\"window\",\"$browser\":\"\",\"$device\":\"\"}}}";
    std::cout << "Authorizing...\n";
    c->send(hdl,auth_str,websocketpp::frame::opcode::text,ec);
    if (ec) {
        std::cout << "Authorization failed because: " << ec.message() << std::endl;
    }
    return;
}
void resume(websocketpp::connection_hdl hdl, client* c) {
    websocketpp::lib::error_code ec;
    json resume;
    resume["op"] = 6;
    resume["d"]["token"] = "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss";
    resume["d"]["session_id"] = ready["d"]["session_id"];
    resume["d"]["seq"] = seq_num;
    /*std::string a = R"({
                "op": 6,
                "d": {
                    "token": "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss",
                    "session_id": "session_id_i_stored",
                    "seq": 1337
                }
            })";*/
    std::cout << "Resuming...\n";
    c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
    if (ec) {
        std::cout << "Resume failed because: " << ec.message() << std::endl;
        return;
    }
    is_websocket_restart = false; //reset flag
    return;
}
std::string getHeartBeatPayload() {
    if (seq_num == 0) {
        std::string result = R"({"op":1,"d":null})";
        return result;
    }
    else {
        std::string result = R"({"op":1,"d":)";
        result += std::to_string(seq_num);
        result += R"(})";
        return result;
    }
}

//************
concurrency::task<void> heartBeat(websocketpp::connection_hdl hdl, client* c, int heartbeat_interval, concurrency::cancellation_token token) {
    if (is_websocket_restart == true) {
        //this is restart session so don't auth
        resume(hdl, c);
    }
    else {
        //fresh session, need auth
        auth(hdl, c);
    }
    //std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval));
    //std::shared_ptr<websocketpp::lib::error_code> shared_ec = std::make_shared<websocketpp::lib::error_code>(ec);
    std::shared_ptr<websocketpp::connection_hdl> shared_hdl = std::make_shared<websocketpp::connection_hdl>(hdl);   //share message handle
    std::shared_ptr<int> shared_heartbeat = std::make_shared<int>(heartbeat_interval);                              //share interval 
    std::shared_ptr<int> shared_seq_num = std::make_shared<int>(seq_num);
    std::shared_ptr<client*> shared_client_context = std::make_shared<client*>(c); 
    return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, token]
        {
            //check is task is canceled
            if (token.is_canceled()) { 
                concurrency::cancel_current_task();
            }
            else {
                while (*shared_heartbeat == 50) {
                    //server not provide heartbeat interval wait a bit
                    std::this_thread::sleep_for(std::chrono::milliseconds(*shared_heartbeat));
                    std::cout << "Wait..." << std::endl;
                }

                std::string payload = "{\"op\":1,\"d\":null}";

                //convert shared shared client pointer to local client 
                auto client = *shared_client_context;
                websocketpp::lib::error_code ec;
                std::cout << "Heartbeat sent. " << ec.message() << std::endl;
                client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                if (!ec) {
                    while (!ec) {
                        int localhb = *shared_heartbeat;
                        while (localhb) {
                            if (token.is_canceled()) {
                                std::cout << "Stop heartbeating...\n";
                                concurrency::cancel_current_task();
                            }
                            sleep(50);
                            localhb -= 50;
                        }
                        //sleep(*shared_heartbeat);
                        std::cout << "Heartbeat sent. " << ec.message() << std::endl;
                        client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                    }

                }
                else {
                    std::cout << "Heartbeat failed because: " << ec.message() << std::endl;
                }
            }
        }, token);
}


//restart connection
void restart(websocketpp::connection_hdl hdl, client* c) {
    
    std::cout << "================================Websocket restart================================\n";
    cts.cancel();
    t.wait();

    //reset token
    cts = concurrency::cancellation_token_source();
    token = cts.get_token();
    //

    websocketpp::lib::error_code ec;
        //c->close(hdl, websocketpp::close::status::going_away, "",ec);
    if (ec) {
        std::cout << "Can not close endpoint because" << ec.message() << std::endl;
    }
    //std::cout << "=======================Stop heartbeating=======================\n";
    client::connection_ptr con_ptr = c->get_con_from_hdl(hdl);
    con_ptr->close(websocketpp::close::status::service_restart, "");
    //con_ptr->close(1012, "Restart");
    //sleep(5000);
    //c->close(hdl, 1012, "Restart");
    /*std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";
    client::connection_ptr con = c->get_connection(uri, ec);
    if (ec) {
        std::cout << "could not create connection because: " << ec.message() << std::endl;
        return;
    }
    c->connect(con);
    c->run();*/
}




// pull out the type of messages sent by our config
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

// This message handler will be invoked once for each incoming message. It
// prints the message and then sends a copy of the message back to the server.
void on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
    std::cout << "on_message called with hdl: " << hdl.lock().get()
        << " and message: ";

    std::cout << (msg->get_payload())
        << std::endl;
    std::cout << "Now begin parsing data"
        << std::endl;
    std::string str_msg = msg->get_payload();
    json js_msg;
    js_msg = json::parse(str_msg);
    int opcode = -1;
    if (js_msg["op"].is_number_integer()) {
        opcode = js_msg["op"];
    }
    
    switch (opcode) {
    case -1:
        std::cout << "Can not parse discord OP code, skip this message. \n";
        break;
    case 9: //invalid session
        //just restart
        is_websocket_restart = false;
        restart(hdl, c);
        break;
    case 10: //Hello packet
        std::cout << "Discord opcode: 10\n";
        if (js_msg["d"]["heartbeat_interval"].is_number_integer()) {
            heartbeat_interval = js_msg["d"]["heartbeat_interval"];
            std::cout << "Heartbeat interval: " << heartbeat_interval << "\n";
        }
        else {
            std::cout << "Can't parse heartbeart interval from messages:" << msg->get_payload() << std::endl;
            std::cout << "Using default heartbeat interval: " << heartbeat_interval << "\n";
        }
        //cts = concurrency::cancellation_token_source a;
        t = heartBeat(hdl, c, heartbeat_interval, token);
        break;
    case 11: //Heartbeat ACK
        std::cout << "Discord opcode: 11\n";
        std::cout << "Heartbeat ACK" << std::endl;
        break;
    case 7: //Request reconnect
        is_websocket_restart = true;
        restart(hdl, c);
        break;
    case 0:
        std::cout << "Discord opcode: 0\n";
        std::cout << "Discord event name:" << js_msg["t"] << "\n";

        //update SEQUENCE NUMBER
        if (js_msg["s"].is_number_integer()) {
            seq_num = js_msg["s"];
        }
        else {
            std::cout << "Can not parse sequense number from message:" << msg->get_payload() << std::endl;
        }
         
        
        //begin EVENT logic
         
        std::string event;
        if (js_msg["t"].is_string()) {
            event = js_msg["t"];
        }
        else {
            std::cout << "Can not parse discord event from message:" << msg->get_payload() << std::endl;
        }
        
        if (event == R"(READY)") { //receive ready packet 
            ready = js_msg; //cache ready data
        }
        if (event == R"(MESSAGE_CREATE)") { //New command

            //int a = js_msg["d"]["channel_id"];
            std::string channel = "";
            if (js_msg["d"]["channel_id"].is_string()) {
                channel = js_msg["d"]["channel_id"];
            }
            else {
                std::cout << "Can not parse channel id from message:" << msg->get_payload() << std::endl;
                //channel = "";
            }

            std::string content = "";
            if (js_msg["d"]["content"].is_string()) {
                content = js_msg["d"]["content"];
            }
            else {
                std::cout << "Can not parse content from message:" << msg->get_payload() << std::endl;
                //content = "";
            }

            if (js_msg["d"]["mentions"][0]["id"] == ready["d"]["user"]["id"]) {
                sendMsg("Why mention me?", channel);
                break;
            }
            if (content == "ping" || content == "Ping") { 
                int RTT = ping("162.159.136.232"); //discord IP: 162.159.136.232
                std::cout << "RTT: " << std::to_string(RTT) << std::endl;
                std::string msg = "Pong! ";
                msg += std::to_string(RTT);
                msg += "ms";
                sendMsg(msg, channel);
            }
            if (content == "Hello" || content == "hello") {  
                sendMsg("Hi!", channel);
            }
            if (content == "restart" || content == "Restart") {
                if (isOwner(js_msg)) {
                    sendMsg("Restarting...", channel);
                    is_websocket_restart = true;
                    restart(hdl, c);
                }
                else {
                    sendMsg("\x43\xc3\xb3\x20\x63\xc3\xa1\x69\x20\x63\x6f\x6e\x20\x63\xe1\xba\xb7\x63", channel);
                }
            }
            if (isStartWith(content, "?p ")) {
                //parse string
                std::string querry = content.erase(0, 3);
                if (querry == "") {
                    sendMsg("Please provide paramenter", channel);
                }
                else {
                    json result = youtubePerformQuerry(querry,true);
                    std::cout << "Querry result for keyword: " << querry << std::endl;
                    std::cout << result.dump(4) << std::endl;
                    sendMsg(result.dump(4), channel);
                }
            }
        }
        break;
    }
       
    websocketpp::lib::error_code ec;

}

void on_close(client* c, websocketpp::connection_hdl hdl) {
    c->get_alog().write(websocketpp::log::alevel::app, "Connection Closed");
    std::cout << "Connection closed on hdl: " << hdl.lock().get() << std::endl;
    std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";
    websocketpp::lib::error_code ec;
    client::connection_ptr con_ptr = c->get_connection(uri, ec);
    if (ec) {
        std::cout << "Could not create connection because: " << ec.message() << std::endl;
        return;
    }

    // Note that connect here only requests a connection. No network messages are
    // exchanged until the event loop starts running in the next line.
    c->connect(con_ptr);
}

static context_ptr on_tls_init() {
    // establishes a SSL connection
    context_ptr ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

    try {
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 |
            boost::asio::ssl::context::single_dh_use);
        
    }
    catch (std::exception& e) {
        std::cout << "Error in context pointer: " << e.what() << std::endl;
    }
    return ctx;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8); 

    //Create client endpoint
    client c;

    std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";


    try {
        // Set logging to be pretty verbose (everything except message payloads)
        c.set_access_channels(websocketpp::log::alevel::all);
        c.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize ASIO
        c.init_asio();
        c.set_tls_init_handler(bind(&on_tls_init));

        // Register our message handler
        c.set_message_handler(bind(&on_message, &c, ::_1, ::_2));
        c.set_close_handler(bind(&on_close, &c, ::_1));
       // while (1) {
            websocketpp::lib::error_code ec;
            client::connection_ptr con_ptr = c.get_connection(uri, ec);
            if (ec) {
                std::cout << "Could not create connection because: " << ec.message() << std::endl;
                return 0;
            }

            // Note that connect here only requests a connection. No network messages are
            // exchanged until the event loop starts running in the next line.
            c.connect(con_ptr);
            c.start_perpetual();
            // Start the ASIO io_service run loop
            // this will cause a single connection to be made to the server. c.run()
            // will exit when this connection is closed.

            c.run();
            //c.reset();
            
    }
    catch (websocketpp::exception const& e) {
        std::cout << e.what() << std::endl;
    }
}





/* =========================================READY EXAMPLE========================================================
* {
  "t": "READY",
  "s": 1,
  "op": 0,
  "d": {
    "v": 8,
    "user_settings": {},
    "user": {
      "verified": true,
      "username": "BigBoss",
      "mfa_enabled": false,
      "id": "808645331747078174",
      "flags": 0,
      "email": null,
      "discriminator": "8831",
      "bot": true,
      "avatar": "c3ead24bbc3c22b0f586af9069315971"
    },
    "session_id": "080c188e0a3b1bbc4cbee3c4bb6b66ae",
    "relationships": [],
    "private_channels": [],
    "presences": [],
    "guilds": [
      {
        "unavailable": true,
        "id": "633218964771569676"
      }
    ],
    "guild_join_requests": [],
    "geo_ordered_rtc_regions": [ "hongkong", "singapore", "india", "japan", "russia" ],
    "application": {
      "id": "808645331747078174",
      "flags": 0
    },
    "_trace": [ "[\"gateway-prd-main-n01x\",{\"micros\":87499,\"calls\":[\"discord-sessions-prd-2-74\",{\"micros\":77772,\"calls\":[\"start_session\",{\"micros\":63940,\"calls\":[\"api-prd-main-7wbz\",{\"micros\":58609,\"calls\":[\"get_user\",{\"micros\":4795},\"add_authorized_ip\",{\"micros\":4108},\"get_guilds\",{\"micros\":14816},\"coros_wait\",{\"micros\":0}]}]},\"guilds_connect\",{\"micros\":1,\"calls\":[]},\"presence_connect\",{\"micros\":11249,\"calls\":[]}]}]}]" ]
  }
}
*/


/* =========================================MESSAGE_CREATE EXAMPLE===============================================
{
  "t": "MESSAGE_CREATE",
  "s": 16,
  "op": 0,
  "d": {
    "type": 0,
    "tts": false,
    "timestamp": "2021-02-24T02:22:44.630000+00:00",
    "referenced_message": null,
    "pinned": false,
    "nonce": "813959060171784192",
    "mentions": [
      {
        "username": "BigBoss",
        "public_flags": 0,
        "member": {
          "roles": [ "813675422117396511" ],
          "premium_since": null,
          "pending": false,
          "nick": null,
          "mute": false,
          "joined_at": "2021-02-23T07:35:38.025711+00:00",
          "is_pending": false,
          "hoisted_role": null,
          "deaf": false
        },
        "id": "808645331747078174",
        "discriminator": "8831",
        "bot": true,
        "avatar": "c3ead24bbc3c22b0f586af9069315971"
      }
    ],
    "mention_roles": [],
    "mention_everyone": false,
    "member": {
      "roles": [],
      "premium_since": null,
      "pending": false,
      "nick": null,
      "mute": false,
      "joined_at": "2021-01-30T15:11:59.144000+00:00",
      "is_pending": false,
      "hoisted_role": null,
      "deaf": false
    },
    "id": "813959068800516108",
    "flags": 0,
    "embeds": [],
    "edited_timestamp": null,
    "content": "<@!808645331747078174>",
    "channel_id": "766182417865244693",
    "author": {
      "username": "MysticCoss",
      "public_flags": 0,
      "id": "308556224910327808",
      "discriminator": "6886",
      "avatar": "a_3ea9bcf327d367fbadd6ace10b6f76b0"
    },
    "attachments": [],
    "guild_id": "633218964771569676"
  }
}*/

