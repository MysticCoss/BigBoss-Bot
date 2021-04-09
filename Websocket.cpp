#pragma once
// BOOST_INTERPROCESS_USE_DLL
#include "boost/regex.hpp"
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
#include <sodium.h>
#include <concrt.h>
#include <boost/chrono.hpp>
#include <windows.h>
#include <math.h>
#include "FFmpegAudioSource.hpp"
#include <opus/opus.h>
#include "utils.hpp"
#include "embed.hpp"
#include "log.hpp"

boost::asio::io_context context_io;
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef nlohmann::json json;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
boost::log::sources::severity_logger_mt<severity_level> lg;
extern std::string str_token = "";

struct querryqueue {
    std::string userid = "";
    std::string guildid = "";
    std::string channelid = "";
    std::string messageid = "";
    std::string data[5];
    bool set = false;
    discordbot::logger* logger;
    querryqueue(discordbot::logger* logger) {
        this->logger = logger;
        for (int i = 0; i < 5; i++) {
            data[i] = "";
        }
    }
    bool push(json js_msg, json querrydata) {
        if (js_msg["d"]["author"]["id"].is_null() || js_msg["d"]["guild_id"].is_null() || js_msg["d"]["channel_id"].is_null()) {
            logger->log("Can not queue querry data", info);
            set = false;
            return false;
        }
        else if (querrydata["items"][0]["id"]["videoId"].is_null() || querrydata["items"][1]["id"]["videoId"].is_null() || querrydata["items"][2]["id"]["videoId"].is_null() || querrydata["items"][3]["id"]["videoId"].is_null() || querrydata["items"][4]["id"]["videoId"].is_null()) {
            logger->log("Can not queue querry data", info);
            set = false;
            return false;
        }
        else {
            logger->log("Queued search data\n", info);
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
        if (messageid != "") {
            return set;
        }
        else return false;
    }
    void setMessageId(std::string messageid) {
        if (messageid == "") {
            logger->log("Cache queue received empty messageid (line 88 websocket.cpp)", error);
            return;
        }
        else {
            this->messageid = messageid;
            logger->log("Cached message id: " + messageid, info);
        }
    }
    std::string gettMessageId() {
        if (messageid == "") {
            logger->log("Cache queue return empty messageid (line 96 websocket.cpp)", error);
        }
        return messageid;
    }
    void reset() {
        for (int i = 0; i < 5; i++) {
            data[i] = "";
        }
        std::string userid = "";
        std::string guildid = "";
        std::string channelid = "";
        std::string messageid = "";
        set = false;
        logger->log("Cleared cache data", info);
    }
};

//enum severity_level
//{
//    normal,
//    notification,
//    warning,
//    error,
//    critical
//};


struct userinfo {
    std::string userid = "";
    std::string guildid = "";
    std::string channelid = "";
    std::string sessionid = "";
    void update(json data, discordbot::logger* logger) {
        if (data["d"]["channel_id"].is_null()) {
            channelid = "";
        }
        else channelid = data["d"]["channel_id"];
        userid = data["d"]["user_id"];
        guildid = data["d"]["guild_id"];
        sessionid = data["d"]["session_id"];
        logger->log({ "Update cache data for user " + userid + " in guild " + guildid + ": Channel ID = " + (channelid == "" ? "NULL" : channelid) + ", session ID = " + sessionid }, info);
    }
    std::string get_voice_channel_id() {
        return channelid;
    }
    std::string get_session_id() {
        return sessionid;
    }
};

namespace discordbot {
    class voiceclient {
    private:
        bool loop = false;
        std::string nowplaying = "";
        logger* logger;
        std::string current_voice_channel = "";
        std::string default_channel = "";
        bool is_pusher_locked = false;
        bool first_time = true;
        bool running = false; //set true: when player is playing.
        bool connect = false; //set true: when encrypt key received. set false: when close handshake; usage: indicate connection status
        bool state = false;   //set true: when encrypt key received. set false: when cleanup; usage: to lock operate of pusher
    public:
        //Variable zone
        concurrency::task<void> pusher;
        std::queue<std::string> selfqueue; //queue for video id
        client* gatewayclient; //cache gateway endpoint
        websocketpp::connection_hdl gatewayhdl; //cache gateway hdl for sending message
        std::vector<unsigned char> key;
        udp::udpclient udpclient;
        std::string user_id = "";
        std::string _token = "";
        std::string guildid = "";
        std::string endpoint = "";
        std::string session = "";
        int expected_packet_loss = 30;
        long long offset = 10;
        int ssrc = 0;
        int heartbeat_interval = 0;
        int seq_num = 0;
        bool is_websocket_restart = false;
        json ready;
        client c;
        websocketpp::connection_hdl hdl;
        concurrency::cancellation_token_source cts;
        concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
        concurrency::task<void> t;

        concurrency::cancellation_token_source p_cts;
        concurrency::cancellation_token p_token = p_cts.get_token();
        concurrency::task<void> playing;

        //lock the pusher until next start
        void lockPusher() {
            logger->log("Try locking pusher", info);
            if (is_pusher_locked == false) {
                logger->log("Pusher locked", info);
                is_pusher_locked = true;
            }
            else {
                logger->log("Pusher is already locked", info);
            }
        }

        void unlockPusher() {
            logger->log("Try unlocking pusher", info);
            if (is_pusher_locked == true) {
                logger->log("Pusher unlocked", info);
                is_pusher_locked = false;
            }
            else {
                logger->log("Pusher is already unlocked", info);
            }
        }

        void setCurrentVoiceChannel(std::string current_voice_channel) {
            logger->log("Set current voice channel to: " + current_voice_channel, info);
            this->current_voice_channel = current_voice_channel;
            return;
        }

        std::string getCurrentVoiceChannel() {
            logger->log("Get current voice channel: " + current_voice_channel, info);
            return current_voice_channel;
        }

        void setDefaultChannel(std::string channel) {
            default_channel = channel;
            logger->log("Set default channel to " + channel, info);
            return;
        }

        std::string getDefaultChannel() {
            logger->log("Get default channel: " + default_channel, info);
            return default_channel;
        }

        bool isConnect() {
            return connect;
        }

        bool isRunning() {
            return running;
        }

        bool _loop() {
            if (loop) {
                loop = false;
                logger->log("Loop changed to false", info);
                return false;
            }
            else {
                loop = true;
                logger->log("Loop changed to true", info);
                return true;
            }
        }
        void selectProtocol(websocketpp::connection_hdl hdl, client* c, std::string address, int port) {
            std::string payload = R"({ "op": 1,"d": {"protocol": "udp","data": {"address": ")";
            payload += address += R"(","port": )";
            payload += std::to_string(port);
            payload += R"(, "mode": "xsalsa20_poly1305"}}})";
            logger->log("Select protocol sent with payload: " + payload, info);
            websocketpp::lib::error_code ec;
            c->send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Select protocol failed because: " + ec.message(), info);
            }
            return;
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
            c->send(hdl, auth_str, websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Authorization failed because: " + ec.message(), error);
            }
            logger->log("Authorize packet sent", info);
            return;
        }

        void resume(discordbot::voiceclient* a, websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            json resume;
            resume["op"] = 6;
            resume["d"]["token"] = str_token;
            resume["d"]["session_id"] = (a->ready)["d"]["session_id"];
            resume["d"]["seq"] = a->seq_num;
            /*std::string a = R"({
                        "op": 6,
                        "d": {
                            "token": "SOMETHINGHERE",
                            "session_id": "session_id_i_stored",
                            "seq": 1337
                        }
                    })";*/
            logger->log("Resume packet sent with payload:" + resume.dump(), info);
            c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Resume failed because: " + ec.message(), error);
                return;
            }
            is_websocket_restart = false; //reset flag
            return;
        }

        void speak() {
            std::string payload = R"({"op":5,"d":{"speaking":1,"delay":0,"ssrc":)";
            //payload += "1";
            payload += std::to_string(ssrc);
            payload += R"(}})";
            logger->log("Speak packet with payload: " + payload, info);
            websocketpp::lib::error_code ec;
            c.send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Can not send speak message because: " + ec.message(), error);
            }
            return;
        }

        void unspeak() {
            std::string payload = R"({"op":5,"d":{"speaking":0,"delay":0,"ssrc":)";
            //payload += "1";
            payload += std::to_string(ssrc);
            payload += R"(}})";
            logger->log("Unspeak packet with payload: " + payload, info);
            websocketpp::lib::error_code ec;
            c.send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Can not send unspeak message because: " + ec.message(), error);
            }
            utils::sleep(1000);
            return;
        }

        std::string getHeartBeatPayload(int seq_num) {
            std::string result = R"({"op":3,"d":633218964771569676})";
            /*633218964771569676
                1234567890123*/
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
            auto s_log = std::make_shared<discordbot::logger*>(logger);
            //concurrency::cancellation_token this_is_token = *token;
            return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, token, this, s_log]
                {
                    //check is task is canceled
                    if (token->is_canceled()) {
                        (*s_log)->log("Stop heartbeat", info);
                        concurrency::cancel_current_task();
                        return;
                    }
                    else {
                        int i = 0;
                        while (*shared_heartbeat == 50) {
                            //server not provide heartbeat interval wait a bit
                            std::this_thread::sleep_for(std::chrono::milliseconds(*shared_heartbeat));
                            (*s_log)->log("Wait for heartbeat interval", info);
                            utils::sleep(500);
                            i++;
                            if (i > 5) {
                                (*s_log)->log("Wait for heatbeat for too long", warning);
                                break;
                            }
                        }
                        std::string payload = getHeartBeatPayload(**shared_seq_num);
                        //convert shared shared client pointer to local client 
                        auto client = *shared_client_context;
                        websocketpp::lib::error_code ec;
                        (*s_log)->log("Heartbeat sent with payload: " + payload, info);
                        client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                        if (!ec) {
                            while (!ec) {
                                int localhb = *shared_heartbeat;
                                while (localhb) {
                                    if (token->is_canceled()) {
                                        (*s_log)->log("Stop heartbeat", info);
                                        concurrency::cancel_current_task();
                                    }
                                    utils::sleep(50);
                                    localhb -= 50;
                                }
                                //sleep(*shared_heartbeat);
                                (*s_log)->log("Heartbeat sent with payload: " + payload, info);
                                client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                            }
                        }
                        else {
                            (*s_log)->log("Heartbeat failed because: " + ec.message(), error);
                        }
                    }
                });
        }

        concurrency::task<void> on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
            this->hdl = hdl;
            auto s_is_restart = std::make_shared<bool*>(&is_websocket_restart);
            auto s_hbi = std::make_shared<int>(heartbeat_interval);
            auto s_token = std::make_shared<concurrency::cancellation_token*>(&token);
            auto s_cts = std::make_shared<concurrency::cancellation_token_source*>(&cts);
            auto s_sn = std::make_shared<int*>(&seq_num);
            auto s_ready = std::make_shared<json*>(&ready);
            auto s_t = std::make_shared<concurrency::task<void>*>(&t);
            auto s_log = std::make_shared<discordbot::logger*>(logger);
            return concurrency::create_task([c, hdl, msg, s_is_restart, s_hbi, s_token, s_sn, s_ready, s_t, s_cts, this, s_log] {
                std::ostringstream str;
                str << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload();
                (*s_log)->log(str.str(), info);

                std::string str_msg = msg->get_payload();
                json js_msg;
                js_msg = json::parse(str_msg);
                int opcode = -1;
                if (js_msg["op"].is_number_integer()) {
                    opcode = js_msg["op"];
                }
                (*s_log)->log("Discord opcode: " + std::to_string(opcode), info);
                switch (opcode) {
                case 8: //Hello packet
                    //(*s_log)->log(, info); << "Discord opcode: 8\n";
                    this->hdl = hdl;
                    if (js_msg["d"]["heartbeat_interval"].is_number_float() || js_msg["d"]["heartbeat_interval"].is_number_integer()) {
                        *s_hbi = js_msg["d"]["heartbeat_interval"];
                        (*s_log)->log("Heartbeat interval: " + *s_hbi, info);
                    }
                    else {
                        (*s_log)->log("Can't parse heartbeart interval from messages:" + msg->get_payload(), error);
                        (*s_log)->log("Using default heartbeat interval: " + *s_hbi, error);
                        return;
                    }
                    //cts = concurrency::cancellation_token_source a;
                    **s_t = heartBeat(hdl, c, *s_hbi, *s_is_restart, *s_token, s_sn);
                    break;
                case 6: //Heartbeat ACK
                    //(*s_log)->log(, info); << "Discord opcode: 6\n";
                    (*s_log)->log({ "Heartbeat ACK" }, info);
                    break;
                case 2: // Voice ready
                //(*s_log)->log(, info); << "Discord opcode: 2\n";
                    udpclient.start(js_msg["d"]["port"], js_msg["d"]["ip"]);
                    udpclient.setssrc(js_msg["d"]["ssrc"]);
                    ssrc = js_msg["d"]["ssrc"];
                    this->ready = js_msg;
                    udpclient.ipDiscovery();
                    selectProtocol(hdl, c, udpclient.clientip(), udpclient.clientport());
                    break;
                case 4: //Session info
                    //(*s_log)->log(, info); << "Discord opcode: 4\n";
                    //(*s_log)->log(, info); << js_msg["d"]["secret_key"].is_array() << std::endl;
                    /*if (js_msg["d"]["encodings"][0]["ssrc"].is_number()) {
                        ssrc = js_msg["d"]["encodings"][0]["ssrc"];
                        udpclient.setssrc(ssrc);
                    }
                    else {
                        (*s_log)->log(, info); << "Can not parse ssrc\n";
                        break;
                    }*/
                    if (js_msg["d"]["secret_key"].is_array()) {
                        //(*s_log)->log("Encryt key: " + (std::array)js_msg["d"]["secret_key"], info);
                        //(*s_log)->log(, info); << "oh let's parse key: ";
                        for (int i = 0; i < 32; i++) {
                            key.push_back(js_msg["d"]["secret_key"][i]);
                            //(*s_log)->log(, info); << i;
                        }
                        state = true;
                        connect = true;
                        unlockPusher();
                        break;
                    }
                    else {
                        (*s_log)->log({ "Can not parse secret key" }, error);
                        break;
                    }
                }
                });
        }

        void on_close(client* c, websocketpp::connection_hdl hdl) {
            c->get_alog().write(websocketpp::log::alevel::app, "[on_close] Connection Closed");
            std::ostringstream str;
            str << "[on_close] Connection closed on hdl: " << hdl.lock().get();
            logger->log(str.str(), notification);
            logger->log("Connection close, performing cleanup", info);
            state = false; //lock pusher
            endpoint = "";
            guildid = "";
            _token = "";
            session = "";
            user_id = "";
            loop = false;
            key.clear();

            cts.cancel();
            if (connect) {
                logger->log({ "Waiting for heartbeat thread to exit" }, info);
                t.wait();
            }


            if (running) {
                logger->log({ "Stop player" }, info);
                p_cts.cancel(); 
                playing.wait();
                utils::sleep(500);
                p_cts = concurrency::cancellation_token_source();
                p_token = p_cts.get_token();
            }
            else {
                logger->log({ "Player is not running" }, info);
            }

            logger->log({ "Close udp socket" }, info);
            udpclient.cleanup();

            //reset token
            cts = concurrency::cancellation_token_source();
            token = cts.get_token();
            connect = false;
        }

        void stop() {
            
        }

        //clear self queue
        void clear() {
            std::queue<std::string> empty;
            std::swap(selfqueue, empty);
            logger->log({ "Queue cleared" }, info);
        }

        //skip current playing track by cancel playing task
        void skip() {

        }

        //remove a song in current queue
        void remove(int position) {

        }

        //stop operation, clear self queue and disconnect
        void leave() {
            clear();
            //cleanup();
        }

        //cancel all operation
        /*void cleanup() {
            if (0) {
                logger.log(, info); << "Connection close, performing cleanup\n";
                state = false; //lock pusher
                endpoint = "";
                guildid = "";
                _token = "";
                session = "";
                user_id = "";
                key.clear();

                cts.cancel();
                if (connect) {
                    logger.log(, info); << "Waiting for heartbeat thread to exit\n";
                    t.wait();
                }

                
                if (running) {
                    std::string str = "Stop player\n";
                    logger.log(, info); << str;
                    p_cts.cancel();
                    playing.wait();
                    p_cts = concurrency::cancellation_token_source();
                    p_token = p_cts.get_token();
                }
                else {
                    std::string str = "Player is not running\n";
                    logger.log(, info); << str;
                }

                logger.log(, info); << "Close udp socket\n";
                udpclient.cleanup();
                //pusher.wait();

                //reset token
                cts = concurrency::cancellation_token_source();
                token = cts.get_token();

                //reset play token

                websocketpp::lib::error_code ec;
                c.ping(hdl, "", ec);
                if (ec) {
                    logger.log(, info); << "Handle ping failed because: " << ec.message() << std::endl;
                    return;
                }
                else {
                    logger.log(, info); << "Connection is still alive, killing it\n";
                }
                client::connection_ptr con_ptr = c.get_con_from_hdl(hdl);
                con_ptr->close(websocketpp::close::status::service_restart, "", ec);
                if (ec) {
                    logger.log(, info); << "Can not close connection because: " << ec.message() << std::endl;
                }
                while (connect) {
                    logger.log(, info); << "Waiting for close handshake\n";
                    utils::sleep(50);
                }

                //connect = false;
            }
            else {
                //logger.log(, info); << "No connection exist. Skip clean up\n";
                return;
            }
        }*/
        
        void updateHandler(client* gatewayclient, websocketpp::connection_hdl gatewayhdl) {
            this->gatewayclient = gatewayclient;
            this->gatewayhdl = gatewayhdl;
        }

        void start(client* gatewayclient, websocketpp::connection_hdl gatewayhdl, std::string uri, std::string guildid, std::string _token, std::string session, std::string user_id, std::queue<std::string>* disconnect_queue) {
            auto shared_running = std::make_shared<bool*>(&running);
            auto shared_pusher_lock = std::make_shared<bool*>(&is_pusher_locked);
            auto shared_loop = std::make_shared<bool*>(&loop);
            auto shared_nowplaying = std::make_shared<std::string*>(&nowplaying);
            this->endpoint = uri;
            this->guildid = guildid;
            this->_token = _token;
            this->session = session;
            this->user_id = user_id;
            this->gatewayclient = gatewayclient;
            this->gatewayhdl = gatewayhdl;
            uri = R"(wss://)" + uri + R"(/?v=4)";
            websocketpp::lib::error_code ec;
            logger->log("Voice connection established to: " + uri, notification);
            client::connection_ptr con_ptr = c.get_connection(uri, ec);
            if (ec) {
                logger->log("Could not create connection because: " + ec.message(), error);
                return;
            }

            // Note that connect here only requests a connection. No network messages are
            // exchanged until the event loop starts running in the next line.
            c.set_close_handler(bind(&voiceclient::on_close, this, &c, ::_1));
            c.reset();
            c.connect(con_ptr);

            auto s_log = std::make_shared<discordbot::logger*>(logger);
            if (first_time) {
                (*s_log)->log("First time launch voiceclient, start endpoint and pusher", notification);
                first_time = false;
                
                concurrency::create_task([this, s_log] {
                    c.run();
                    (*s_log)->log({"Endpoint created"}, info);
                    });
                pusher = concurrency::create_task([this, shared_running, shared_pusher_lock, s_log, shared_loop, shared_nowplaying, disconnect_queue] {
                    int counter = 0;
                    while (1) {
                        int i = 0;
                        if (selfqueue.size() > 0 && state && !(**shared_running) && !(**shared_pusher_lock)) {
                            counter = 0;
                            i = 1;
                            (*s_log)->log("[Pusher] Push video " + selfqueue.front(), notification);
                            this->playing = play(selfqueue.front());
                            selfqueue.pop();
                            (*s_log)->log("[Pusher] Sleep 50ms", info);
                            utils::sleep(50);
                            (*s_log)->log("[Pusher] Player playing, wait for player", info);
                            while (**shared_running) {
                                utils::sleep(50);
                            }
                            (*s_log)->log("[Pusher] No longer wait for player", info);
                            utils::sleep(50);
                            if (**shared_loop && **shared_nowplaying != "") {
                                (*s_log)->log("[Pusher] Loop on, push last played to queue", info);
                                selfqueue.push(**shared_nowplaying);
                            }
                        }
                        else {
                            if (i == 1) {
                                i = 0;
                                (*s_log)->log("[Pusher] Pusher sleep", info);
                                if (selfqueue.size() <= 0) (*s_log)->log({ "[Pusher] Queue Size <= 0" }, info);
                                if (state == false) (*s_log)->log("[Pusher] State = false", info);
                                if (**shared_running) (*s_log)->log("Player is running", info);
                                if (**shared_pusher_lock) (*s_log)->log("Pusher locked", info);
                                utils::sleep(50);
                            }
                            if (state) {
                                counter++;
                            }
                            else {
                                counter = 0;
                            }
                            //std::cout << counter << std::endl;
                        }
                        if (counter >= 1800) {
                            disconnect_queue->push(this->guildid);
                            counter = 0;
                        }
                        utils::sleep(50);
                    }
                });
            } 
            else {
                if (pusher.is_done()) { //somehow the thread is terminated, unknown reason
                    (*s_log)->log("Somehow pusher is terminated, start it again", info);
                    //auto s_log = std::make_shared<discordbot::logger*>(logger);
                    pusher = concurrency::create_task([this, shared_running, shared_pusher_lock, s_log, shared_loop, shared_nowplaying, disconnect_queue] {
                        int counter = 0;
                        while (1) {
                            int i = 0;
                            if (selfqueue.size() > 0 && state && !(**shared_running) && !(**shared_pusher_lock)) {
                                counter = 0;
                                i = 1;
                                (*s_log)->log("[Pusher] Push video " + selfqueue.front(), notification);
                                this->playing = play(selfqueue.front());
                                selfqueue.pop();
                                (*s_log)->log("[Pusher] Sleep 50ms", info);
                                utils::sleep(50);
                                (*s_log)->log("[Pusher] Player playing, wait for player", info);
                                while (**shared_running) {
                                    utils::sleep(50);
                                }
                                (*s_log)->log("[Pusher] No longer wait for player", info);
                                utils::sleep(50);
                                if (**shared_loop && **shared_nowplaying != "") {
                                    (*s_log)->log("[Pusher] Loop on, push last played to queue", info);
                                    selfqueue.push(**shared_nowplaying);
                                }
                            }
                            else {
                                if (i == 1) {
                                    i = 0;
                                    (*s_log)->log("[Pusher] Pusher sleep", info);
                                    if (selfqueue.size() <= 0) (*s_log)->log({ "[Pusher] Queue Size <= 0" }, info);
                                    if (state == false) (*s_log)->log("[Pusher] State = false", info);
                                    if (**shared_running) (*s_log)->log("Player is running", info);
                                    if (**shared_pusher_lock) (*s_log)->log("Pusher locked", info);
                                    utils::sleep(50);
                                }
                                if (state) {
                                    counter++;
                                }
                                else {
                                    counter = 0;
                                }
                                //std::cout << counter << std::endl;
                            }
                            if (counter >= 35555) {
                                disconnect_queue->push(this->guildid);
                                counter = 0;
                            }
                            utils::sleep(50);
                        }
                        });
                }
            }
        }

        concurrency::task<void> play(std::string id) {
            running = true;
            auto shared_nowplaying = std::make_shared<std::string*>(&nowplaying);
            auto shared_token = std::make_shared<concurrency::cancellation_token*>(&p_token);
            auto shared_running = std::make_shared<bool*>(&running);
            auto s_log = std::make_shared<discordbot::logger*>(logger);
            return concurrency::create_task([this, id, shared_token, shared_running, s_log, shared_nowplaying] {
                speak();
                std::string title = utils::youtubeGetTitle(id);
                **shared_nowplaying = id;
                std::string payload = ":white_flower: Now playing:\n";
                payload = payload + "\"" + title + "\":notes: ";
                utils::sendMsg(payload, default_channel);
                (*s_log)->log("Create audio source", info);
                audio* source = new audio(*s_log, id);
                const unsigned short FRAME_MILLIS = 20;
                const unsigned short FRAME_SIZE = 960;
                const unsigned short SAMPLE_RATE = 48000;
                const unsigned short CHANNELS = 2;
                const unsigned int BITRATE = 100000;
                #define MAX_PACKET_SIZE FRAME_SIZE * 7
                int error;
                (*s_log)->log("Creating opus encoder", info);
                (*s_log)->log("FRAME_MILLIS: " + std::to_string(FRAME_MILLIS), info);
                (*s_log)->log("FRAME_SIZE: " + std::to_string(FRAME_SIZE), info);
                (*s_log)->log("SAMPLE_RATE: " + std::to_string(SAMPLE_RATE), info);
                (*s_log)->log("CHANNELS: " + std::to_string(CHANNELS), info);
                (*s_log)->log("BITRATE: " + std::to_string(BITRATE), info);
                OpusEncoder* encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &error);
                if (error < 0) {
                    (*s_log)->log("Failed to create opus encoder: " + std::string(opus_strerror(error)), severity_level::error);
                    throw "Failed to create opus encoder: " + std::string(opus_strerror(error));
                }
                //error = opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
                //if (error < 0) {
                //    throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                //}
                error = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE));
                if (error < 0) {
                    (*s_log)->log("Failed to set bitrate for opus encoder: " + std::string(opus_strerror(error)), severity_level::error);
                    throw "Failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                }
                error = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(expected_packet_loss));
                if (error < 0) {
                    (*s_log)->log("Failed to set packet loss perc for opus encoder: " + std::string(opus_strerror(error)), severity_level::error);
                    throw "Failed to set packet loss perc for opus encoder: " + std::string(opus_strerror(error));
                }

                if (sodium_init() == -1) {
                    (*s_log)->log("Libsodium initialisation failed", severity_level::error);
                    throw "Libsodium initialisation failed";
                }

                int num_opus_bytes;
                unsigned char* pcm_data = new unsigned char[FRAME_SIZE * CHANNELS * 2];
                opus_int16* in_data;
                std::vector<unsigned char> opus_data(MAX_PACKET_SIZE);

                class timer_event {
                    bool is_set = false;

                public:
                    bool get_is_set() { return is_set; };
                    void set() { is_set = true; };
                    void unset() { is_set = false; };
                };

                timer_event* run_timer = new timer_event();
                run_timer->set();
                concurrency::create_task([run_timer, this, shared_token, s_log] {
                    while (run_timer->get_is_set()) {
                        speak();
                        (*s_log)->log("Speak sent", info);
                        int i = 0;
                        while (i < 15) {
                            utils::sleep(1000);
                            if (run_timer->get_is_set() == false) {
                                (*s_log)->log("Speak packet sender canceled", info);
                                concurrency::cancel_current_task();
                                return;
                            }
                            if ((*shared_token)->is_canceled()) {
                                (*s_log)->log("Speak packet sender canceled", info);
                                concurrency::cancel_current_task();
                                return;
                            }
                        }
                    }});
                std::deque<std::string>* buffer = new std::deque<std::string>();
                auto shared_offset = std::make_shared<long long*>(&offset);
                auto timer = concurrency::create_task([run_timer, this, buffer, FRAME_MILLIS, shared_token, shared_offset, shared_running, s_log] {
                    while (run_timer->get_is_set() || buffer->size() > 0) {
                        utils::sleep(5 * FRAME_MILLIS);
                        **shared_running = true;
                        int sent = 0;
                        auto start = boost::chrono::high_resolution_clock::now();
                        while (buffer->size() > 0) {
                            if (udpclient.send(buffer->front()) != 0) {
                                (*s_log)->log("UDP error, stop player", severity_level::error);
                                **shared_running = false;
                                return;
                            }
                            buffer->pop_front();
                            if ((*shared_token)->is_canceled()) {
                                (*s_log)->log("Player  canceled", info);
                                **shared_running = false;
                                concurrency::cancel_current_task();
                            }
                            sent++;
                            long long next_time = (long long)(sent+1) * (long long)(FRAME_MILLIS) * 1000 ;
                            auto now = boost::chrono::high_resolution_clock::now();
                            long long mcs_elapsed = (boost::chrono::duration_cast<boost::chrono::microseconds>(now - start)).count(); // elapsed time from start loop
                            long long delay = std::max((long long)0, (next_time - mcs_elapsed));
                            utils::timerSleep(delay*10e-6); //sleep microseconds                           
                        }    
                        **shared_running = false;
                    }
                    });
                unsigned short _sequence = 0;
                unsigned int _timestamp = 0;
                int totalsize = 0;
                int cached_expected_packet_loss = 0;
                while (1) {
                    if (buffer->size() >= 20) {
                        utils::sleep(FRAME_MILLIS);
                    }
                    if (cached_expected_packet_loss != expected_packet_loss) {
                        cached_expected_packet_loss = expected_packet_loss;
                        error = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(expected_packet_loss));
                        if (error < 0) {
                            (*s_log)->log("Failed to set packet loss perc for opus encoder: " + std::string(opus_strerror(error)), severity_level::error);
                            throw "Failed to set packet loss perc for opus encoder: " + std::string(opus_strerror(error));
                        }
                    }
                    if (source->read((char*)pcm_data, FRAME_SIZE * CHANNELS * 2) != true)
                        break;
                    if ((*shared_token)->is_canceled()) {
                        (*s_log)->log("Encoder canceled", info);
                        break;
                    }

                    in_data = reinterpret_cast<opus_int16*>(pcm_data);
                    //in_data = (opus_int16*)pcm_data;
                    num_opus_bytes = opus_encode(encoder, in_data, FRAME_SIZE, opus_data.data(), MAX_PACKET_SIZE);
                    if (num_opus_bytes <= 0) {
                        (*s_log)->log("failed to encode frame: " + std::string(opus_strerror(num_opus_bytes)), severity_level::error);
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
                    _timestamp += SAMPLE_RATE / 1000 * FRAME_MILLIS; //48000Hz / 1000 * 20(ms)
                    
                    if (_sequence < 10) { //skip first 10 frames
                        continue;
                    }
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
                    totalsize += msg.length();                    
                    buffer->push_back(msg);
                }
                (*s_log)->log("Total size: " + std::to_string(totalsize), info);
                (*s_log)->log("Unset timer", info);
                run_timer->unset();
                (*s_log)->log("Wait for player exit", info);
                timer.wait();   
                unspeak();
                (*s_log)->log("Unspeack packet sent", info);
                **shared_running = false;
                delete run_timer;
                delete buffer;
                (*s_log)->log("Destroy encoder", info);
                opus_encoder_destroy(encoder);

                delete[] pcm_data;

                });
        }

        void disconnect(std::string guildid, client* c, websocketpp::connection_hdl hdl) {
                std::string payload = R"({"op": 4,"d": {"guild_id": ")";
                payload += guildid;
                payload += R"(","channel_id": ")";
                payload += "null";
                payload += R"(","self_mute": true,"self_deaf": true}})";
                websocketpp::lib::error_code ec;
                c->send(hdl, payload, websocketpp::frame::opcode::text, ec);
                logger->log({ "Disconnect packet sent" }, info);
                return;
        }

        voiceclient(std::string guildid) {
            this->guildid = guildid;
            std::string logfile = "voice_" + guildid;
            this->logger = new discordbot::logger(logfile);
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
                c.set_close_handler(bind(&voiceclient::on_close, this, &c, ::_1));
                // while (1) {
                //c.reset();
                c.start_perpetual();
            }
            catch (websocketpp::exception const& e) {
                logger->log({ e.what() }, error);
            }
        }
    };
    class gatewayclient {
    private:
        std::queue<std::string> disconnect_queue;
        logger* logger;
    public:
        std::unordered_map<std::string, std::unordered_map<std::string, querryqueue*>> mainqueue; //[guild_id][user_id] -> querrydata
        std::unordered_map<std::string, voiceclient*> voicegroup;  // guild id ~~ voice endpoint
        std::unordered_map<std::string, std::unordered_map<std::string, userinfo*>> usergroup;  //[guild_id][user_id]->userinfor*
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

        void auth(websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            std::string auth_str = "{\"op\":2,\"d\":{\"token\":\"";
            auth_str += str_token;
            auth_str += "\",\"intents\":1664,\"properties\":{\"$os\":\"window\",\"$browser\":\"\",\"$device\":\"\"}}}";
            c->send(hdl, auth_str, websocketpp::frame::opcode::text, ec);
            logger->log({ "Authorize packet sent" }, info);
            if (ec) {
                logger->log("Authorization failed because: " + ec.message(), error);
            }
            return;
        }

        void resume(discordbot::gatewayclient* a, websocketpp::connection_hdl hdl, client* c) {
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
            c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                logger->log("Resume failed because: " + ec.message(), error);
                return;
            }
            logger->log("Resume packet sent with payload: " + resume.dump(), info);
            is_websocket_restart = false; //reset flag
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
            auto s_log = std::make_shared<discordbot::logger*>(logger);
            concurrency::cancellation_token this_is_token = *token;
            return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, this_is_token, s_log]
                {
                    //check is task is canceled
                    if (this_is_token.is_canceled()) {
                        concurrency::cancel_current_task();
                    }
                    else {
                        int i = 0;
                        while (*shared_heartbeat == 50) {
                            //server not provide heartbeat interval wait a bit
                            std::this_thread::sleep_for(std::chrono::milliseconds(*shared_heartbeat));
                            (*s_log)->log("Wait for heartbeat interval", info);
                            i++;
                            utils::sleep(500);
                            if (i > 5) {
                                (*s_log)->log("Wait for heartbeat interval for too long", error);
                                break;
                            }
                        }

                        //convert shared shared client pointer to local client 
                        auto client = *shared_client_context;
                        websocketpp::lib::error_code ec;
                        std::string payload = gatewayclient::getHeartBeatPayload(**shared_seq_num);
                        client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                        if (!ec) {
                            while (!ec) {
                                int localhb = *shared_heartbeat;
                                while (localhb) {
                                    if (this_is_token.is_canceled()) {
                                        (*s_log)->log("Heartbeat canceled", info);
                                        concurrency::cancel_current_task();
                                    }
                                    utils::sleep(50);
                                    localhb -= 50;
                                }
                                //sleep(*shared_heartbeat);
                                payload = gatewayclient::getHeartBeatPayload(**shared_seq_num);
                                client->send(*shared_hdl, payload, websocketpp::frame::opcode::text, ec);
                                (*s_log)->log("Heartbeat sent with payload: " + payload, info);
                            }
                        }
                        else {
                            (*s_log)->log("Heartbeat failed because: " + ec.message(), error);
                        }
                    }
                });
        }

        concurrency::task<void> on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
            auto s_is_restart = std::make_shared<bool*>(&is_websocket_restart);
            auto s_hbi = std::make_shared<int>(heartbeat_interval);
            auto s_token = std::make_shared<concurrency::cancellation_token*>(&token);
            auto s_cts = std::make_shared<concurrency::cancellation_token_source*>(&cts);
            auto s_sn = std::make_shared<int*>(&seq_num);
            auto s_ready = std::make_shared<json*>(&ready);
            auto s_t = std::make_shared<concurrency::task<void>*>(&t);
            auto s_log = std::make_shared<discordbot::logger*>(logger);
            return concurrency::create_task([c, hdl, msg, s_is_restart, s_hbi, s_token, s_sn, s_ready, s_t, s_cts, this, s_log] {
                std::ostringstream str;
                str << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload();
                (*s_log)->log(str.str(), info); 
                std::string str_msg = msg->get_payload();
                json js_msg;
                js_msg = json::parse(str_msg);
                int opcode = -1;
                if (js_msg["op"].is_number_integer()) {
                    opcode = js_msg["op"];
                }

                switch (opcode) {
                case 9: //invalid session
                    //just restart
                    **s_is_restart = false;
                    utils::restart(hdl, c, *s_cts, *s_token, *s_t);
                    break;
                case 10: //Hello packet
                    (*s_log)->log("Discord opcode: 10", info);
                    if (js_msg["d"]["heartbeat_interval"].is_number_integer()) {
                        *s_hbi = js_msg["d"]["heartbeat_interval"];
                        (*s_log)->log("Heartbeat interval: " + std::to_string(*s_hbi), info);
                    }
                    else {
                        (*s_log)->log("Can't parse heartbeart interval from messages:" + msg->get_payload(), error);
                        (*s_log)->log("Using default heartbeat interval: " + std::to_string(*s_hbi), error);
                    }
                    //cts = concurrency::cancellation_token_source a;
                    **s_t = heartBeat(hdl, c, *s_hbi, *s_is_restart, *s_token);
                    break;
                case 11: //Heartbeat ACK
                    (*s_log)->log("Discord opcode: 11", info);
                    (*s_log)->log("Heartbeat ACK", info);
                    while (!disconnect_queue.empty()) {
                        websocketpp::lib::error_code ec;
                        c->send(hdl, utils::getVoiceStateUpdatePayload(disconnect_queue.front(), "null"), websocketpp::frame::opcode::text, ec);
                        if (ec) {
                            (*s_log)->log("Can not send msg because: " + ec.message(), error);
                        }
                        else {
                            (*s_log)->log("Inactive for too long, disconnect", info);
                            utils::sendMsg("I left the voice channel because i have been inactive for too long", voicegroup[disconnect_queue.front()]->getDefaultChannel());
                            disconnect_queue.pop();
                        }
                        utils::sleep(2000);
                    }
                    break;
                case 7: //Request reconnect
                    **s_is_restart = true;
                    utils::restart(hdl, c, *s_cts, *s_token, *s_t);
                    break;
                case 0:
                    (*s_log)->log("Discord opcode: 0", info);
                    (*s_log)->log("Discord event name:" + (std::string)js_msg["t"], info);

                    //update SEQUENCE NUMBER
                    if (js_msg["s"].is_number_integer()) {
                        **s_sn = js_msg["s"];
                    }
                    else {
                        (*s_log)->log("Can not parse sequense number from message:" + msg->get_payload(), error);
                    }


                    //begin EVENT logic

                    std::string event;
                    if (js_msg["t"].is_string()) {
                        event = js_msg["t"];
                    }
                    else {
                        (*s_log)->log("Can not parse discord event from message:" + msg->get_payload(), error);
                    }

                    if (event == R"(READY)") { //receive ready packet 
                        **s_ready = js_msg; //cache ready data
                        break;
                    }
                    if (event == R"(VOICE_STATE_UPDATE)") { //VOICE STATE UPDATE EVENT
                        //if (utils::isSelf(js_msg, **s_ready)) 
                        std::string userid = "";
                        if (js_msg["d"]["user_id"].is_string()) {
                            userid = js_msg["d"]["user_id"];
                        }
                        else (*s_log)->log("Can not parse userid from message: " + msg->get_payload(), error);
                        std::string guild = "";
                        if (js_msg["d"]["guild_id"].is_string()) {
                            guild = js_msg["d"]["guild_id"];
                        }
                        else (*s_log)->log("Can not parse guild id from message:" + msg->get_payload(), error);
                        std::string channel = "";
                        if (js_msg["d"]["channel_id"].is_string()) {
                            channel = js_msg["d"]["channel_id"].is_string();
                        }
                        if (userid == ready["d"]["user"]["id"] && channel == "") { //disconnect packet
                            if (voicegroup.find(guild) != voicegroup.end()) { //endpoint exist
                                //voicegroup[guild]->cleanup();
                            }
                        }
                        usergroup[guild][userid] = new userinfo;
                        usergroup[guild][userid]->update(js_msg, logger);
                        break;
                    }
                    if (event == R"(VOICE_SERVER_UPDATE)") {
                        std::string _token = "";
                        std::string guildid = "";
                        std::string endpoint = "";
                        if (js_msg["d"]["token"].is_string() && js_msg["d"]["guild_id"].is_string() && js_msg["d"]["endpoint"].is_string()) {
                            _token = js_msg["d"]["token"];
                            guildid = js_msg["d"]["guild_id"];
                            endpoint = js_msg["d"]["endpoint"];
                            (*s_log)->log("Successful get token id: " + _token, info);
                            (*s_log)->log("Successful get guild id: " + guildid, info);
                            (*s_log)->log("Successful get endpoint: " + endpoint, info);
                        }
                        else {
                            (*s_log)->log("Can not parse: token, guild id, endpoint ", error);
                            break;
                        }
                        int i = 1;
                        std::string selfid = ready["d"]["user"]["id"];                  
                        while (1) {
                            (*s_log)->log("Waiting for session id " + std::to_string(i), info);
                            if (usergroup.find(guildid) == usergroup.end()) { 
                            }
                            else {
                                if (usergroup[guildid].find(selfid) == usergroup[guildid].end()) {
                                }
                                else {
                                    if (usergroup[guildid][selfid]->sessionid != "") {
                                        (*s_log)->log("Found valid session id: " + usergroup[guildid][selfid]->sessionid, info);
                                        break;
                                    }
                                }
                            }
                            utils::sleep(1000);

                            i++;
                            if (i > 5) {
                                break;
                            }
                        }
                        if (i > 5) {
                            (*s_log)->log("Fail to wait for session id\n", error);
                            break;
                        }
                        else {
                            if (voicegroup.find(guildid) == voicegroup.end()) { //not exist
                                (*s_log)->log("Start voice connection with endpoint: " + endpoint + ", guild ID: " + guildid + ", session ID: " + usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, info);
                                voicegroup[guildid] = new voiceclient(guildid);
                                voicegroup[guildid]->setCurrentVoiceChannel(usergroup[guildid][selfid]->get_voice_channel_id());
                                voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][selfid]->sessionid, selfid, &disconnect_queue);
                            }
                            else { //exist
                                //stop old connection and start a new connection
                                (*s_log)->log("Exist voice connection with endpoint: " + endpoint + ", guild ID: " + guildid + ", session ID: " + usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, info);
                                //voicegroup[guildid]->cleanup();
                                voicegroup[guildid]->setCurrentVoiceChannel(usergroup[guildid][selfid]->get_voice_channel_id());
                                voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, selfid, &disconnect_queue);
                            }
                        }
                        break;
                    }               

                    if (event == R"(MESSAGE_CREATE)") { //MSG CREATE EVENT
                        std::string channel = ""; //CHANNEL ID
                        if (js_msg["d"]["channel_id"].is_string()) {
                            channel = js_msg["d"]["channel_id"];
                            (*s_log)->log("Parsed channel id from message, channel id:" + channel, info);
                        }
                        else {
                            (*s_log)->log("Can not parse channel id from message:" + msg->get_payload(), error);
                        }

                        std::string guild = ""; //GUILD ID
                        if (js_msg["d"]["guild_id"].is_string()) {
                            guild = js_msg["d"]["guild_id"];
                            (*s_log)->log("Parsed guild id from message, guild id:" + guild, info);
                        }
                        else {
                            (*s_log)->log("Can not parse guild id from message:" + msg->get_payload(), error);
                        }

                        std::string content = ""; //MSG CONTENT
                        if (js_msg["d"]["content"].is_string()) {
                            content = js_msg["d"]["content"];
                            (*s_log)->log("Parsed message content from message, message content:" + content, info);
                        }
                        else {
                            (*s_log)->log("Can not parse content from message:" + msg->get_payload(), error);
                        }

                        std::string userid = ""; //AUTHOR ID
                        if (js_msg["d"]["author"]["id"].is_string()) {
                            userid = js_msg["d"]["author"]["id"];
                            (*s_log)->log("Parsed author id from message, author id:" + userid, info);
                        }
                        else (*s_log)->log("Can not parse userid from message: " + msg->get_payload(), error);

                        if (js_msg["d"]["mentions"][0]["id"] == (**s_ready)["d"]["user"]["id"]) {  //mention
                            utils::sendMsg("Why mention me? I won't show you my prefix is ?", channel);
                            (*s_log)->log(js_msg["d"]["mentions"][0]["id"], info);
                            (*s_log)->log((**s_ready)["d"]["user"]["id"], info);
                            break;
                        }
                        if (content == "ping" || content == "Ping") { //ping command
                            int RTT = utils::ping("162.159.136.232"); //discord IP: 162.159.136.232
                            (*s_log)->log("RTT: " + std::to_string(RTT), info);
                            std::string msg = "Pong! ";
                            msg += std::to_string(RTT);
                            msg += "ms";
                            utils::sendMsg(msg, channel);
                            break;
                        }
                        if (content == "Hello" || content == "hello") { //Hello
                            utils::sendMsg("Hi!", channel);
                            break;
                        }
                        if (content == "restart" || content == "Restart") { //restart command
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

                        if (content == "?join") { //join command
                            if (usergroup.find(guild) != usergroup.end()) {
                                if (usergroup[guild].find(userid) != usergroup[guild].end()) {
                                    if (usergroup[guild][userid]->get_voice_channel_id() != "") {
                                        websocketpp::lib::error_code ec;
                                        if (voicegroup.find(guild) == voicegroup.end()) { //not exist
                                            voicegroup[guild] = new voiceclient(guild);
                                            voicegroup[guild]->setDefaultChannel(channel);
                                        }
                                        else { //exist
                                            //stop old connection and start a new connection
                                            //(*s_log)->log(, info); << "Exist voice connection with endpoint: " << endpoint << ", guild ID: " << guildid << ", session ID: " << usergroup[guildid][ready["d"]["user"]["id"]]->sessionid << std::endl;
                                            voicegroup[guild]->setDefaultChannel(channel);
                                            //voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, ready["d"]["user"]["id"]);
                                        }
                                        c->send(hdl, utils::getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                        if (ec) {
                                            (*s_log)->log("Can not send voice state update payload because: " + ec.message(), error);
                                            break;
                                        }
                                    }
                                    else {
                                        std::string payload = "You are not in voice channel!";
                                        utils::sendMsg(payload, channel);
                                        break;
                                    }
                                }
                                else {
                                    std::string payload = "```Internal error, please rejoin voice channel```";
                                    utils::sendMsg(payload, channel);
                                    break;
                                }
                            }
                            else {
                                std::string payload = "```Internal error, please rejoin voice channel```";
                                utils::sendMsg(payload, channel);
                                break;
                            }
                            break;
                        }

                        if (content == "?leave") {
                            if (voicegroup.find(guild) == voicegroup.end()) {
                                std::string payload = "Not currently in any voice channel";
                                utils::sendMsg(payload, channel);
                            }
                            else {
                                voicegroup[guild]->clear();
                                utils::sendMsg("Got it", channel);
                                websocketpp::lib::error_code ec;
                                std::string payload = utils::getVoiceStateUpdatePayload(guild, "null");
                                c->send(hdl, payload, websocketpp::frame::opcode::text);
                                if (ec) {
                                    (*s_log)->log("Cannot send voice update payload because: " + ec.message(), error);
                                }
                            }
                            break;
                        }

                        if (content == "?loop") {
                            if (voicegroup.find(guild) != voicegroup.end()) {
                                if (voicegroup[guild]->_loop()) {
                                    utils::sendMsg("Loop `on`", channel);
                                }
                                else {
                                    utils::sendMsg("Loop `off`", channel);
                                }
                            }
                            else {
                                utils::sendMsg("You must play music at lease one time to be able to config", channel);
                            }
                        }

                        if (content == "1" || content == "2" || content == "3" || content == "4" || content == "5") {
                            int selected = std::stoi(content);
                            if (mainqueue.find(guild) == mainqueue.end()) { //Not found key in database
                                break;
                            }
                            else if (mainqueue[guild].find(userid) == mainqueue[guild].end()) { // Not found key in database
                                break;
                            }
                            else {
                                querryqueue* temp = mainqueue[guild][userid];
                                if (temp->is_avaiable()) { //Ye object has data
                                    if (temp->channelid == channel && temp->guildid == guild) { //channel and guild match + userid match -> match
                                        if (voicegroup.find(guild) == voicegroup.end()) { //voice endpoint not exist
                                            break;
                                            (*s_log)->log("Endpoint for guild id " + guild + " not exist\n", info);
                                        }
                                        else {
                                            if (usergroup.find(guild) != usergroup.end()) {
                                                if (usergroup[guild].find(userid) != usergroup[guild].end()) {
                                                    if (usergroup[guild][userid]->get_voice_channel_id() != "") {
                                                        if (usergroup[guild][userid]->get_voice_channel_id() != voicegroup[guild]->getCurrentVoiceChannel()) {
                                                            (*s_log)->log("User request music is in different voice channel with target voice endpoint, expected change", info);
                                                            voicegroup[guild]->lockPusher();
                                                            voicegroup[guild]->selfqueue.push(temp->data[selected - 1]);
                                                            if (utils::deleteMsg(*s_log, temp->gettMessageId(), channel)) {
                                                                (*s_log)->log("Deleted print result message", info);
                                                            }
                                                            (*s_log)->log("User " + userid + " selected video " + temp->data[selected - 1], info);
                                                        }
                                                        else {
                                                            if (utils::deleteMsg(*s_log, temp->gettMessageId(), channel)) {
                                                                (*s_log)->log("Deleted print result message", info);
                                                            }
                                                            voicegroup[guild]->selfqueue.push(temp->data[selected - 1]);
                                                            (*s_log)->log("User " + userid + " selected video " + temp->data[selected - 1], info);
                                                        }
                                                        websocketpp::lib::error_code ec;
                                                        c->send(hdl, utils::getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                                        if (ec) {
                                                            (*s_log)->log("Can not send voice state update payload because: " + ec.message(), error);
                                                        }
                                                    }
                                                    else {
                                                        std::string payload = "You are not in any voice channel!";
                                                        utils::sendMsg(payload, channel);
                                                    }
                                                }
                                                else {
                                                    std::string payload = "```Internal error, please rejoin voice channel```";
                                                    utils::sendMsg(payload, channel);
                                                }
                                            }
                                            else {
                                                std::string payload = "```Internal error, please rejoin voice channel```";
                                                utils::sendMsg(payload, channel);
                                            }
                                            mainqueue[guild][userid]->reset();
                                        }
                                    }
                                    else break;
                                }
                                else break;
                            }
                            break;
                        }

                        if (utils::isStartWith(content, "?p")) {
                            //parse string
                            if (voicegroup.find(guild) == voicegroup.end())  {
                                (*s_log)->log("Create voice endpoint for guild: " + guild, info);
                                voicegroup[guild] = new voiceclient(guild);
                                voicegroup[guild]->setDefaultChannel(channel);
                            }
                            else {
                                (*s_log)->log("Voice endpoint already exist", info);
                                voicegroup[guild]->setDefaultChannel(channel);
                            }
                            if (content[2] != ' ') {
                                utils::sendMsg("```Invalid paramenter, usage: ?p <string>```", channel);
                                return;
                            }
                            std::string querry = content.erase(0, 3);
                            boost::smatch result;
                            if (boost::regex_search(querry, result, boost::regex(R"(https:\/\/www\.youtube.com\/watch\?v=(.{11}))"),boost::match_default)) {
                                std::string id = result[1].str();
                                if (usergroup.find(guild) != usergroup.end()) {
                                    if (usergroup[guild].find(userid) != usergroup[guild].end()) {
                                        if (usergroup[guild][userid]->get_voice_channel_id() != "") {
                                            if (usergroup[guild][userid]->get_voice_channel_id() != voicegroup[guild]->getCurrentVoiceChannel()) {
                                                (*s_log)->log("User request music is in different voice channel with target voice endpoint, expected change", info);
                                                voicegroup[guild]->lockPusher();
                                                voicegroup[guild]->selfqueue.push(id);
                                                (*s_log)->log("User " + userid + " selected video " + id, info);
                                            }
                                            else {
                                                voicegroup[guild]->selfqueue.push(id);
                                                (*s_log)->log("User " + userid + " selected video " + id, info);
                                            }
                                            websocketpp::lib::error_code ec;
                                            c->send(hdl, utils::getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                            if (ec) {
                                                (*s_log)->log("Can not send voice state update payload because: " + ec.message(), error);
                                            }
                                        }
                                        else {
                                            std::string payload = "You are not in any voice channel!";
                                            utils::sendMsg(payload, channel);
                                        }
                                    }
                                    else {
                                        std::string payload = "```Internal error, please rejoin voice channel```";
                                        utils::sendMsg(payload, channel);
                                    }
                                }
                                else {
                                    std::string payload = "```Internal error, please rejoin voice channel```";
                                    utils::sendMsg(payload, channel);
                                }
                                break;
                            }


                            //https://youtu.be/XFc_pWUp41k
                            if (boost::regex_search(querry, result, boost::regex(R"(https:\/\/youtu\.be\/(.{11}))"), boost::match_default)) {
                                std::string id = result[1].str();
                                if (usergroup.find(guild) != usergroup.end()) {
                                    if (usergroup[guild].find(userid) != usergroup[guild].end()) {
                                        if (usergroup[guild][userid]->get_voice_channel_id() != "") {
                                            if (usergroup[guild][userid]->get_voice_channel_id() != voicegroup[guild]->getCurrentVoiceChannel()) {
                                                (*s_log)->log("User request music is in different voice channel with target voice endpoint, expected change", info);
                                                voicegroup[guild]->lockPusher();
                                                voicegroup[guild]->selfqueue.push(id);
                                                (*s_log)->log("User " + userid + " selected video " + id, info);
                                            }
                                            else {
                                                voicegroup[guild]->selfqueue.push(id);
                                                (*s_log)->log("User " + userid + " selected video " + id, info);
                                            }
                                            websocketpp::lib::error_code ec;
                                            c->send(hdl, utils::getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                            if (ec) {
                                                (*s_log)->log("Can not send voice state update payload because: " + ec.message(), error);
                                            }
                                        }
                                        else {
                                            std::string payload = "You are not in any voice channel!";
                                            utils::sendMsg(payload, channel);
                                        }
                                    }
                                    else {
                                        std::string payload = "```Internal error, please rejoin voice channel```";
                                        utils::sendMsg(payload, channel);
                                    }
                                }
                                else {
                                    std::string payload = "```Internal error, please rejoin voice channel```";
                                    utils::sendMsg(payload, channel);
                                }
                                break;
                            }

                            if (querry == "") {
                                utils::sendMsg("```Please provide paramenter, usage: ?p <string>```", channel);
                            }
                            else {                                
                                //sendMsg(querry, channel);
                                json result = utils::youtubePerformQuerry(querry);
                                (*s_log)->log("Querry result for keyword: " + querry, info);
                                //(*s_log)->log(, info); << result.dump() << std::endl;
                                if (mainqueue.find(guild) != mainqueue.end()) {
                                    if (mainqueue[guild].find(userid) != mainqueue[guild].end()) {
                                        if (mainqueue[guild][userid]->gettMessageId() != "") {
                                            utils::deleteMsg(*s_log, mainqueue[guild][userid]->gettMessageId(), channel);
                                        }
                                    }
                                }
                                mainqueue[guild][userid] = new querryqueue(logger);
                                mainqueue[guild][userid]->push(js_msg, result);
                                //(*s_log)->log("Cached search querry", info);
                                json response = utils::youtubePrintSearchResult(result, querry, channel);
                                if (response["id"].is_string()) {
                                    std::string id = response["id"];
                                    mainqueue[guild][userid]->setMessageId(response["id"]);
                                    //utils::addEmoji();
                                }
                                else {
                                    (*s_log)->log("Can not get message id (youtubePrintSearchResult)", error);
                                }                                
                               //std::cout << response.dump(4);
                            }
                            break;
                        }

                        std::string value = "";
                        if (utils::parse(&value, "loss", content) || utils::parse(&value, "Loss", content)) {
                            if (voicegroup.find(guild) == voicegroup.end()) {
                                utils::sendMsg("You must play music at lease 1 time to be able to config.", channel);
                                break;
                            }
                            std::string num = "";
                            if (value == "?") {
                                std::string payload = "Current expected loss: " + std::to_string(voicegroup[guild]->expected_packet_loss);
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
                            if (stoi(num) >= 100 || stoi(num) < 0) {
                                std::string payload = "Paramenter must be between 0 and 99";
                                utils::sendMsg(payload, channel);
                                break;
                            }
                            websocketpp::lib::error_code ec;
                            std::string payload = "Changed expected loss to " + std::to_string((unsigned short)stoi(value)) + ". Old value: " + std::to_string(voicegroup[guild]->expected_packet_loss);
                            voicegroup[guild]->expected_packet_loss = (unsigned short)stoi(num);
                            utils::sendMsg(payload, channel);
                            break;
                        }

                        value = "";
                        if (utils::parse(&value, "offset", content)) {
                            if (voicegroup.find(guild) == voicegroup.end()) {
                                utils::sendMsg("You must play music at lease 1 time to be able to config", channel);
                                break;
                            }
                            std::string num = "";
                            if (value == "?") {
                                std::string payload = "Current offset: " + std::to_string(voicegroup[guild]->offset);
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
                            std::string payload = "Changed offset to " + std::to_string((unsigned short)stoi(value)) + ". Old value: " + std::to_string(voicegroup[guild]->offset);
                            voicegroup[guild]->offset = (unsigned short)stoi(value);
                            utils::sendMsg(payload, channel);
                            break;
                        }

                        value = "";
                        if (utils::parse(&value, "Offset", content)) {
                            if (voicegroup.find(guild) == voicegroup.end()) {
                                utils::sendMsg("You must play music at lease 1 time to be able to config.", channel);
                                break;
                            }
                            std::string num = "";
                            if (value == "?") {
                                std::string payload = "Current offset: " + std::to_string(voicegroup[guild]->offset);
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
                            std::string payload = "Changed offset to " + std::to_string((unsigned short)stoi(value)) + ". Old value: " + std::to_string(voicegroup[guild]->offset);
                            voicegroup[guild]->offset = (unsigned short)stoi(value);
                            utils::sendMsg(payload, channel);
                            break;
                        }
                    }
                }
                });
        }

        void on_close(client* c, websocketpp::connection_hdl hdl) {
            c->get_alog().write(websocketpp::log::alevel::app, "Connection Closed");
            std::ostringstream str;
            str << "Connection closed on hdl: " << hdl.lock().get();
            logger->log(str.str(), info);
            std::string uri = "wss://gateway.discord.gg/?v=8&encoding=json";
            websocketpp::lib::error_code ec;
            client::connection_ptr con_ptr = c->get_connection(uri, ec);
            if (ec) {
                logger->log("Could not create connection because: " + ec.message(), error);
                return;
            }
            c->connect(con_ptr);
        }

        gatewayclient() {
            this->logger = new discordbot::logger("gateway");
            client c;
            std::string uri = R"(wss://gateway.discord.gg/?v=8&encoding=json)";
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
                    logger->log("Could not create connection because: " + ec.message(), error);
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
                logger->log(e.what(), info);
            }
        }
    };
};

int main() {
    std::ifstream info("info.txt");
    info >> str_token;
    SetConsoleOutputCP(CP_UTF8);
    boost::log::register_simple_formatter_factory<severity_level, char>("Severity");
    boost::log::add_common_attributes();
    discordbot::gatewayclient gw; //blocking call
}