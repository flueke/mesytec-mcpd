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
#include "qt_monospace_textedit.h"

#include <QPlainTextEdit>
#include <QTextEdit>

#include "qt_font.h"

namespace
{

template<typename T>
std::unique_ptr<T> impl(const QFont &font)
{
    auto result = std::make_unique<T>();
    result->setFont(font);
    result->setLineWrapMode(T::NoWrap);
    set_tabstop_width(result.get(), 4);

    return result;
}

template<typename T>
std::unique_ptr<T> impl(qreal pointSizeDelta)
{
    auto font = make_monospace_font();
    font.setPointSizeF(font.pointSizeF() + pointSizeDelta);

    return impl<T>(font);
}

} // end anon namespace

namespace mesytec
{
namespace mvme
{
namespace util
{

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit()
{
    return impl<QPlainTextEdit>(0.0);
}

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit(qreal pointSizeDelta)
{
    return impl<QPlainTextEdit>(pointSizeDelta);
}

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit(const QFont &font)
{
    return impl<QPlainTextEdit>(font);
}

std::unique_ptr<QTextEdit> make_monospace_textedit()
{
    return impl<QTextEdit>(0.0);
}

std::unique_ptr<QTextEdit> make_monospace_textedit(qreal pointSizeDelta)
{
    return impl<QTextEdit>(pointSizeDelta);
}

std::unique_ptr<QTextEdit> make_monospace_textedit(const QFont &font)
{
    return impl<QTextEdit>(font);
}

} // end namespace util
} // end namespace mvme
} // end namespace mesytec
