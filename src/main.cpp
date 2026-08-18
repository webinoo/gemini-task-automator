#pragma comment(lib, "libcurl.lib")
#pragma comment(lib, "advapi32.lib")

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- Helper: Check Admin Rights ---
bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// --- Network Callback ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// --- Extended System Automation Handlers ---
namespace SystemActions {

    void OpenUrl(const std::string& url) {
        std::wstring wurl(url.begin(), url.end());
        ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        std::cout << "[Action] Opened URL: " << url << "\n";
    }

    void LaunchApp(const std::string& appIdentifier) {
        std::wstring wapp(appIdentifier.begin(), appIdentifier.end());
        
        // Try direct ShellExecute (handles registered protocols like roblox:// or system apps)
        HINSTANCE res = ShellExecuteW(NULL, L"open", wapp.c_str(), NULL, NULL, SW_SHOWNORMAL);
        
        if ((INT_PTR)res <= 32) {
            // Fallback via Windows Command Shell / Protocol launcher
            std::string cmd = "start \"\" \"" + appIdentifier + "\"";
            system(cmd.c_str());
        }
        std::cout << "[Action] Launching app/protocol: " << appIdentifier << "\n";
    }

    void WriteToFile(const std::string& filePath, const std::string& content, bool append = false) {
        try {
            std::ios_base::openmode mode = std::ios::out;
            if (append) mode |= std::ios::app;

            std::ofstream file(filePath, mode);
            if (!file.is_open()) {
                std::cout << "[Error] Could not open file for writing: " << filePath << "\n";
                return;
            }

            file << content;
            file.close();
            std::cout << "[Action] File successfully " << (append ? "appended" : "written") << ": " << filePath << "\n";
        } catch (const std::exception& e) {
            std::cout << "[Error] File I/O exception: " << e.what() << "\n";
        }
    }

    void ShutdownPC() {
        std::cout << "[Action] Initiating System Shutdown...\n";
        HANDLE hToken;
        TOKEN_PRIVILEGES tkp;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
            tkp.PrivilegeCount = 1;
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
        }

        InitiateSystemShutdownExW(NULL, (LPWSTR)L"Automator requested shutdown", 10, TRUE, FALSE, SHTDN_REASON_FLAG_PLANNED);
    }
}

// --- Gemini AI Engine ---
class GeminiAutomator {
private:
    std::string apiKey;

    json GetToolDeclaration() {
        return json::array({
            {
                {"name", "open_url"},
                {"description", "Opens a web link or URL in default browser."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", {
                        {"url", {{"type", "STRING"}, {"description", "Full URL e.g. https://google.com"}}}
                    }},
                    {"required", json::array({"url"})}
                }}
            },
            {
                {"name", "launch_app"},
                {"description", "Launches executable files, third-party desktop apps (OBS, Roblox, Discord), or Windows protocols."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", {
                        {"app_name", {{"type", "STRING"}, {"description", "Executable name, protocol, or path. Examples: 'obs64.exe', 'roblox://', 'notepad.exe', 'calc.exe'"}}}
                    }},
                    {"required", json::array({"app_name"})}
                }}
            },
            {
                {"name", "edit_file"},
                {"description", "Creates, modifies, or appends text to local files on the system."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", {
                        {"file_path", {{"type", "STRING"}, {"description", "Path to file e.g. C:\\Users\\Public\\notes.txt"}}},
                        {"content", {{"type", "STRING"}, {"description", "Text content to write or append"}}},
                        {"append", {{"type", "BOOLEAN"}, {"description", "Set to true to append text instead of overwriting"}}}
                    }},
                    {"required", json::array({"file_path", "content"})}
                }}
            },
            {
                {"name", "shutdown_pc"},
                {"description", "Shuts down the computer."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", json::object()}
                }}
            }
        });
    }

public:
    void SetApiKey(const std::string& key) {
        apiKey = key;
        std::cout << "[System] Gemini API key configured.\n";
    }

    void ProcessCommand(const std::string& userPrompt) {
        if (apiKey.empty()) {
            std::cout << "[Error] Set API key first using > /set-api <YOUR_KEY>\n";
            return;
        }

        CURL* curl = curl_easy_init();
        if (!curl) return;

        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

        json payload = {
            {"contents", json::array({
                {
                    {"role", "user"},
                    {"parts", json::array({
                        {{"text", userPrompt}}
                    })}
                }
            })},
            {"tools", json::array({
                {{"function_declarations", GetToolDeclaration()}}
            })},
            {"tool_config", {
                {"function_calling_config", {
                    {"mode", "AUTO"}
                }}
            }}
        };

        std::string jsonStr = payload.dump();
        std::string responseBuffer;

        struct curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res == CURLE_OK) {
            ParseAndExecute(responseBuffer);
        } else {
            std::cout << "[Network Error] Request failed: " << curl_easy_strerror(res) << "\n";
        }
    }

private:
    void ParseAndExecute(const std::string& rawResponse) {
        try {
            auto resJson = json::parse(rawResponse);

            if (resJson.contains("error")) {
                std::cout << "[API Error] " << resJson["error"]["message"].get<std::string>() << "\n";
                return;
            }

            auto candidate = resJson["candidates"][0]["content"]["parts"][0];

            if (candidate.contains("functionCall")) {
                auto fnCall = candidate["functionCall"];
                std::string fnName = fnCall["name"].get<std::string>();
                auto args = fnCall["args"];

                if (fnName == "open_url") {
                    SystemActions::OpenUrl(args.value("url", "https://google.com"));
                } 
                else if (fnName == "launch_app") {
                    SystemActions::LaunchApp(args.value("app_name", ""));
                } 
                else if (fnName == "edit_file") {
                    std::string path = args.value("file_path", "");
                    std::string content = args.value("content", "");
                    bool append = args.value("append", false);
                    SystemActions::WriteToFile(path, content, append);
                } 
                else if (fnName == "shutdown_pc") {
                    SystemActions::ShutdownPC();
                }
            } else if (candidate.contains("text")) {
                std::cout << "[Gemini] " << candidate["text"].get<std::string>() << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "[Parser Error] " << e.what() << "\n";
        }
    }
};

int main() {
    SetConsoleTitleA("Gemini Task Automator [ADMIN]");

    if (!IsRunAsAdmin()) {
        std::cout << "[!] WARNING: Not running as Administrator. Some system calls may be restricted.\n\n";
    } else {
        std::cout << "[+] Status: Elevated Administrator privileges enabled.\n\n";
    }

    curl_global_init(CURL_GLOBAL_ALL);
    GeminiAutomator automator;

    std::cout << "====================================================\n";
    std::cout << "        GEMINI TASK AUTOMATOR v2.0 (OPTIMIZED)       \n";
    std::cout << "====================================================\n";
    std::cout << " Examples:\n";
    std::cout << "  > /set-api <YOUR_API_KEY>\n";
    std::cout << "  > open roblox\n";
    std::cout << "  > open obs studio\n";
    std::cout << "  > write 'Hello World' to C:\\test.txt\n";
    std::cout << "  > shut down the pc\n";
    std::cout << "====================================================\n\n";

    std::string input;
    while (true) {
        std::cout << "Automator> ";
        if (!std::getline(std::cin, input) || input == "exit") break;
        if (input.empty()) continue;

        if (input.rfind("/set-api ", 0) == 0) {
            std::string key = input.substr(9);
            if (!key.empty() && key.front() == '(' && key.back() == ')') {
                key = key.substr(1, key.length() - 2);
            }
            automator.SetApiKey(key);
        } else {
            automator.ProcessCommand(input);
        }
    }

    curl_global_cleanup();
    return 0;
}
