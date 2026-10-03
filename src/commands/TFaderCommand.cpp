/*
Copyright (C) 2010-2019 Remon Sijrier

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

#include "GainCommand.h"

#include "TContextItem.h"
#include "TAudioProcessingNode.h"
#include "Mixer.h"
#include "Utils.h"
#include "Debugger.h"

/**
 *	\class GainCommand
    \brief Change (jog) the GainCommand of an TAudioProcessingNode, or set to a pre-defined value

    \sa TraversoCommands
 */


GainCommand::GainCommand(TAudioProcessingNode* context, const QVariantList& /*args*/)
    : TCommand(context, "")
    , m_audioProcessingNode(context)
{
    m_newGain = m_origGain = m_audioProcessingNode->get_gain();
}

GainCommand::~GainCommand()
{
    PENTERDES;
}

int GainCommand::prepare_actions()
{
    if (TraversoDAW::Float::compare(m_origGain, m_newGain)) {
        // Nothing happened!
        return -1;
    }
    return 1;
}

void GainCommand::apply_new_gain_to_object(float newGain)
{
    m_newGain = newGain;
    m_audioProcessingNode->set_gain(m_newGain);
    // the gainobject is able to refuse the new value, so we set our
    // newGain value to the value the gainobject internally decided to go for
    m_newGain = m_audioProcessingNode->get_gain();
}

int GainCommand::do_action()
{
    PENTER;

    // We already set the new gain value during process_mouse_move()
    // however, do_action() is always called from the TInputEventDispatcher
    // So do not start the animated gain setting since it will start from
    // the m_oldgain value.
    if (TraversoDAW::Float::compare(m_newGain, m_audioProcessingNode->get_gain())) {
        return 1;
    }

    // so this will only be reached after an undo/redo sequence
    m_audioProcessingNode->set_gain_animated(m_newGain);


    return 1;
}

int GainCommand::undo_action()
{
    PENTER;

    m_audioProcessingNode->set_gain_animated(m_origGain);

    return 1;
}

void GainCommand::cancel_action()
{
    undo_action();
}

void GainCommand::increase_gain(  )
{
    audio_sample_t dbFactor = Mixer::coefficient_to_dB(m_newGain);
    dbFactor += 0.2f;
    apply_new_gain_to_object(dB_to_scale_factor(dbFactor));
}

void GainCommand::decrease_gain()
{
    audio_sample_t dbFactor = Mixer::coefficient_to_dB(m_newGain);
    dbFactor -= 0.2f;
    apply_new_gain_to_object(dB_to_scale_factor(dbFactor));
}

void GainCommand::set_new_gain(float newGain)
{
    m_newGain = newGain;
    do_action();
}

void GainCommand::set_new_gain_numerical_input(float newGain)
{
    m_newGain = newGain;
}

int GainCommand::process_mouse_move(qreal diffY)
{
    qreal of = 0;
    audio_sample_t dbFactor;

#if defined(Q_OS_MAC)
    // On MacOS, if the user hasn't given the app accessibility privileges in:
    // System Setings -> Privacy & Security -> Accessibility -> Allow the applications below to control your computer,
    // the mouse doesn't snap back to the original position after each jog,
    // so we need to calculate the gain based on the original gain value, not the new gain value
    // to avoid the gain integrating the total mouse movement over time, and skyrocketing.
    if (TraversoDAW::Utils::can_set_mouse_pos()) {
        // Is trusted, so mouse position gets reset
        dbFactor = Mixer::coefficient_to_dB(m_newGain);
    }
    else {
        // Is NOT trusted, so mouse position does not get reset
        dbFactor = Mixer::coefficient_to_dB(m_origGain);
    }
#else
    dbFactor = Mixer::coefficient_to_dB(m_newGain);
#endif


    if (dbFactor > -1) {
        of = diffY * 0.05;
    }
    if (dbFactor <= -1) {
        of = diffY * ((1 - double(dB_to_scale_factor(dbFactor))) / 3);
    }

    apply_new_gain_to_object(dB_to_scale_factor(dbFactor + float(of)));

    return 1;
}
