#include <libvapi/param_json.hpp>
#include <fstream>

vapi::ParamJson::ParamJson(std::string& jsonFile) :
    m_jsonState(false)
{
    jsonParse(jsonFile);
}

void vapi::ParamJson::jsonParse(std::string& jsonFile)
{
    std::ifstream file(jsonFile);
    if (!file.is_open()) {
        std::cout << "open json file " << jsonFile << " failed..." << std::endl;
        return;
    }

    file >> m_jsonObj;
    if (m_jsonObj.is_discarded()) {
        std::cout << "parse " << jsonFile << " data failed..." << std::endl;
        return;
    }

    m_jsonState = true;
}

int32_t vapi::ParamJson::splitStr(std::string strSrc, char flag, std::vector<std::string>& output)
{
    if (strSrc.empty() || !flag)
        return -1;

    std::string strContent = strSrc;
    std::string strTemp;
    std::string::size_type begin = 0, end = 0;
    while (1) {
        end = strContent.find(flag, begin);
        if (end == std::string::npos) {
            strTemp = strContent.substr(begin, strContent.length());
            if (!strTemp.empty())
                output.push_back(strTemp);
            break;
        }

        strTemp = strContent.substr(begin, end-begin);
        begin = end + 1;
        output.push_back(strTemp);
    }

    return static_cast<int32_t>(output.size());
}
