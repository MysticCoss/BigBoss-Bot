#pragma once
#include <string>
#include <fstream>
#include <boost/log/sinks.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/core/null_deleter.hpp>

enum severity_level {
    info,
    notification,
    warning,
    error,
    critical
};

template< typename CharT, typename TraitsT >
inline std::basic_ostream< CharT, TraitsT >& operator<< (std::basic_ostream< CharT, TraitsT >& strm, severity_level lvl) {
    static const char* const str[] =
    {
        "info",
        "notification",
        "warning",
        "error",
        "critical"
    };
    if (static_cast<std::size_t>(lvl) < (sizeof(str) / sizeof(*str)))
        strm << str[lvl];
    else
        strm << static_cast<int>(lvl);
    return strm;
}
extern boost::log::sources::severity_logger_mt<severity_level> lg;
BOOST_LOG_ATTRIBUTE_KEYWORD(tag_attr, "Tag", std::string);
const std::string g_format = "[%TimeStamp%] (%LineID%) [%Severity%] [%Tag%]: %Message%";

namespace discordbot {
    class logger
    {
    public:
        logger(std::string file)
            : tag_(file)
        {
            //text logging backend
            auto backend = boost::make_shared<boost::log::sinks::text_file_backend>(
                boost::log::keywords::file_name = file + ".log",
                boost::log::keywords::open_mode = std::ios::out | std::ios::app,
                //boost::log::keywords::rotation_size = 10 * 1024 * 1024,
                //boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
                boost::log::keywords::auto_flush = true);

            auto sink = boost::make_shared<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>>(backend);
            sink->set_formatter(boost::log::parse_formatter(g_format));
            sink->set_filter(tag_attr == tag_);

            auto sink2 = boost::make_shared< boost::log::sinks::synchronous_sink< boost::log::sinks::text_ostream_backend > >(boost::log::keywords::auto_flush = true);
            boost::shared_ptr< std::ostream > stream(&std::clog, boost::null_deleter());
            sink2->set_formatter(boost::log::parse_formatter(g_format));
            sink2->locked_backend()->add_stream(stream);
            sink2->set_filter(tag_attr == tag_);

            // Register the sink in the logging core
            boost::log::core::get()->add_sink(sink);
            boost::log::core::get()->add_sink(sink2);

        }

        void log(std::string str, severity_level serv)
        {
            BOOST_LOG_SCOPED_THREAD_TAG("Tag", tag_);
            BOOST_LOG_SEV(lg, serv) << str;
        }

    private:
        const std::string tag_;
    };
}
