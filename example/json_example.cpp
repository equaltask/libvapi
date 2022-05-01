
#include <libvapi/param_json.hpp>

int main(int argc, char **argv)
{
    auto showVector = [](std::string comment, auto &val)
    {
        std::cout << "    " << comment << ":";
        for (auto& item : val)
            std::cout << " " << item;
        std::cout << std::endl;
    };
    auto showParam = [](std::string comment, auto &val)
    {
        std::cout << "    " << comment << ": " << val << std::endl;
    };

    std::string chipName = "device.json";
    auto instance = vapi::Singleton<vapi::ParamJson>::initInstance(chipName);
    std::string strVal;
    uint32_t uintVal;
    std::vector<std::string> vectStrVal;
    std::vector<uint32_t> vectIntVal;

    std::cout << "chip param:" << std::endl;
    bool ret = instance.getJsonParam("chip.device_name", strVal);
    if (ret)
        showParam("device name", strVal);
    ret = instance.getJsonParam("chip.product_name", strVal);
    if (ret)
        showParam("product name", strVal);
    ret = instance.getJsonParam("chip.device_type", strVal);
    if (ret)
        showParam("type", strVal);
    ret = instance.getJsonParam("chip.addr_lib_features", vectIntVal);
    if (ret)
        showVector("addr_lib_features", vectIntVal);

    std::cout << "gpu param:" << std::endl;
    ret = instance.getJsonParam("chip.gpu_param.device_id", vectIntVal);
    if (ret)
        showVector("deviceId", vectIntVal);
    ret = instance.getJsonParam("chip.gpu_param.feature", vectStrVal);
    if (ret)
        showVector("feature", vectStrVal);
    ret = instance.getJsonParam("chip.gpu_param.root_complex_devid", uintVal);
    if (ret)
        showParam("root_complex devid", uintVal);

    std::cout << "cpu param:" << std::endl;
    ret = instance.getJsonParam("chip.cpu_param.base_model", vectIntVal);
    if (ret)
        showVector("baseModel", vectIntVal);

    return 0;
}
