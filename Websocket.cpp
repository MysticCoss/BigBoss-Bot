
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



typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef nlohmann::json json;
#define CURL_STATICLIB






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
concurrency::cancellation_token_source cts;              //this and
concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
concurrency::task<void> t;
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
void sleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
json sendMsg(CURL* curl, std::string msg, std::string channelID, bool debug = false) {
    if (debug) std::cout << "=========================================================================\n";
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
    //client::connection_ptr con_ptr= c->get_con_from_hdl(hdl);
    std::cout << "================================Websocket restart================================\n";
    cts.cancel();
    websocketpp::lib::error_code ec;
    c->close(hdl, websocketpp::close::status::going_away, "",ec);
    if (ec) {
        std::cout << "Can not close endpoint because" << ec.message() << std::endl;
    }
    std::cout << "=======================Stop heartbeating=======================\n";
    t.wait();
    //con_ptr->close(1012, "Restart");
    //sleep(5000);
    //c->close(hdl, 1012, "Restart");
    std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";
    client::connection_ptr con = c->get_connection(uri, ec);
    if (ec) {
        std::cout << "could not create connection because: " << ec.message() << std::endl;
        return;
    }
    c->connect(con);
    c->run();
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
    int opcode = js_msg["op"];
    switch (opcode) {
    case 9: //invalid session
        //just restart
        is_websocket_restart = false;
        restart(hdl, c);
    case 10: //Hello packet
        std::cout << "Discord opcode: 10\n";
        heartbeat_interval = js_msg["d"]["heartbeat_interval"];
        std::cout << "Heartbeat interval: " << heartbeat_interval << "\n";
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
        seq_num = js_msg["s"]; //update SEQUENCE NUMBER

        //begin EVENT logic
        
        std::string event = js_msg["t"];

        if (event == R"(READY)") { //receive ready packet 
            ready = js_msg; //cache ready data
        }
        if (event == R"(MESSAGE_CREATE)") { //New command
            std::string channel = js_msg["d"]["channel_id"];
            std::string content = js_msg["d"]["content"];
            //std::cout << "=============================================I regconize CREATE MESSAGE\n";
            is_websocket_restart = true;
            restart(hdl,c);
            std::ofstream file;
            file.open("file.txt");
            file << msg -> get_payload();
            file.close();
            CURL* curl = curl_easy_init();
            if (curl) {}//std::cout << "Curl init done!\n";
            else {
                std::cout << "Curl init failed, program terminated\n";
            }


            if (content == "ping" || content == "Ping") { //check message content
                if (curl) {}//std::cout << "Curl init done!\n";
                sendMsg(curl, "Pong!", channel);
            }
            if (content == "Hello" || content == "hello") {  

                sendMsg(curl, "Hi!", channel);
            }
        }
        break;
    }
       
    websocketpp::lib::error_code ec;

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
    SetConsoleOutputCP(CP_UTF8); //set console output to UTF8
    //check if there's another instant exist

    //=========EXPERIMENTAL===========
    //std::ifstream a;
    ////string zz = "TEMP";
    //char* temp_dir = std::getenv("TEMP");
    //std::string str_temp_dir(temp_dir);
    //str_temp_dir += "\__________________z";
    //a.open(str_temp_dir);
    //while (a.is_open() == true) { //exist
    //    sleep(100); //hold until file not exist
    //    a.open(str_temp_dir); 
    //}
    //Create temp file indicate session
    //=============================


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

        websocketpp::lib::error_code ec;
        client::connection_ptr con = c.get_connection(uri, ec);
        if (ec) {
            std::cout << "could not create connection because: " << ec.message() << std::endl;
            return 0;
        }

        // Note that connect here only requests a connection. No network messages are
        // exchanged until the event loop starts running in the next line.
        c.connect(con);

        // Start the ASIO io_service run loop
        // this will cause a single connection to be made to the server. c.run()
        // will exit when this connection is closed.
        c.run();
    }
    catch (websocketpp::exception const& e) {
        std::cout << e.what() << std::endl;
    }
}
