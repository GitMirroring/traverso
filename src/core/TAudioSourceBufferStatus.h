#ifndef TAUDIOSOURCEBUFFERSTATUS_H
#define TAUDIOSOURCEBUFFERSTATUS_H

#include <atomic>
#include <QString>

class TAudioSourceBufferStatus {

public:
    enum SyncStatus {
        UNKNOWN,
        OUT_OF_SYNC,
        IN_SYNC,
        QUEUE_SEEKING_TO_NEW_LOCATION,
        QUEUE_SEEKED_TO_NEW_LOCATION,
        FILL_RTBUFFER_DEQUEUE_FAILURE,
        FILL_RTBUFFER_ENQUEUE_FAILURE,
        QUEUE_ABOUT_TO_BE_DELETED
    };

    inline bool out_of_sync() const {return m_syncStatus.load() != IN_SYNC;}

    inline void set_sync_status(int status) {
        m_syncStatus.store(status);
    }
    inline int get_sync_status() {
        return m_syncStatus.load();
    }
    void set_fill_status(int status) {
        m_fillStatus = status;
    }
    inline int get_fill_status() const {
        return m_fillStatus;
    }

    QString get_readable_sync_status() {
        switch(m_syncStatus) {
        case UNKNOWN: return QString("Unknown");
        case OUT_OF_SYNC: return QString("Out of Sync");
        case IN_SYNC: return QString("In Sync");
        case QUEUE_SEEKING_TO_NEW_LOCATION: return QString("Queue seeking to new location");
        case QUEUE_SEEKED_TO_NEW_LOCATION: return QString("Queue seekd to new location");
        case FILL_RTBUFFER_DEQUEUE_FAILURE: return QString("Fill RT buffer dequeue failure");
        case FILL_RTBUFFER_ENQUEUE_FAILURE: return QString("Fill RT buffer enqueue failure");
        case QUEUE_ABOUT_TO_BE_DELETED: return QString("Queue about to be deleted");
        default: return QString("Syncstatus invalid");
        }
    }

private:
    int     m_fillStatus;
    std::atomic<int>     m_syncStatus;
};

#endif // TAUDIOSOURCEBUFFERSTATUS_H
