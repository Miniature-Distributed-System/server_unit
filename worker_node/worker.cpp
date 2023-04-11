#include "../include/debug_rp.hpp"
#include "../sched/timeout.hpp"
#include "../packet_processor/out_data_registry.hpp"
#include "worker.hpp"

OutPacket:: OutPacket(json packet, OutDataState* outData, bool ackable){
    this->packet = packet;
    this->outData = outData;
    this->ackable = ackable;
    status.initFlag();
}

void OutPacket::checkIn()
{
    outData->worker->checkIn();
    status.setFlag();
}

void OutPacket::checkOut()
{
    status.resetFlag();
}

bool OutPacket::isCheckedIn()
{
    return status.isFlagSet();
}

bool OutPacket::isAckable()
{
    return ackable;
}

OutDataState* OutPacket::getOutDataState()
{
    return outData;
}

Worker::Worker(){}

Worker::Worker(std::uint64_t workerUID)
{
    this->workerUID = workerUID;
    attendance.initFlag(true);
    ackPacketPop.initFlag();
    quickSendMode.initFlag();
    sem_init(&workerLock, 0, 0);
}

void Worker::checkIn()
{
    DEBUG_MSG(__func__,"Worker-",workerUID, ": attendence marked");  
    attendance.setFlag();
}

void Worker::checkOut()
{
    DEBUG_MSG(__func__,"Worker-",workerUID, ": attendence marked");  
    attendance.resetFlag();
}

bool Worker::isCheckedIn()
{
    return attendance.isFlagSet();
}

std::uint64_t Worker::getWorkerUID()
{  
    return workerUID;
}

int Worker::queuePacket(OutPacket* packet)
{
    if(senderQueue.size() > WORKER_QUEUE_SIZE){
        DEBUG_MSG(__func__,"worker-", workerUID,": max limit reached");
        return 1;
    }
    sem_wait(&workerLock);
    senderQueue.push_back(packet);
    sem_post(&workerLock);
    DEBUG_MSG(__func__, "worker-", workerUID, ": pushed packet to queue");
    return 0;
}

json Worker::getQueuedPacket()
{
    OutPacket* outPacket = NULL;

    sem_wait(&workerLock);
    if(ackPacketPop.isFlagSet()){
        DEBUG_MSG(__func__,"worker-", workerUID,": re-sending non-acked packet to worker");
        ackPacketPop.resetFlag();
        return ackPendingQueue.front()->packet;
    }

    if(senderQueue.size() > 0){
        outPacket = senderQueue.front();
        if(outPacket->isAckable()){
            if(ackPendingQueue.size() > WORKER_QUEUE_SIZE / 2){
                // Only send non ackable packets
                for(auto i = senderQueue.begin(); i != senderQueue.end(); i++){
                    if(!(*i)->isAckable()){
                        outPacket = (*i);
                        senderQueue.erase(i);
                        DEBUG_MSG(__func__,"worker-", workerUID,": sending non-ackable packet to worker");
                        return (*i)->packet;
                    }
                }
            } else {
                //Add packet into timeout counter
                packetTimeout->addPacket(outPacket);
                ackPendingQueue.push_back(outPacket);
            }
        }
    } else {
        return json({});
    }
    
    senderQueue.pop_front();
    sem_post(&workerLock);
    return outPacket->packet;
}

int Worker::getQueueSize()
{
    return WORKER_QUEUE_SIZE - senderQueue.size();
}

bool Worker::matchAckablePacket(std::string id)
{
    sem_wait(&workerLock);
    for(auto i = ackPendingQueue.begin(); i != ackPendingQueue.end(); i++){
        if((*i)->getOutDataState()->id == id){
            delete (*i);
            ackPendingQueue.erase(i);
            DEBUG_MSG(__func__,"worker-", workerUID,": packet acked");
            sem_post(&workerLock);
            return true;
        }
    }

    sem_post(&workerLock);
    DEBUG_ERR(__func__,"worker-", workerUID,": no such packet found!");
    return false;
}

std::list<OutPacket*> Worker::shutDown()
{
    std::list<OutPacket*> outPacket;
    sem_wait(&workerLock);
    for(auto i = ackPendingQueue.begin(); i != ackPendingQueue.end(); i++)
    {
        outPacket.push_back(*i);
    }
    for(auto i = senderQueue.begin(); i != senderQueue.end(); i++)
    {
        outPacket.push_back(*i);
    }

    DEBUG_MSG(__func__,"worker-",workerUID, ": shut down complete");
    sem_post(&workerLock);
    sem_destroy(&workerLock);
    return outPacket;
}

void Worker::pushToFront(OutPacket* outPacket)
{
    sem_wait(&workerLock);
    auto removed = std::remove(ackPendingQueue.begin(), ackPendingQueue.end(), outPacket);
    ackPendingQueue.erase(removed, ackPendingQueue.end());
    ackPendingQueue.push_front(outPacket);
    ackPacketPop.setFlag();
    sem_post(&workerLock);
}

void Worker::setQuickSendMode()
{
    quickSendMode.setFlag();
}

void Worker::resetQuickSendMode()
{
    quickSendMode.resetFlag();
}

bool Worker::isQuickSendMode()
{
    return quickSendMode.isFlagSet();
}