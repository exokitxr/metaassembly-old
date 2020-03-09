#include <chrono>
#include <thread>

// XXX

#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <filesystem>

#include <windows.h>

// #include "device/vr/detours/detours.h"
#include "json.hpp"

#include "out.h"
#include "io.h"

using json = nlohmann::json;

std::string logSuffix = "";
// HWND g_hWnd = NULL;
// CHAR s_szDllPath[MAX_PATH] = "vrclient_x64.dll";
std::string dllDir;

void getChildEnvBuf(char *pEnvBuf) {
  LPSTR lpvEnv = GetEnvironmentStringsA();
  std::vector<std::string> vars;
  for (LPSTR lpszVariable = (LPTSTR)lpvEnv; *lpszVariable; lpszVariable++) {
    std::string var;
    while (*lpszVariable) {
      var += *lpszVariable++;
    }
    vars.push_back(std::move(var));
  }
  FreeEnvironmentStrings(lpvEnv);

  char cwdBuf[MAX_PATH];
  if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
    getOut() << "failed to get current directory" << std::endl;
    abort();
  }

  /* {
    std::string vrOverrideString = "VR_OVERRIDE=";
    vrOverrideString += cwdBuf;
    vrOverrideString += R"EOF(\..\)EOF";

    bool vrOverrideFound = false;
    for (size_t i = 0; i < vars.size(); i++) {
      std::string &s = vars[i];
      std::string s2 = s;
      for (auto &c : s2) {
        c = toupper(c);
      }
      if (s2.rfind("VR_OVERRIDE=", 0) == 0) {
        s = std::move(vrOverrideString);
        vrOverrideFound = true;
        break;
      }
    }
    if (!vrOverrideFound) {
      vars.push_back(std::move(vrOverrideString));
    }
  } */

  for (auto iter : vars) {
    const std::string &s = iter;
    // getOut() << "write arg: " << s << std::endl;
    memcpy(pEnvBuf, s.c_str(), s.size() + 1);
    pEnvBuf += s.size() + 1;
  }
  pEnvBuf[0] = '\0';
}

void
ArgvQuote (
    const std::string& Argument,
    std::string& CommandLine,
    bool Force
    )
    
/*++
    
Routine Description:
    
    This routine appends the given argument to a command line such
    that CommandLineToArgvW will return the argument string unchanged.
    Arguments in a command line should be separated by spaces; this
    function does not add these spaces.
    
Arguments:
    
    Argument - Supplies the argument to encode.

    CommandLine - Supplies the command line to which we append the encoded argument string.

    Force - Supplies an indication of whether we should quote
            the argument even if it does not contain any characters that would
            ordinarily require quoting.
    
Return Value:
    
    None.
    
Environment:
    
    Arbitrary.
    
--*/
    
{
    //
    // Unless we're told otherwise, don't quote unless we actually
    // need to do so --- hopefully avoid problems if programs won't
    // parse quotes properly
    //
    
    if (Force == false &&
        Argument.empty () == false &&
        Argument.find_first_of (" \t\n\v\"") == Argument.npos)
    {
        CommandLine.append (Argument);
    }
    else {
        CommandLine.push_back ('"');
        
        for (auto It = Argument.begin () ; ; ++It) {
            unsigned NumberBackslashes = 0;
        
            while (It != Argument.end () && *It == '\\') {
                ++It;
                ++NumberBackslashes;
            }
        
            if (It == Argument.end ()) {
                
                //
                // Escape all backslashes, but let the terminating
                // double quotation mark we add below be interpreted
                // as a metacharacter.
                //
                
                CommandLine.append (NumberBackslashes * 2, '\\');
                break;
            }
            else if (*It == '"') {

                //
                // Escape all backslashes and the following
                // double quotation mark.
                //
                
                CommandLine.append (NumberBackslashes * 2 + 1, '\\');
                CommandLine.push_back (*It);
            }
            else {
                
                //
                // Backslashes aren't special here.
                //
                
                CommandLine.append (NumberBackslashes, '\\');
                CommandLine.push_back (*It);
            }
        }
    
        CommandLine.push_back ('"');
    }
}

/* void terminateKnownProcesses() {
  terminateProcesses(std::vector<const char *>{
    "chrome.exe",
  });
  terminateProcesses(std::vector<const char *>{
    "native_host.exe",
  });
  terminateProcesses(std::vector<const char *>{
    "process.exe",
  });
} */
HANDLE startChrome(const std::string &indexHtmlPath) {
  std::string cmd(R"EOF(..\..\..\..\Chrome-bin\chrome.exe --enable-features="WebXR,OpenVR" --disable-features="WindowsMixedReality" --no-sandbox --disable-xr-device-consent-prompt-for-testing --load-extension="..\..\extension" --whitelisted-extension-id="glmgcjligejadkfhgebnplablaggjbmm"" )EOF");
  cmd += indexHtmlPath;
  // --allow-insecure-localhost
  // --unsafely-treat-insecure-origin-as-secure="http://localhost:9002"

  std::vector<char> cmdVector(cmd.size() + 1);
  memcpy(cmdVector.data(), cmd.c_str(), cmd.size() + 1);

  getOut() << "launch chrome command: " << cmd << std::endl;
  
  char envBuf[64 * 1024];
  getChildEnvBuf(envBuf);

  STARTUPINFO si{};
  si.cb = sizeof(STARTUPINFO);
  
  PROCESS_INFORMATION pi{};
  if (CreateProcessA(
    NULL,
    cmdVector.data(),
    NULL,
    NULL,
    true,
    CREATE_NO_WINDOW,
    envBuf,
    NULL,
    &si,
    &pi
  )) {
    return pi.hProcess;
  } else {
    return NULL;
  }
}

HANDLE chromeProcessHandle = NULL;
int main(int argc, char **argv, char **envp) {
  getOut() << "Start" << std::endl;
  
  char cwdBuf[MAX_PATH];
  if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
    getOut() << "failed to get current directory" << std::endl;
    abort();
  }
  
  {
    std::string indexHtmlPath;
    ArgvQuote(std::string(cwdBuf) + std::string(R"EOF(\extension\index.html)EOF"), indexHtmlPath, false);
    chromeProcessHandle = startChrome(indexHtmlPath);
    if (chromeProcessHandle) {
      getOut() << "launched chrome ui process" << std::endl;
    } else {
      getOut() << "failed to launch chrome ui process: " << (void *)GetLastError() << std::endl;
    }
  }

  {  
    char cwdBuf[MAX_PATH];
    if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
      getOut() << "failed to get current directory" << std::endl;
      abort();
    }
    dllDir = cwdBuf;
    dllDir += "\\";
    {
      std::string manifestTemplateFilePath = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(..\..\..\avrenderer\native-manifest-template.json)EOF"))).string();
      std::string manifestFilePath = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(\native-manifest.json)EOF"))).string();

      std::string s;
      {
        std::ifstream inFile(manifestTemplateFilePath);
        s = std::string((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
      }
      {
        json j = json::parse(s);
        j["path"] = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(\avrenderer.exe)EOF"))).string();
        s = j.dump(2);
      }
      {    
        std::ofstream outFile(manifestFilePath);
        outFile << s;
        outFile.close();
      }
      
      HKEY hKey;
      LPCTSTR sk = R"EOF(Software\Google\Chrome\NativeMessagingHosts\com.exokit.xrchrome)EOF";
      LONG openRes = RegOpenKeyEx(HKEY_CURRENT_USER, sk, 0, KEY_ALL_ACCESS , &hKey);
      if (openRes == ERROR_FILE_NOT_FOUND) {
        openRes = RegCreateKeyExA(HKEY_CURRENT_USER, sk, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
        
        if (openRes != ERROR_SUCCESS) {
          getOut() << "failed to create registry key: " << (void*)openRes << std::endl;
          abort();
        }
      } else if (openRes != ERROR_SUCCESS) {
        getOut() << "failed to open registry key: " << (void*)openRes << std::endl;
        abort();
      }

      LPCTSTR value = "";
      LPCTSTR data = manifestFilePath.c_str();
      LONG setRes = RegSetValueEx(hKey, value, 0, REG_SZ, (LPBYTE)data, strlen(data)+1);
      if (setRes != ERROR_SUCCESS) {
        getOut() << "failed to set registry key: " << (void*)setRes << std::endl;
        abort();
      }

      LONG closeRes = RegCloseKey(hKey);
      if (closeRes != ERROR_SUCCESS) {
        getOut() << "failed to close registry key: " << (void*)closeRes << std::endl;
        abort();
      }
    }
  }
  
  WaitForSingleObject(chromeProcessHandle, INFINITE);
  // terminateKnownProcesses();
  // PostMessage(g_hWnd, WM_DESTROY, 0, 0);

	return 0;
}
