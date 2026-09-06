#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <shared_mutex>
#include <thread>
#include <variant>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <optional>
#include <algorithm>
#include <atomic>
#include <cstddef>

namespace threadsafe{

//these used to sit at global namespace scope, which made this header unsafe
//for any other project to include
using sizeT = std::size_t;
using string = std::string;
using optFlag = std::optional<bool>;

class Trie{
    public:
    /**
     * @version - 1.1
     * @brief node class is wrapped inside Trie class and not accessible by user
     * 
     * 
     * LOCKING, as it actually stands:
     * a single tree-level std::shared_mutex (v_treeLock). add/remove/clear take it
     * exclusively; find/getWords take it shared. node::count is atomic.
     *
     * This is coarse. It serialises writers against each other, which is fine for
     * the current one-thread-per-file callers but is the obvious thing to improve.
     *
     * NOT YET IMPLEMENTED - the intended design, kept here as the target:
     * a hybrid of spinlocks on deep nodes (cheap, low contention) and mutexes on
     * shallow nodes (expensive, high contention), chosen by depth against
     * v_mutexCutoff. The mutexLock/spinLock/node::lockGuard machinery below is the
     * start of that, and v_mutexCutoff is stored ready for it, but nothing selects
     * between the two yet: every node default-constructs to mutexLock, and the old
     * per-node locking it supported was not actually hand-over-hand (add() released
     * each node's lock before descending, so the traversal was never safe).
     *
     * @struct mutexLock a shared_mutex wrapper, intended for shallow nodes
     * @struct spinLock  an atomic_flag spinner, intended for deep nodes
     * @struct node      one character, its children, and its (currently unused) lock
     * @details has methods for adding words, removing words, removing all instances of word, clear entire tree, 
     * and create vector of all words (returned as a string)
     */   
    struct mutexLock{
        private:
        std::shared_mutex mtx;

        public:
        void unlock(){
            this->mtx.unlock();
        }

        void lock(){
            this->mtx.lock();
        }

    };

    struct spinLock{
        private:
        std::atomic_flag atomic_flag = ATOMIC_FLAG_INIT;

        public:
        void lock(){
            while(atomic_flag.test_and_set(std::memory_order_acquire)){/*spin infinitely*/}
        }

        void unlock(){
            atomic_flag.clear(std::memory_order_release);
        }


    };
struct node {
    char value;
    bool isEndpoint;
    bool isRoot;
    std::atomic<sizeT> count;
    std::vector<std::unique_ptr<node>> childrenNodes;
    std::variant<mutexLock, spinLock> nodeLock;
    node(char val) : value(val), isEndpoint(false), isRoot(false), count(0) {};

    struct lockGuard {
        std::variant<mutexLock, spinLock>& _lock;
        bool _owns;

        explicit lockGuard(std::variant<mutexLock, spinLock>& l) : _lock(l), _owns(true) {
            std::visit([](auto& lk) { lk.lock(); }, _lock);
        }

        ~lockGuard() {
            if (_owns){ //if owner
                std::visit([](auto& lk) { lk.unlock(); }, _lock);
            }
        }

        lockGuard(const lockGuard&) = delete;
        lockGuard& operator=(const lockGuard&) = delete;

        lockGuard(lockGuard&& other) noexcept : _lock(other._lock), _owns(other._owns) {
            other._owns = false; //transfer ownership, prevent double unlock
        }

            lockGuard& operator=(lockGuard&&) = delete;
            }; //end of lockguard
        };// end of node

    private:
        std::unique_ptr<node> v_root; 
        //these were default-constructed atomics, so they held indeterminate values.
        //getWords() called reserve() on wordCount.
        std::atomic<sizeT> v_nodeCount{0};
        std::atomic<sizeT> wordCount{0};
        sizeT v_mutexCutoff{3};
        mutable std::shared_mutex v_treeLock; //guards structure during reads

        //add() used to call these as two SEPARATE lock acquisitions on the terminal
        //node, on top of the one it already took while walking. count is atomic now,
        //so the endpoint flag is all that needs the lock.
        void markEndpoint(node& n){
            {
                node::lockGuard guard(n.nodeLock);
                n.isEndpoint = true;
            }
            n.count.fetch_add(1, std::memory_order_relaxed);
        }

        void getAllWordsHelper(std::vector<std::string>& put, const std::string& prefix, const node* current) const {
            if(current == nullptr){
                return;
            }
    
            std::string currentWord = prefix + current->value; 
    
            if(current->isEndpoint){ //push word into vector here
                const sizeT n = current->count.load(std::memory_order_relaxed);
                for(std::size_t i = 0; i < n; i++){
                put.push_back(currentWord);
            }}

            for(auto& child : current->childrenNodes){ //check the rest here
                getAllWordsHelper(put, currentWord, child.get()); 
            }
        }

        // returns true if the current node should be deleted by its parent
        bool removeHelper(node* current, const string& word, sizeT depth){
            if(depth == word.size()){ //base case
                if(!current->isEndpoint) return false; // word doesn't exist
        
                current->isEndpoint = false;
                current->count.store(0, std::memory_order_relaxed);
        
                // tell parent to delete us only if we have no children(no other words run through this node)
                return current->childrenNodes.empty();
            }

                // find the child matching the next character
            char ch = word[depth];
            node* next = findChildNode(*current, ch);
    
            if(next == nullptr) return false; // word doesn't exist in trie

            bool shouldDeleteChild = removeHelper(next, word, depth + 1);

            if(shouldDeleteChild){
                // caller already holds v_treeLock exclusively
                // erase the unique_ptr from the vector, which triggers child's destructor
                // which cascades down through all of that child's unique_ptr children
                auto itr = std::find_if(current->childrenNodes.begin(), 
                               current->childrenNodes.end(),
                               [ch](const std::unique_ptr<node>& n){ 
                                   return n->value == ch; 
                               });
                if(itr != current->childrenNodes.end()){
                    current->childrenNodes.erase(itr);
                }
            }

            // tell parent to delete us if now a dead non word
            return !current->isEndpoint && current->childrenNodes.empty();
            }


    public: //user methods
        static constexpr sizeT MIN_CUTOFF = 3;

        Trie() {
            v_root = std::make_unique<node>('*');
            v_root->isEndpoint = false;
            v_root->isRoot = true; //was never set anywhere
            v_mutexCutoff = MIN_CUTOFF; // reserved for the hybrid lock design above
        }

        explicit Trie(const string& initial) : Trie() {
            add(initial);
        }

        //this used to throw and immediately catch its own exception, leaving an
        //object in a "failed" state the caller had no way to detect. the guard also
        //tested < 3 while the message said "below 2". clamp instead.
        explicit Trie(sizeT HIGH_CONTENTION_CUTOFF) : Trie() {
            this->v_mutexCutoff = (HIGH_CONTENTION_CUTOFF < MIN_CUTOFF) ? MIN_CUTOFF
                                                                        : HIGH_CONTENTION_CUTOFF;
        }

        Trie(const string& initial, sizeT HIGH_CONTENTION_CUTOFF) : Trie(HIGH_CONTENTION_CUTOFF) {
                add(initial);
        }

        //deletes all of roots children
        void clear(){
            std::unique_lock<std::shared_mutex> writeGuard(v_treeLock);
            v_root->childrenNodes.clear(); //clear the vec and the children go out of scope
            wordCount.store(0);
            v_nodeCount.store(0);
        }

        bool getIsEndPoint(const node* thisNode) const{
            return thisNode != nullptr && thisNode->isEndpoint;
        }

    
        sizeT getWordCount() const{
            return this->wordCount.load(std::memory_order_relaxed);
        }

        sizeT getNodeCount() const{
            return this->v_nodeCount.load(std::memory_order_relaxed);
        }


        //find/getWords used to traverse with no locking at all (find locked only the
        //root), which is a data race against a concurrent add(). they take a shared
        //lock on the tree now; add() takes it exclusively.
        bool find(const string& word) const {
            std::shared_lock<std::shared_mutex> readGuard(v_treeLock);
            const node* current = v_root.get();
            for (char c : word) {
                const node* next = findChildNode(*current, c);
                if (next == nullptr)
                    return false;
                current = next;
            }
            return current->isEndpoint;
        }

    //returns a node at requested position, if not returns nullptr
        static node* findChildNode(node& n, const char lookingFor){
            for(sizeT i = 0; i < n.childrenNodes.size(); i++){ //was int vs size_type
                if(lookingFor == n.childrenNodes[i]->value){
                    return n.childrenNodes[i].get();
                }
            }
            return nullptr;
        }

        static const node* findChildNode(const node& n, const char lookingFor){
            for(sizeT i = 0; i < n.childrenNodes.size(); i++){
                if(lookingFor == n.childrenNodes[i]->value){
                    return n.childrenNodes[i].get();
                }
            }
            return nullptr;
        }

        //took `node` BY VALUE, which cannot compile: node holds unique_ptrs and a
        //shared_mutex, both non-copyable
        sizeT getChildCount(const node& n) const{
            return n.childrenNodes.size();
        }


            //adds entire word to the trie. 
        void add(const string& word){
            if(word.empty()){ //nothing to insert, and it would mark the root an endpoint
                return;
            }

            std::unique_lock<std::shared_mutex> writeGuard(v_treeLock);
            node* current = v_root.get();
            
            for(sizeT i = 0; i < word.length(); i++){
                char ch = word[i];
        
                node* thisChild = findChildNode(*current, ch);
                if(thisChild == nullptr){
                    current->childrenNodes.emplace_back(std::make_unique<node>(ch));
                    thisChild = current->childrenNodes.back().get();
                    v_nodeCount.fetch_add(1, std::memory_order_relaxed);
                }
        
                current = thisChild;
            }
    
            markEndpoint(*current);
            wordCount.fetch_add(1, std::memory_order_relaxed);
    }

    std::vector<string> getWords() const {
        std::shared_lock<std::shared_mutex> readGuard(v_treeLock);
        std::vector<string> r;
        r.reserve(this->getWordCount());
        for(auto& child : v_root->childrenNodes){
            getAllWordsHelper(r, "", child.get());
            }
        return r;
    }

    //return false if the remove fails
    optFlag remove(const string& toRemove) {
        return remove(toRemove, false);
    }

    optFlag remove(const string& toRemove, bool removeAll) {
        if(toRemove.empty()){
            return false;
        }

        std::unique_lock<std::shared_mutex> writeGuard(v_treeLock);

        //walk to the terminal node ourselves rather than calling find(), which
        //would try to take the same lock and deadlock
        node* current = v_root.get();
        for (char c : toRemove) {
            current = findChildNode(*current, c);
            if(current == nullptr){ //return early if we ever hit nullptr
                return false;
            }
        }

        if(!current->isEndpoint){ //if its not a endpoint, dont do anything
            return false;
        }

        if(removeAll){
            //remove all insertions of this word
            sizeT countToRemove = current->count.load(std::memory_order_relaxed);
            removeHelper(v_root.get(), toRemove, 0);
            wordCount.fetch_sub(countToRemove, std::memory_order_relaxed);
        }else{
            //remove only one occurrence
            const sizeT left = current->count.fetch_sub(1, std::memory_order_relaxed) - 1;
            if (left == 0) {
                //no more occurrences, trim node
                removeHelper(v_root.get(), toRemove, 0);
            }
            wordCount.fetch_sub(1, std::memory_order_relaxed);
        }

        return true;
        }



    }; //end of trie

}