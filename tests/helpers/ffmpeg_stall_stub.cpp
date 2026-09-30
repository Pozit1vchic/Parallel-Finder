#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "-encoders") {
            std::cout << " V..... libx264 test encoder\n";
            return 0;
        }
    }
    // A live process with an initial progress record but no further advance.
    std::cout << "out_time_us=0\nprogress=continue\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
