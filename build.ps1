# build
echo building...

$ProgressPreference = 'SilentlyContinue'

Invoke-WebRequest "https://github.com/Hibbiki/chromium-win64/releases/download/v79.0.3945.130-r706915/chrome.sync.7z" -OutFile "chrome.7z"
7z x chrome.7z -aoa
rm chrome.7z

Invoke-WebRequest "https://raw.githubusercontent.com/avaer/swsdk/master/steamworks_sdk_148.zip" -OutFile "steamworks_sdk_148.zip"
7z x steamworks_sdk_148.zip -aoa
rm steamworks_sdk_148.zip

ls
cd src
mkdir -Force build
cd build

cmake -G "Visual Studio 16 2019" -A x64 ..
msbuild Aardvark.sln

cp -Recurse Chrome-bin src\build\avrenderer\Debug\
cp -Recurse extension src\build\avrenderer\Debug\
cp -Recurse data src\build\avrenderer\Debug\
cp -Recurse bin\openvr_api.dll src\build\avrenderer\Debug\
# cp -Recurse bin\openvr_api.dll src\build\avrenderer\Debug\Chrome-bin\79.0.3945.130\
cp steam_appid.txt src\build\avrenderer\Debug\

echo zipping artifact... 
7z a avrenderer.zip -r .\src\build\avrenderer\Debug\
echo done zipping artifact

ls

echo done