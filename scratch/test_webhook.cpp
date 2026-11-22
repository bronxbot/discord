#include <dpp/dpp.h>
#include <iostream>
int main() {
    dpp::message msg;
    msg.webhook_username = "test";
    msg.webhook_avatar_url = "test_url";
    std::cout << "success" << std::endl;
    return 0;
}
