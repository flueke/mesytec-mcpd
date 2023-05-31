#ifndef __MESYTEC_SPDLOG_UTIL_H__
#define __MESYTEC_SPDLOG_UTIL_H__

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/sink.h>

namespace mesytec
{
namespace spdlog_util
{

// Slight modification of spdlog::sinks::qt_sink. Passes the messages log_level
// as the first argument to the target method.
template<typename Mutex>
class QtSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    QtSink(QObject *qt_object, const std::string &meta_method)
    {
        qt_object_ = qt_object;
        meta_method_ = meta_method;
    }

    ~QtSink()
    {
        flush_();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        spdlog::string_view_t str = spdlog::string_view_t(formatted.data(), formatted.size());
        QMetaObject::invokeMethod(qt_object_, meta_method_.c_str(), Qt::AutoConnection,
            Q_ARG(spdlog::level::level_enum, msg.level),
            Q_ARG(QString, QString::fromUtf8(str.data(), static_cast<int>(str.size())).trimmed()));
    }

    void flush_() override {}

private:
    QObject *qt_object_ = nullptr;
    std::string meta_method_;
};

using QtSink_mt = QtSink<std::mutex>;

}
}

#endif /* __MESYTEC_SPDLOG_UTIL_H__ */
