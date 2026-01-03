#ifndef TOOLS_SETTINGS_H
#define TOOLS_SETTINGS_H

#include <string>

namespace Tools {

// Khởi tạo HTTP server để phục vụ settings page
// Gọi hàm này sau khi WiFi kết nối thành công
void StartSettingsHttpServer();

}  // namespace Tools

#endif  // TOOLS_SETTINGS_H
