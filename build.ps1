# build
echo building...

$ProgressPreference = 'SilentlyContinue'

Invoke-WebRequest "https://github.com/Hibbiki/chromium-win64/releases/download/v79.0.3945.130-r706915/chrome.sync.7z" -OutFile "chrome.7z"
7z x chrome.7z -aoa
rm chrome.7z

Invoke-WebRequest "https://raw.githubusercontent.com/avaer/swsdk/master/steamworks_sdk_148.zip" -OutFile "steamworks_sdk_148.zip"
7z x steamworks_sdk_148.zip -aoa
rm steamworks_sdk_148.zip

msbuild Aardvark.sln

echo zipping artifact... 
7z a avrenderer.zip -r .\src\build\avrenderer\Debug\
echo done zipping artifact

ls

echo done