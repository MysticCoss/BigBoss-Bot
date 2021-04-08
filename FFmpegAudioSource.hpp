#pragma once
#include <boost/process.hpp>
#include <fstream>
#include "log.hpp"
class audio {
private:
	std::shared_ptr <boost::process::child> _process;
	std::shared_ptr <boost::process::ipstream> _pipe;
	discordbot::logger* logger;
public:
	audio(discordbot::logger* logger, std::string id, const std::string& before_options = "", const std::string& options="") {
		this->logger = logger;
		//download audio
		std::string command = "youtube-dl -f 251 --id -- " + id;
		this->logger->log("Calling youtubedl with command: " + command, info);
		boost::process::system(command);

		//get raw audio stream using ffmpeg (s16le format)
		std::string path = R"(C:\Users\Admin\source\repos\Websocket)";
		std::string input = path + "\\" + id + ".webm";
		command = "ffmpeg " + before_options + " -i \"" + input + "\" " + options + " -loglevel quiet -f s16le -ac 2 -ar 48000 pipe:1";
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		this->logger->log("Calling subprocess with command: " + command, info);
		_pipe = std::make_shared<boost::process::ipstream>(boost::process::ipstream());
		_process = std::make_shared <boost::process::child>(boost::process::child(command, boost::process::std_out > *_pipe));
	}

	~audio() {
		if (_process->running()) {
			this->logger->log("Terminate ffmpeg", info);
			_process->terminate();
		}
		_process->wait();
	}

	bool read(char* pcm_data, const int length) {
		_pipe->read((char*)pcm_data, length);

		if (_pipe->gcount() == 0)
			return false;

		return true;
	}
};
