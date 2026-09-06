#include <filesystem>


namespace swift_search{

struct options{
    using fsPath = std::filesystem::path;
    using u16 = u_int16_t;
    private:
    bool _keepRunning;
    bool _keepHashed;
    bool _onlyHashed;
    bool _NO_CAPITALS_FLAG;
    bool _SHOW_THREADTIME_FLAG;
    bool _FLOAT_NUMBER_LIST_FLAG;
    u16 threads;
    std::filesystem::path outputDir;
    std::filesystem::path _hashedoutPut;



    


    std::string_view hashedName(const std::string_view& _name){
        std::string v = static_cast<std::string>(_name);
        v += ("_hashed");
        return static_cast<std::string_view>(v);
    }

    public:


    /**
     * full access constructor
     * @param `FOLDER_NAME` name of folder for output files.
     * @param `KEEP_HASHED` Keep an unreadable, non plaintext lookup on disk
     * @param `HASHED_FOLDER_NAME` name of hashed folder
     * @param `ONLY_HASHED` should swiftSearch only have the hashed version on disk?
     * @returns an `options` object to be passed into swiftSearch class.
     * @throws `runtime_exception` if only hashed is selected and keep hashed is not selected
     */
    options(const std::string_view& FOLDER_NAME, const bool& KEEP_HASHED, 
        const std::string_view& HASHED_FOLDER_NAME, const bool& ONLY_HASHED){

        _keepHashed = KEEP_HASHED;

        if(ONLY_HASHED &&!KEEP_HASHED){
            std::__throw_runtime_error
            ("Error in swiftSearch `options` constructor: Cannot select only_hashed and not select keep hashed, check and try again.");
        }

        
        if(FOLDER_NAME.empty()){
            outputDir = "swiftSearchOutput";
        }else{
            outputDir = FOLDER_NAME;
        }
        if(HASHED_FOLDER_NAME.empty() && KEEP_HASHED){
            std::string hashedName{FOLDER_NAME};
            hashedName += "_hashed";
            _hashedoutPut = hashedName;
        }else{
            _hashedoutPut = HASHED_FOLDER_NAME;
        }
    } 

    /**
     * Preferred constructor
     * @param `IS_CLI_MODE` does swiftSearch run in the legacy-style CLI harness?
     * @param `KEEP_RUNNING` does swiftSearch keep running to allow for additional input?
     * @param `FOLDER_NAME` name of folder for output files.
     * @param `KEEP_HASHED` Keep an unreadable, non plaintext lookup on disk
     * @returns an `options` object to be passed into swiftSearch class.
     */
    options(const std::string_view& FOLDER_NAME, const bool& KEEP_HASHED) noexcept{
        std::string_view noName = "";
        options(FOLDER_NAME, KEEP_HASHED, noName, false);


    }
    /**
     * @attention Recommend to not use the default constructor.
     * Blank constructor leads to old swiftSearch behavior (CLI mode, single run)
     */
    options() noexcept{
        options("swiftSearchOutput", false);
    }
};

struct CLIoptions{
    bool interactive;
    bool keepRunning;
    options Engine;
};


}