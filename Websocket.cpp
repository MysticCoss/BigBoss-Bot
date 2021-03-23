﻿#pragma once
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
boost::asio::io_context context_io;
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

struct userinfo {
    std::string userid = "";
    std::string guildid = "";
    std::string channelid = "";
    std::string sessionid = "";
    void update(json data) {
        if (data["d"]["channel_id"].is_null()) {
            channelid = "";
        }
        else channelid = data["d"]["channel_id"];
        userid = data["d"]["user_id"];
        guildid = data["d"]["guild_id"];
        sessionid = data["d"]["session_id"];
        std::cout << "Update cache data for user " << userid << " in guild " << guildid << ": Channel ID = " << (channelid == "" ? "NULL" : channelid) << ", session ID = " << sessionid << std::endl;
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
        bool first_time = true;
        bool running = false; //set true: when player is playing.
        bool connect = false; //set true: when encrypt key received. set false: when close handshake; usage: indicate connection status
        bool state = false;   //set true: when encrypt key received. set false: when cleanup; usage: to lock operate of pusher
        int expected_packet_loss = 20;
        long long offset = 10;
        int ssrc = 0;
        int heartbeat_interval = 0;
        int seq_num = 0;
        bool is_websocket_restart = false;
        json ready;
        client c;
        std::string default_channel;
        websocketpp::connection_hdl hdl;
        concurrency::cancellation_token_source cts;
        concurrency::cancellation_token token = cts.get_token(); //this is global token specially for cancelling heartbeat func
        concurrency::task<void> t;

        concurrency::cancellation_token_source p_cts;
        concurrency::cancellation_token p_token = p_cts.get_token();
        concurrency::task<void> playing;

        /*void setFrameInterval(std::string interval) {
            FrameInterval = (unsigned short)stoi(interval);
            std::string p = "Changed frame interval: " + FrameInterval;
            std::cout << p;
            return;
        }*/

        void set_default_channel(std::string channel) {
            default_channel = channel;
            return;
        }

        bool is_connect() {
            return connect;
        }

        bool is_running() {
            return running;
        }

        void selectProtocol(websocketpp::connection_hdl hdl, client* c, std::string address, int port) {
            std::string payload = R"({ "op": 1,"d": {"protocol": "udp","data": {"address": ")";
            payload += address += R"(","port": )";
            payload += std::to_string(port);
            payload += R"(, "mode": "xsalsa20_poly1305"}}})";
            std::string p = "Select protocol sent with payload: " + payload + "\n";
            std::cout << p;
            websocketpp::lib::error_code ec;
            c->send(hdl, payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                p = "Select protocol failed because: " + ec.message() + "\n";
                std::cout << p;
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
            std::cout << "Authorizing...\n";
            c->send(hdl, auth_str, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::string p = "Authorization failed because: " + ec.message() + "\n";
                std::cout << p;
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
            std::string p = "Resume payload:" + resume.dump() + "\n";
            std::cout << p;
            c->send(hdl, resume.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                p = "Resume failed because: " + ec.message() + "\n";
                std::cout << p;
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
                std::cout << "Can not send speak message because: " << ec.message() << std::endl;
            }
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
                std::cout << "Can not send unspeak message because: " << ec.message() << std::endl;
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
            //concurrency::cancellation_token this_is_token = *token;
            return concurrency::create_task([shared_heartbeat, shared_hdl, shared_client_context, shared_seq_num, token, this]
                {
                    //check is task is canceled
                    if (token->is_canceled()) {
                        std::cout << "<Voiceclient> Stop heartbeating";
                        concurrency::cancel_current_task();
                        return;
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
                                    if (token->is_canceled()) {
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
            return concurrency::create_task([c, hdl, msg, s_is_restart, s_hbi, s_token, s_sn, s_ready, s_t, s_cts, this] {
                std::cout << "<Voiceclient> on_message called with hdl: " << hdl.lock().get() << " and message: ";
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
                        //std::cout << "oh let's parse key: ";
                        for (int i = 0; i < 32; i++) {
                            key.push_back(js_msg["d"]["secret_key"][i]);
                            //std::cout << i;
                        }
                        state = true;
                        connect = true;
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
            std::cout << "<Voiceclient> Connection closed on hdl: " << hdl.lock().get() << std::endl;
            std::cout << "Connection close, performing cleanup\n";
            state = false; //lock pusher
            endpoint = "";
            guildid = "";
            _token = "";
            session = "";
            user_id = "";
            key.clear();

            cts.cancel();
            if (connect) {
                std::cout << "Waiting for heartbeat thread to exit\n";
                t.wait();
            }


            if (running) {
                std::string str = "Stop player\n";
                std::cout << str;
                p_cts.cancel();
                playing.wait();
                p_cts = concurrency::cancellation_token_source();
                p_token = p_cts.get_token();
            }
            else {
                std::string str = "Player is not running\n";
                std::cout << str;
            }

            std::cout << "Close udp socket\n";
            udpclient.cleanup();
            //pusher.wait();

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
            cleanup();
        }

        //cancel all operation
        void cleanup() {
            if (0) {
                std::cout << "Connection close, performing cleanup\n";
                state = false; //lock pusher
                endpoint = "";
                guildid = "";
                _token = "";
                session = "";
                user_id = "";
                key.clear();

                cts.cancel();
                if (connect) {
                    std::cout << "Waiting for heartbeat thread to exit\n";
                    t.wait();
                }

                
                if (running) {
                    std::string str = "Stop player\n";
                    std::cout << str;
                    p_cts.cancel();
                    playing.wait();
                    p_cts = concurrency::cancellation_token_source();
                    p_token = p_cts.get_token();
                }
                else {
                    std::string str = "Player is not running\n";
                    std::cout << str;
                }

                std::cout << "Close udp socket\n";
                udpclient.cleanup();
                //pusher.wait();

                //reset token
                cts = concurrency::cancellation_token_source();
                token = cts.get_token();

                //reset play token

                websocketpp::lib::error_code ec;
                c.ping(hdl, "", ec);
                if (ec) {
                    std::cout << "Handle ping failed because: " << ec.message() << std::endl;
                    return;
                }
                else {
                    std::cout << "Connection is still alive, killing it\n";
                }
                client::connection_ptr con_ptr = c.get_con_from_hdl(hdl);
                con_ptr->close(websocketpp::close::status::service_restart, "", ec);
                if (ec) {
                    std::cout << "Can not close connection because: " << ec.message() << std::endl;
                }
                while (connect) {
                    std::cout << "Waiting for close handshake\n";
                    utils::sleep(50);
                }

                //connect = false;
            }
            else {
                std::cout << "No connection exist. Skip clean up\n";
                return;
            }
        }
        
        void start(client* gatewayclient, websocketpp::connection_hdl gatewayhdl, std::string uri, std::string guildid, std::string _token, std::string session, std::string user_id) {
            sodium_init();
            auto shared_running = std::make_shared<bool*>(&running);
            this->endpoint = uri;
            this->guildid = guildid;
            this->_token = _token;
            this->session = session;
            this->user_id = user_id;
            this->gatewayclient = gatewayclient;
            this->gatewayhdl = gatewayhdl;
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
            c.reset();
            c.connect(con_ptr);
            if (first_time) {
                std::string str = "First time launch voiceclient, start endpoint and pusher\n";
                std::cout << str;
                first_time = false;
                concurrency::create_task([this] {
                    c.run();
                    });
                pusher = concurrency::create_task([this, shared_running] {
                    while (1) {
                        if (selfqueue.size() > 0 && state && !(**shared_running)) {
                            std::cout << "[Pusher] Push video " << selfqueue.front() << std::endl;
                            this->playing = play(selfqueue.front());
                            selfqueue.pop();
                            while (**shared_running) {
                                utils::sleep(100);
                            }
                            utils::sleep(100);
                        }
                        else {
                            utils::sleep(100);
                        }
                        utils::sleep(100);
                    }
                });
            } 
        }

        concurrency::task<void> play(std::string id) {
            running = true;
            auto shared_token = std::make_shared<concurrency::cancellation_token*>(&p_token);
            auto shared_running = std::make_shared<bool*>(&running);
            return concurrency::create_task([this, id, shared_token, shared_running] {
                speak();
                std::string title = utils::youtubeGetTitle(id);
                std::string payload = "Now playing:\n";
                payload = payload + "\"" + title + "\"";
                utils::sendMsg(payload, default_channel);
                printf("creating opus encoder\n");
                audio* source = new audio(id);
                const unsigned short FRAME_MILLIS = 20;
                const unsigned short FRAME_SIZE = 960;
                const unsigned short SAMPLE_RATE = 48000;
                const unsigned short CHANNELS = 2;
                const unsigned int BITRATE = 80000;
                #define MAX_PACKET_SIZE FRAME_SIZE * 5
                int error;
                OpusEncoder* encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &error);
                if (error < 0) {
                    throw "failed to create opus encoder: " + std::string(opus_strerror(error));
                }
                error = opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
                if (error < 0) {
                    throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                }
                error = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE));
                if (error < 0) {
                    throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                }
                error = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(expected_packet_loss));
                if (error < 0) {
                    throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                }

                if (sodium_init() == -1) {
                    throw "libsodium initialisation failed";
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
                concurrency::create_task([run_timer, this, shared_token] {
                    while (run_timer->get_is_set()) {
                        speak();
                        int i = 0;
                        while (i < 15) {
                            utils::sleep(1000);
                            if (run_timer->get_is_set() == false) {
                                std::cout << "Stop sending speak packet due to turn off\n";
                                concurrency::cancel_current_task();
                                return;
                            }
                            if ((*shared_token)->is_canceled()) {
                                std::cout << "Stop sending speak packet due to cancel\n";
                                concurrency::cancel_current_task();
                                return;
                            }
                        }
                    }});
                std::deque<std::string>* buffer = new std::deque<std::string>();
                auto shared_offset = std::make_shared<long long*>(&offset);
                auto timer = concurrency::create_task([run_timer, this, buffer, FRAME_MILLIS, shared_token, shared_offset, shared_running] {
                    while (run_timer->get_is_set() || buffer->size() > 0) {
                        utils::sleep(5 * FRAME_MILLIS);
                        **shared_running = true;
                        int sent = 0;
                        auto start = boost::chrono::high_resolution_clock::now();
                        while (buffer->size() > 0) {
                            if (udpclient.send(buffer->front()) != 0) {
                                std::cout << "Stop sendding voice data due to udp error\n";
                                **shared_running = false;
                                return;
                            }
                            buffer->pop_front();
                            if ((*shared_token)->is_canceled()) {
                                std::cout << "Stop sending voice data due to cancel\n";
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
                            throw "failed to set bitrate for opus encoder: " + std::string(opus_strerror(error));
                        }
                    }
                    if (source->read((char*)pcm_data, FRAME_SIZE * CHANNELS * 2) != true)
                        break;
                    if ((*shared_token)->is_canceled()) {
                        std::cout << "Stop encoding due to cancel\n";
                        break;
                    }

                    in_data = reinterpret_cast<opus_int16*>(pcm_data);
                    //in_data = (opus_int16*)pcm_data;
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

                std::cout << "Total size: " << totalsize << std::endl;
                run_timer->unset();
                timer.wait();   
                unspeak();
                **shared_running = false;
                delete run_timer;
                delete buffer;

                opus_encoder_destroy(encoder);

                delete[] pcm_data;

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

        voiceclient() {
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
                std::cout << e.what() << std::endl;
            }
        }
    };
    class gatewayclient {
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

        static void auth(websocketpp::connection_hdl hdl, client* c) {
            websocketpp::lib::error_code ec;
            std::string auth_str = "{\"op\":2,\"d\":{\"token\":\"ODA4NjQ1MzMxNzQ3MDc4MTc0.YCJjpg.pNK7l9i3SoDvX8PtLipK_1ZlIss\",\"intents\":1664,\"properties\":{\"$os\":\"window\",\"$browser\":\"\",\"$device\":\"\"}}}";
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
                        else std::cout << "Can not parse userid from message: " << msg->get_payload() << std::endl;
                        std::string guild = "";
                        if (js_msg["d"]["guild_id"].is_string()) {
                            guild = js_msg["d"]["guild_id"];
                        }
                        else std::cout << "Can not parse guild id from message:" << msg->get_payload() << std::endl;
                        std::string channel = "";
                        if (js_msg["d"]["channel_id"].is_string()) {
                            channel = js_msg["d"]["channel_id"].is_string();
                        }
                        if (userid == ready["d"]["user"]["id"] && channel == "") { //disconnect packet
                            if (voicegroup.find(guild) != voicegroup.end()) { //endpoint exist
                                voicegroup[guild]->cleanup();
                            }
                        }
                        usergroup[guild][userid] = new userinfo;
                        usergroup[guild][userid]->update(js_msg);
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
                            std::cout << "Successful get token id: " << _token << std::endl;
                            std::cout << "Successful get guild id: " << guildid << std::endl;
                            std::cout << "Successful get endpoint: " << endpoint << std::endl;
                        }
                        else {
                            std::cout << "Can not parse: token, guild id, endpoint \n";
                            break;
                        }
                        int i = 1;
                        std::string selfid = ready["d"]["user"]["id"];                  
                        while (1) {
                            std::string out = "Waiting for session id " + std::to_string(i) + "\n";
                            std::cout << out;
                            if (usergroup.find(guildid) == usergroup.end()) { 
                            }
                            else {
                                if (usergroup[guildid].find(selfid) == usergroup[guildid].end()) {
                                }
                                else {
                                    if (usergroup[guildid][selfid]->sessionid != "") {
                                        std::cout << "Found valid session id: " << usergroup[guildid][selfid]->sessionid << std::endl;
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
                            std::cout << "Fail to wait for session id\n";
                            break;
                        }
                        else {
                            if (voicegroup.find(guildid) == voicegroup.end()) { //not exist
                                std::cout << "Start voice connection with endpoint: " << endpoint << ", guild ID: " << guildid << ", session ID: " << usergroup[guildid][ready["d"]["user"]["id"]]->sessionid << std::endl;
                                voicegroup[guildid] = new voiceclient;
                                voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][selfid]->sessionid, selfid);
                            }
                            else { //exist
                                //stop old connection and start a new connection
                                std::cout << "Exist voice connection with endpoint: " << endpoint << ", guild ID: " << guildid << ", session ID: " << usergroup[guildid][ready["d"]["user"]["id"]]->sessionid << std::endl;
                                voicegroup[guildid]->cleanup();
                                voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, ready["d"]["user"]["id"]);
                            }
                        }
                        break;
                    }               

                    if (event == R"(MESSAGE_CREATE)") { //MSG CREATE EVENT
                        std::string channel = ""; //CHANNEL ID
                        if (js_msg["d"]["channel_id"].is_string()) {
                            channel = js_msg["d"]["channel_id"];
                        }
                        else {
                            std::cout << "Can not parse channel id from message:" << msg->get_payload() << std::endl;
                        }

                        std::string guild = ""; //GUILD ID
                        if (js_msg["d"]["guild_id"].is_string()) {
                            guild = js_msg["d"]["guild_id"];
                        }
                        else {
                            std::cout << "Can not parse guild id from message:" << msg->get_payload() << std::endl;
                        }

                        std::string content = ""; //MSG CONTENT
                        if (js_msg["d"]["content"].is_string()) {
                            content = js_msg["d"]["content"];
                        }
                        else {
                            std::cout << "Can not parse content from message:" << msg->get_payload() << std::endl;
                        }

                        std::string userid = ""; //AUTHOR ID
                        if (js_msg["d"]["author"]["id"].is_string()) {
                            userid = js_msg["d"]["author"]["id"];
                        }
                        else std::cout << "Can not parse userid from message: " << msg->get_payload() << std::endl;

                        if (js_msg["d"]["mentions"][0]["id"] == (**s_ready)["d"]["user"]["id"]) {  //mention
                            utils::sendMsg("Why mention me? I won't show you my prefix is ?", channel);
                            std::cout << js_msg["d"]["mentions"][0]["id"] << std::endl;
                            std::cout << (**s_ready)["d"]["user"]["id"] << std::endl;
                            break;
                        }
                        if (content == "ping" || content == "Ping") { //ping command
                            int RTT = utils::ping("162.159.136.232"); //discord IP: 162.159.136.232
                            std::cout << "RTT: " << std::to_string(RTT) << std::endl;
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
                                            //std::cout << "Start voice connection with endpoint: " << endpoint << ", guild ID: " << guildid << ", session ID: " << usergroup[guildid][ready["d"]["user"]["id"]]->sessionid << std::endl;
                                            voicegroup[guild] = new voiceclient;
                                            voicegroup[guild]->set_default_channel(channel);
                                            //voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][selfid]->sessionid, selfid);
                                        }
                                        else { //exist
                                            //stop old connection and start a new connection
                                            //std::cout << "Exist voice connection with endpoint: " << endpoint << ", guild ID: " << guildid << ", session ID: " << usergroup[guildid][ready["d"]["user"]["id"]]->sessionid << std::endl;
                                            voicegroup[guild]->set_default_channel(channel);
                                            //voicegroup[guildid]->start(c, hdl, endpoint, guildid, _token, usergroup[guildid][ready["d"]["user"]["id"]]->sessionid, ready["d"]["user"]["id"]);
                                        }
                                        c->send(hdl, getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                        if (ec) {
                                            std::cout << "Can not send voice state update payload because: " << ec.message();
                                            break;
                                        }
                                    }
                                    else {
                                        std::string payload = "You are not in voice channel!";
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

                        if (content == "?leave") {
                            if (voicegroup.find(guild) == voicegroup.end()) {
                                std::string payload = "Not currently in any voice channel";
                                utils::sendMsg(payload, channel);
                            }
                            else {
                                voicegroup[guild]->clear();
                                utils::sendMsg("Got it", channel);
                                websocketpp::lib::error_code ec;
                                std::string payload = getVoiceStateUpdatePayload(guild, "null");
                                c->send(hdl, payload, websocketpp::frame::opcode::text);
                                if (ec) {
                                    std::cout << "Cannot send voice update payload because: " << ec.message() << std::endl;
                                    break;
                                }
                            }
                            //else if (voicegroup[guild]->connect == false) {
                            //    break;
                            //} else {
                            //    //voicegroup[guild]->cleanup();
                            //    ///*while (voicegroup[guild]->connect == true) {
                            //    //    std::cout << "Waiting for connection close\n";
                            //    //    utils::sleep(100);
                            //    //}*/
                            //    //if (voicegroup[guild]->connect == false) {
                            //    //    websocketpp::lib::error_code ec;
                            //    //    std::string payload = getVoiceStateUpdatePayload(guild, "null");
                            //    //    c->send(hdl, payload, websocketpp::frame::opcode::text);
                            //    //    if (ec) {
                            //    //        std::cout << "Cannot send voice update payload because: " << ec.message() << std::endl;
                            //    //        break;
                            //    //    }
                            //    //    payload = "Okela";
                            //    //    utils::sendMsg(payload, channel);
                            //    //}
                            //}
                            break;
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
                                            std::cout << "Endpoint for guild id " << guild << " not exist\n";
                                        }
                                        else {
                                            if (usergroup.find(guild) != usergroup.end()) {
                                                if (usergroup[guild].find(userid) != usergroup[guild].end()) {
                                                    if (usergroup[guild][userid]->get_voice_channel_id() != "") {
                                                        voicegroup[guild]->selfqueue.push(temp->data[selected-1]);
                                                        std::cout << "User " << userid << " selected video " << temp->data[selected - 1] << std::endl;
                                                        websocketpp::lib::error_code ec;
                                                        /*if (voicegroup[guild]->is_connect() && !(voicegroup[guild]->is_running())) {
                                                            std::cout << "Voice client already connected, and player is running this should be a change channel request. Double push.\n";
                                                            voicegroup[guild]->selfqueue.push(temp->data[selected - 1]);
                                                        }*/
                                                        c->send(hdl, getVoiceStateUpdatePayload(guild, usergroup[guild][userid]->get_voice_channel_id()), websocketpp::frame::opcode::text, ec);
                                                        if (ec) {
                                                            std::cout << "can not send voice state update payload because: " << ec.message() << std::endl;
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
                                std::string str = "Create voice endpoint for guild: " + guild + "\n";
                                std::cout << str;
                                voicegroup[guild] = new voiceclient;
                                str = "Set default channel of guild " + guild + " to " + channel + "\n";
                                std::cout << str;
                                voicegroup[guild]->set_default_channel(channel);
                            }
                            else {
                                std::cout << "Voice endpoint already exist\n";
                                std::string str = "Set default channel of guild " + guild + " to " + channel + "\n";
                                std::cout << str;
                                voicegroup[guild]->set_default_channel(channel);
                            }
                            std::string querry = content.erase(0, 3);
                            if (querry == "") {
                                utils::sendMsg("Please provide paramenter", channel);
                            }
                            else {
                                //sendMsg(querry, channel);
                                json result = utils::youtubePerformQuerry(querry);
                                std::cout << "Querry result for keyword: " << querry << std::endl;
                                //std::cout << result.dump() << std::endl;
                                mainqueue[guild][userid] = new querryqueue;
                                mainqueue[guild][userid]->push(js_msg, result);
                                std::cout << "Cached search querry\n";
                                utils::youtubePrintSearchResult(result, querry, channel);
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