#include "workmanager.h"

#include <thread>

#include "eventreceiver.h"

namespace Core {

#define READY_SIZE 8
#define OVERLOAD_SIZE 32
#define ASYNC_WORKERS 2

void WorkerRunner::run() {
  wm->runWorker(workerid);
}

WorkManager::WorkManager() : numworkers(0), running(false) {
  for (unsigned int i = 0; i < ASYNC_WORKERS; ++i) {
    asyncworkers.emplace_back(*this, asyncqueue);
  }
}

WorkManager::~WorkManager() {
  stop();
}

void WorkManager::init(const std::string& prefix, int id, int num_workers) {
  if (num_workers <= 0) {
    num_workers = static_cast<int>(std::thread::hardware_concurrency());
    if (num_workers <= 0) {
      num_workers = 1;
    }
  }
  numworkers = num_workers;
  workers.reserve(numworkers);
  for (int i = 0; i < numworkers; ++i) {
    workers.emplace_back(std::make_unique<WorkerData>());
  }
  runners.resize(numworkers);
  threads.resize(numworkers);

  for (int i = 0; i < numworkers; ++i) {
    runners[i].setup(this, i);
  }

  {
    std::lock_guard<std::mutex> lock(worklock);
    running = true;
  }

  for (int i = 0; i < numworkers; ++i) {
    std::string tname = prefix + "-work-" + std::to_string(id) + "-" + std::to_string(i);
    threads[i].start(tname.c_str(), &runners[i]);
  }

  for (AsyncWorker& aw : asyncworkers) {
    aw.init(prefix, id);
  }
}

int WorkManager::getWorkerIndex(EventReceiver* er) const {
  if (!er || numworkers <= 1) {
    return 0;
  }
  int key = er->getAffinityKey();
  // Ensure positive modulo
  return ((key % numworkers) + numworkers) % numworkers;
}

void WorkManager::dispatchFDData(EventReceiver* er, int sockid) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->highprioqueue.push(Event(er, EventType::DATA, sockid));
  workers[widx]->event.post();
  workers[widx]->readdata.wait();
}

bool WorkManager::dispatchFDData(EventReceiver* er, int sockid, char * buf, int len, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::DATABUF, sockid, buf, len));
  workers[widx]->event.post();
  if (prio == Prio::LOW) {
    return !workerLowPrioOverload(widx);
  }
  return !workerOverload(widx);
}

void WorkManager::dispatchTick(EventReceiver* er, int interval) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->highprioqueue.push(Event(er, EventType::TICK, interval));
  workers[widx]->event.post();
}

bool WorkManager::dispatchEventNew(EventReceiver* er, int sockid, int newsockid, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::NEW, sockid, newsockid));
  workers[widx]->event.post();
  if (prio == Prio::LOW) {
    return !workerLowPrioOverload(widx);
  }
  return !workerOverload(widx);
}

void WorkManager::dispatchEventConnecting(EventReceiver* er, int sockid, const std::string & addr, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::CONNECTING, sockid, addr));
  workers[widx]->event.post();
}

void WorkManager::dispatchEventConnected(EventReceiver* er, int sockid, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::CONNECTED, sockid));
  workers[widx]->event.post();
}

void WorkManager::dispatchEventDisconnected(EventReceiver* er, int sockid, const DisconnectType& reason, const std::string& details, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::DISCONNECTED, sockid, details, static_cast<int>(reason)));
  workers[widx]->event.post();
}

void WorkManager::dispatchEventSSLSuccess(EventReceiver* er, int sockid, const std::string & cipher, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::SSL_SUCCESS, sockid, cipher));
  workers[widx]->event.post();
}

void WorkManager::dispatchEventFail(EventReceiver* er, int sockid, const std::string & error, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::FAIL, sockid, error));
  workers[widx]->event.post();
}

void WorkManager::dispatchEventSendComplete(EventReceiver* er, int sockid, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::SEND_COMPLETE, sockid));
  workers[widx]->event.post();
}

void WorkManager::dispatchSignal(EventReceiver* er, int signal, int value) {
  er->bindWorkManager(this);
  if (signalevents.set(er, signal, value)) {
    // Signal events are always delivered to worker 0
    workers[0]->event.post();
  }
}

void WorkManager::dispatchAsyncTaskComplete(AsyncTask & task) {
  int widx = getWorkerIndex(task.getReceiver());
  if (task.dataIsPointer()) {
    workers[widx]->dataqueue.push(Event(task.getReceiver(), EventType::ASYNC_COMPLETE_P, task.getType(), task.getData()));
  }
  else {
    workers[widx]->dataqueue.push(Event(task.getReceiver(), EventType::ASYNC_COMPLETE, task.getType(), task.getNumData()));
  }
  workers[widx]->event.post();
}

void WorkManager::dispatchApplicationMessage(EventReceiver* er, int messageid, void* messagedata, Prio prio) {
  er->bindWorkManager(this);
  int widx = getWorkerIndex(er);
  workers[widx]->eventqueues[static_cast<int>(prio)]->push(Event(er, EventType::APPLICATION_MESSAGE, messageid, messagedata));
  workers[widx]->event.post();
}

void WorkManager::deferDelete(const std::shared_ptr<EventReceiver>& er) {
  int widx = getWorkerIndex(er.get());
  workers[widx]->lowprioqueue.push(Event(er, EventType::DELETE));
  workers[widx]->event.post();
}

void WorkManager::asyncTask(EventReceiver* er, int type, void (*taskfunction)(EventReceiver *, int), int data) {
  asyncqueue.push(AsyncTask(er, type, taskfunction, data));
}

void WorkManager::asyncTask(EventReceiver* er, int type, void (*taskfunction)(EventReceiver *, void *), void * data) {
  asyncqueue.push(AsyncTask(er, type, taskfunction, data));
}

DataBlockPool& WorkManager::getBlockPool() {
  return blockpool;
}

bool WorkManager::workerOverload(int widx) {
  WorkerData& w = *workers[widx];
  bool currentlyoverloaded = w.highprioqueue.size() + w.dataqueue.size() >= OVERLOAD_SIZE;
  if (currentlyoverloaded) {
    w.overloaded = true;
    w.lowpriooverloaded = true;
  }
  return w.overloaded;
}

bool WorkManager::workerLowPrioOverload(int widx) {
  WorkerData& w = *workers[widx];
  bool currentlyoverloaded = w.highprioqueue.size() + w.dataqueue.size() + w.lowprioqueue.size() >= OVERLOAD_SIZE;
  if (currentlyoverloaded) {
    w.lowpriooverloaded = true;
  }
  return w.lowpriooverloaded;
}

bool WorkManager::overload() {
  for (int i = 0; i < numworkers; ++i) {
    if (workerOverload(i)) {
      return true;
    }
  }
  return false;
}

bool WorkManager::lowPrioOverload() {
  for (int i = 0; i < numworkers; ++i) {
    if (workerLowPrioOverload(i)) {
      return true;
    }
  }
  return false;
}

unsigned int WorkManager::getQueueSize() const {
  unsigned int total = 0;
  for (int i = 0; i < numworkers; ++i) {
    total += workers[i]->highprioqueue.size() + workers[i]->dataqueue.size() + workers[i]->lowprioqueue.size();
  }
  return total;
}

void WorkManager::addReadyNotify(EventReceiver* er) {
  std::lock_guard<std::mutex> lock(readylock);
  readynotify.push_back(er);
}

void WorkManager::flushEventReceiver(EventReceiver* er)
{
  worklock.lock();
  if (running) {
    worklock.unlock();
    int widx = getWorkerIndex(er);
    // Check if any of our worker threads is the current thread
    bool iscurrentthread = false;
    for (int i = 0; i < numworkers; ++i) {
      if (threads[i].isCurrentThread()) {
        iscurrentthread = true;
        break;
      }
    }
    if (iscurrentthread) {
      signalevents.flushEventReceiver(er);
      for (int w = 0; w < numworkers; ++w) {
        for (size_t i = 0; i < workers[w]->eventqueues.size(); ++i) {
          workers[w]->eventqueues[i]->lock();
          for (auto it = workers[w]->eventqueues[i]->begin(); it != workers[w]->eventqueues[i]->end();) {
            if (it->getReceiver() == er) {
              workers[w]->eventqueues[i]->erase(it);
            }
            else {
              ++it;
            }
          }
          workers[w]->eventqueues[i]->unlock();
        }
      }
    }
    else {
      workers[widx]->eventqueues[static_cast<int>(Prio::LOW)]->push(Event(nullptr, EventType::FLUSH, 0));
      workers[widx]->event.post();
      workers[widx]->flush.wait();
    }
    return;
  }
  worklock.unlock();
}

void WorkManager::notifyReady(int widx) {
  WorkerData& w = *workers[widx];
  if ((w.overloaded || w.lowpriooverloaded) &&
      w.dataqueue.size() + w.highprioqueue.size() + w.lowprioqueue.size() <= READY_SIZE)
  {
    w.overloaded = false;
    w.lowpriooverloaded = false;
    std::lock_guard<std::mutex> lock(readylock);
    for (EventReceiver* ernotify : readynotify) {
      ernotify->workerReady();
    }
  }
}

void WorkManager::runWorker(int workerid) {
  WorkerData& w = *workers[workerid];
  while (true) {
    w.event.wait();
    // Only worker 0 handles signal events
    if (workerid == 0 && signalevents.hasEvent()) {
      SignalData signal = signalevents.getClearFirst();
      if (!signal.er) {
        std::lock_guard<std::mutex> lock(worklock);
        running = false;
        // Wake up all other workers to exit
        for (int i = 1; i < numworkers; ++i) {
          workers[i]->event.post();
        }
        w.flush.post();
        return;
      }
      signal.er->signal(signal.signal, signal.value);
    }
    else {
      // Check if we should exit (non-zero workers)
      if (workerid != 0) {
        std::lock_guard<std::mutex> lock(worklock);
        if (!running) {
          w.flush.post();
          return;
        }
      }
      Event event;
      if (w.highprioqueue.size()) {
        event = w.highprioqueue.pop();
      }
      else if (w.dataqueue.size()) {
        event = w.dataqueue.pop();
      }
      else if (w.lowprioqueue.size()) {
        event = w.lowprioqueue.pop();
      }
      else {
        // Spurious wakeup (e.g. shutdown signal for non-zero worker)
        continue;
      }
      EventReceiver* er = event.getReceiver();
      int numdata = event.getNumericalData();
      switch (event.getType()) {
        case EventType::DATA:
          er->FDData(numdata);
          w.readdata.post();
          break;
        case EventType::DATABUF: {
          char* data = static_cast<char*>(event.getData());
          er->FDData(numdata, data, event.getDataLen());
          blockpool.returnBlock(data);
          break;
        }
        case EventType::TICK:
          er->tick(numdata);
          break;
        case EventType::CONNECTING:
          er->FDConnecting(numdata, event.getStrData());
          break;
        case EventType::CONNECTED:
          er->FDConnected(numdata);
          break;
        case EventType::DISCONNECTED:
          er->FDDisconnected(numdata, static_cast<DisconnectType>(event.getNumericalData2()), event.getStrData());
          break;
        case EventType::SSL_SUCCESS:
          er->FDSSLSuccess(numdata, event.getStrData());
          break;
        case EventType::NEW:
          er->FDNew(numdata, event.getNumericalData2());
          break;
        case EventType::FAIL:
          er->FDFail(numdata, event.getStrData());
          break;
        case EventType::SEND_COMPLETE:
          er->FDSendComplete(numdata);
          break;
        case EventType::DELETE: // will be deleted when going out of scope
          break;
        case EventType::ASYNC_COMPLETE:
          er->asyncTaskComplete(numdata, event.getNumericalData2());
          break;
        case EventType::ASYNC_COMPLETE_P:
          er->asyncTaskComplete(numdata, event.getData());
          break;
        case EventType::APPLICATION_MESSAGE:
          er->receivedApplicationMessage(numdata, event.getData());
          break;
        case EventType::FLUSH:
          w.flush.post();
          break;
      }
      notifyReady(workerid);
    }
  }
}

void WorkManager::run() {
  runWorker(0);
}

void WorkManager::preStop() {
  std::lock_guard<std::mutex> lock(worklock);
  if (!running) {
    return;
  }
  signalevents.set(nullptr, 0, 0);
  workers[0]->event.post();
  for (unsigned int i = 0; i < asyncworkers.size(); ++i) {
    asyncqueue.push(AsyncTask(nullptr, 0, nullptr, 0));
  }
}

void WorkManager::stop() {
  preStop();
  for (int i = 0; i < numworkers; ++i) {
    threads[i].join();
  }
  for (auto& aswk : asyncworkers) {
    aswk.join();
  }
  for (int i = 0; i < numworkers; ++i) {
    workers[i]->flush.post();
  }
}

} // namespace Core
