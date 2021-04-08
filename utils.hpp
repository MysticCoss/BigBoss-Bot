#pragma once
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"
#include <nlohmann/json.hpp>
#include "curl/curl.h"
#include <iostream>
#include <ppltasks.h>
#include <time.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <algorithm>
#include <regex>
#include <boost/process.hpp>
#include <concrt.h>
#include <boost/chrono.hpp>
#include <windows.h>
#include <math.h>
#include "log.hpp"
#include <boost/regex.hpp>
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef nlohmann::json json;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr; 
namespace discordbot {
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

        static bool parse(std::string* output, std::string param, std::string msg) {
            if (isStartWith(msg, param)) {
                std::string result = msg.erase(0, param.length() + 1); //delete param from content, including space
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

        inline static void timerSleep(double seconds) {
            using namespace std::chrono;

            static HANDLE timer = CreateWaitableTimer(NULL, FALSE, NULL);
            static double estimate = 5e-3;
            static double mean = 5e-3;
            static double m2 = 0;
            static int64_t count = 1;

            while (seconds - estimate > 1e-7) {
                double toWait = seconds - estimate;
                LARGE_INTEGER due;
                due.QuadPart = -int64_t(toWait * 1e7);
                auto start = high_resolution_clock::now();
                SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0);
                WaitForSingleObject(timer, INFINITE);
                auto end = high_resolution_clock::now();

                double observed = (end - start).count() / 1e9;
                seconds -= observed;

                ++count;
                double error = observed - toWait;
                double delta = error - mean;
                mean += delta / count;
                m2 += delta * (error - mean);
                double stddev = sqrt(m2 / (count - 1));
                estimate = mean + stddev;
            }

            // spin lock
            auto start = high_resolution_clock::now();
            while ((high_resolution_clock::now() - start).count() / 1e9 < seconds);
        }

        static void sleep(int ms) {
            //std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            concurrency::wait(ms);
        }

        static void sleepmcs(int mcs) {
            std::this_thread::sleep_for(std::chrono::microseconds(mcs));
        }

        static void sleepex(int ms) {
            auto now = std::chrono::system_clock::now();
            long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - now).count();
            while (elapsed < ms) {
                //sleep(1);
                elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - now).count();
            }
        }

        static bool isStartWith(std::string source, std::string prefix) {
            return source.rfind(prefix, 0) == 0 ? true : false;
        }

        static json youtubePerformQuerry(std::string querry, bool debug = false) {
            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* list = NULL;
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

        static int regexParse(logger* logger, std::string regex, std::string input, boost::smatch* _return) {
            boost::smatch res;
            if (boost::regex_match(input, res, boost::regex(regex), boost::match_extra))
            {
                logger->log("Match found: string \"" + input + "\" with pattern \"" + regex + "\"", info);
                *_return = res;
                return 1;
            }
            else
            {
                logger->log("No match found: string \"" + input + "\" with pattern \"" + regex + "\"", error);
                return 0;
            }
        }

        static std::string youtubeGetTitle(std::string video_id, bool debug = false) {
            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* list = NULL;
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
                /* Provide CA Certs from http://curl.haxx.se/docs/caextract.html */
                curl_easy_setopt(curl, CURLOPT_CAINFO, "curl-ca-bundle.crt");

                // url encode example https://youtube.googleapis.com/youtube/v3/videos?part=snippet%2CcontentDetails%2Cstatistics&id=Ks-_Mh1QhMc&key=[YOUR_API_KEY]
                std::string url = R"(https://youtube.googleapis.com/youtube/v3/videos?part=snippet%2CcontentDetails%2Cstatistics&id=)";
                //std::replace(querry.begin(), querry.end(), " ", "+");
                url += utf8_url_encode(video_id); //URL encode
                url += "&key=";
                url += "AIzaSyB0_DpMR1gi_iuNrPsKgn1LcAN5t5d8_j4";
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
                if (jsonpayload["items"][0]["snippet"]["title"].is_string()) {
                    std::string ret = jsonpayload["items"][0]["snippet"]["title"];
                    return ret;
                }
                else {
                    return "";
                }
            }
            else {
                std::cout << "[sendMsg] Invalid handle\n";
                return NULL;
            }
        }

        static int addReaction(logger* logger, std::string emoji, std::string message) {

        }

        static void restart(websocketpp::connection_hdl hdl, client* c, concurrency::cancellation_token_source* cts, concurrency::cancellation_token* token, concurrency::task<void>* t) {
            std::cout << "================================Websocket restart================================\n";
            cts->cancel();
            t->wait();
            //reset token
            client::connection_ptr con_ptr = c->get_con_from_hdl(hdl);
            con_ptr->close(websocketpp::close::status::service_restart, "");
            *cts = concurrency::cancellation_token_source();
            *token = cts->get_token();
            std::cout << "Cancel token reset\n";

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
                return jsonpayload;
            }
            else {
                std::cout << "sendMsg invalid handle\n";
                return NULL;
            }
        }

        static int deleteMsg(discordbot::logger* logger, std::string messageID, std::string channelID, bool debug = false) {
            //Request DELETE /channels/{channel.id}/messages/{message.id}
            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* list = NULL;
                json postData;
                std::string url = "https://discord.com/api/v8/channels/" + channelID + "/messages/" + messageID;
                //url = "https://discord.com/api/v8/channels/" + channelID + "/messages";
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
                /* Provide CA Certs from http://curl.haxx.se/docs/caextract.html */
                curl_easy_setopt(curl, CURLOPT_CAINFO, "curl-ca-bundle.crt");
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

                list = curl_slist_append(list, "Authorization: Bot ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss");
                list = curl_slist_append(list, "Content-Type: application/json");
                char buf[CURL_ERROR_SIZE];
                curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, buf);
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                struct payload chunk;
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    std::string errmsg(buf);
                    logger->log("deleteMsg failed with error code " + std::to_string(res) + ": " + errmsg, error);
                    return 0;
                }
                curl_slist_free_all(list); /* free the list again */
                curl_easy_reset(curl);
                curl_easy_cleanup(curl);
                return 1;
            }
            else {
                logger->log("deleteMsg invalid handle", error);
                return 0;
            }
        }

        static std::string getVoiceStateUpdatePayload(std::string guild, std::string channel) {
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
            if (channel == "null") {
                payload += R"(","channel_id": )";
                payload += channel;
                payload += R"(,"self_mute": false,"self_deaf": true}})";
            }
            else {
                payload += R"(","channel_id": ")";
                payload += channel;
                payload += R"(","self_mute": false,"self_deaf": true}})";
            }
            return payload;
        }

        static json youtubePrintSearchResult(json result, std::string querry, std::string channel, bool debug = false) {
            if (result == NULL) {
                std::cout << "youtubePrintSearchResult receive null object as paramenter\n";
                utils::sendMsg(R"(```Got bad result, please try searching again!```)", channel);
                return NULL;
            }
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
                temp = std::regex_replace(temp, std::regex(R"(&#39;)"), R"(')");
                temp = std::regex_replace(temp, std::regex(R"(&amp;)"), R"(&)");
                temp = std::regex_replace(temp, std::regex(R"(\|)"), R"(\|)");
                temp = std::regex_replace(temp, std::regex(R"(\*)"), R"(\*)");
                //temp = std::regex_replace(temp, std::regex(R"(_)"), R"(\_)");
                printString += temp;
                printString += "\n";
            }
            printString += "Choose 1-5 by type `1` or `2`,... to the chat";
            if (debug) std::cout << printString << std::endl;
            return sendMsg(printString, channel);
            //sendMsg("Note: Voice function is still in development", channel);
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
};
