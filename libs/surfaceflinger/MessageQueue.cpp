/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/Timers.h>
#include <utils/Log.h>
#include <binder/IPCThreadState.h>

#include "MessageQueue.h"

namespace android {

// ---------------------------------------------------------------------------

void MessageList::insert(const sp<MessageBase>& node) 
{
    LIST::iterator cur(mList.begin());
    LIST::iterator end(mList.end());
    while (cur != end) {
        if (*node < **cur) {
            mList.insert(cur, node);
            return;
        }
        ++cur;
    }
    mList.insert(++end, node);
}

void MessageList::remove(MessageList::LIST::iterator pos) 
{
    mList.erase(pos);
}

// ---------------------------------------------------------------------------

MessageQueue::MessageQueue()
    : mInvalidate(false)
{
    mInvalidateMessage = new MessageBase(INVALIDATE);
}

MessageQueue::~MessageQueue()
{
}

MessageList::value_type MessageQueue::waitMessage(nsecs_t timeout)
{
    MessageList::value_type result;

    bool again;
    do {
        const nsecs_t timeoutTime = systemTime() + timeout;
        while (true) {
            Mutex::Autolock _l(mLock);
            nsecs_t now = systemTime();
            nsecs_t nextEventTime = -1;

            // invalidate messages are always handled first
            if (mInvalidate) {
                mInvalidate = false;
                mInvalidateMessage->when = now;
                result = mInvalidateMessage;
                break;
            }

            LIST::iterator cur(mMessages.begin());
            if (cur != mMessages.end()) {
                result = *cur;
            }
            
            if (result != 0) {
                if (result->when <= now) {
                    // there is a message to deliver
                    mMessages.remove(cur);
                    break;
                }
                if (timeout>=0 && timeoutTime < now) {
                    // we timed-out, return a NULL message
                    result = 0;
                    break;
                }
                nextEventTime = result->when;
                result = 0;
            }

            if (timeout >= 0 && nextEventTime > 0) {
                if (nextEventTime > timeoutTime) {
                    nextEventTime = timeoutTime;
                }
            }

            if (nextEventTime >= 0) {
                //LOGD("nextEventTime = %lld ms", nextEventTime);
                if (nextEventTime > 0) {
                    // we're about to wait, flush the binder command buffer
                    IPCThreadState::self()->flushCommands();
                    mCondition.wait(mLock, nextEventTime);
                }
            } else {
                //LOGD("going to wait");
                // we're about to wait, flush the binder command buffer
                IPCThreadState::self()->flushCommands();
                mCondition.wait(mLock);
            }
        } 
        // here we're not holding the lock anymore

        if (result == 0)
            break;

        again = result->handler();
        if (again) {
            // the message has been processed. release our reference to it
            // without holding the lock.
            result = 0;
        }
        
    } while (again);

    return result;
}

status_t MessageQueue::postMessage(
        const MessageList::value_type& message, nsecs_t relTime, uint32_t flags)
{
    return queueMessage(message, relTime, flags);
}

status_t MessageQueue::invalidate() {
    Mutex::Autolock _l(mLock);
    mInvalidate = true;
    mCondition.signal();
    return NO_ERROR;
}

status_t MessageQueue::queueMessage(
        const MessageList::value_type& message, nsecs_t relTime, uint32_t flags)
{
    Mutex::Autolock _l(mLock);
    message->when = systemTime() + relTime;
    mMessages.insert(message);
    
    //LOGD("MessageQueue::queueMessage time = %lld ms", message->when);
    //dumpLocked(message);

    mCondition.signal();
    return NO_ERROR;
}

void MessageQueue::dump(const MessageList::value_type& message)
{
    Mutex::Autolock _l(mLock);
    dumpLocked(message);
}

void MessageQueue::dumpLocked(const MessageList::value_type& message)
{
    LIST::const_iterator cur(mMessages.begin());
    LIST::const_iterator end(mMessages.end());
    int c = 0;
    while (cur != end) {
        const char tick = (*cur == message) ? '>' : ' ';
        LOGD("%c %d: msg{.what=%08x, when=%lld}",
                tick, c, (*cur)->what, (*cur)->when);
        ++cur;
        c++;
    }
}

// ---------------------------------------------------------------------------

}; // namespace android
