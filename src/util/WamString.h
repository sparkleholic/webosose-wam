/*
    Implementation of string utilities not supported by std::string
*/
#include <string>

class WamString {
    public :
    explicit WamString(std::string& str) : s(str) {
        m_count = 1;
        m_pos = 0;
    }
    
    void arg(std::string replaceStr) {
        std::string toSearch = "%" + std::to_string(m_count);
        size_t pos = s.find(toSearch, m_pos); 
        if (pos != std::string::npos) {
            s.replace(pos, toSearch.size(), replaceStr);
            m_count++;
            m_pos = pos + toSearch.size();
        }
    }

    static void findAndReplaceAll(std::string& data, std::string toSearch, std::string replaceStr)
    {
        size_t pos = data.find(toSearch);
        while( pos != std::string::npos) {
            data.replace(pos, toSearch.size(), replaceStr);
            pos = data.find(toSearch, pos + toSearch.size());
        }
    }

    private :
    WamString (const WamString&) = delete;
    WamString& operator= (const WamString&) = delete;
    std::string& s;
    unsigned int m_count;
    unsigned int m_pos;
};