/*
Copyright (C) 2019-2024 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
*/

#include "TGainGroupCommand.h"

#include "GainCommand.h"
#include "Mixer.h"
#include "Utils.h"
#include "TAudioProcessingNode.h"
#include "Debugger.h"


TGainGroupCommand::TGainGroupCommand(TContextItem *context)
    : TCommand (context)
    , m_contextItem(context)
    , m_primaryGainOnly(false)
{
    m_canvasCursorFollowsMouseCursor = false;

    TAudioProcessingNode* node = qobject_cast<TAudioProcessingNode*>(context);
    Q_ASSERT(node);
    setText("Gain: " + node->get_name());
}

TGainGroupCommand::~ TGainGroupCommand()
{
    PENTERDES;
}

void TGainGroupCommand::set_cursor_shape(int useX, int useY)
{
    Q_UNUSED(useX);
    Q_UNUSED(useY);

    cpointer().set_canvas_cursor_shape(":/cursorGain");
}


int TGainGroupCommand::begin_hold()
{
    m_origPos = cpointer().scene_pos();

    cpointer().set_canvas_cursor_text(get_db_string_from_object());
    return 1;
}

int TGainGroupCommand::finish_hold()
{
    return 1;
}

void TGainGroupCommand::cancel_action()
{
    PENTER;
    for (auto const &gain : m_gainCommands) {
        gain->cancel_action();
    }
}

void TGainGroupCommand::process_collected_number(const QString &collected)
{
    Q_ASSERT(m_gainCommands.size() > 0);

    if (collected.size() == 0) {
        cpointer().set_canvas_cursor_text(" dB");
        return;
    }

    bool ok;
    audio_sample_t dbFactor = audio_sample_t(collected.toDouble(&ok));
    if (!ok) {
        if (collected.contains(".") || collected.contains("-")) {
            QString s = collected;
            s.append(" dB");
            cpointer().set_canvas_cursor_text(s);
        }
        return;
    }

    int rightfromdot = 0;
    if (collected.contains(".")) {
        rightfromdot = collected.size() - collected.lastIndexOf(".") - 1;
    }

    float newGain = dB_to_scale_factor(dbFactor);

    // Update the vieport's hold cursor with the _actuall_ gain value!
    if(rightfromdot) {
        cpointer().set_canvas_cursor_text(QByteArray::number(double(dbFactor), 'f', rightfromdot).append(" dB"));
    } else {
        cpointer().set_canvas_cursor_text(QByteArray::number(double(dbFactor)).append(" dB"));
    }

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->set_new_gain(newGain);
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->set_new_gain_numerical_input(newGain);
        }
    }
}

int TGainGroupCommand::jog()
{
    Q_ASSERT(m_gainCommands.size() > 0);

    qreal diff = m_origPos.y() - cpointer().scene_y();

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->process_mouse_move(diff);
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->process_mouse_move(diff);
        }
    }

    cpointer().set_canvas_cursor_pos(m_origPos);

    // Update the vieport's hold cursor!
    cpointer().set_canvas_cursor_text(get_db_string_from_object());

    return 1;
}


int TGainGroupCommand::prepare_actions()
{
    if (m_gainCommands.size() == 0) {
        return -1;
    }

    for (auto const &gain : m_gainCommands) {
        if (gain->prepare_actions() == -1) {
            printf("one of the commands in the group failed prepare_actions\n");
            return -1;
        }
    }

    return 1;
}

int TGainGroupCommand::do_action()
{
    Q_ASSERT(m_gainCommands.size() > 0);

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->do_action();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->do_action();
        }
    }

    return 1;
}

int TGainGroupCommand::undo_action()
{
    Q_ASSERT(m_gainCommands.size() > 0);

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->undo_action();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->undo_action();
        }
    }

    return 1;
}

void TGainGroupCommand::add_audio_processing_node(TAudioProcessingNode* audioProcessingNode, const QVariantList& args)
{
    Q_ASSERT(audioProcessingNode);

    m_gainCommands.push_back(std::make_unique<GainCommand>(audioProcessingNode, args));
}

QString TGainGroupCommand::get_db_string_from_object()
{
    QString dbString;

    if ( ! QMetaObject::invokeMethod(m_contextItem, "get_gain_db_string",
                                   Qt::DirectConnection,
                                   Q_RETURN_ARG(QString, dbString)) ) {
        PWARN("Gain::get_gain_from_object QMetaObject::invokeMethod failed");
    }

    return dbString;
}


void TGainGroupCommand::increase_gain(  )
{
    Q_ASSERT(m_gainCommands.size() > 0);

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->increase_gain();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->increase_gain();
        }
    }

    // Update the vieport's hold cursor with the _actuall_ gain value!
    cpointer().set_canvas_cursor_text(get_db_string_from_object());
}

void TGainGroupCommand::decrease_gain()
{
    Q_ASSERT(m_gainCommands.size() > 0);

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->decrease_gain();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->decrease_gain();
        }
    }

    // Update the vieport's hold cursor with the _actuall_ gain value!
    cpointer().set_canvas_cursor_text(get_db_string_from_object());
}

void TGainGroupCommand::reset_gain()
{
    for (auto const &gain : m_gainCommands) {
        gain->set_new_gain(1.0f);
    }

    // Update the vieport's hold cursor with the _actuall_ gain value!
    cpointer().set_canvas_cursor_text(get_db_string_from_object());
}

void TGainGroupCommand::toggle_primary_gain_only()
{
    m_primaryGainOnly = !m_primaryGainOnly;
    cpointer().set_canvas_cursor_text(m_primaryGainOnly ? tr("To Selection: Off") : tr("To Selection: On"));
}

void TGainGroupCommand::numerical_input()
{
    cpointer().set_canvas_cursor_text(tr("Use numerical keys to set gain dB value..."), 2000);
}

// eof

