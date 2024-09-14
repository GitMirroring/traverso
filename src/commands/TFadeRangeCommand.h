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

    int begin_hold() override;
    int finish_hold() override;
    int prepare_actions() override;
    int do_action() override;
    int undo_action() override;
    void cancel_action() override;

    int jog() override;

    bool wants_cursor_position_to_be_restored() const  override {return true;}

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
    void update_canvas_cursor_text();


public slots:
    void next_snap_pos();
    void prev_snap_pos();
    void move_left() override;
    void move_right() override;
    void move_up() override {}; // no support for moving up atm
    void move_down() override {}; // no support for moving down atm
    void reset_length();
};

#endif // FADERANGECOMMAND_H
