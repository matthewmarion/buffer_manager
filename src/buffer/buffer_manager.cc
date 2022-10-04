#include "buffer/buffer_manager.h"
#include <algorithm>
#include "storage/file.h"

namespace buzzdb {

// BUFFERFRAME
BufferFrame::BufferFrame(const uint64_t page_id, const uint64_t page_size,
                    const int64_t counter, const bool is_exclusive,
                    const bool is_dirty) {
    this->pageId = page_id;
    this->pageSize = page_size;
    this->counter = counter;
    this->mIsExclusive = is_exclusive;
    this->mIsDirty = is_dirty;
    this->data.resize(page_size, 0);
}

BufferFrame::~BufferFrame() {}

char* BufferFrame::get_data() {
    return data.data();
}

void BufferFrame::readDisk() {
    std::string fileName = std::to_string(BufferManager::get_segment_id(pageId));
    std::unique_ptr<File> file = File::open_file(fileName.c_str(), File::WRITE);
    size_t offset = BufferManager::get_segment_page_id(pageId) * pageSize;
    file->read_block(offset, pageSize, data.data());
}

void BufferFrame::writeDisk() {
    std::string fileName = std::to_string(BufferManager::get_segment_id(pageId));
    std::unique_ptr<File> file = File::open_file(fileName.c_str(), File::WRITE);
    size_t offset = BufferManager::get_segment_page_id(pageId) * pageSize;
    file->write_block(data.data(), offset, pageSize);
}

void BufferFrame::lockPage(const bool exclusive) {
    exclusive == true ? pageMutex.lock() : pageMutex.lock_shared();
    mIsExclusive = exclusive;
}

void BufferFrame::unlockPage(const bool is_dirty) {
    mIsDirty = is_dirty;
    mIsExclusive == true ? pageMutex.unlock() : pageMutex.unlock_shared();
}
// END BUFFERFRAME

// BUFFERMANAGER
BufferManager::BufferManager(size_t page_size, size_t page_count) {
    std::unique_lock managerLock(managerMutex);
    pageSize = page_size;
    pageCount = page_count;

    std::unique_lock queueLock(queueMutex);
    fifoQueue.clear();
    lruQueue.clear();
}


BufferManager::~BufferManager() {
    std::unique_lock managerLock(managerMutex);
    std::unique_lock queueLock(queueMutex);
    for (auto entry : bufferMapping) {
        BufferFrame* page = entry.second;
        if (page->isDirty()) {
            page->writeDisk();
        }
        delete entry.second;
    }
}

int BufferManager::getPageIndexToRemove(bool isFifo) {
    if (isFifo) {
        for (int i = 0; i < static_cast<int>(fifoQueue.size()); i++) {
            BufferFrame *tempPage = bufferMapping.at(fifoQueue[i]);
            if (tempPage->getCounter() != 0) { continue; }
            tempPage->writeDisk();
            return i;
        }
    } else {
        for (int i = 0; i < static_cast<int>(lruQueue.size()); i++) {
            BufferFrame *tempPage = bufferMapping.at(lruQueue[i]);
            if (tempPage->getCounter() != 0) { continue; }
            tempPage->writeDisk();
            return i;
        }
    }
    return -1;
}

BufferFrame& BufferManager::removePage(uint64_t page_id, int indexToRemove, bool exclusive, bool isFifo) {
    BufferFrame * pFrame = nullptr;
    pFrame = new BufferFrame(page_id, pageSize);
    pFrame->incCounter();
    if (isFifo) {
        bufferMapping.erase(fifoQueue[indexToRemove]);
        bufferMapping.insert(std::pair<uint64_t, BufferFrame*>(page_id, pFrame));

        fifoQueue.erase(fifoQueue.begin() + indexToRemove);
        fifoQueue.push_back(page_id);
    } else {
        bufferMapping.erase(lruQueue[indexToRemove]);
        bufferMapping.insert(std::pair<uint64_t, BufferFrame*>(page_id, pFrame));

        lruQueue.erase(lruQueue.begin() + indexToRemove);
        fifoQueue.push_back(page_id);
    }
    queueMutex.unlock();
    managerMutex.unlock();
    pFrame->lockPage(exclusive);
    pFrame->readDisk();
    return *pFrame;
}

BufferFrame& BufferManager::updateExistingPage(uint64_t page_id, bool exclusive) {
    auto entry = bufferMapping.find(page_id);
    BufferFrame* pFrame = entry->second;
    pFrame->incCounter();
    auto lruPage = std::find(std::begin(lruQueue), std::end(lruQueue), page_id);
    auto fifoPage = std::find(std::begin(fifoQueue), std::end(fifoQueue), page_id);
    if (lruPage != std::end(lruQueue)) {
        lruQueue.erase(lruPage);
    }  else {
        fifoQueue.erase(fifoPage);
    }
    lruQueue.push_back(page_id);

    queueMutex.unlock();
    managerMutex.unlock();
    pFrame->lockPage(exclusive);

    return *pFrame;
}

BufferFrame& BufferManager::addNewPage(uint64_t page_id, bool exclusive) {
    BufferFrame *pFrame = new BufferFrame(page_id, pageSize);
    pFrame->incCounter();

    bufferMapping.insert(std::pair<uint64_t, BufferFrame*>(page_id, pFrame));
    fifoQueue.push_back(page_id);

    queueMutex.unlock();
    managerMutex.unlock();
    pFrame->lockPage(exclusive);

    pFrame->readDisk();
    return *pFrame;
}


BufferFrame& BufferManager::fix_page(uint64_t page_id, bool exclusive) {
    managerMutex.lock();
    queueMutex.lock();

    bool bufferContainsPage = bufferMapping.find(page_id) != bufferMapping.end();
    if (bufferContainsPage) {
        return updateExistingPage(page_id, exclusive);
    }

    bool bufferIsFull = bufferMapping.size() == pageCount;
    if (!bufferIsFull) {
        return addNewPage(page_id, exclusive);
    }

    int fifoToRemove = getPageIndexToRemove(true);
    if (fifoToRemove != -1) {
        return removePage(page_id, fifoToRemove, exclusive, true);
    }

    int lruToRemove = getPageIndexToRemove(false);
    if (lruToRemove != -1) {
        return removePage(page_id, lruToRemove, exclusive, false);
    }

    queueMutex.unlock();
    managerMutex.unlock();

    throw buffer_full_error{};
}


void BufferManager::unfix_page(BufferFrame& page, bool is_dirty) {
    std::unique_lock managerLock(managerMutex);
    std::unique_lock queueLock(queueMutex);

    page.decCounter();
    page.unlockPage(is_dirty);
    auto lruPage = std::find(std::begin(lruQueue), std::end(lruQueue), page.pageId);

    if (lruPage == std::end(lruQueue)) {
        return;
    }

    lruQueue.erase(lruPage);
    lruQueue.push_back(page.pageId);
    return;
}


std::vector<uint64_t> BufferManager::get_fifo_list() const {
    std::unique_lock queueLock(queueMutex);
    return fifoQueue;
}


std::vector<uint64_t> BufferManager::get_lru_list() const {
    std::unique_lock queueLock(queueMutex);
    return lruQueue;
}

}  // namespace buzzdb
