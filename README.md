# eventbus

单头文件的 C++ 事件总线

## 使用

仅头文件，将[eventbus.hpp](https://github.com/geoyee/eventbus/blob/main/eventbus.hpp)拷贝到目标项目，或添加为子模块，链接`eventbus`即可。运行示例[main.cpp](https://github.com/geoyee/eventbus/blob/main/sample/main.cpp)可以使用 CMake 直接构建此项目。

仅头文件，将[hypara.hpp](./hypara.hpp)拷贝到目标项目，或添加为子模块，链接`hypara`即可。运行示例[main.cpp](./sample/main.cpp)可以使用 CMake 直接构建此项目。

## 示例

```c++
#include <eventbus.hpp>

int main()
{
    // 发送通知
    PropertyMap data;
    data["a"] = std::string("xxx");
    data["b"] = 1;
    data["c"] = std::vector<doube>{1.1, 2.2, 3.3};
    EventBus::GetInstance().("DATA-UPDATE", data);

    // 注册通知
    auto id = EventBus::GetInstance().listen("DATA-UPDATE",
                                             [](const PropertyMap& data)
                                             {
                                                 // 获取数据
                                                 auto a = data.at("a").get_cref<std::string>();
                                                 auto b = data.at("b").get_cref<int>();
                                                 auto c = data.at("c").get_cref<std::vector<doube>>();
                                                 // 相关操作
                                                 ...
                                             });

    // 注销通知
    EventBus::GetInstance().unlisten("DATA-UPDATE", id);

    return 0;
}
```
