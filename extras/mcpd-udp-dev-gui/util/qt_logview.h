/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVME_UTIL_QT_LOGVIEW_H__
#define __MVME_UTIL_QT_LOGVIEW_H__

#include <memory>
#include <QDebug>
#include <QPlainTextEdit>
#include <spdlog/common.h>
#include "qt_str.h"

static const size_t LogViewMaximumBlockCount = 10 * 1000u;

std::unique_ptr<QPlainTextEdit> make_logview(
    size_t maxBlockCount = LogViewMaximumBlockCount);

// Appends log messages to the textedit passed at construction time.
// Can be used as the destination for spdlog::sinks::qt_sink_mt, e.g.:
// auto mySink = std::make_shared<spdlog::sinks::qt_sink_mt>(myWrapper, "logMessage");
class LogViewWrapper: public QObject
{
    Q_OBJECT
    public:
        explicit LogViewWrapper(QPlainTextEdit *logView, QObject *parent = nullptr)
            : QObject(parent)
            , logView_(logView)
        {
            connect(logView, &QObject::destroyed,
                    this, [this] () { logView_ = nullptr; });
        }

    public slots:
        void logMessage(const QString &msg)
        {
            if (logView_)
                logView_->appendPlainText(msg);
        }

        // Alternative version taking the level value as the first argument.
        void logMessage(const spdlog::level::level_enum &level, const QString &msg)
        {
            auto ilevel = static_cast<int>(level);

            if (ilevel < spdlog::level::warn)
            {
                auto escaped = msg.toHtmlEscaped();
                auto html = QSL("<font color=\"black\"><pre>%1</pre></font>").arg(escaped);
                logView_->appendHtml(html);
            }
            else if (ilevel == spdlog::level::warn)
            {
                auto escaped = msg.toHtmlEscaped();
                auto html = QSL("<font color=\"orange\"><pre>%1</pre></font>").arg(escaped);
                logView_->appendHtml(html);
            }
            else
            {
                auto escaped = msg.toHtmlEscaped();
                auto html = QSL("<font color=\"red\"><pre>%1</pre></font>").arg(escaped);
                logView_->appendHtml(html);
            }
        }

    private:
        QPlainTextEdit *logView_;
};

#endif /* __MVME_UTIL_QT_LOGVIEW_H__ */
