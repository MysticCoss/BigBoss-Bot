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
#include <regex>
#include <boost/process.hpp>
#include "udp.hpp"
#include "ConsoleLogger.h"
#include <sodium.h>
//#include "AudioSource.hpp"
#include "FFmpegAudioSource.hpp"
//#include "FileAudioSource.hpp"
#include <opus/opus.h>
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef nlohmann::json json;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

struct querryqueue {
    std::string userid = "";
    std::string guildid = "";
    std::string channelid = "";
    std::string data[5];
    bool set = false;
    querryqueue() {
        for (int i = 0; i < 5; i++) {
            data[i] = "";
        }
    }
    bool push(json js_msg, json querrydata) {
        if (js_msg["d"]["author"]["id"].is_null() || js_msg["d"]["guild_id"].is_null() || js_msg["d"]["channel_id"].is_null()) {
            std::cout << "Can not queue querry data: 1\n";
            set = false;
            return false;
        }
        else if (querrydata["items"][0]["id"]["videoId"].is_null() || querrydata["items"][1]["id"]["videoId"].is_null() || querrydata["items"][2]["id"]["videoId"].is_null() || querrydata["items"][3]["id"]["videoId"].is_null() || querrydata["items"][4]["id"]["videoId"].is_null()) {
            std::cout << "Can not queue querry data: 2\n";
            set = false;
            return false;
        }
        else {
            std::cout << "Queued search data\n";
            userid = js_msg["d"]["author"]["id"];
            guildid = js_msg["d"]["guild_id"];
            channelid = js_msg["d"]["channel_id"];
            for (int i = 0; i < 5; i++) {
                data[i] = querrydata["items"][i]["id"]["videoId"];
            }
            set = true;
            return true;
        }
    }
    bool is_avaiable() {
        return set;
    }
    void reset() {
        for (int i = 0; i < 5; i++) {
            data[i] = "";
        }
        set = false;
    }
};




class discordbot {
public:
    class utils {
    public:
        struct payload {
            payload() {
                memory = NULL;
                size = 0;
            }
            char* memory;
            size_t size;
        };
        /*
        if (utils::isStartWith(content, "?p")) {
                            //parse string
                            std::string querry = content.erase(0, 3);
                            if (querry == "") {
                                utils::sendMsg("Please provide paramenter", channel);
                            }
                            else {
                                //sendMsg(querry, channel);
                                json result = utils::youtubePerformQuerry(querry, true);
                                std::cout << "Querry result for keyword: " << querry << std::endl;
                                std::cout << result.dump() << std::endl;
                                utils::youtubePrintSearchResult(result, querry, channel, true);
                                if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                    //insert new key
                                    queuemap[userid] = new querryqueue;
                                    queuemap[userid]->push(js_msg, result);
                                }
                                else {
                                    queuemap[userid]->push(js_msg, result); //already has pair, perform querryqueue.push()
                                }
                                std::cout << "Cached search querry\n";
                            }
                            break;
                        }*/
        static bool parse(std::string* output, std::string param, std::string msg) {
            if (isStartWith(msg, param)) {
                std::string result = msg.erase(0, param.length()+1); //delete param from content, including space
                if (result == "") { //validate result
                    return false;
                }
                else {
                    *output = result;
                    return true;
                }
            }
            else return false;
        }

        static size_t write_data(void* contents, size_t size, size_t nmemb, void* userp)
        {
            size_t realsize = size * nmemb;
            struct payload* mem = (struct payload*)userp;

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

        static std::string utf8_url_encode(const std::string& value) {
            std::ostringstream out;
            for (int i = 0; i < value.length(); ++i) {
                out << '%' << std::hex << std::uppercase << (int)(unsigned char)value[i];
            }
            return out.str();
        }

        static std::string utf8_url_decode(const std::string& value) {
            std::ostringstream out;
            for (int i = 0; i < value.length(); i++) {
                if (value[i] == '%') {
                    std::ostringstream hex;
                    hex << value[i + 1] << value[i + 2];
                    //std::cout << hex.str() << std::endl;
                    out << (unsigned char)stoi(hex.str(), 0, 16);
                    i += 2;
                }
                else {
                    out << value[i];
                }
            }
            return out.str();
        }

        static int ping(std::string ip) {
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

        static void sleep(int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        static bool isStartWith(std::string source, std::string prefix) {
            return source.rfind(prefix, 0) == 0 ? true : false;
        }
        static json youtubePerformQuerry(std::string querry, bool debug = false) {
            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* list = NULL;

                //for (int i = 0; i < zzzz.length(); ++i) {
                //    out2 << '%' << std::hex << std::uppercase << (int)(unsigned char)zzzz[i];
                //}
                //cout << out.str() << endl;
                //cout << out2.str() << endl;
                //SSL option
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
                /* Provide CA Certs from http://curl.haxx.se/docs/caextract.html */
                curl_easy_setopt(curl, CURLOPT_CAINFO, "curl-ca-bundle.crt");

                // url encode example https://youtube.googleapis.com/youtube/v3/search?part=snippet&q=this&key=[YOUR_API_KEY]
                std::string url = "https://www.googleapis.com/youtube/v3/search?part=snippet&maxResults=5&q=";
                //std::replace(querry.begin(), querry.end(), " ", "+");
                url += utf8_url_encode(querry); //URL encode
                url += "&key=";
                url += "AIzaSyB0_DpMR1gi_iuNrPsKgn1LcAN5t5d8_j4";
                url += "&type=video";
                if (debug) std::cout << "Querry URL: " << url << std::endl;
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                list = curl_slist_append(list, "Accept: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
                struct payload chunk;
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
                if (curl_easy_perform(curl) != CURLE_OK) {
                    std::cout << "[sendMsg] Perform error!\n";
                    return NULL;
                }
                if (debug) {
                    std::cout << "[sendMsg] Payload size: " << chunk.size << " bytes" << std::endl;
                    std::cout << "[sendMsg] payload data: \n" << chunk.memory << std::endl;
                }
                std::string stringpayload(chunk.memory, chunk.size + 1);
                curl_slist_free_all(list); /* free the list again */
                curl_easy_reset(curl);
                curl_easy_cleanup(curl);
                json jsonpayload = json::parse(stringpayload);
                return jsonpayload;
            }
            else {
                std::cout << "[sendMsg] Invalid handle\n";
                return NULL;
            }
        }

        static void restart(websocketpp::connection_hdl hdl, client* c, concurrency::cancellation_token_source *cts, concurrency::cancellation_token *token, concurrency::task<void> *t) {
            std::cout << "================================Websocket restart================================\n";
            cts->cancel();
            t->wait();
            //reset token
            client::connection_ptr con_ptr = c->get_con_from_hdl(hdl);
            con_ptr->close(websocketpp::close::status::service_restart, "");
            *cts = concurrency::cancellation_token_source();
            *token = cts->get_token();
            std::cout << "Cancel token reset\n";
            //

            websocketpp::lib::error_code ec;
            //c->close(hdl, websocketpp::close::status::going_away, "",ec);
            if (ec) {
                std::cout << "Can not close endpoint because" << ec.message() << std::endl;
            }
            //std::cout << "=======================Stop heartbeating=======================\n";
            
        }

        static json sendMsg(std::string msg, std::string channelID, bool debug = false) {
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
                struct payload chunk;
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
                if (curl_easy_perform(curl) != CURLE_OK) {
                    std::cout << "[sendMsg] Perform error!\n";
                    return NULL;
                }
                if (debug) {
                    std::cout << "[sendMsg] Payload size: " << chunk.size << " bytes" << std::endl;
                    std::cout << "[sendMsg] payload data: \n" << chunk.memory << std::endl;
                }
                std::string stringpayload(chunk.memory, chunk.size + 1);
                curl_slist_free_all(list); /* free the list again */
                curl_easy_reset(curl);
                //free(chunk.memory);
                curl_easy_cleanup(curl);
                json jsonpayload = json::parse(stringpayload);
                //sleep(delay);     //prevent accidental DDOS which leads to token revoked
                return jsonpayload;
            }
            else {
                std::cout << "[sendMsg] Invalid handle\n";
                return NULL;
            }
        }
        static void youtubePrintSearchResult(json result, std::string querry, std::string channel, bool debug = false) {
            std::string printString = "Show result for keyword: ";
            printString += "\"";
            printString += querry += "\"\n";
            //printString += "1: " += result["item"][i]["snippet"]["title"];
            for (int i = 0; ((!result["items"][i].is_null()) && result["items"][i]["snippet"]["title"].is_string()); i++) {
                //result["item"].co
                printString += std::to_string(i + 1);
                printString += ": ";
                std::string temp = result["items"][i]["snippet"]["title"];
                temp = std::regex_replace(temp, std::regex("&quot;"), R"(")");
                printString += temp;
                printString += "\n";
            }
            if (debug) std::cout << printString << std::endl;
            sendMsg(printString, channel);
            sendMsg("Note: Voice function is still in development", channel);
        }

        static bool isOwner(json data) {
            std::string ownerid = "308556224910327808";
            if (data["d"]["author"]["id"].is_string()) {
                return data["d"]["author"]["id"] == ownerid ? true : false;
            }
            else {
                std::cout << "Can not parse author id" << std::endl;
                return false;
            }
        }

        static bool isSelf(json data, json ready) {
            if (data["d"]["user_id"] == ready["d"]["user"]["id"]) return true;
            else return false;
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
    };
    class voiceclient {
    public:
        //Variable zone
        std::vector<unsigned char> key;
        udp::udpclient udpclient;
        std::string user_id = "";
        std::string _token = "";
        std::string guildid = "";
        std::string endpoint = "";
        std::string session = "";
        unsigned short FrameInterval = 56;
        //int a[6];
        int ssrc = 0;
        int heartbeat_interval = 0;
        int seq_num = 0;
        bool is_websocket_restart = false;
        bool state = false;
        json ready;
        client c;
        websocketpp::connection_hdl hdl;
        //client::connection_ptr con_ptr;
        concurrency::cancellation_token_source cts;
        //this and
        concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
        concurrency::task<void> t;
        //

        void setFrameInterval (std::string interval){
            FrameInterval = (unsigned short)stoi(interval);
            std::cout << "Changed frame interval: " << FrameInterval;
            return;
        }

        void selectProtocol(websocketpp::connection_hdl hdl, client* c, std::string address, int port) {
            std::string payload = R"({ "op": 1,"d": {"protocol": "udp","data": {"address": ")";
            payload += address += R"(","port": )";
            payload += std::to_string(port);
            payload += R"(, "mode": "xsalsa20_poly1305"}}})";
            std::cout << "Select protocol sent with payload: " << payload << std::endl;
            websocketpp::lib::error_code ec;
            c->send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Select protocol failed because: " << ec.message() << std::endl;
            }
        }

        bool isReady() {
            return state;
        }

        void auth(websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            /* AUTH EXAMPLE
            {
                "op": 0,
                "d": {
                "server_id": "41771983423143937",
                "user_id": "104694319306248192",
                "session_id": "my_session_id",
                "token": "my_token"
                }
            }
            */
            std::string auth_str = R"({"op": 0,"d": {"server_id": ")";
            auth_str += guildid;
            auth_str += R"(","user_id": ")";
            auth_str += user_id;
            auth_str += R"(","session_id": ")";
            auth_str += session;
            auth_str += R"(","token": ")";
            auth_str += _token;
            auth_str += R"("}})";
            std::cout << "Authorizing...\n";
            c->send(hdl, auth_str, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Authorization failed because: " << ec.message() << std::endl;
            }
            return;
        }

        static void resume(discordbot::voiceclient* a, websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            json resume;
            resume["op"] = 6;
            resume["d"]["token"] = "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss";
            resume["d"]["session_id"] = (a->ready)["d"]["session_id"];
            resume["d"]["seq"] = a->seq_num;
            /*std::string a = R"({
                        "op": 6,
                        "d": {
                            "token": "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss",
                            "session_id": "session_id_i_stored",
                            "seq": 1337
                        }
                    })";*/
            std::cout << "Resuming...\n";
            std::cout << "Resume payload:" << resume.dump() << std::endl;
            c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Resume failed because: " << ec.message() << std::endl;
                return;
            }
            a->is_websocket_restart = false; //reset flag
            return;
        }

        void speak() {
            std::string payload = R"({"op":5,"d":{"speaking":1,"delay":0,"ssrc":)";
            //payload += "1";
            payload += std::to_string(ssrc);
            payload += R"(}})";
            std::cout << "Speak packet with payload: " << payload << std::endl;
            websocketpp::lib::error_code ec;
            c.send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Can not send speak message";
            }
            utils::sleep(1000);
            return;
        }

        void unspeak() {
            std::string payload = R"({"op":5,"d":{"speaking":0,"delay":0,"ssrc":)";
            //payload += "1";
            payload += std::to_string(ssrc);
            payload += R"(}})";
            std::cout << "Unspeak packet with payload: " << payload << std::endl;
            websocketpp::lib::error_code ec;
            c.send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Can not send unspeak message";
            }
            utils::sleep(1000);
            return;
        }

        std::string getHeartBeatPayload(int seq_num) {
            std::string result = R"({"op":3,"d":1234567890123})";
            return result;
        }

        concurrency::task<void> heartBeat(websocketpp::connection_hdl hdl, client* c, int heartbeat_interval, bool* is_websocket_restart, concurrency::cancellation_token* token, std::shared_ptr<int*> s_sn) {
            if (*is_websocket_restart == true) {
                //this is restart session so don't auth
                resume(this, hdl, c);
            }
            else {
                //fresh session, need auth
                auth(hdl, c);
            }
            //std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval));
            //std::shared_ptr<websocketpp::lib::error_code> shared_ec = std::make_shared<websocketpp::lib::error_code>(ec);
            std::shared_ptr<websocketpp::connection_hdl> shared_hdl = std::make_shared<websocketpp::connection_hdl>(hdl);   //share message handle
            std::shared_ptr<int> shared_heartbeat = std::make_shared<int>(heartbeat_interval);                              //share interval 
            std::shared_ptr<int*> shared_seq_num = std::make_shared<int*>(&seq_num);
            std::shared_ptr<client*> shared_client_context = std::make_shared<client*>(c);
            concurrency::cancellation_token this_is_token = *token;
            return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, this_is_token, this]
                {
                    //check is task is canceled
                    if (this_is_token.is_canceled()) {
                        concurrency::cancel_current_task();
                    }
                    else {
                        while (*shared_heartbeat == 50) {
                            //server not provide heartbeat interval wait a bit
                            std::this_thread::sleep_for(std::chrono::milliseconds(*shared_heartbeat));
                            std::cout << "Wait..." << std::endl;
                        }
                        std::string payload = getHeartBeatPayload(**shared_seq_num);
                        //convert shared shared client pointer to local client 
                        auto client = *shared_client_context;
                        websocketpp::lib::error_code ec;
                        std::cout << "Heartbeat sent with payload: " << payload << std::endl;
                        client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                        if (!ec) {
                            while (!ec) {
                                int localhb = *shared_heartbeat;
                                while (localhb) {
                                    if (this_is_token.is_canceled()) {
                                        std::cout << "Stop heartbeating...\n";
                                        concurrency::cancel_current_task();
                                    }
                                    utils::sleep(50);
                                    localhb -= 50;
                                }
                                //sleep(*shared_heartbeat);
                                std::cout << "Heartbeat sent with payload: " << payload << std::endl;
                                client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                            }
                        }
                        else {
                            std::cout << "Heartbeat failed because: " << ec.message() << std::endl;
                        }
                    }
                }, this_is_token);
        }

        concurrency::task<void> on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
            auto s_is_restart = std::make_shared<bool*>(&is_websocket_restart);
            auto s_hbi = std::make_shared<int>(heartbeat_interval);
            auto s_token = std::make_shared<concurrency::cancellation_token*>(&token);
            auto s_cts = std::make_shared<concurrency::cancellation_token_source*>(&cts);
            auto s_sn = std::make_shared<int*>(&seq_num);
            auto s_ready = std::make_shared<json*>(&ready);
            auto s_t = std::make_shared<concurrency::task<void>*>(&t);
            return concurrency::create_task([c, hdl, msg, s_is_restart, s_hbi, s_token, s_sn, s_ready, s_t, s_cts, this] {
                std::cout << "<Voiceclient> on_message called with hdl: " << hdl.lock().get()
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
                std::cout << "Discord opcode: " << std::to_string(opcode) << std::endl;
                switch (opcode) {
                case 8: //Hello packet
                    //std::cout << "Discord opcode: 8\n";
                    this->hdl = hdl;
                    if (js_msg["d"]["heartbeat_interval"].is_number_float() || js_msg["d"]["heartbeat_interval"].is_number_integer()) {
                        *s_hbi = js_msg["d"]["heartbeat_interval"];
                        std::cout << "Heartbeat interval: " << *s_hbi << "\n";
                    }
                    else {
                        std::cout << "Can't parse heartbeart interval from messages:" << msg->get_payload() << std::endl;
                        std::cout << "Using default heartbeat interval: " << *s_hbi << "\n";
                        return;
                    }
                    //cts = concurrency::cancellation_token_source a;
                    **s_t = heartBeat(hdl, c, *s_hbi, *s_is_restart, *s_token, s_sn);
                    break;
                case 6: //Heartbeat ACK
                    //std::cout << "Discord opcode: 6\n";
                    std::cout << "Heartbeat ACK" << std::endl;
                    break;
                case 2: // Voice ready
                //std::cout << "Discord opcode: 2\n";
                    udpclient.start(js_msg["d"]["port"], js_msg["d"]["ip"]);
                    udpclient.setssrc(js_msg["d"]["ssrc"]);
                    ssrc = js_msg["d"]["ssrc"];
                    this->ready = js_msg;
                    udpclient.ipDiscovery();
                    selectProtocol(hdl, c, udpclient.clientip(), udpclient.clientport());
                    break;
                case 4: //Session info
                    //std::cout << "Discord opcode: 4\n";
                    //std::cout << js_msg["d"]["secret_key"].is_array() << std::endl;
                    if (js_msg["d"]["encodings"][0]["ssrc"].is_number()) {
                        ssrc = js_msg["d"]["encodings"][0]["ssrc"];
                        udpclient.setssrc(ssrc);
                    }
                    else {
                        std::cout << "Can not parse ssrc\n";
                        break;
                    }
                    if (js_msg["d"]["secret_key"].is_array()) {
                        std::cout << "Encryt key: " << js_msg["d"]["secret_key"] << std::endl;
                        unsigned char a[32];
                        std::vector<unsigned char> b = js_msg["d"]["secret_key"];
                        //std::cout << "oh let's parse key: ";
                        for (int i = 0; i < 32; i++) {
                            key.push_back(js_msg["d"]["secret_key"][i]);
                            //std::cout << i;
                        }
                        state = true;
                        break;
                    }
                    else {
                        std::cout << "Can not parse secret key \n";
                        break;
                    }
                    
                }
            });
        }

        void on_close(client* c, websocketpp::connection_hdl hdl) {
            c->get_alog().write(websocketpp::log::alevel::app, "<Voiceclient> Connection Closed");
            std::cout << "Connection closed on hdl: " << hdl.lock().get() << std::endl;
            std::string uri = "wss://" + endpoint;
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
        //voice.start(endpoint, guildid, _token, session);
        void start(std::string uri, std::string guildid, std::string _token, std::string session, std::string user_id) {
            //CConsoleLogger another_console;
            //voiceClientConsole.Create("wow");
            //voiceClientConsole.print("WOW!!");
            sodium_init();
            this->endpoint = uri;
            this->guildid = guildid;
            this->_token = _token;
            this->session = session;
            this->user_id = user_id;
            uri = R"(wss://)" + uri + R"(/?v=4)";
            websocketpp::lib::error_code ec;
            std::cout << "Voice connection established to: " << uri << std::endl;
            client::connection_ptr con_ptr = c.get_connection(uri, ec);
            if (ec) {
                std::cout << "Could not create connection because: " << ec.message() << std::endl;
                return;
            }

            // Note that connect here only requests a connection. No network messages are
            // exchanged until the event loop starts running in the next line.
            c.set_close_handler(bind(&voiceclient::on_close, this, &c, ::_1));
            c.connect(con_ptr);
            c.start_perpetual();
            concurrency::create_task([this] {
                c.run();
            });
        }

        concurrency::task<void> play(std::string path) {
            return concurrency::create_task([this, path] {
                //audio source(path);
                audio* source = new audio(path);
                printf("creating opus encoder\n");
                const unsigned short FRAME_MILLIS = 60;
                const unsigned short FRAME_SIZE = 2880;
                const unsigned short SAMPLE_RATE = 48000;
                const unsigned short CHANNELS = 2;
                const unsigned int BITRATE = 64000;
                
                #define MAX_PACKET_SIZE FRAME_SIZE * 12
                int error;
                OpusEncoder* encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &error);
                if (error < 0) {
                    throw "failed to create opus encoder: " + std::string(opus_strerror(error));
                }

                error = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE));
                if (error < 0) {
                    throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                }

                //_log.debug("initialising libsodium");
                if (sodium_init() == -1) {
                    throw "libsodium initialisation failed";
                }

                int num_opus_bytes;
                unsigned char* pcm_data = new unsigned char[FRAME_SIZE * CHANNELS * 2];
                opus_int16* in_data;
                std::vector<unsigned char> opus_data(MAX_PACKET_SIZE);

                //_log.debug("starting loop");

                class timer_event {
                    bool is_set = false;

                public:
                    bool get_is_set() { return is_set; };

                    void set() { is_set = true; };
                    void unset() { is_set = false; };
                };

                timer_event* run_timer = new timer_event();
                run_timer->set();

                std::queue<std::string>* buffer = new std::queue<std::string>();
                unsigned short* interval = &FrameInterval;
                auto timer = concurrency::create_task([run_timer, this, buffer, FRAME_MILLIS, interval] {
                    utils::sleep(5 * FRAME_MILLIS);
                    int i = 0;
                    concurrency::create_task([run_timer, this] {
                        while (run_timer->get_is_set()) {
                            speak();
                            utils::sleep(5000);
                        }});
                    while (run_timer->get_is_set() || buffer->size() > 0) {
                        unsigned short modified = 14;
                        unsigned short modified15 = 15;
                        unsigned short test = 48;
                        if (i == 1) {
                            i = 0;
                            utils::sleep(*interval);
                            concurrency::create_task([this, buffer] {
                                if (buffer->size() > 0) {
                                    udpclient.send(buffer->front());
                                    buffer->pop();
                                }
                                });
                        }
                        else {
                            i = 1;
                            utils::sleep(*interval);
                            concurrency::create_task([this, buffer] {
                                if (buffer->size() > 0) {
                                    udpclient.send(buffer->front());
                                    buffer->pop();
                                }
                                });
                        }
                    }
                    });
                unsigned short _sequence = 0;
                unsigned int _timestamp = 0;
                int totalsize = 0;
                concurrency::create_task([this] {

                    }).wait();
                while (1) {
                    if (buffer->size() >= 10) {
                        utils::sleep(FRAME_MILLIS);
                    }

                    if (source->read((char*)pcm_data, FRAME_SIZE * CHANNELS * 2) != true)
                        break;

                    in_data = reinterpret_cast<opus_int16*>(pcm_data);

                    num_opus_bytes = opus_encode(encoder, in_data, FRAME_SIZE, opus_data.data(), MAX_PACKET_SIZE);
                    if (num_opus_bytes <= 0) {
                        throw "failed to encode frame: " + std::string(opus_strerror(num_opus_bytes));
                    }

                    opus_data.resize(num_opus_bytes);

                    std::vector<unsigned char> packet(12 + opus_data.size() + crypto_secretbox_MACBYTES);

                    packet[0] = 0x80;	//Type
                    packet[1] = 0x78;	//Version

                    packet[2] = _sequence >> 8;	//Sequence
                    packet[3] = (unsigned char)_sequence;

                    packet[4] = _timestamp >> 24;	//Timestamp
                    packet[5] = _timestamp >> 16;
                    packet[6] = _timestamp >> 8;
                    packet[7] = _timestamp;

                    packet[8] = (unsigned char)(ssrc >> 24);	//SSRC
                    packet[9] = (unsigned char)(ssrc >> 16);
                    packet[10] = (unsigned char)(ssrc >> 8);
                    packet[11] = (unsigned char)ssrc;

                    _sequence++;
                    _timestamp += SAMPLE_RATE / 1000 * FRAME_MILLIS;

                    unsigned char nonce[crypto_secretbox_NONCEBYTES];
                    memset(nonce, 0, crypto_secretbox_NONCEBYTES);

                    for (int i = 0; i < 12; i++) {
                        nonce[i] = packet[i];
                    }

                    crypto_secretbox_easy(packet.data() + 12, opus_data.data(), opus_data.size(), nonce, key.data());

                    packet.resize(12 + opus_data.size() + crypto_secretbox_MACBYTES);

                    std::string msg;
                    msg.resize(packet.size(), '\0');

                    for (unsigned int i = 0; i < packet.size(); i++) {
                        msg[i] = packet[i];
                    }
                    std::cout << "|";
                    //std::cout << "\nMsg size: " << msg.length();
                    totalsize += msg.length();
                    //std::cout << "\nTotal size: " << totalsize;
                    buffer->push(msg);
                }
                std::cout << "\nTotal size: " << totalsize;
                run_timer->unset();
                unspeak();
                timer.wait();

                delete run_timer;
                delete buffer;

                opus_encoder_destroy(encoder);

                delete[] pcm_data;

                printf("finished playing audio\n");
                });
        }

        concurrency::task<void> disconnect(std::string guildid, client* c, websocketpp::connection_hdl hdl) {
            return concurrency::create_task([this, guildid, c, hdl] {
                std::string payload = R"({"op": 4,"d": {"guild_id": ")";
                payload += guildid;
                payload += R"(","channel_id": ")";
                payload += "null";
                payload += R"(","self_mute": true,"self_deaf": true}})";
                websocketpp::lib::error_code ec;
                c->send(hdl, payload, websocketpp::frame::opcode::text, ec);
                return;
                });
        }

        voiceclient () {
            //client c;
            //uri = R"(wss://)" + uri;
            try {
                // Set logging to be pretty verbose (everything except message payloads)
                c.set_access_channels(websocketpp::log::alevel::all);
                c.clear_access_channels(websocketpp::log::alevel::frame_payload);
                // Initialize ASIO
                c.init_asio();
                c.set_tls_init_handler(bind(&utils::on_tls_init));
                // Register our message handler
                c.set_message_handler(bind(&voiceclient::on_message, this, &c, ::_1, ::_2));
                //c.set_close_handler(bind(&voiceclient::on_close, this, &c, ::_1);
                // while (1) {
                //c.reset();
            }
            catch (websocketpp::exception const& e) {
                std::cout << e.what() << std::endl;
            }
        }
    };
    class gatewayclient {
    public:
        std::unordered_map<std::string, std::queue<std::string>*> mainqueue;
        //link userID with a struct contain queue data of that user
        std::unordered_map<std::string, querryqueue*> queuemap;
        //Variable zone

        /* Querry selection queue
        *  Element:
        *  0: User ID
        *  1: Guild ID
        *  2: Channel ID
        *  3-7: video ID
        */ 
        
        voiceclient voice;
        std::string session = "";
        std::string _token = "";
        std::string guildid = "";
        std::string endpoint = "";
        int heartbeat_interval = 50;
        int seq_num = 0;
        bool is_websocket_restart = false;
        json ready;
        //client::connection_ptr con_ptr;
        concurrency::cancellation_token_source cts;
        //this and
        concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
        concurrency::task<void> t;
        //

        static std::string getHeartBeatPayload(int seq_num) {
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

        static void auth(websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            std::string auth_str = "{\"op\":2,\"d\":{\"token\":\"ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss\",\"intents\":648,\"properties\":{\"$os\":\"window\",\"$browser\":\"\",\"$device\":\"\"}}}";
            std::cout << "Authorizing...\n";
            c->send(hdl, auth_str, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Authorization failed because: " << ec.message() << std::endl;
            }
            return;
        }

        static void resume(discordbot::gatewayclient* a, websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            json resume;
            resume["op"] = 6;
            resume["d"]["token"] = "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss";
            resume["d"]["session_id"] = (a->ready)["d"]["session_id"];
            resume["d"]["seq"] = a->seq_num;
            /*std::string a = R"({
                        "op": 6,
                        "d": {
                            "token": "ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss",
                            "session_id": "session_id_i_stored",
                            "seq": 1337
                        }
                    })";*/
            std::cout << "Resuming...\n";
            std::cout << "Resume payload:" << resume.dump() << std::endl;
            c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Resume failed because: " << ec.message() << std::endl;
                return;
            }
            a->is_websocket_restart = false; //reset flag
            return;
        }

        concurrency::task<void> heartBeat(websocketpp::connection_hdl hdl, client* c, int heartbeat_interval, bool* is_websocket_restart, concurrency::cancellation_token* token) {
            if (*is_websocket_restart == true) {
                //this is restart session so don't auth
                resume(this, hdl, c);
            }
            else {
                //fresh session, need auth
                auth(hdl, c);
            }
            //std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval));
            //std::shared_ptr<websocketpp::lib::error_code> shared_ec = std::make_shared<websocketpp::lib::error_code>(ec);
            std::shared_ptr<websocketpp::connection_hdl> shared_hdl = std::make_shared<websocketpp::connection_hdl>(hdl);   //share message handle
            std::shared_ptr<int> shared_heartbeat = std::make_shared<int>(heartbeat_interval);                              //share interval 
            std::shared_ptr<int*> shared_seq_num = std::make_shared<int*>(&seq_num);
            std::shared_ptr<client*> shared_client_context = std::make_shared<client*>(c);
            concurrency::cancellation_token this_is_token = *token;
            return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, this_is_token]
                {
                    //check is task is canceled
                    if (this_is_token.is_canceled()) {
                        concurrency::cancel_current_task();
                    }
                    else {
                        while (*shared_heartbeat == 50) {
                            //server not provide heartbeat interval wait a bit
                            std::this_thread::sleep_for(std::chrono::milliseconds(*shared_heartbeat));
                            std::cout << "Wait..." << std::endl;
                        }

                        //convert shared shared client pointer to local client 
                        auto client = *shared_client_context;
                        websocketpp::lib::error_code ec;
                        std::cout << "Heartbeat sent. " << ec.message() << std::endl;
                        std::string payload = gatewayclient::getHeartBeatPayload(**shared_seq_num);
                        client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                        if (!ec) {
                            while (!ec) {
                                int localhb = *shared_heartbeat;
                                while (localhb) {
                                    if (this_is_token.is_canceled()) {
                                        std::cout << "Stop heartbeating...\n";
                                        concurrency::cancel_current_task();
                                    }
                                    utils::sleep(50);
                                    localhb -= 50;
                                }
                                //sleep(*shared_heartbeat);
                                payload = gatewayclient::getHeartBeatPayload(**shared_seq_num);
                                client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                                std::cout << "Heartbeat sent with payload: " << payload << std::endl;
                            }
                        }
                        else {
                            std::cout << "Heartbeat failed because: " << ec.message() << std::endl;
                        }
                    }
                }, this_is_token);
        }

        concurrency::task<void> on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
            auto s_is_restart = std::make_shared<bool*>(&is_websocket_restart);
            auto s_hbi = std::make_shared<int>(heartbeat_interval);
            auto s_token = std::make_shared<concurrency::cancellation_token*>(&token);
            auto s_cts = std::make_shared<concurrency::cancellation_token_source*>(&cts);
            auto s_sn = std::make_shared<int*>(&seq_num);
            auto s_ready = std::make_shared<json*>(&ready);
            auto s_t = std::make_shared<concurrency::task<void>*>(&t);
            return concurrency::create_task([c, hdl, msg, s_is_restart, s_hbi, s_token, s_sn, s_ready, s_t, s_cts, this] {
                std::cout << "<Gateway> on_message called with hdl: " << hdl.lock().get()
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
                /*case -1:
                    std::cout << "Voice state update received \n";
                    _token = js_msg["token"];
                    guildid = js_msg["guild_id"];
                    endpoint = js_msg["endpoint"];
                    int i = 1;
                    while (session == "") {
                        std::string out = "Waiting for session id" + std::to_string(i) + "\n";
                        std::cout << out;
                        utils::sleep(1000);
                        i++;
                        if (i > 5) {
                            break;
                        }
                        voice.start(endpoint, guildid, _token, session);
                    }
                    if (i > 5) {
                        std::cout << "Fail to wait for session id\n";
                        break;
                    }
                    else {

                    }
                    break;*/
                case 9: //invalid session
                    //just restart
                    **s_is_restart = false;
                    utils::restart(hdl, c, *s_cts, *s_token, *s_t);
                    break;
                case 10: //Hello packet
                    std::cout << "Discord opcode: 10\n";
                    if (js_msg["d"]["heartbeat_interval"].is_number_integer()) {
                        *s_hbi = js_msg["d"]["heartbeat_interval"];
                        std::cout << "Heartbeat interval: " << *s_hbi << "\n";
                    }
                    else {
                        std::cout << "Can't parse heartbeart interval from messages:" << msg->get_payload() << std::endl;
                        std::cout << "Using default heartbeat interval: " << *s_hbi << "\n";
                    }
                    //cts = concurrency::cancellation_token_source a;
                    **s_t = heartBeat(hdl, c, *s_hbi, *s_is_restart, *s_token);
                    break;
                case 11: //Heartbeat ACK
                    std::cout << "Discord opcode: 11\n";
                    std::cout << "Heartbeat ACK" << std::endl;
                    break;
                case 7: //Request reconnect
                    **s_is_restart = true;
                    utils::restart(hdl, c, *s_cts, *s_token, *s_t);
                    break;
                case 0:
                    std::cout << "Discord opcode: 0\n";
                    std::cout << "Discord event name:" << js_msg["t"] << "\n";

                    //update SEQUENCE NUMBER
                    if (js_msg["s"].is_number_integer()) {
                        **s_sn = js_msg["s"];
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

                    //if (event == R"(VOICE_STATE_UPDATE)") {
                    //    json ready = **s_ready;
                    //    if (js_msg["d"]["user_id"] == ready["d"]["user"]["id"]) {
                    //        //voice::session = js_msg["d"]["session_id"];
                    //    }
                    //}
                    if (event == R"(READY)") { //receive ready packet 
                        **s_ready = js_msg; //cache ready data
                        break;
                    }
                    if (event == R"(VOICE_STATE_UPDATE)") {
                        if (utils::isSelf(js_msg, **s_ready)) {
                            //indicate that we started a voice connection
                            if (js_msg["d"]["session_id"].is_string()) {
                                this->session = js_msg["d"]["session_id"];
                                std::cout << "Successful get session id: " << session << std::endl;
                            }
                            else {
                                std::cout << "Can not parse session id\n";
                            }
                        }
                        break;
                    }
                    if (event == R"(VOICE_SERVER_UPDATE)") {
                        if (js_msg["d"]["token"].is_string() && js_msg["d"]["guild_id"].is_string() && js_msg["d"]["endpoint"].is_string()) {
                            _token = js_msg["d"]["token"];
                            guildid = js_msg["d"]["guild_id"];
                            endpoint = js_msg["d"]["endpoint"];
                            std::cout << "Successful get token id: " << _token << std::endl;
                            std::cout << "Successful get guild id: " << guildid << std::endl;
                            std::cout << "Successful get endpoint: " << endpoint << std::endl;
                        }
                        else {
                            std::cout << "Can not parse: token, guild id, endpoint \n";
                        }
                        int i = 1;
                        while (session == "") {
                            std::string out = "Waiting for session id " + std::to_string(i) + "\n";
                            std::cout << out;
                            utils::sleep(1000);
                            i++;
                            if (i > 5) {
                                break;
                            }
                        }
                        if (i > 5) {
                            std::cout << "Fail to wait for session id\n";
                            break;
                        }
                        else {
                            voice.start(endpoint, guildid, _token, session, ready["d"]["user"]["id"]);
                        }
                        break;
                    }

                    if (event == R"(MESSAGE_CREATE)") { //New command
                        std::string channel = "";
                        if (js_msg["d"]["channel_id"].is_string()) {
                            channel = js_msg["d"]["channel_id"];
                        }
                        else {
                            std::cout << "Can not parse channel id from message:" << msg->get_payload() << std::endl;
                            //channel = "";
                        }

                        std::string guild = "";
                        if (js_msg["d"]["guild_id"].is_string()) {
                            guild = js_msg["d"]["guild_id"];
                        }
                        else {
                            std::cout << "Can not parse guild id from message:" << msg->get_payload() << std::endl;
                            //channel = "";
                        }

                        std::string content = "";
                        if (js_msg["d"]["content"].is_string()) {
                            content = js_msg["d"]["content"];
                        }
                        else {
                            std::cout << "Can not parse content from message:" << msg->get_payload() << std::endl;
                        }

                        std::string userid = "";
                        if (js_msg["d"]["author"]["id"].is_string()) {
                            userid = js_msg["d"]["author"]["id"];
                        }
                        else std::cout << "Can not parse userid from message: " << msg->get_payload() << std::endl;

                        if (js_msg["d"]["mentions"][0]["id"] == (**s_ready)["d"]["user"]["id"]) {  //mention
                            utils::sendMsg("Why mention me? I won't show you my prefix is -", channel);
                            std::cout << js_msg["d"]["mentions"][0]["id"] << std::endl;
                            std::cout << (**s_ready)["d"]["user"]["id"] << std::endl;
                            break;
                        }
                        if (content == "ping" || content == "Ping") {
                            int RTT = utils::ping("162.159.136.232"); //discord IP: 162.159.136.232
                            std::cout << "RTT: " << std::to_string(RTT) << std::endl;
                            std::string msg = "Pong! ";
                            msg += std::to_string(RTT);
                            msg += "ms";
                            utils::sendMsg(msg, channel);
                            break;
                        }
                        if (content == "Hello" || content == "hello") {
                            utils::sendMsg("Hi!", channel);
                            break;
                        }
                        if (content == "restart" || content == "Restart") {
                            if (utils::isOwner(js_msg)) {
                                utils::sendMsg("Restarting...", channel);
                                **s_is_restart = true;
                                utils::restart(hdl, c, *s_cts, *s_token, *s_t);
                            }
                            else {
                                utils::sendMsg("\x43\xc3\xb3\x20\x63\xc3\xa1\x69\x20\x63\x6f\x6e\x20\x63\xe1\xba\xb7\x63", channel);
                            }
                            break;
                        }
                        if (content == "1") {
                            if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = queuemap[userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (mainqueue.find(userid) == mainqueue.end()) {//not found key, create pair
                                            mainqueue[userid] = new std::queue<std::string>;
                                            mainqueue[userid]->push(temp->data[0]);
                                            std::cout << "Queued video id: " << temp->data[0] << std::endl;
                                            temp->reset();
                                        }
                                        else { //found pair
                                            mainqueue[userid]->push(temp->data[0]);
                                            std::cout << "Queued video id: " << temp->data[0] << std::endl;
                                            temp->reset();
                                        }
                                    }
                                }
                            }
                            websocketpp::lib::error_code ec;
                            std::cout << "Establish voice connection to channel id: 633233029208473601";
                            c->send(hdl, getVoiceStateUpdatePayload(guild, "633233029208473601"), websocketpp::frame::opcode::text, ec);
                            concurrency::create_task([this] {
                                while (!voice.isReady()) {
                                    utils::sleep(1000);
                                }
                                voice.play(R"(D:\Download\pytube-d282c0aaf4f10f1d1c4ca2eb6157980c31671f52\pytube\b.mp4)");
                                });
                            break;
                        }
                        if (content == "2") {
                            if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = queuemap[userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (mainqueue.find(userid) == mainqueue.end()) {//not found key, create pair
                                            mainqueue[userid] = new std::queue<std::string>;
                                            mainqueue[userid]->push(temp->data[1]);
                                            temp->reset();
                                        }
                                        else {
                                            mainqueue[userid]->push(temp->data[1]);
                                            temp->reset();
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        if (content == "3") {
                            if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = queuemap[userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (mainqueue.find(userid) == mainqueue.end()) {//not found key, create pair
                                            mainqueue[userid] = new std::queue<std::string>;
                                            mainqueue[userid]->push(temp->data[2]);
                                            temp->reset();
                                        }
                                        else {
                                            mainqueue[userid]->push(temp->data[2]);
                                            temp->reset();
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        if (content == "4") {
                            if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = queuemap[userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (mainqueue.find(userid) == mainqueue.end()) {//not found key, create pair
                                            mainqueue[userid] = new std::queue<std::string>;
                                            mainqueue[userid]->push(temp->data[3]);
                                            temp->reset();
                                        }
                                        else {
                                            mainqueue[userid]->push(temp->data[3]);
                                            temp->reset();
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        if (content == "5") {
                            if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = queuemap[userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (mainqueue.find(userid) == mainqueue.end()) {//not found key, create pair
                                            mainqueue[userid] = new std::queue<std::string>;
                                            mainqueue[userid]->push(temp->data[4]);
                                            temp->reset();
                                        }
                                        else {
                                            mainqueue[userid]->push(temp->data[4]);
                                            temp->reset();
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        if (utils::isStartWith(content, "?p")) {
                            //parse string
                            std::string querry = content.erase(0, 3);
                            if (querry == "") {
                                utils::sendMsg("Please provide paramenter", channel);
                            }
                            else {
                                //sendMsg(querry, channel);
                                json result = utils::youtubePerformQuerry(querry, true);
                                std::cout << "Querry result for keyword: " << querry << std::endl;
                                std::cout << result.dump() << std::endl;
                                utils::youtubePrintSearchResult(result, querry, channel, true);
                                if (queuemap.find(userid) == queuemap.end()) { //Not found key in database
                                    //insert new key
                                    queuemap[userid] = new querryqueue;
                                    queuemap[userid]->push(js_msg, result);
                                }
                                else {
                                    queuemap[userid]->push(js_msg, result); //already has pair, perform querryqueue.push()
                                }
                                std::cout << "Cached search querry\n";
                            }
                            break;
                        }

                        std::string value="";
                        if (utils::parse(&value, "frameinterval", content)) {
                            std::string num="";
                            if (value == "?") {
                                std::string payload = "Current frame interval: " + std::to_string(voice.FrameInterval);
                                utils::sendMsg(payload, channel);
                                break;
                            }
                            for (int i = 0; i < value.length(); i++) {
                                if (48 <= value[i] && value[i] <= 57) {
                                    num += value[i];
                                }
                                else break;
                            }
                            if (num == "") {
                                std::string payload = "Paramenter must be an interger, for real!";
                                utils::sendMsg(payload, channel);
                                break;
                            }
                            websocketpp::lib::error_code ec;
                            std::string payload = "Changed frame interval to " + std::to_string((unsigned short)stoi(value)) + ". Old value: " + std::to_string(voice.FrameInterval);
                            voice.FrameInterval = (unsigned short)stoi(value);
                            utils::sendMsg(payload, channel);
                            break;
                        }
                    }
                }
            });
        }

        std::string getVoiceStateUpdatePayload(std::string guild, std::string channel) {
            /*
            {
                "op": 4,
                "d": {
                "guild_id": "41771983423143937",
                "channel_id": "127121515262115840",
                "self_mute": false,
                "self_deaf": false
                }
            }
            */
            std::string payload = R"({"op": 4,"d": {"guild_id": ")";
            payload += guild;
            payload += R"(","channel_id": ")";
            payload += channel;
            payload += R"(","self_mute": false,"self_deaf": true}})";
            return payload;
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

        gatewayclient() {
            client c;
            std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";
            try {
                // Set logging to be pretty verbose (everything except message payloads)
                c.set_access_channels(websocketpp::log::alevel::all);
                c.clear_access_channels(websocketpp::log::alevel::frame_payload);
                // Initialize ASIO
                c.init_asio();
                c.set_tls_init_handler(bind(&utils::on_tls_init));
                // Register our message handler
                c.set_message_handler(bind(&gatewayclient::on_message, this, &c, ::_1, ::_2));
                c.set_close_handler(bind(&gatewayclient::on_close, this, &c, ::_1));
                // while (1) {
                websocketpp::lib::error_code ec;
                client::connection_ptr con_ptr = c.get_connection(uri, ec);
                if (ec) {
                    std::cout << "Could not create connection because: " << ec.message() << std::endl;
                    return;
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
    };
};
    
int main() {
    SetConsoleOutputCP(CP_UTF8);
    discordbot::gatewayclient gw; //blocking call
    std::cout << "this is a test \n";

}