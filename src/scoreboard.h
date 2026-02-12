#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class PrioType;
class ScoreBoardElement;
class SiteLogic;
class FileList;
class Race;
class SiteRace;

#define RESIZE_CHUNK 1000
#define USHORT_MAX 0x10000

class ScoreBoard {
  private:
    std::vector<ScoreBoardElement*> elements;
    std::vector<ScoreBoardElement*> elementstmp;
    unsigned int showsize;
    unsigned int* count;
    unsigned int* bucketpositions;
    unsigned int countarraybytesize;
    void shuffle(unsigned int firstpos, unsigned int lastpos);
    bool removeInternal(const std::string& name, const std::shared_ptr<FileList>& fls, const std::shared_ptr<FileList>& fld);
    void wipeInternal();
    std::unordered_map<std::shared_ptr<FileList>, std::unordered_map<std::shared_ptr<FileList>,
      std::unordered_map<std::string, ScoreBoardElement*>>> elementlocator;
    std::unordered_map<std::shared_ptr<FileList>, std::unordered_set<ScoreBoardElement*>> destinationlocator;
    mutable std::shared_mutex mutex_;
  public:
    ScoreBoard();
    ~ScoreBoard();
    void update(
        const std::string& name, unsigned short score, unsigned long long int filesize,
        PrioType priotype,
        const std::shared_ptr<SiteLogic>& src, const std::shared_ptr<FileList>& fls, const std::shared_ptr<SiteRace>& srs,
        const std::shared_ptr<SiteLogic>& dst, const std::shared_ptr<FileList>& fld, const std::shared_ptr<SiteRace>& srd,
        const std::shared_ptr<Race>& race, const std::string & subdir);
    void update(ScoreBoardElement* sbe);
    ScoreBoardElement* find(const std::string & name, const std::shared_ptr<FileList>& fls, const std::shared_ptr<FileList>& fld) const;
    bool remove(ScoreBoardElement* sbe);
    bool remove(const std::string& name, const std::shared_ptr<FileList>& fls, const std::shared_ptr<FileList>& fld);
    unsigned int size() const;
    std::vector<ScoreBoardElement*>::const_iterator begin() const;
    std::vector<ScoreBoardElement*>::const_iterator end() const;
    std::vector<ScoreBoardElement*>::iterator begin();
    std::vector<ScoreBoardElement*>::iterator end();
    void sort();
    void shuffleEquals();
    const std::vector<ScoreBoardElement*>& getElementVector() const;
    void wipe();
    void wipe(const std::shared_ptr<FileList>& fl);
    void resetSkipChecked(const std::shared_ptr<FileList>& fl);

    /* Thread-safe snapshot of the element vector for iteration without holding locks */
    std::vector<ScoreBoardElement*> getSnapshot() const;

    /* Lock access for external callers needing extended operations */
    std::shared_lock<std::shared_mutex> readLock() const { return std::shared_lock<std::shared_mutex>(mutex_); }
    std::unique_lock<std::shared_mutex> writeLock() { return std::unique_lock<std::shared_mutex>(mutex_); }
};
