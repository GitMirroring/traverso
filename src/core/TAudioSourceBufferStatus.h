#ifndef TAUDIOSOURCEBUFFERSTATUS_H
#define TAUDIOSOURCEBUFFERSTATUS_H

#include <atomic>

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

private:
    int     m_fillStatus;
    std::atomic<int>     m_syncStatus;
};

#endif // TAUDIOSOURCEBUFFERSTATUS_H
