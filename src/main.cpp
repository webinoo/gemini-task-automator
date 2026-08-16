#pragma comment(lib, "libcurl.lib")
#pragma comment(lib, "advapi32.lib")

#include <iostream>
#include <string>
#include <windows.h>
#include <shellapi.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

namespace SystemActions {
    void OpenUrl(const std::string& url) {
        std::wstring wurl(url.begin(), url.end());
        ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        std::cout << "[Action] Opening URL/Browser: " << url << "\n";
    }

    void LaunchApp(const std::string& appPath) {
        std::wstring wapp(appPath.begin(), appPath.end());
        HINSTANCE res = ShellExecuteW(NULL, L"open", wapp.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)res <= 32) {
            std::string cmd = "start \"\" \"" + appPath + "\"";
            system(cmd.c_str());
        }
        std::cout << "[Action] Launching app: " << appPath << "\n";
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

class GeminiAutomator {
private:
    std::string apiKey;

    json GetToolDeclaration() {
        return json::array({
            {
                {"name", "open_url"},
                {"description", "Opens a URL or specific web page/tab in the user's default browser or Opera GX."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", {
                        {"url", {{"type", "STRING"}, {"description", "The full URL to open, e.g. https://www.google.com"}}}
                    }},
                    {"required", json::array({"url"})}
                }}
            },
            {
                {"name", "launch_app"},
                {"description", "Launches a Windows executable or application by name/executable path."},
                {"parameters", {
                    {"type", "OBJECT"},
                    {"properties", {
                        {"app_name", {{"type", "STRING"}, {"description", "Executable name or path, e.g. 'opera.exe', 'notepad.exe', 'calc.exe'"}}}
                    }},
                    {"required", json::array({"app_name"})}
                }}
            },
            {
                {"name", "shutdown_pc"},
                {"description", "Shuts down or powers off the host Windows PC."},
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
        std::cout << "[System] Gemini API key configured successfully.\n";
    }

    void ProcessCommand(const std::string& userPrompt) {
        if (apiKey.empty()) {
            std::cout << "[Error] API Key not set! Use > /set-api (your_key) first.\n";
            return;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cout << "[Error] Failed to initialize Network Subsystem.\n";
            return;
        }

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

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            std::cout << "[Network Error] Request failed: " << curl_easy_strerror(res) << "\n";
            return;
        }

        ParseAndExecuteResponse(responseBuffer);
    }

private:
    void ParseAndExecuteResponse(const std::string& rawResponse) {
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
                    std::string url = args.value("url", "https://google.com");
                    SystemActions::OpenUrl(url);
                } 
                else if (fnName == "launch_app") {
                    std::string app = args.value("app_name", "");
                    SystemActions::LaunchApp(app);
                } 
                else if (fnName == "shutdown_pc") {
                    SystemActions::ShutdownPC();
                }
            } else if (candidate.contains("text")) {
                std::cout << "[Gemini Response] " << candidate["text"].get<std::string>() << "\n";
            }
        } 
        catch (const std::exception& e) {
            std::cout << "[Parser Error] Failed to parse API output: " << e.what() << "\n";
        }
    }
};

int main() {
    SetConsoleTitleA("Gemini Task Automator [ADMIN]");

    if (!IsRunAsAdmin()) {
        std::cout << "====================================================\n";
        std::cout << " WARNING: Application is NOT running as Administrator!\n";
        std::cout << " Some administrative tasks or commands may fail.\n";
        std::cout << "====================================================\n\n";
    } else {
        std::cout << "[System status]: Running with Elevated Administrator privileges.\n\n";
    }

    curl_global_init(CURL_GLOBAL_ALL);
    GeminiAutomator automator;

    std::cout << "====================================================\n";
    std::cout << "          GEMINI WINDOWS TASK AUTOMATOR             \n";
    std::cout << "====================================================\n";
    std::cout << " Usage:\n";
    std::cout << "  > /set-api <YOUR_GEMINI_API_KEY>\n";
    std::cout << "  > open me an opera gx tab\n";
    std::cout << "  > open notepad\n";
    std::cout << "  > shut down the pc\n";
    std::cout << "  > exit\n";
    std::cout << "====================================================\n\n";

    std::string input;
    while (true) {
        std::cout << "Automator> ";
        if (!std::getline(std::cin, input) || input == "exit") {
            break;
        }

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