#include "pch.h"

#include <unistd.h>

#include "Proactor.h"

ProactorService::ProactorService(const char *pollerType, bool blocking): 
    mPoller(IPoller::create(pollerType)), mBlocking(blocking) {

    if (string(pollerType) == "epoll-et" && blocking) {
        LOG_ERR_MSG("Warning: edge-trigger poller should work with blocking socket!");
    }
}
ProactorService::~ProactorService() {
    if (!mActiveFiles.empty()) {
        for (ProactorFile *file : vector<ProactorFile*>(mActiveFiles.begin(), mActiveFiles.end())) {
            LOG_ERR("Abnormal closed file: %d", file->getFd());
            destroyFile(file);
        }
    }
    ASSERT(mActiveFiles.empty());

    clearDeferDestroyFiles();

    while (!mFreeFiles.empty()) {
        delete mFreeFiles.back();
        mFreeFiles.pop_back();
    }

    DELETE(mPoller);
}
void ProactorService::createClientSocket(const HostAddress &serverAddr, function<void(ProactorFile* file)> connectedCallback) {
    TCPSocket socket = TCPSocket::create();
    socket.setNonBlocking(true);
    if (socket.connectAsync(serverAddr)) {
        socket.setNonBlocking(!mBlocking);

        ProactorFile *file = allocFile();
        file->init(this, socket.getFd());
        mActiveFiles.insert(file);
        connectedCallback(file);
    } else {
        ProactorFile *file = allocFile();
        file->initWithConnectedCallbak(this, socket.getFd(), connectedCallback);
        mActiveFiles.insert(file);
    }
}
ProactorFile* ProactorService::createListenSocket(const HostAddress &bindAddr, int backLog) {
    TCPSocket socket = TCPSocket::create();
    socket.setReuseAddress();
    socket.bind(bindAddr);
    socket.listen(backLog);
    socket.setNonBlocking(!mBlocking);

    ProactorFile *file = allocFile();
    file->init(this, socket.getFd());
    mActiveFiles.insert(file);
    return file;
}
ProactorFile* ProactorService::attachFd(int fd) {
    TCPSocket::fromFd(fd).setNonBlocking(!mBlocking);

    ProactorFile *file = allocFile();
    file->init(this, fd);
    mActiveFiles.insert(file);
    return file;
}
void ProactorService::destroyFile(ProactorFile *file) {
    mActiveFiles.erase(file);
    mDeferDestoryFiles.push_back(file);
}
void ProactorService::clearDeferDestroyFiles() {
    while (!mDeferDestoryFiles.empty()) {
        ProactorFile *file = mDeferDestoryFiles.back();
        mDeferDestoryFiles.pop_back();

        file->uninit();
        freeFile(file);
    }
}
void ProactorService::wait(int timeout) {
    clearDeferDestroyFiles();

    vector<IPoller::Event> events;
    if (!mPoller->wait(events, timeout)) return;

    for (auto &event : events) {
        ProactorFile *file = ((ProactorFile*)event.ud);
        if (event.flag & IPoller::EF_Readable) file->onRead();
        if (event.flag & IPoller::EF_Writeable) file->onWrite();
        if (event.flag & IPoller::EF_ErrFound) {
            LOG_ERR("Found error ini proactor file: %d,%s", file->getFd(), strerror(errno));
            destroyFile(file);
        }
    }
}
ProactorFile* ProactorService::allocFile() {
    if (!mFreeFiles.empty()) {
        ProactorFile *file = mFreeFiles.back();
        mFreeFiles.pop_back();
        return file;
    }
    return new ProactorFile();
}
void ProactorService::freeFile(ProactorFile *file) {
    mFreeFiles.push_back(file);
}

void ProactorFile::accept(function<void(ProactorFile* file, const HostAddress& addr)> callback) {
    ASSERT(mAcceptCallback == nullptr);
    mAcceptCallback = callback;
}
void ProactorFile::readLine(char delmit, function<void(bool eof, const char *buf, int n)> callback) {
    mReadBuf.readLine(delmit, callback);
}
void ProactorFile::readN(int n, function<void(bool eof, const char *buf, int n)> callback) {
    mReadBuf.readN(n, callback);
}
void ProactorFile::readSome(function<void(bool eof, const char *buf, int n)> callback) {
    mReadBuf.readSome(callback);
}
void ProactorFile::writeN(const char *buf, int n, function<void()> callback) {
    if (!mWriteBuf.hasPendingData()) {
        mService->getPoller()->update(mFd, this, IPoller::EF_Readable | IPoller::EF_Writeable);
    }
    mWriteBuf.writeN(buf, n, callback);
}
void ProactorFile::destroy() {
    mService->destroyFile(this);
}
void ProactorFile::setDestroyCallback(function<void()> callback) {
    mDestroyCallback = callback;
}
void ProactorFile::init(ProactorService *service, int fd) {
    ASSERT(fd != -1);
    mReadBuf.init(fd);
    mWriteBuf.init(fd);
    mFd = fd;
    mService = service;
    mService->getPoller()->add(mFd, this, IPoller::EF_Readable);
}
void ProactorFile::initWithConnectedCallbak(ProactorService *service, int fd, function<void(ProactorFile*)> callback) {
    ASSERT(fd != -1);
    mConnectedCallback = callback;
    mReadBuf.init(fd);
    mWriteBuf.init(fd);
    mFd = fd;
    mService = service;
    mService->getPoller()->add(mFd, this, IPoller::EF_Writeable);
}
void ProactorFile::uninit() {
    if (mDestroyCallback != nullptr) {
        mDestroyCallback();
        mDestroyCallback = nullptr;
    }

    mConnectedCallback = nullptr;
    mAcceptCallback = nullptr;
    mReadBuf.uninit();
    mWriteBuf.uninit();
    mService->getPoller()->del(mFd);
    CLOSE(mFd);
}
void ProactorFile::acceptBlocking() {
    HostAddress addr;
    TCPSocket socket = TCPSocket::fromFd(mFd);
    socket = socket.accept(&addr);

    ProactorFile *file = mService->attachFd(socket.getFd());

    auto f = mAcceptCallback;
    mAcceptCallback = nullptr;
    f(file, addr);
}
void ProactorFile::acceptNonblocking() {
    TCPSocket listenSocket = TCPSocket::fromFd(mFd);

    for (;;) {
        TCPSocket socket;
        HostAddress addr;
        if (!listenSocket.acceptAsync(&socket, &addr)) break;

        ProactorFile *file = mService->attachFd(socket.getFd());

        auto f = mAcceptCallback;
        mAcceptCallback = nullptr;
        f(file, addr);
    }
}
void ProactorFile::onRead() {
    if (mAcceptCallback) {
        if (mService->isBlocking()) acceptBlocking();
        else acceptNonblocking();
    } else {
        if (mService->isBlocking()) mReadBuf.onReadBlocking();
        else mReadBuf.onReadNonblocking();
    }
}
void ProactorFile::onWrite() {
    if (mConnectedCallback) {
        auto f = mConnectedCallback;
        mConnectedCallback = nullptr;

        TCPSocket socket = TCPSocket::fromFd(mFd);
        int err = socket.getOption<int>(SO_ERROR);
        if (err == 0) {
            socket.setNonBlocking(!mService->isBlocking());
            mService->getPoller()->update(mFd, this, IPoller::EF_Readable);

            f(this);
        } else {
            LOG_ERR("Connect failed: %s", strerror(err));
            mService->destroyFile(this);
        }
    } else {
        if (mService->isBlocking()) mWriteBuf.onWriteBlocking();
        else mWriteBuf.onWriteNonblocking();

        if (!mWriteBuf.hasPendingData()) {
            mService->getPoller()->update(mFd, this, IPoller::EF_Readable);
        }
    }
}
