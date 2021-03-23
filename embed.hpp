#include "utils.hpp"
namespace discordbot {
	class embed {
	private:
		class footer {
		private:
			std::string text = "";
			std::string icon_url = "";
			std::string proxy_icon_url = "";
		public:
			int setText(std::string text) {
				if (text.length() > 2048) {
					std::cout << "Footer.text exceed limit(2048)\n";
					return 0;
				}
				else {
					this->text = text;
					return 1;
				}
			}
			int setIconUrl(std::string icon_url) {
				this->icon_url = icon_url;
				return 1;
			}
			int setProxyIconUrl(std::string proxy_icon_url) {
				this->proxy_icon_url = proxy_icon_url;
				return 1;
			}
			std::string getText() {
				return text;
			}
			std::string getIconUrl() {
				return icon_url;
			}
			std::string getProxyIconUrl() {
				return proxy_icon_url;
			}
		};
		footer a;
		std::string title = "";
		std::string description = "";
		std::string url = "";
		std::string timestamp = "";
		int color = 0;
		std::string footer = "";
	public:
		int setTitle(std::string title) {
			if (title.length() > 256) {
				std::cout << "Title exceed limit(256)\n";
				return 0;
			}
			else {
				this->title = title;
				return 1;
			}
		}
		int setDescription(std::string description) {
			if (description.length() > 2048) {
				std::cout << "Description exceed limit(2048)\n";
				return 0;
			}
			else {
				this->description = description;
				return 1;
			}			
		}
		int setUrl(std::string url) {
			this->url = url;
			return 1;
		}
		int setTimestamp(std::string timestamp) {
			if (footer.length() > 6000) {
				std::string str = "Length limit exceeded while setTimestamp: " + timestamp + "\n";
				std::cout << str;
				return 0;
			}
			else {
				this->timestamp = timestamp;
				return 1;
			}
		}
		bool setColor(int color) {

		}
		bool setFooter(std::string footer) {

		}

	};
}
