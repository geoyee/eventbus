#include <eventbus.hpp>
#include <chrono>
#include <random>

int main()
{
    std::thread simulate(
        []()
        {
            for (int i = 0; i < 10; ++i)
            {
                PropertyMap data;
                data["time"] = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                data["size"] = i;
                data["value"] = (rand() % 100) / 100.0;
                EventBus::GetInstance().notify("DATA-UPDATE", data);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

    EventBus::GetInstance().listen("DATA-UPDATE",
                                   [](const PropertyMap& data)
                                   {
                                       std::cout << "[" << data.at("time").get_cref<std::string>() << "] "
                                                 << data.at("size").get_cref<int>() << " -> "
                                                 << data.at("value").get_cref<double>() << std::endl;
                                   });

    if (simulate.joinable())
    {
        simulate.join();
    }

    return 0;
}