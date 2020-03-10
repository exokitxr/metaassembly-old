{
  'targets': [
    {
      'target_name': 'exokit',
      'sources': [
        'module/module.cpp',
        'module/renderer.cpp',
        'src/avrenderer/aardvark_renderer.cpp',
        'src/avrenderer/aardvark_renderer.h',
        'src/avrenderer/avrenderer_main.cpp',
        'src/avrenderer/avserver.cpp',
        'src/avrenderer/avserver.h',
        'src/avrenderer/av_cef_app.cpp',
        'src/avrenderer/av_cef_app.h',
        'src/avrenderer/iapplication.h',
        'src/avrenderer/javascript_renderer.cpp',
        'src/avrenderer/javascript_renderer.h',
        'src/avrenderer/vrmanager.cpp',
        'src/avrenderer/vrmanager.h',
        'src/avrenderer/out.cpp',
        'src/avrenderer/out.h',
        'src/avrenderer/file_io.cpp',
        'src/avrenderer/file_io.h',
        'src/avrenderer/matrix.cpp',
        'src/avrenderer/matrix.h',
      ],
      'include_dirs': [
        "<!(node -e \"console.log(require.resolve('nan').slice(0, -16))\")",
        'src/avrenderer',
        'src/public',
        'src/thirdparty',
        'src/thirdparty/tinygltf',
        'src/thirdparty/glm',
        'src/thirdparty/gli',
        'src/gltfpbr_base',
        'src/thirdparty/openvr/headers',
        'src/thirdparty/tiny-process-library',
      ],
      'library_dirs': [
        # "<!(node -e \"console.log(require.resolve('native-graphics-deps').slice(0, -9) + '/lib/windows/glew')\")",
      ],
      'libraries': [
        # 'opengl32.lib',
      ],
      'copies': [
      ],
      'defines': [
        '_CRT_SECURE_NO_WARNINGS',
        '_USE_MATH_DEFINES',
        '_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING',
        'NOMINMAX',
        'VK_USE_PLATFORM_WIN32_KHR',
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "AdditionalOptions": ['/std:c++17']
        }
      },
    },
  ],
}
