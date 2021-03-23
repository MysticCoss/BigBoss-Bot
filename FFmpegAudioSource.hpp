#include <boost/process.hpp>
#include <fstream>

class audio {
private:
	std::shared_ptr <boost::process::child> _process;
	std::shared_ptr <boost::process::ipstream> _pipe;
public:
	audio(std::string id, const std::string& before_options = "", const std::string& options="") {
		//download audio
		std::string command = "youtube-dl -f 251 " + id + " --id";
		boost::process::system(command);

		//get raw audio stream using ffmpeg (s16le format)
		std::string path = R"(C:\Users\Admin\source\repos\Websocket)";
		std::string input = path + "\\" + id + ".webm";
		command = "ffmpeg " + before_options + " -i \"" + input + "\" " + options + " -loglevel quiet -f s16le -ac 2 -ar 48000 pipe:1";
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		std::cout << "Calling subprocess with command: " << command << std::endl;
		_pipe = std::make_shared<boost::process::ipstream>(boost::process::ipstream());
		_process = std::make_shared <boost::process::child>(boost::process::child(command, boost::process::std_out > *_pipe));
	}

	~audio() {
		if (_process->running()) {
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
