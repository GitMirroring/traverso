#ifndef TFADERANGECOMMAND_H
#define TFADERANGECOMMAND_H

#include "TMoveCommand.h"

class TAudioClip;
class TFadeCurve;
class TSheet;

class TFadeRangeCommand : public TMoveCommand
{
    Q_OBJECT

public :
    TFadeRangeCommand(TAudioClip* clip, TFadeCurve* fadeIn, TFadeCurve* fadeOut, qint64 scalefactor);
    ~TFadeRangeCommand();

    int begin_hold();
    int finish_hold();
    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();

    int jog();

    void set_cursor_shape(int useX, int useY);
    bool wants_cursor_position_to_be_restored() const {return true;}

private :
    TFadeCurve*	m_fadeIn{nullptr};
    TFadeCurve*	m_fadeOut{nullptr};
    double 		m_fadeInOrigRange;
    double 		m_fadeInNewRange;
    double 		m_fadeOutOrigRange;
    double 		m_fadeOutNewRange;
    struct FadeRangePrivate {
        TSheet* sheet;
        TAudioClip* clip;
        int origX;
        qint64 scalefactor;
    };
    FadeRangePrivate* frp;

    void do_keyboard_move();


public slots:
    void next_snap_pos();
    void prev_snap_pos();
    void move_left();
    void move_right();
    void reset_length();
};

#endif // FADERANGECOMMAND_H
