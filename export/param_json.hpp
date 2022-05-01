#ifndef __PARAM_JSON_HPP__
#define __PARAM_JSON_HPP__

#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include <libvapi/singleton.hpp>

using Json = nlohmann::json;

namespace vapi
{

class ParamJson
{
public:
    ParamJson(std::string& jsonFile);
    virtual ~ParamJson() = default;

    /**
     * @brief get parameters from json item
     *
     * @param item: json item name with comma seperated, chip.gpu_param.device_id
     * @param param: output value, vector or basic type.
     * @return true: success to get parameters
     *         false: failed to get parameters for item not exist or output type incorrect.
     */
    [[nodiscard]] bool getJsonParam(std::string item, auto& param)
    {
        std::vector<std::string> name;
        int32_t ret = splitStr(item, '.', name);
        uint32_t count = static_cast<uint32_t>(name.size());
        if ((ret < 0) || (count < 1) || (m_jsonState == false))
            return false;

        Json obj;
        try {
            obj = m_jsonObj.at(name[0]);
            for (uint32_t i = 1; i < count; i++)
                obj = obj.at(name[i]);
        }
        catch (std::exception &) {
            std::cout << "no json item for " << item << "..." << std::endl;
            return false;
        }

        try {
            obj.get_to(param);
        }
        catch (std::exception &) {
            std::cout << "json param or output type error for " << item << "..." << std::endl;
            return false;
        }

        return true;
    }

private:
    void jsonParse(std::string& chipName);
    int32_t splitStr(std::string str, char flag, std::vector<std::string>& output);

    Json m_jsonObj;
    bool m_jsonState;
};


}  // namespace diag

#endif
