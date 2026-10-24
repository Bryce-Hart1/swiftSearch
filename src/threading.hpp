#include <thread>
#include "constants.hpp"


inline void joinThreads(std::vector<std::thread>& threadVec){
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(const std::exception& e){
            print::swiftSearch_logbook.write("joinThreads(): failed to join thread", 
                print::swiftSearch_logbook.HIGH);
        }
    }
}
