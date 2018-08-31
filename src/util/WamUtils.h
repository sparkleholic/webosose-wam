/*
    Implementation of string utilities not supported by std::string
*/
#include <string>
#include <unordered_map>

class WamUtils {
    public :
    static void findAndReplaceAll(std::string& data, std::string toSearch, std::string replaceStr)
    {
        size_t pos = data.find(toSearch);
        while( pos != std::string::npos) {
            data.replace(pos, toSearch.size(), replaceStr);
            pos = data.find(toSearch, pos + toSearch.size());
        }
    }

    static void parseURL(std::string url, std::unordered_map<std::string, std::string>& urlInfo) {
        // NOTE : It's temparary solution and not tested.
        // TODO : Find better solution eg. boost
        std::size_t pos1 = url.find_first_of("http");
        std::size_t pos2 = url.find_first_of("https"); 
        bool ishttp = (pos1 == 0);
        bool ishttps = (pos2 == 0);
        std::string new_url = url;

        if(ishttp || ishttps) {
            std::size_t found = url.find_first_of(":");
            std::string protocol = url.substr(0,found); 
            new_url = ishttp ? url.substr(found + 3) : url.substr(found + 4);
            urlInfo.insert(make_pair("PROTOCOL", protocol));
        }

        size_t found1 = new_url.find_first_of(":");
        std::string host = new_url.substr(0,found1);

        size_t found2 = new_url.find_first_of("/");
        std::string port = new_url.substr(found1 + 1 , found2 - found1 - 1);
        std::string path = new_url.substr(found2);

        urlInfo.insert(make_pair("HOST", host));
        urlInfo.insert(make_pair("PORT", port));
        urlInfo.insert(make_pair("PATH", path));
    }
};
