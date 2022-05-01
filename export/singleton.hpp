#ifndef __SINGLETON_HPP__
#define __SINGLETON_HPP__


namespace vapi
{

/** class object for a singleton instance class
 * constructor with variable parameters
 */
template<typename T>
class Singleton
{
public:
    /**
     * @brief initialize singleton class object
     *
     *  it will create object with constructor of variable parameters
     *
     * @param args variable parameters of constructor
     * @return reference to the singleton instance
     */
    template<typename... Args>
    [[nodiscard]] static T& initInstance(Args&&... args)
    {
        if (s_m_instance == nullptr)
        {
            s_m_instance = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }

        return *s_m_instance;
    }

    /** @brief get singleton object instance
     * @return reference to the singleton instance */
    [[nodiscard]] static T& getInstance()
    {
        assert(s_m_instance != nullptr);
        return *s_m_instance;
    }

    /** @brief remove default constructor */
    Singleton() = delete;

    /** @brief default destructor */
    virtual ~Singleton() = default;

private:
    static std::unique_ptr<T> s_m_instance;  ///< private object instance
};

/// @brief private instance
template<typename T>
std::unique_ptr<T> Singleton<T>::s_m_instance = nullptr;

} //namespace vapi

#endif
