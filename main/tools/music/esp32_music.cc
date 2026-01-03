#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>  // For isdigit function
#include <thread>   // For thread ID comparison
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"


// Try primary server first, then fallback servers if primary fails
static const std::vector<std::string> FALLBACK_MUSIC_URLS = {
    "http://14.225.204.77:5005",   // Primary server
    "http://110.42.59.54:2233"     // Fallback server 1
};

// ========== Simple ESP32 Authentication Function ==========

/**
 * @brief Get device MAC address
 * @return MAC address string
 */
static std::string get_device_mac() {
    return SystemInfo::GetMacAddress();
}

/**
 * @brief Get device chip ID
 * @return Chip ID string
 */
static std::string get_device_chip_id() {
    // Use MAC address as chip ID, remove colon separators
    std::string mac = SystemInfo::GetMacAddress();
    // Remove all colons
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return mac;
}

/**
 * @brief Generate dynamic key
 * @param timestamp Timestamp
 * @return Dynamic key string
 */
static std::string generate_dynamic_key(int64_t timestamp) {
    // Secret key (please modify to match the server)
    const std::string secret_key = "your-esp32-secret-key-2024";
    
    // Get device information
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // Combine data: MAC:Chip ID:Timestamp:Secret Key
    std::string data = mac + ":" + chip_id + ":" + std::to_string(timestamp) + ":" + secret_key;
    
    // SHA256 hash
    unsigned char hash[32];
    mbedtls_sha256((unsigned char*)data.c_str(), data.length(), hash, 0);
    
    // Convert to hexadecimal string (first 16 bytes)
    std::string key;
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", hash[i]);
        key += hex;
    }
    
    return key;
}

/**
 * @brief Add authentication headers to HTTP request
 * @param http HTTP client pointer
 */
static void add_auth_headers(Http* http) {
    // Get current timestamp
    int64_t timestamp = esp_timer_get_time() / 1000000;  // Convert to seconds
    
    // Generate dynamic key
    std::string dynamic_key = generate_dynamic_key(timestamp);
    
    // Get device information
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // Add authentication headers
    if (http) {
        http->SetHeader("X-MAC-Address", mac);
        http->SetHeader("X-Chip-ID", chip_id);
        http->SetHeader("X-Timestamp", std::to_string(timestamp));
        http->SetHeader("X-Dynamic-Key", dynamic_key);
        
        ESP_LOGI(TAG, "Added auth headers - MAC: %s, ChipID: %s, Timestamp: %lld", 
                 mac.c_str(), chip_id.c_str(), timestamp);
    }
}

// URL encoding function with UTF-8 support
static std::string url_encode(const std::string& str) {
    std::string encoded;
    static const char hex_chars[] = "0123456789ABCDEF";
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // Space encoded as '+' for form data
        } else {
            // Handle UTF-8 multi-byte sequences
            encoded += '%';
            encoded += hex_chars[c >> 4];
            encoded += hex_chars[c & 0x0F];
        }
    }
    return encoded;
}

Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                         song_name_displayed_(false), current_lyric_url_(), lyrics_(), 
                         current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                         display_mode_(DISPLAY_MODE_SPECTRUM), is_playing_(false), is_downloading_(false), 
                         play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(), 
                         buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(), 
                         mp3_decoder_initialized_(false) {
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");
    
    // Stop all operations
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    
    // Notify all waiting threads
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // Wait for download thread to finish with 5-second timeout
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
        auto start_time = std::chrono::steady_clock::now();
        
        // Wait for thread to finish
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 5) {
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
                break;
            }
            
            // Set stop flag again to ensure thread can detect it
            is_downloading_ = false;
            
            // Notify condition variable
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // Check if the thread has already finished
            if (!download_thread_.joinable()) {
                thread_finished = true;
            }
            
            // Periodically print waiting information
            if (elapsed > 0 && elapsed % 1 == 0) {
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }
        
        if (download_thread_.joinable()) {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }
    
    // Wait for the playback thread to finish, with a 3-second timeout
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
        auto start_time = std::chrono::steady_clock::now();
        
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 3) {
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
                break;
            }
            
            // Set the stop flag again
            is_playing_ = false;
            
            // Notify the condition variable
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // Check if the thread has already finished
            if (!play_thread_.joinable()) {
                thread_finished = true;
            }
        }
        
        if (play_thread_.joinable()) {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Playback thread finished");
    }
    
    // Wait for the lyric thread to finish
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }
    
    // Clear the buffer and MP3 decoder
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Music player destroyed successfully");
}

void Esp32Music::Initialize() {
    ESP_LOGI(TAG, "Initializing music player");
    InitializeMp3Decoder();
}

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "Starting to get music details for: %s by %s", song_name.c_str(), artist_name.c_str());
    
    // Validate input
    if (song_name.empty()) {
        ESP_LOGE(TAG, "Song name is empty");
        return false;
    }
    
    // Clear previous download state
    last_downloaded_data_.clear();
    title_name_.clear();
    artist_name_.clear();
    current_song_name_ = song_name;
    
    // Build list of server URLs to try (primary + fallbacks)
    std::vector<std::string> server_urls;
    
    // Add primary server from settings first
    std::string primary_url = GetCheckMusicServerUrl();
    if (!primary_url.empty()) {
        server_urls.push_back(primary_url);
    }
    
    // Add fallback servers (if not already in list)
    for (const auto& fallback_url : FALLBACK_MUSIC_URLS) {
        if (fallback_url != primary_url && 
            std::find(server_urls.begin(), server_urls.end(), fallback_url) == server_urls.end()) {
            server_urls.push_back(fallback_url);
        }
    }
    
    // Log all servers being used - DISABLED to protect IP addresses
    // ESP_LOGI(TAG, "=== Music Server Configuration ===");
    // ESP_LOGI(TAG, "Total servers configured: %d", (int)server_urls.size());
    // for (size_t i = 0; i < server_urls.size(); i++) {
    //     if (i == 0) {
    //         ESP_LOGI(TAG, "  [PRIMARY] Server %d: %s", (int)i, server_urls[i].c_str());
    //     } else {
    //         ESP_LOGI(TAG, "  [FALLBACK] Server %d: %s", (int)i, server_urls[i].c_str());
    //     }
    // }
    ESP_LOGI(TAG, "Searching for: '%s' by '%s'", song_name.c_str(), artist_name.c_str());
    ESP_LOGI(TAG, "=============================");
    
    // Try each server in the list
    for (size_t server_idx = 0; server_idx < server_urls.size(); server_idx++) {
        std::string base_url = server_urls[server_idx];
        
        // Skip empty URLs
        if (base_url.empty()) {
            continue;
        }
        
        // Log which server we're trying - DISABLED to protect IP addresses
        // if (server_idx == 0) {
        //     ESP_LOGI(TAG, "[%d/%d] Trying PRIMARY server: %s", (int)server_idx + 1, (int)server_urls.size(), base_url.c_str());
        // } else {
        //     ESP_LOGW(TAG, "[%d/%d] PRIMARY server failed, trying FALLBACK server: %s", (int)server_idx + 1, (int)server_urls.size(), base_url.c_str());
        // }
        
        std::string full_url = base_url + "/stream_pcm?song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);
        // ESP_LOGD(TAG, "Request URL: %s", full_url.c_str()); // DISABLED to protect server IP
        
        // Retry logic with exponential backoff (per server)
        int retry_count = 0;
        int max_retries = 3;
        int timeout_ms = 8000;  // 8 seconds timeout
        int backoff_ms = 1000;  // Start with 1 second backoff
        
        // Retry loop for current server
        while (retry_count < max_retries) {
                if (retry_count > 0) {
                ESP_LOGI(TAG, "Retry attempt %d of %d (waiting %dms)", retry_count + 1, max_retries, backoff_ms);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                backoff_ms *= 2;  // Exponential backoff: 1s, 2s, 4s
            }
            
            // Use the HTTP client provided by the Board with explicit timeout
            auto network = Board::GetInstance().GetNetwork();
            auto http = network->CreateHttp(timeout_ms);
            
            if (!http) {
                ESP_LOGE(TAG, "Failed to create HTTP client on server %d", (int)server_idx);
                retry_count++;
                continue;
            }
            
            // Set basic request headers
            http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
            http->SetHeader("Accept", "application/json; charset=utf-8");
            http->SetHeader("Accept-Language", "vi-VN,vi");
            http->SetHeader("Connection", "close");  // Close after response
            
            // Add ESP32 authentication headers
            add_auth_headers(http.get());
            
            // Open GET connection
            if (!http->Open("GET", full_url)) {
                ESP_LOGW(TAG, "Failed to connect to server %d (attempt %d of %d)", 
                         (int)server_idx, retry_count + 1, max_retries);
                retry_count++;
                continue;
            }
            
            // Check the response status code
            int status_code = http->GetStatusCode();
            
            // Accept 2xx status codes
            if (status_code < 200 || status_code >= 300) {
                ESP_LOGW(TAG, "HTTP GET failed with status code: %d on server %d (attempt %d)", 
                         status_code, (int)server_idx, retry_count + 1);
                
                // Retry on 5xx errors or 429 (rate limited)
                if ((status_code >= 500 && status_code < 600) || status_code == 429) {
                    http->Close();
                    retry_count++;
                    continue;
                } else if (status_code == 404) {
                    // 404 means song not found on this server, try next server
                    ESP_LOGI(TAG, "Song not found on server %d (404), trying next server", (int)server_idx);
                    http->Close();
                    break;  // Break inner retry loop, try next server
                } else {
                    // Other 4xx errors, try next server
                    http->Close();
                    break;  // Try next server
                }
            }
            
            // Read the response data
            last_downloaded_data_ = http->ReadAll();
            http->Close();
            
            ESP_LOGI(TAG, "HTTP GET Status = %d, response_length = %d", status_code, last_downloaded_data_.length());
            
            // Validate response is not empty
            if (last_downloaded_data_.empty()) {
                ESP_LOGW(TAG, "Empty response from server %d (attempt %d)", (int)server_idx, retry_count + 1);
                retry_count++;
                continue;
            }
            
            // Log response for debugging (limited to 200 chars)
            if (last_downloaded_data_.length() > 200) {
                ESP_LOGD(TAG, "Music details response (first 200 chars): %.200s...", last_downloaded_data_.c_str());
            } else {
                ESP_LOGD(TAG, "Music details response: %s", last_downloaded_data_.c_str());
            }
            
            // Check for authentication failure
            if (last_downloaded_data_.find("ESP32动态密钥验证失败") != std::string::npos) {
                ESP_LOGE(TAG, "Authentication failed for song: %s on server %d", song_name.c_str(), (int)server_idx);
                break;  // Try next server
            }
            
            // Parse the response JSON to extract the audio URL
            cJSON* response_json = cJSON_Parse(last_downloaded_data_.c_str());
            if (!response_json) {
                const char* error_ptr = cJSON_GetErrorPtr();
                ESP_LOGW(TAG, "Failed to parse JSON response from server %d (attempt %d)", 
                         (int)server_idx, retry_count + 1);
                if (error_ptr != NULL) {
                    ESP_LOGD(TAG, "JSON parse error at: %s", error_ptr);
                }
                retry_count++;
                continue;
            }
            
            // Check if server returned error status (old format)
            cJSON* status = cJSON_GetObjectItem(response_json, "status");
            if (cJSON_IsString(status)) {
                if (strcmp(status->valuestring, "error") == 0) {
                    cJSON* error_msg = cJSON_GetObjectItem(response_json, "message");
                    if (cJSON_IsString(error_msg)) {
                        ESP_LOGW(TAG, "Server %d returned error: %s", (int)server_idx, error_msg->valuestring);
                    } else {
                        ESP_LOGW(TAG, "Server %d returned error status", (int)server_idx);
                    }
                    cJSON_Delete(response_json);
                    break;  // Try next server
                }
            }
            
            // Check if this is the new format with "songs" array
            cJSON* songs_array = cJSON_GetObjectItem(response_json, "songs");
            if (cJSON_IsArray(songs_array)) {
                // New JSON format with multiple songs
                ESP_LOGI(TAG, "=== NEW FORMAT: Multiple Songs Found ===");
                
                // Try each song in the array
                cJSON* song_item = NULL;
                int song_idx = 0;
                
                cJSON_ArrayForEach(song_item, songs_array) {
                    song_idx++;
                    if (!cJSON_IsObject(song_item)) {
                        ESP_LOGW(TAG, "Song item %d is not an object, skipping", song_idx);
                        continue;
                    }
                    
                    // Extract song information
                    cJSON* artist = cJSON_GetObjectItem(song_item, "artist");
                    cJSON* title = cJSON_GetObjectItem(song_item, "title");
                    cJSON* audio_url_item = cJSON_GetObjectItem(song_item, "audio_url");
                    cJSON* lyric_url = cJSON_GetObjectItem(song_item, "lyric_url");
                    
                    // Validate required fields - audio_url is mandatory
                    if (!cJSON_IsString(audio_url_item) || !audio_url_item->valuestring) {
                        ESP_LOGW(TAG, "Song %d has no valid audio_url, skipping", song_idx);
                        continue;
                    }
                    
                    std::string audio_url_path = audio_url_item->valuestring;
                    
                    // Extract optional fields
                    if (cJSON_IsString(artist) && artist->valuestring && strlen(artist->valuestring) > 0) {
                        artist_name_ = artist->valuestring;
                    } else {
                        artist_name_.clear();
                    }
                    
                    if (cJSON_IsString(title) && title->valuestring && strlen(title->valuestring) > 0) {
                        title_name_ = title->valuestring;
                    } else {
                        title_name_.clear();
                    }
                    
                    ESP_LOGI(TAG, "Trying song %d/%d: Title=%s, Artist=%s", 
                             song_idx, cJSON_GetArraySize(songs_array),
                             title_name_.empty() ? "Unknown" : title_name_.c_str(),
                             artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                    
                    // Construct the full audio URL
                    if (audio_url_path.find("http://") == 0 || audio_url_path.find("https://") == 0) {
                        // Already a full URL
                        current_music_url_ = audio_url_path;
                    } else if (audio_url_path[0] == '/') {
                        // Relative path from server root, append to base URL
                        current_music_url_ = base_url + audio_url_path;
                    } else {
                        // Shouldn't happen, but handle it
                        current_music_url_ = base_url + "/" + audio_url_path;
                    }
                    
                    // ESP_LOGI(TAG, "Testing proxy URL: %s", current_music_url_.c_str());
                    // Try to connect to this proxy URL to validate it works
                    auto test_http = Board::GetInstance().GetNetwork()->CreateHttp(5000);  // 5 second timeout for test
                    if (!test_http) {
                        ESP_LOGW(TAG, "Failed to create HTTP client for song %d test", song_idx);
                        ESP_LOGI(TAG, "✗ Song %d FAILED - Could not create HTTP client", song_idx);
                        continue;
                    }
                    
                    test_http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
                    test_http->SetHeader("Connection", "close");
                    add_auth_headers(test_http.get());
                    
                    // Attempt to open connection to proxy URL
                    if (!test_http->Open("GET", current_music_url_)) {
                        ESP_LOGW(TAG, "Failed to connect to proxy URL for song %d", song_idx);
                        ESP_LOGI(TAG, "==================================================");
                        ESP_LOGI(TAG, "✗ Song %d FAILED - Connection error", song_idx);
                        ESP_LOGI(TAG, "  Title: %s", title_name_.empty() ? "Unknown" : title_name_.c_str());
                        ESP_LOGI(TAG, "  Artist: %s", artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                        // ESP_LOGI(TAG, "  URL: %s", current_music_url_.c_str());
                        ESP_LOGI(TAG, "  Trying next song...");
                        ESP_LOGI(TAG, "==================================================");
                        test_http->Close();
                        continue;
                    }
                    
                    int test_status = test_http->GetStatusCode();
                    
                    if (test_status == 200 || test_status == 206) {
                        // Successfully connected and got valid status
                        test_http->Close();
                        
                        ESP_LOGI(TAG, "==================================================");
                        ESP_LOGI(TAG, "✓ Song %d is PLAYABLE! Status=%d", song_idx, test_status);
                        ESP_LOGI(TAG, "  Title: %s", title_name_.empty() ? "Unknown" : title_name_.c_str());
                        ESP_LOGI(TAG, "  Artist: %s", artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                        // ESP_LOGI(TAG, "  URL: %s", current_music_url_.c_str());
                        ESP_LOGI(TAG, "==================================================");
                        
                        // Display early info on LCD
                        if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                            auto display = Board::GetInstance().GetDisplay();
                            if (display) {
                                char buf[256];
                                snprintf(buf, sizeof(buf),
                                         "ONLINE 《%s》\n%s • Đang phát...",
                                         title_name_.empty() ? song_name.c_str() : title_name_.c_str(),
                                         artist_name_.empty() ? "Unknown Artist" : artist_name_.c_str());
                                display->SetMusicInfo(buf);
                                ESP_LOGI(TAG, "Early display info: %s", buf);
                            }
                        }
                        
                        // Start streaming with this working URL
                        ESP_LOGI(TAG, "=== MUSIC FOUND ===");
                        ESP_LOGI(TAG, "Song: %s (Song %d of %d)", song_name.c_str(), song_idx, cJSON_GetArraySize(songs_array));
                        // ESP_LOGI(TAG, "Server %d: %s", (int)server_idx + 1, base_url.c_str()); // DISABLED to protect IP
                        ESP_LOGI(TAG, "Title: %s | Artist: %s", 
                                title_name_.empty() ? "Unknown" : title_name_.c_str(),
                                artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                        // ESP_LOGI(TAG, "Audio URL: %s", current_music_url_.c_str());
                        ESP_LOGI(TAG, "=============================");
                        ESP_LOGI(TAG, "Starting streaming playback for: %s (from server %d)", song_name.c_str(), (int)server_idx + 1);
                        
                        song_name_displayed_ = false;
                        full_info_displayed_ = false;
                        
                        // ESP_LOGI(TAG, "***** SELECTED SONG TO PLAY *****");
                        // ESP_LOGI(TAG, "Final URL for playback: %s", current_music_url_.c_str());
                        // ESP_LOGI(TAG, "*********************************");
                        
                        StartStreaming(current_music_url_);
                        
                        // Handle lyric URL if provided
                        if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                            std::string lyric_path = lyric_url->valuestring;
                            
                            if (lyric_path.find("http://") == 0 || lyric_path.find("https://") == 0) {
                                current_lyric_url_ = lyric_path;
                            } else if (lyric_path[0] == '/') {
                                current_lyric_url_ = base_url + lyric_path;
                            } else {
                                current_lyric_url_ = base_url + "/" + lyric_path;
                            }
                            
                            if (display_mode_ == DISPLAY_MODE_LYRICS) {
                                ESP_LOGI(TAG, "Loading lyrics for: %s", song_name.c_str());
                                
                                if (is_lyric_running_) {
                                    is_lyric_running_ = false;
                                    if (lyric_thread_.joinable()) {
                                        lyric_thread_.join();
                                    }
                                }
                                
                                is_lyric_running_ = true;
                                current_lyric_index_ = -1;
                                lyrics_.clear();
                                
                                lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                            }
                        }
                        
                        cJSON_Delete(response_json);
                        return true;  // ✅ Success with working song
                        
                    } else {
                        test_http->Close();
                        
                        //ESP_LOGI(TAG, "==================================================");
                        //ESP_LOGI(TAG, "✗ Song %d FAILED - HTTP %d", song_idx, test_status);
                        //ESP_LOGI(TAG, "  Title: %s", title_name_.empty() ? "Unknown" : title_name_.c_str());
                        //ESP_LOGI(TAG, "  Artist: %s", artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                        //ESP_LOGI(TAG, "  URL: %s", current_music_url_.c_str());
                        //ESP_LOGI(TAG, "  Trying next song...");
                        //ESP_LOGI(TAG, "==================================================");
                        continue;
                    }
                }  // End of cJSON_ArrayForEach loop
                
                // No songs in array were playable
                ESP_LOGW(TAG, "None of the %d songs in the array were playable", cJSON_GetArraySize(songs_array));
                cJSON_Delete(response_json);
                break;  // Try next server
                
            } else {
                // Old format - single song response
                ESP_LOGI(TAG, "=== OLD FORMAT: Single Song Response ===");
                
                // Extract key information
                cJSON* artist    = cJSON_GetObjectItem(response_json, "artist");
                cJSON* title     = cJSON_GetObjectItem(response_json, "title");
                cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
                cJSON* lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
                
                // Validate and store artist/title
                if (cJSON_IsString(artist) && artist->valuestring && strlen(artist->valuestring) > 0) {
                    artist_name_ = artist->valuestring;
                    ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
                }
                
                if (cJSON_IsString(title) && title->valuestring && strlen(title->valuestring) > 0) {
                    title_name_ = title->valuestring;
                    ESP_LOGI(TAG, "Title: %s", title->valuestring);
                }
                
                // Hiển thị thông tin bài hát sớm (nếu có LCD)
                if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                    auto display = Board::GetInstance().GetDisplay();
                    if (display) {
                        char buf[256];
                        snprintf(buf, sizeof(buf),
                                 "ONLINE 《%s》\n%s • Đang phát...",
                                 title_name_.empty() ? song_name.c_str() : title_name_.c_str(),
                                 artist_name_.empty() ? "Unknown Artist" : artist_name_.c_str());
                        display->SetMusicInfo(buf);
                        ESP_LOGI(TAG, "Early display info: %s", buf);
                    }
                }
            
                // Check if audio_url is valid
                if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                    // ESP_LOGI(TAG, "Audio URL path: %s", audio_url->valuestring); // DISABLED to protect URL
                    
                    // Step 2: Construct the complete audio download URL
                    std::string audio_path = audio_url->valuestring;
                    
                    // If audio_path starts with '/' (relative path from server), it's a path returned by server
                    // If it starts with 'http', it's already a full URL
                    if (audio_path.find("http://") == 0 || audio_path.find("https://") == 0) {
                        // Already a full URL, use as-is
                        current_music_url_ = audio_path;
                        // ESP_LOGD(TAG, "Audio URL is already full: %s", audio_path.c_str());
                    } else if (audio_path[0] == '/') {
                        // Relative path from server root, just append to base URL (don't re-encode)
                        // Server already provided the path with proper encoding/parameters
                        current_music_url_ = base_url + audio_path;
                        // ESP_LOGD(TAG, "Constructed audio URL from relative path: %s", current_music_url_.c_str()); // DISABLED to protect URL
                    } else {
                        // Shouldn't happen, but handle it
                        current_music_url_ = base_url + "/" + audio_path;
                        // ESP_LOGD(TAG, "Audio URL: %s", current_music_url_.c_str());
                    }
                    
                    ESP_LOGI(TAG, "=== MUSIC FOUND ===");
                    ESP_LOGI(TAG, "Song: %s", song_name.c_str());
                    // ESP_LOGI(TAG, "Server %d: %s", (int)server_idx + 1, base_url.c_str()); // DISABLED to protect IP
                    ESP_LOGI(TAG, "Title: %s | Artist: %s", 
                            title_name_.empty() ? "Unknown" : title_name_.c_str(),
                            artist_name_.empty() ? "Unknown" : artist_name_.c_str());
                    // ESP_LOGI(TAG, "Audio URL: %s", current_music_url_.c_str());
                    // ESP_LOGI(TAG, "=============================");
                    // ESP_LOGI(TAG, "Starting streaming playback for: %s (from server %d)", song_name.c_str(), (int)server_idx + 1);
                    song_name_displayed_ = false;
                    full_info_displayed_ = false;
                    
                    StartStreaming(current_music_url_);
                    
                    // Handle lyric URL - only start lyrics in lyric display mode
                    if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                        std::string lyric_path = lyric_url->valuestring;
                        
                        // If lyric_path starts with '/' (relative path from server), append to base URL
                        // If it starts with 'http', it's already a full URL
                        if (lyric_path.find("http://") == 0 || lyric_path.find("https://") == 0) {
                            current_lyric_url_ = lyric_path;
                            // ESP_LOGD(TAG, "Lyric URL is already full: %s", lyric_path.c_str()); // DISABLED to protect URL
                        } else if (lyric_path[0] == '/') {
                            // Relative path from server root, just append to base URL
                            current_lyric_url_ = base_url + lyric_path;
                            // ESP_LOGD(TAG, "Constructed lyric URL from relative path: %s", current_lyric_url_.c_str()); // DISABLED to protect URL
                        } else {
                            current_lyric_url_ = base_url + "/" + lyric_path;
                            // ESP_LOGD(TAG, "Lyric URL: %s", current_lyric_url_.c_str()); // DISABLED to protect URL
                        }
                        
                        // Decide whether to start lyrics based on display mode
                        if (display_mode_ == DISPLAY_MODE_LYRICS) {
                            ESP_LOGI(TAG, "Loading lyrics for: %s (lyrics display mode)", song_name.c_str());
                            
                            // Start lyric download and display
                            if (is_lyric_running_) {
                                is_lyric_running_ = false;
                                if (lyric_thread_.joinable()) {
                                    lyric_thread_.join();
                                }
                            }
                            
                            is_lyric_running_ = true;
                            current_lyric_index_ = -1;
                            lyrics_.clear();
                            
                            lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                        } else {
                            ESP_LOGI(TAG, "Spectrum mode active, skipping lyrics");
                        }
                    } else {
                        // ESP_LOGD(TAG, "No lyric URL found for this song"); // DISABLED to protect URL
                    }
                    
                    cJSON_Delete(response_json);
                    return true;  // ✅ Success with current server
                        
                    } else {
                        // audio_url is empty or invalid
                        ESP_LOGW(TAG, "Audio URL not found or empty from server %d (attempt %d)", 
                                 (int)server_idx, retry_count + 1);
                        
                        // Check if response indicates "not found"
                        if (last_downloaded_data_.find("not found") != std::string::npos ||
                            last_downloaded_data_.find("404") != std::string::npos) {
                            // Song genuinely not found, try next server
                            cJSON_Delete(response_json);
                            break;  // Try next server
                        }
                        
                        // Other reason, retry
                        cJSON_Delete(response_json);
                        retry_count++;
                        continue;
                    }
            }
        }  // end of retry loop for current server
        
        // If we reach here, current server failed all retries
        // Continue to next server in outer loop
    }  // end of server loop
    
    // All servers and retries failed
    // ESP_LOGE(TAG, "=== MUSIC SEARCH FAILED ===");
    // ESP_LOGE(TAG, "Failed to find music: '%s' by '%s'", song_name.c_str(), artist_name.c_str());
    // ESP_LOGE(TAG, "Tried %d server(s):", (int)server_urls.size());
    // for (size_t i = 0; i < server_urls.size(); i++) {
    //     ESP_LOGE(TAG, "  Server %d: %s (3 retries, 8s timeout each)", (int)i + 1, server_urls[i].c_str());
    // }
    // ESP_LOGE(TAG, "=============================");
    return false;
}



std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

// Start streaming playback
bool Esp32Music::StartStreaming(const std::string& music_url) {
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    // ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());
    // Stop previous playback and download
    is_downloading_ = false;
    is_playing_ = false;
	
	// ================== DỌN FFT CŨ + RESET FLAG ==================
    {
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->StopFFT();              // Dừng FFT của bài trước
            display->ReleaseAudioBuffFFT();  // Giải phóng buffer FFT
            ESP_LOGI(TAG, "[PATCH] Cleared FFT canvas before starting new song");
        }
    }
    fft_started_         = false;
    full_info_displayed_ = false;
    song_name_displayed_ = false;
    // ============================================================
    
    // Wait for the previous threads to fully terminate
    if (download_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // Notify threads to exit
        }
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // Notify threads to exit
        }
        play_thread_.join();
    }
    
    // Clear the buffer
    ClearAudioBuffer();
    
    // Configure thread stack size to avoid stack overflow
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 1024 * 3;  // 3KB stack size
    cfg.prio = 5;               // Medium priority
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);
    
    // Start the download thread
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    
    // Start the playback thread (will wait for the buffer to have enough data)
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    
    ESP_LOGI(TAG, "Streaming threads started successfully");
    
    return true;
}

// Stop streaming playback
bool Esp32Music::StopStreaming() {
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress to stop");
        return true;
    }

    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d", 
            is_downloading_, is_playing_);

    // Reset the sample rate to the original value
    ResetSampleRate();
    
    // Check if there is any streaming in progress
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }
    
    // Stop download and playback flags
    is_downloading_ = false;
    is_playing_ = false;
    
    // Clear the song name display
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");  // Clear the song name display
        ESP_LOGI(TAG, "Cleared song name display");
    }
    
    // Notify all waiting threads
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // Wait for threads to finish (avoid duplicate code, ensure StopStreaming waits for threads to fully stop)
    if (download_thread_.joinable()) {
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread joined in StopStreaming");
    }
    
    // Wait for the playback thread to finish, using a safer approach
    if (play_thread_.joinable()) {
        // First, set the stop flag
        is_playing_ = false;
        
        // Notify the condition variable to ensure the thread can exit
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }
        
        // Use a timeout mechanism to wait for the thread to finish, avoiding deadlocks
        bool thread_finished = false;
        int wait_count = 0;
        const int max_wait = 100; // Maximum wait time of 1 second
        
        while (!thread_finished && wait_count < max_wait) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
            
            // Check if the thread is still joinable
            if (!play_thread_.joinable()) {
                thread_finished = true;
                break;
            }
        }
        
        if (play_thread_.joinable()) {
            if (wait_count >= max_wait) {
                ESP_LOGW(TAG, "Play thread join timeout, detaching thread");
                play_thread_.detach();
            } else {
                play_thread_.join();
                ESP_LOGI(TAG, "Play thread joined in StopStreaming");
            }
        }
    }
    
    // After threads have fully stopped, stop FFT display only in spectrum mode
	if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
		display->StopFFT();
		display->ReleaseAudioBuffFFT();   
		final_pcm_data_fft = nullptr;     

		ESP_LOGI(TAG, "Stopped FFT display and cleared FFT buffer in StopStreaming (spectrum mode)");
	} else if (display) {
		ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop in StopStreaming");
	}
		
	// Reset FFT flag for next use
	fft_started_ = false;
	
    ESP_LOGI(TAG, "Music streaming stop signal sent");
	
	title_name_.clear();
	artist_name_.clear();
	current_song_name_.clear();

    return true;
}

// Stream audio data
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    // ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str()); // DISABLED to protect URL
    
    // Validate URL
    if (music_url.empty() || music_url.find("http") != 0) {
        // ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str()); // DISABLED to protect URL
        is_downloading_ = false;
        return;
    }
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(30000);  // 30 second timeout for audio streaming
    
    // Set basic request headers
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Accept-Encoding", "identity");  // Don't compress audio
    http->SetHeader("Range", "bytes=0-");  // Support range requests
    http->SetHeader("Connection", "keep-alive");  // Keep connection stable
    http->SetHeader("Cache-Control", "no-cache"); // Avoid stale cache
    
    // Add ESP32 authentication headers
    add_auth_headers(http.get());
    
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "Failed to connect to music stream URL");
        is_downloading_ = false;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {  // 206 for partial content
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }
    
    // ESP_LOGI(TAG, "Started downloading audio stream, status: %d", status_code);
    
    // Read audio data in chunks
    const size_t chunk_size = 4096;  // 4KB per chunk
    char* buffer = new char[chunk_size];
    size_t total_downloaded = 0;
    size_t total_print_bytes = 0;
    int read_attempts = 0;
    int consecutive_zero_reads = 0;
    
    while (is_downloading_ && is_playing_) {
        read_attempts++;
        int bytes_read = http->Read(buffer, chunk_size);
        
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Failed to read audio data on attempt %d: error code %d", read_attempts, bytes_read);
            break;
        }
        if (bytes_read == 0) {
            consecutive_zero_reads++;
            if (consecutive_zero_reads <= 3) {
                ESP_LOGW(TAG, "Read returned 0 bytes (attempt %d, total %d bytes so far)", read_attempts, total_downloaded);
            }
            if (total_downloaded == 0 && consecutive_zero_reads > 5) {
                ESP_LOGE(TAG, "Proxy URL returned 0 bytes after 5 attempts - connection issue or invalid URL");
                break;
            }
            if (consecutive_zero_reads > 3) {
                ESP_LOGI(TAG, "Audio stream download completed, total: %d bytes", total_downloaded);
                break;
            }
            continue;
        }
        
        consecutive_zero_reads = 0;
        
        // Log chunk information
        // ESP_LOGI(TAG, "Downloaded chunk: %d bytes at offset %d", bytes_read, total_downloaded);
        
        // Safely log the first 16 bytes of the chunk in hexadecimal
        if (bytes_read >= 16) {
            // ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...", 
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        } else {
            ESP_LOGI(TAG, "Data chunk too small: %d bytes", bytes_read);
        }
        
        // Attempt to detect file format (check file header)
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "ID3", 3) == 0) {
                ESP_LOGI(TAG, "Detected MP3 file with ID3 tag");
            } else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
                ESP_LOGI(TAG, "Detected MP3 file header");
            } else if (memcmp(buffer, "RIFF", 4) == 0) {
                ESP_LOGI(TAG, "Detected WAV file");
            } else if (memcmp(buffer, "fLaC", 4) == 0) {
                ESP_LOGI(TAG, "Detected FLAC file");
            } else if (memcmp(buffer, "OggS", 4) == 0) {
                ESP_LOGI(TAG, "Detected OGG file");
            } else {
                ESP_LOGI(TAG, "Unknown audio format, first 4 bytes: %02X %02X %02X %02X", 
                        (unsigned char)buffer[0], (unsigned char)buffer[1], 
                        (unsigned char)buffer[2], (unsigned char)buffer[3]);
            }
        }
        
        // Create audio data chunk
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // Wait for buffer space
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
            
            if (is_downloading_) {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                total_print_bytes += bytes_read;
                
                // Notify playback thread of new data
                buffer_cv_.notify_one();
                
                if (total_print_bytes >= (128 * 1024)) {  // Log progress every 128KB
                    total_print_bytes = 0;
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }
    delete[] buffer;
    
    http->Close();

    if (is_downloading_) {
        ESP_LOGI(TAG, "Audio stream download finished successfully, total downloaded: %d bytes", total_downloaded);
    } else {
        ESP_LOGI(TAG, "Audio stream download stopped by user, total downloaded: %d bytes", total_downloaded);
    }
    
    is_downloading_ = false;
    
    // Notify playback thread that download is complete
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// Stream audio data
void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting audio stream playback");
    
    // Initialize time tracking variables
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available or not enabled");
        is_playing_ = false;
        return;
    }

    // Ensure audio output is enabled
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    
    // Initialize MP3 decoder if not already initialized
    if (!mp3_decoder_initialized_) {
        ESP_LOGI(TAG, "MP3 decoder not initialized, initializing now");
        if (!InitializeMp3Decoder()) {
            ESP_LOGE(TAG, "Failed to initialize MP3 decoder during playback");
            is_playing_ = false;
            return;
        }
    }
    
    // Wait for the buffer to have enough data to start playback
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        auto timeout = std::chrono::seconds(5);
        bool has_data = buffer_cv_.wait_for(lock, timeout, [this] { 
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); 
        });
        
        if (!has_data) {
            ESP_LOGW(TAG, "Timeout waiting for buffer, buffer_size=%d, is_downloading=%d, buffer_empty=%d", 
                    buffer_size_, is_downloading_, audio_buffer_.empty());
        }
    }
    
    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);
    
    size_t total_played_bytes = 0;
    size_t total_print_bytes = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    
    // Allocate MP3 input buffer
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }
    
    // Flag to indicate if ID3 tags have been processed
    bool id3_processed = false;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    int16_t* pcm_buffer = new int16_t[2304];  // Max PCM samples per MP3 frame
    if (!pcm_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        heap_caps_free(mp3_input_buffer);
        is_playing_ = false;
        return;
    }
    
    while (is_playing_) {
        // Check device state, only play music in idle state
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        // State transition: Speaking -> Listening -> Idle -> Play music
        if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
            if (current_state == kDeviceStateSpeaking) {
                ESP_LOGI(TAG, "Device is in speaking state, switching to listening state for music playback");
            }
            if (current_state == kDeviceStateListening) {
                ESP_LOGI(TAG, "Device is in listening state, switching to idle state for music playback");
            }
            // Switch state
            app.ToggleChatState(); // Transition to idle state
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        } else if (current_state != kDeviceStateIdle) { // If not idle, block music playback
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // Start FFT only once per streaming session
		if (!fft_started_) {
			if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
				vTaskDelay(pdMS_TO_TICKS(150));
				display->StartFFT();
				ESP_LOGI(TAG, "StartFFT() triggered ONE TIME");

				// Refresh song info AFTER canvas created
				if (!current_song_name_.empty()) {
					char buf[256];
					snprintf(buf, sizeof(buf),
							 "ONLINE 《%s》\n%s • Đang phát...",
							 title_name_.empty() ? current_song_name_.c_str() : title_name_.c_str(),
							 artist_name_.empty() ? "Unknown Artist" : artist_name_.c_str());

					display->SetMusicInfo(buf);
					ESP_LOGI(TAG, "REFRESH SONG INFO AFTER CANVAS: %s", buf);
				}
			}
			fft_started_ = true;
		}


		// Show song title only once
		if (!song_name_displayed_ && !current_song_name_.empty()) {
			song_name_displayed_ = true;
		}
        
        // If more MP3 data is needed, read from the buffer
        if (bytes_left < 4096) {  // Maintain at least 4KB of data for decoding
            AudioChunk chunk;
            
            // Retrieve audio data from the buffer
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_) {
                        // Download complete and buffer empty, playback ends
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played_bytes);
                        break;
                    }
                    // Wait for new data with timeout
                    auto timeout = std::chrono::milliseconds(500);
                    bool has_data = buffer_cv_.wait_for(lock, timeout, [this] { 
                        return !audio_buffer_.empty() || !is_downloading_; 
                    });
                    if (!has_data) {
                        ESP_LOGD(TAG, "Timeout waiting for audio data, will retry");
                    }
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }
                
                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                total_played_bytes += chunk.size;
                total_print_bytes += chunk.size;
                
                // Notify download thread that buffer has space
                buffer_cv_.notify_one();
            }
            
            // Add new data to the MP3 input buffer
            if (chunk.data && chunk.size > 0) {
                // Move remaining data to the beginning of the buffer
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                
                // Check buffer space
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                
                // Copy new data
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                
                // Check and skip ID3 tags (allow multi-chunk ID3 skipping)
				if (!id3_processed && bytes_left >= 10)
				{
					size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
					if (id3_skip > 0) {

						// skip ID3 part in current buffer
						read_ptr += id3_skip;
						bytes_left -= id3_skip;

						ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);

						// If ID3 header is larger than buffer, wait for next chunk
						if (bytes_left < 256) {
							ESP_LOGW(TAG, "Remaining buffer too small after ID3 skip, waiting for next chunk");
							bytes_left = 0;
							continue;   // pump next incoming data
						}
					}

					// Mark as processed ONLY when buffer seems OK (≥ sync possible)
					if (bytes_left >= 256) {
						id3_processed = true;
					}
				}
								
                // Free chunk memory
                heap_caps_free(chunk.data);
            }
        }
        
        // Attempt to find MP3 frame sync
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            vTaskDelay(pdMS_TO_TICKS(2));
			bytes_left = 0;
            continue;
        }
        
		if (bytes_left < 128) {            
			vTaskDelay(pdMS_TO_TICKS(2));
			continue;
		}

        // Skip to sync position
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // Decode MP3 frame
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // Decode successful, get frame info
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
			
			// ---- SONG INFO DISPLAY ----
			if (!full_info_displayed_) {

			auto display = Board::GetInstance().GetDisplay();
			if (display) {

				char buf[256];

				int br = (mp3_frame_info_.bitrate > 0)
							? mp3_frame_info_.bitrate / 1000
							: 0;

				int hz = (mp3_frame_info_.samprate > 0)
							? mp3_frame_info_.samprate
							: 44100;

				const char* ch = (mp3_frame_info_.nChans == 2) ? "Stereo" : "Mono";

				snprintf(buf, sizeof(buf),
						 "ONLINE 《%s》\n%s • %d kbps | %d Hz | %s",
						 title_name_.empty() ? current_song_name_.c_str() : title_name_.c_str(),
						 artist_name_.empty() ? "Unknown Artist" : artist_name_.c_str(),
						 br, hz, ch);

				display->SetMusicInfo(buf);
			}

			full_info_displayed_ = true;
		}

            total_frames_decoded_++;
            
            // Basic frame info validity check to prevent division by zero
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping", 
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // Calculate the duration of the current frame (in milliseconds)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) / 
                                  (mp3_frame_info_.samprate * mp3_frame_info_.nChans);
            
            // Update current playback time
            current_play_time_ms_ += frame_duration_ms;
            
            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d", 
                    total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                    mp3_frame_info_.samprate, mp3_frame_info_.nChans);
            
            // Update lyric display
            int buffer_latency_ms = 600; // Adjusted based on testing
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);
            
            // Send PCM data to the Application's audio decoding queue
            if (mp3_frame_info_.outputSamps > 0) {
                int16_t* final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;
                
                // If stereo, convert to mono
                if (mp3_frame_info_.nChans == 2) {
                    // Convert stereo to mono: mix left and right channels
                    int stereo_samples = mp3_frame_info_.outputSamps;  // Total samples including both channels
                    int mono_samples = stereo_samples / 2;  // Actual mono sample count
                    
                    mono_buffer.resize(mono_samples);
                    
                    for (int i = 0; i < mono_samples; ++i) {
                        // Mix left and right channels (L + R) / 2
                        int left = pcm_buffer[i * 2];      // Left channel
                        int right = pcm_buffer[i * 2 + 1]; // Right channel
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples", 
                            stereo_samples, mono_samples);
                } else if (mp3_frame_info_.nChans == 1) {
                    // Already mono, no conversion needed
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                } else {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono", 
                            mp3_frame_info_.nChans);
                }
                
                // Create AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60;  // Use Application's default frame duration
                packet.timestamp = 0;
                
                // Convert int16_t PCM data to uint8_t byte array
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                if (display) {
                    if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                        // Create or update FFT audio data buffer
                        final_pcm_data_fft = display->MakeAudioBuffFFT(pcm_size_bytes);

                        // Push PCM data to FFT buffer
                        display->FeedAudioDataFFT(final_pcm_data, pcm_size_bytes);
                    }
                }

                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application", 
                        final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                
                // Send to Application's audio decoding queue
                app.AddAudioData(std::move(packet));
                
                // Log playback progress
                if (total_print_bytes >= (128 * 1024)) {
                    total_print_bytes = 0;
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played_bytes, buffer_size_);
                }
            }
            
        } else {
            // Decode failed
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);
            
            // Skip some bytes and continue
            if (bytes_left > 1) {
                read_ptr++;
                bytes_left--;
            } else {
                bytes_left = 0;
            }
			vTaskDelay(pdMS_TO_TICKS(2));  
			continue;
        }
    }
    
    // Free PCM buffer
    delete[] pcm_buffer;

    if (is_playing_) {
        ESP_LOGI(TAG, "Audio stream playback finished successfully, total played: %d bytes", total_played_bytes);
        ClearAudioBuffer();
        // Reset the sample rate to the original value
        ResetSampleRate();
    } else {
        ESP_LOGI(TAG, "Audio stream playback stopped by user, total played: %d bytes", total_played_bytes);
    }

    // Cleanup
        if (mp3_input_buffer) {
        heap_caps_free(mp3_input_buffer);
    }
    
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played_bytes);
    ESP_LOGI(TAG, "Performing basic cleanup from play thread");
    
    // Stop playback flag
    is_playing_ = false;

    // Chỉ xử lý FFT khi đang ở chế độ SPECTRUM
    if (display) {
        if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
            // 1) Xoá text info (cả trên canvas + chat label, nhờ SetMusicInfo mới chỉnh ở trên)
            display->SetMusicInfo("");

            // 2) Dừng FFT + xoá UI nhạc + free buffer
            display->StopFFT();
            display->ReleaseAudioBuffFFT();

            ESP_LOGI(TAG, "Stopped FFT display and cleared music UI from play thread (spectrum mode)");
        } else {
            ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop");
        }
    }
	final_pcm_data_fft = nullptr;
	ClearAudioBuffer();
	buffer_size_ = 0;
	CleanupMp3Decoder();
	mp3_decoder_initialized_ = false;

	// Bật lại output để radio dùng
	auto codec2 = Board::GetInstance().GetAudioCodec();
	if (codec2) codec2->EnableOutput(true);

	ESP_LOGI(TAG, "[PATCH] Full cleanup done after PlayAudioStream");
	}

// Clear audio buffer
void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// Initialize MP3 decoder
bool Esp32Music::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// 清理MP3解码器
void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// Reset the sample rate to the original value
void Esp32Music::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 && 
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
            ESP_LOGI(TAG, "Resetting sample rate: %d Hz -> %d Hz", 
                codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1)) {  // -1: Reset to the original value
            ESP_LOGI(TAG, "Sample rate reset to original value: %d Hz", codec->output_sample_rate());
        } else {
            ESP_LOGW(TAG, "Failed to reset sample rate to original value");
        }
    }
}

// Skip the ID3 tag at the beginning of the MP3 file
size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // Check for ID3v2 tag header "ID3"
    if (memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    
    // Calculate tag size (synchsafe integer format)
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    
    // ID3v2 header (10 bytes) + tag content
    size_t total_skip = 10 + tag_size;
    
    // Ensure it does not exceed available data size
    if (total_skip > size) {
        total_skip = size;
    }
    
    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// Download lyrics
bool Esp32Music::DownloadLyrics(const std::string& lyric_url) {
    // ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str()); // DISABLED to protect URL
    
    // Check if the URL is empty
    if (lyric_url.empty()) {
        // ESP_LOGE(TAG, "Lyric URL is empty!"); // DISABLED to protect URL
        return false;
    }
    
    // Add retry logic with exponential backoff
    const int max_retries = 3;
    const int timeout_ms = 8000;  // 8 second timeout for lyrics
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5;  // Allow up to 5 redirects
    int backoff_ms = 1000;  // Start with 1 second backoff
    
    while (retry_count < max_retries && !success && redirect_count < max_redirects) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d, waiting %dms)", 
                     retry_count + 1, max_retries, backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms *= 2;  // Exponential backoff
        }
        
        // Use the HTTP client provided by the Board with explicit timeout
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(timeout_ms);
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }
        
        // Set request headers
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain; charset=utf-8");
        http->SetHeader("Accept-Encoding", "deflate");
        http->SetHeader("Connection", "close");
        
        // Add ESP32 authentication headers
        add_auth_headers(http.get());
        
        // Open GET connection
        ESP_LOGD(TAG, "Opening HTTP connection for lyrics (attempt %d)", retry_count + 1);
        if (!http->Open("GET", current_url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics (attempt %d)", retry_count + 1);
            retry_count++;
            continue;
        }
        
        // Check HTTP status code
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
        
        // Handle redirects - Since the Http class does not have a GetHeader method, we can only report the redirect
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            // 由于无法获取Location头，只能报告重定向但无法继续
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // Accept 2xx status codes, retry on 5xx/429, fail on 4xx
        if (status_code < 200 || status_code >= 300) {
            if ((status_code >= 500 && status_code < 600) || status_code == 429) {
                ESP_LOGW(TAG, "Transient HTTP error %d, will retry", status_code);
                http->Close();
                retry_count++;
                continue;
            } else if (status_code >= 400 && status_code < 500) {
                ESP_LOGE(TAG, "HTTP GET failed with client error: %d", status_code);
                http->Close();
                return false;  // Don't retry 4xx errors
            } else {
                ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
                http->Close();
                retry_count++;
                continue;
            }
        }
        
        // Read the response
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;
        
        // Since we cannot retrieve the Content-Length and Content-Type headers, we do not know the expected size and content type
        ESP_LOGD(TAG, "Starting to read lyric content");
        
        while (true) {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // Commented out to reduce log output
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;
                
                // Periodically log download progress
                if (total_read % 4096 == 0) {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            } else if (bytes_read == 0) {
                // Normal end, no more data
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            } else {
                // bytes_read < 0, possible known issue with ESP-IDF
                if (!lyric_content.empty()) {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                } else {
                    ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }
        
        http->Close();
        
        if (read_error) {
            retry_count++;
            continue;
        }
        
        // If data was successfully read, exit the retry loop
        if (success) {
            break;
        }
    }
    
    // Check if maximum retries were exceeded
    if (retry_count >= max_retries) {
        ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
        return false;
    }
    
    // Log the first few bytes of the data for debugging
    if (!lyric_content.empty()) {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
        return false;
    }
    
    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// Parse lyrics
bool Esp32Music::ParseLyrics(const std::string& lyric_content) {
    ESP_LOGI(TAG, "Parsing lyrics content");
    
    // Use a lock to protect access to the lyrics_ array
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    lyrics_.clear();
    
    // Split the lyric content by lines
    std::istringstream stream(lyric_content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove carriage return at the end of the line
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Parse LRC format: [mm:ss.xx]Lyric text
        if (line.length() > 10 && line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos) {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);
                
                // Check if it's metadata instead of a timestamp
                // Metadata tags are usually [ti:Title], [ar:Artist], [al:Album], etc.
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos) {
                    std::string left_part = tag_or_time.substr(0, colon_pos);
                    
                    // Check if the left part of the colon is a time (numeric)
                    bool is_time_format = true;
                    for (char c : left_part) {
                        if (!isdigit(c)) {
                            is_time_format = false;
                            break;
                        }
                    }
                    
                    // If it's not a time format, skip this line (metadata tag)
                    if (!is_time_format) {
                        // Metadata like title, artist, etc., can be processed here
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }
                    
                    // It's a time format, parse the timestamp
                    try {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);
                        
                        // Safely handle lyric text, ensuring proper UTF-8 encoding
                        std::string safe_lyric_text;
                        if (!content.empty()) {
                            // Create a safe copy and validate the string
                            safe_lyric_text = content;
                            // Ensure the string is null-terminated
                            safe_lyric_text.shrink_to_fit();
                        }
                        
                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));
                        
                        if (!safe_lyric_text.empty()) {
                            // Limit log output length to avoid truncation issues with non-ASCII characters
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        } else {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }
    
    // Sort by timestamp
    std::sort(lyrics_.begin(), lyrics_.end());
    
    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// Lyric display thread
void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric display thread started");
    
    if (!DownloadLyrics(current_lyric_url_)) {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }
    
    // Periodically check if the display needs to be updated (frequency can be reduced)
    while (is_lyric_running_ && is_playing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) {
        return;
    }
    
    // Find the current lyric to display
    int new_lyric_index = -1;
    
    // Start searching from the current lyric index to improve efficiency
    int start_index = (current_lyric_index_ >= 0) ? current_lyric_index_ : 0;
    
    // Forward search: find the last timestamp less than or equal to the current time
    for (int i = start_index; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= current_time_ms) {
            new_lyric_index = i;
        } else {
            break;  // Timestamp exceeds the current time
        }
    }
    
    // If no lyric is found (e.g., current time is earlier than the first lyric), display nothing
    if (new_lyric_index == -1) {
        new_lyric_index = -1;
    }
    
    // If the lyric index has changed, update the display
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string lyric_text;
            
            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
                lyric_text = lyrics_[current_lyric_index_].second;
            }
            
            // Display the lyric
            display->SetChatMessage("lyric", lyric_text.c_str());
            
            ESP_LOGD(TAG, "Lyric update at %lldms: %s", 
                    current_time_ms, 
                    lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

// Remove complex authentication initialization methods, use simple static functions

// Remove complex class methods and use simple static functions

/**
 *
 * @param http_client Pointer to the HTTP client
 * 
 * The added authentication headers include:
 * - X-MAC-Address: Device MAC address
 * - X-Chip-ID: Device chip ID
 * - X-Timestamp: Current timestamp
 * - X-Dynamic-Key: Dynamically generated key
 */
// Removed the complex AddAuthHeaders method, using a simple static function

// Removed complex authentication verification and configuration methods, using simple static functions

// Implementation of display mode control methods
void Esp32Music::SetDisplayMode(DisplayMode mode) {
    DisplayMode old_mode = display_mode_;
    display_mode_ = mode;
    
    ESP_LOGI(TAG, "Display mode changed from %s to %s", 
            (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS",
            (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS");
}

std::string Esp32Music::GetCheckMusicServerUrl() {
    Settings settings("wifi", false);
    std::string url = settings.GetString("music_url");
    if (url.empty()) {
        // Use primary server from fallback list
        if (!FALLBACK_MUSIC_URLS.empty()) {
            url = FALLBACK_MUSIC_URLS[0];
        }
    }
    return url;
}