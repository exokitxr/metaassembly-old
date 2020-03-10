{
  'targets': [
    {
      'target_name': 'exokit',
      'sources': [
        'module/module.cpp',
        'module/renderer.cpp',
        'module/renderer.h',
        'src/avrenderer/aardvark_renderer.cpp',
        'src/avrenderer/aardvark_renderer.h',
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

        'src/gltfpbr_base/VulkanExampleBase.cpp',
        'src/gltfpbr_base/vulkanutils.cpp',
        'src/tools/pathtools.cpp',
        'src/tools/stringtools.cpp',
        'src/gltfpbr_base/descriptormanager.cpp',
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
        # 'src/thirdparty/tiny-process-library',
      ],
      'library_dirs': [
        # "<!(node -e \"console.log(require.resolve('native-graphics-deps').slice(0, -9) + '/lib/windows/glew')\")",
        'src/thirdparty/vulkan/libs',
        'src/thirdparty/openvr/lib/win64',
      ],
      'libraries': [
        'psapi.lib',
        'vulkan-1.lib',
        'openvr_api.lib',
        'dxgi.lib',
        'd3d11.lib',
        # 'opengl32.lib',
      ],
      'copies': [
        {
          'destination': '<(module_root_dir)/build/Release/',
          'files': [
            'bin/openvr_api.dll',
          ]
        },
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
